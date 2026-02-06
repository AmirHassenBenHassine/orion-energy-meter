#include <Arduino.h>

#include "battery_manager.h"
#include "button_manager.h"
#include "certificates.h"
#include "config.h"
#include "energy_sensor.h"
#include "led_manager.h"
#include "mqtt_manager.h"
#include "ota_manager.h"
#include "utils.h"
#include "wifi_manager.h"
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <Preferences.h>

#define MAX_BUFFERED_READINGS 672  // 1 week at 15-min intervals
#define BUFFER_NAMESPACE "data_buf"

// Global device ID
String DEVICE_ID;

RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR uint64_t savedRunTime = 0;

static uint64_t lastDataSendTime = 0;
static uint64_t lastTriggerCheck = 0;
static uint64_t lastOtaCheck = 0;
// for sleep mode
static bool inSleepMode = false;
static bool sleepEnabled = true;
static uint64_t sleepStartTime = 0;
static uint64_t totalSleepDuration = 0;
static uint64_t sleepElapsed = 0;

// Current metrics
static EnergySensor::EnergyMetrics currentMetrics;

void handleBootMode(ButtonManager::BootMode mode);
// void handleTriggers();
void sendData();
void enterSmartLightSleep();
void setupMQTT();

struct __attribute__((packed)) CompactReading {
  uint32_t timestampUnix; 
  float voltage;          
  float totalPower;        
  float energyTotal;       
  float current[3];        
  float power[3];         
  uint8_t batteryPercent; 
  uint8_t reserved;        
};

// Buffer metadata
struct BufferMeta {
  uint16_t writeIndex;     // Where to write next
  uint16_t readIndex;      // Where to read next
  uint16_t count;          // How many readings stored
};

// Convert EnergyMetrics to compact format
CompactReading metricsToCompact(const EnergySensor::EnergyMetrics& metrics) {
  CompactReading compact;
  struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      compact.timestampUnix = mktime(&timeinfo);
    } else {
      compact.timestampUnix = millis() / 1000; // Fallback to uptime in seconds
    }  
  compact.voltage = metrics.voltage;
  compact.totalPower = metrics.totalPower;
  compact.energyTotal = metrics.energyTotal;
  // Copy current and power arrays directly
  for (int i = 0; i < 3; i++) {
    compact.current[i] = metrics.current[i];
    compact.power[i] = metrics.power[i];
  }
  
  compact.batteryPercent = (uint8_t)metrics.battery;
  compact.reserved = 0;
  
  return compact;
}

// Convert compact back to JSON for MQTT
String compactToJson(const CompactReading& compact) {
  StaticJsonDocument<512> doc;
  
  doc["deviceId"] = DEVICE_ID;
  
  // Convert Unix timestamp back to formatted string
  time_t rawtime = compact.timestampUnix;
  struct tm* timeinfo = localtime(&rawtime);
  char timeStr[32];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
  doc["timestamp"] = timeStr;
  
  doc["battery"] = compact.batteryPercent;
  doc["voltage"] = compact.voltage;
  doc["totalPower"] = compact.totalPower;
  doc["energyTotal"] = compact.energyTotal;
  
  JsonArray phases = doc.createNestedArray("phases");
  
  for (int i = 0; i < 3; i++) {
    JsonObject phase = phases.createNestedObject();
    phase["current"] = compact.current[i];
    phase["power"] = compact.power[i];
  }
  
  String output;
  serializeJson(doc, output);
  return output;
}

// Load buffer metadata
BufferMeta loadBufferMeta() {
  Preferences prefs;
  BufferMeta meta = {0, 0, 0};
  
  if (prefs.begin(BUFFER_NAMESPACE, true)) {  // Read-only
    meta.writeIndex = prefs.getUShort("wIdx", 0);
    meta.readIndex = prefs.getUShort("rIdx", 0);
    meta.count = prefs.getUShort("count", 0);
    prefs.end();
  }
  
  return meta;
}

// Save buffer metadata
void saveBufferMeta(const BufferMeta& meta) {
  Preferences prefs;
  if (prefs.begin(BUFFER_NAMESPACE, false)) {  // Read-write
    prefs.putUShort("wIdx", meta.writeIndex);
    prefs.putUShort("rIdx", meta.readIndex);
    prefs.putUShort("count", meta.count);
    prefs.end();
  }
}

// Buffer a reading
void bufferReading(const EnergySensor::EnergyMetrics& metrics) {
  BufferMeta meta = loadBufferMeta();
  
  // Convert to compact format
  CompactReading compact = metricsToCompact(metrics);
  
  Preferences prefs;
  if (!prefs.begin(BUFFER_NAMESPACE, false)) {
    Utils::logMessage("MAIN", "❌ Failed to open buffer storage");
    return;
  }
  
  // Store reading as binary blob
  String key = "d" + String(meta.writeIndex);
  size_t written = prefs.putBytes(key.c_str(), &compact, sizeof(CompactReading));
  prefs.end();
  
  if (written != sizeof(CompactReading)) {
    Utils::logMessage("MAIN", "❌ Failed to write reading");
    return;
  }
  
  // Update metadata
  meta.writeIndex = (meta.writeIndex + 1) % MAX_BUFFERED_READINGS;
  
  if (meta.count < MAX_BUFFERED_READINGS) {
    meta.count++;
  } else {
    // Buffer full - overwrite oldest
    meta.readIndex = (meta.readIndex + 1) % MAX_BUFFERED_READINGS;
    Utils::logMessage("MAIN", "⚠️ Buffer full - overwriting oldest");
  }
  
  saveBufferMeta(meta);
  
  Utils::logMessageF("MAIN", "📦 Buffered reading %d/%d (index %d)", 
                    meta.count, MAX_BUFFERED_READINGS, meta.writeIndex - 1);
}

// Send all buffered data
void sendBufferedData() {
  BufferMeta meta = loadBufferMeta();
  
  if (meta.count == 0) {
    return;
  }
  
  Utils::logMessageF("MAIN", "📤 Sending %d buffered readings...", meta.count);
  
  Preferences prefs;
  if (!prefs.begin(BUFFER_NAMESPACE, false)) {
    Utils::logMessage("MAIN", "❌ Failed to open buffer storage");
    return;
  }
  
  uint16_t sent = 0;
  uint16_t failed = 0;
  
  while (meta.count > 0 && failed < 3) {  // Stop after 3 consecutive failures
    String key = "d" + String(meta.readIndex);
    CompactReading compact;
    
    size_t len = prefs.getBytes(key.c_str(), &compact, sizeof(CompactReading));
    
    if (len == sizeof(CompactReading)) {
      // Convert to JSON and publish
      String json = compactToJson(compact);
      bool success = MqttManager::publish(MQTT_TOPIC_ENERGY_METRICS, json.c_str(), false, 0);
      
      if (success) {
        sent++;
        failed = 0;  // Reset failure counter
        
        // Remove the reading
        prefs.remove(key.c_str());
        
        // Update metadata
        meta.readIndex = (meta.readIndex + 1) % MAX_BUFFERED_READINGS;
        meta.count--;
        
        // Progress indicator every 10 readings
        if (sent % 10 == 0) {
          Utils::logMessageF("MAIN", "  Progress: %d/%d sent", sent, sent + meta.count);
        }
        
        delay(100);  // Small delay between messages to avoid overwhelming broker
      } else {
        failed++;
        Utils::logMessageF("MAIN", "⚠️ Failed to send reading (attempt %d/3)", failed);
        delay(1000);  // Wait before retry
      }
    } else {
      // Corrupted reading - skip it
      Utils::logMessageF("MAIN", "⚠️ Corrupted reading at index %d - skipping", meta.readIndex);
      prefs.remove(key.c_str());
      meta.readIndex = (meta.readIndex + 1) % MAX_BUFFERED_READINGS;
      meta.count--;
    }
  }
  
  prefs.end();
  
  // Save updated metadata
  saveBufferMeta(meta);
  
  Utils::logMessageF("MAIN", "✅ Sent %d buffered readings (%d remaining)", sent, meta.count);
}

// Clear all buffered data (for reset/debug)
void clearBuffer() {
  Preferences prefs;
  if (prefs.begin(BUFFER_NAMESPACE, false)) {
    prefs.clear();
    prefs.end();
    Utils::logMessage("MAIN", "🗑️ Buffer cleared");
  }
}

// Get buffer stats
void printBufferStats() {
  BufferMeta meta = loadBufferMeta();
  float usagePercent = (meta.count * 100.0) / MAX_BUFFERED_READINGS;
  Utils::logMessageF("MAIN", "📊 Buffer: %d/%d readings (%.1f%%)", 
                    meta.count, MAX_BUFFERED_READINGS, usagePercent);
}

// FOR FUTURE WITH VPS BROKER
//  void setupSecureMQTT();

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("ORION ENERGY MONITOR");
  Serial.printf("Version: %s\n", FIRMWARE_VERSION);

  // ✅ Disable watchdog timer
  // disableCore0WDT();
  // disableCore1WDT();
  // Increment boot counter
  bootCount++;
  Utils::logMessageF("MAIN", "Boot count: %lu", bootCount);

  // Generate device ID
  DEVICE_ID = Utils::generateDeviceId();
  Utils::logMessageF("MAIN", "Device ID: %s", DEVICE_ID.c_str());

  Utils::logMessage("MAIN", "Step 1: Initializing hardware...");

  LedManager::begin();
  ButtonManager::begin();
  BatteryManager::begin();
  BatteryManager::update();
  EnergySensor::begin();

  //Register button callback ONCE
  ButtonManager::setImmediateActionCallback([](ButtonManager::ButtonId button, uint32_t duration) -> bool {
  //Don't process buttons during sleep mode
  if (inSleepMode) {
    return false;  // Ignore all button events during sleep
  }

  if (button == ButtonManager::BTN_WIFI_PAIRING && 
      duration >= 3000 && duration < 5000) {
    Utils::logMessage("MAIN", "⚡ Starting WiFi pairing...");
    WifiManager::startPortal(true);  // ✅ Pass true = button triggered
    return true;
  }
  
  if (button == ButtonManager::BTN_HARD_RESET && duration >= 5000) {
    Utils::logMessage("MAIN", "⚡ Performing hard reset...");
    ButtonManager::performHardReset();
    return true;
  }
  
  return false;
});

  Utils::logMessage("MAIN", "Step 2: Checking boot mode...");

  ButtonManager::BootMode bootMode = ButtonManager::detectBootMode();

  // Handle special boot modes
  handleBootMode(bootMode);
  
  Utils::logMessage("MAIN", "Step 3: Initializing  MQTT...");
  // Initialize MQTT
  setupMQTT();

  Utils::logMessage("MAIN", "Step 4: Connecting to WiFi...");
  // Initialize WiFi
  bool forcePortal = (bootMode == ButtonManager::BOOT_WIFI_PAIR);
  WifiManager::begin(forcePortal);

  if (!forcePortal) {
    Utils::logMessage("MAIN", "Waiting for WiFi connection...");
    uint32_t wifiWaitStart = millis();
    while (!WifiManager::isFullyConnected() &&
           (millis() - wifiWaitStart) < 30000) {
      Utils::delayTask(500);
      yield();

    }

    if (WifiManager::isFullyConnected()) {
      Utils::logMessage("MAIN", "WiFi connected!");
    } else {
      Utils::logMessage("MAIN", "WiFi connection timeout, continuing anyway...");
      // ✅ Skip MQTT setup and sensor reading
      lastDataSendTime = Utils::millis64();
      lastTriggerCheck = Utils::millis64();
      lastOtaCheck = Utils::millis64();
      
      Utils::logMessage("MAIN", "SYSTEM READY (AP Mode - No Network)");
      return;  // ✅ Exit setup early
    }
  } else {
    // Portal mode - wait for configuration
    Utils::logMessage("MAIN", "Waiting for WiFi configuration via portal...");
    
    while (WifiManager::getState() == WifiManager::WIFI_STATE_PORTAL_ACTIVE ||
           WifiManager::getState() == WifiManager::WIFI_STATE_AP_MODE) {
      // ✅ Process MQTT while waiting
      MqttManager::loop();
      vTaskDelay(pdMS_TO_TICKS(1000));
      yield();
    }
    
    if (!WifiManager::isFullyConnected()) {
      Utils::logMessage("MAIN", "Portal closed without connection. Restarting...");
      vTaskDelay(pdMS_TO_TICKS(2000));
      ESP.restart();
    }
  }

  Utils::logMessage("MAIN", "Step 5: Initializing OTA...");

  OtaManager::begin();

  if (WifiManager::isFullyConnected()) {
      Utils::logMessage("MAIN", "Checking for updates...");
      
      OtaManager::OtaUpdateInfo info;
      if (OtaManager::checkForUpdate(&info)) {
          Utils::logMessageF("MAIN", "✅ Update available: %s -> %s", 
                            info.currentVersion.c_str(), 
                            info.newVersion.c_str());
          Utils::logMessage("MAIN", "Starting update in 3 seconds...");
          delay(3000);
          OtaManager::performUpdate();
      } else {
          Utils::logMessage("MAIN", "No updates available");
          Utils::logMessageF("MAIN", "Current: %s, Latest: %s", 
                            FIRMWARE_VERSION, 
                            info.newVersion.c_str());
      }
  }

  lastDataSendTime = Utils::millis64();
  lastTriggerCheck = Utils::millis64();
  lastOtaCheck = Utils::millis64();

  Serial.println("SYSTEM READY");
}

/**
 * Configure and initialize MQTT connection
 */
void setupMQTT() {
  // Initialize with broker config
  MqttManager::begin();

  // Set callbacks
  MqttManager::setConnectionCallback([](MqttManager::MqttState state) {
    switch (state) {
    case MqttManager::MQTT_STATE_CONNECTED:
      Utils::logMessage("MQTT", "MQTT connected");
      break;
    case MqttManager::MQTT_STATE_DISCONNECTED:
      Utils::logMessage("MQTT", "MQTT disconnected");
      break;
    case MqttManager::MQTT_STATE_TLS_ERROR:
    case MqttManager::MQTT_STATE_AUTH_ERROR:
      Utils::logMessage("MQTT", "MQTT error");
      break;
    default:
      break;
    }
  });
}

// XXXXXXXXXXXXXXXXXXXXXX-XXXXXXXXXXXXXXXXXXXX-XXXXXXXXXXXXXX
// FOR FUTURE WITH VPS BROKER
// void setupSecureMQTT() {
//   // Configure security settings
//   MqttManager::MqttSecurityConfig security;
//   security.useTLS = MQTT_USE_TLS;
//   security.verifyServer = MQTT_VERIFY_CERTIFICATE;
//   security.useClientCert = false;
//   security.caCert = MQTT_CA_CERT;
//   security.clientCert = nullptr;
//   security.clientKey = nullptr;
//   security.username = MQTT_DEFAULT_USERNAME;
//   security.password = MQTT_DEFAULT_PASSWORD;
//
//   MqttManager::MqttBrokerConfig broker;
//   broker.host = MQTT_BROKER_HOST;
//   broker.port = MQTT_BROKER_PORT;
//   broker.clientIdPrefix = MQTT_CLIENT_ID_PREFIX;
//   broker.keepAlive = MQTT_KEEPALIVE_SEC;
//   broker.bufferSize = MQTT_BUFFER_SIZE;
//   broker.qos = MQTT_QOS_DEFAULT;
//   broker.cleanSession = true;
//
//   // Apply security configuration
//   MqttManager::configureSecurity(security);
//
//   // Initialize with broker config
//   MqttManager::begin(broker);
//
//   // Set callbacks
//   MqttManager::setConnectionCallback([](MqttManager::MqttState state) {
//     switch (state) {
//     case MqttManager::MQTT_STATE_CONNECTED:
//       LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_ON);
//       break;
//     case MqttManager::MQTT_STATE_DISCONNECTED:
//       LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_SLOW);
//       break;
//     case MqttManager::MQTT_STATE_TLS_ERROR:
//     case MqttManager::MQTT_STATE_AUTH_ERROR:
//       LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_FAST);
//       break;
//     default:
//       break;
//     }
//   });
// }
// XXXXXXXXXXXXXXXXXXXXXX-XXXXXXXXXXXXXXXXXXXX-XXXXXXXXXXXXXXo

void takeEnergyMeasurement() {
    // ✅ Tell OTA manager that measurement is starting
    OtaManager::setMeasurementActive(true);
    
    // Your existing measurement code
    EnergySensor::readSensors(currentMetrics);
    
    // ✅ Tell OTA manager that measurement is complete
    OtaManager::setMeasurementActive(false);
}

void loop() {
  uint64_t now = Utils::millis64();
  yield();

  // static bool otaValidated = false;
  // if (!otaValidated && millis() > 30000) {
  //   OtaManager::markFirmwareValid();
  //   otaValidated = true;
  //   Utils::logMessage("MAIN", "OTA firmware marked as valid");
  // }

  if (WifiManager::isPortalActive()) {
    Utils::delayTask(1000);
    return;
  }

  if (!WifiManager::isFullyConnected()) {
    Utils::delayTask(1000);
    return;
  }

  //MQTT reconnection with fallback mode
  static uint32_t mqttDisconnectedAt = 0;
  static bool mqttInFallbackMode = false;
  static uint32_t lastFallbackRetry = 0;
  static bool mqttStateResolved = false; 
  
  if (!MqttManager::isConnected()) {
    // Track when disconnection started
    if (mqttDisconnectedAt == 0) {
      mqttDisconnectedAt = millis();
    }
    
    // After 2 minutes, enter fallback mode
    const uint32_t FALLBACK_THRESHOLD = 60000;  // 1 minute
    if (!mqttInFallbackMode && (millis() - mqttDisconnectedAt) > FALLBACK_THRESHOLD) {
      Utils::logMessage("MAIN", "⚠️ MQTT unavailable for 2+ minutes");
      Utils::logMessage("MAIN", "Entering fallback mode - will retry every 5 minutes");
      mqttInFallbackMode = true;
      mqttStateResolved = true; 
      lastFallbackRetry = millis();
      
      MqttManager::setAutoReconnect(false);
      MqttManager::disconnect();
    }
    
    // In fallback mode, retry every 5 minutes
    if (mqttInFallbackMode) {
      const uint32_t FALLBACK_RETRY = 300000;  // 5 minutes
      
      if (millis() - lastFallbackRetry > FALLBACK_RETRY) {
        Utils::logMessage("MAIN", "🔄 Fallback retry: Testing MQTT connection...");
        lastFallbackRetry = millis();
        
        MqttManager::setAutoReconnect(true);
        MqttManager::reconnect();
        
        uint32_t retryStart = millis();
        while (!MqttManager::isConnected() && (millis() - retryStart) < 20000) {
          MqttManager::loop();
          delay(500);
          yield();
        }
        
        if (!MqttManager::isConnected()) {
          Utils::logMessage("MAIN", "Retry failed - continuing in fallback mode");
          MqttManager::setAutoReconnect(false);
          MqttManager::disconnect();
        } else {
          Utils::logMessage("MAIN", "✅ Retry succeeded!");
        }
      }
    } else {
      // NOT in fallback mode yet - let MQTT try its internal reconnection
      MqttManager::loop();
    }
  } else {
    // Connected! Reset everything
    if (mqttDisconnectedAt > 0) {
      uint32_t downtime = (millis() - mqttDisconnectedAt) / 1000;
      mqttDisconnectedAt = 0;
      mqttInFallbackMode = false;
      MqttManager::setAutoReconnect(true);
    }
    
    mqttStateResolved = true;  
    MqttManager::loop();
  }

  if (!mqttStateResolved) {
    Utils::delayTask(1000);
    return; 
  }

  OtaManager::loop();
  
  // if (MqttManager::isConnected() && now - lastTriggerCheck >= TRIGGER_CHECK_INTERVAL_MS) {
  //   handleTriggers();
  //   lastTriggerCheck = now;
  // }

  if (now - lastDataSendTime >= DATA_SEND_INTERVAL_MS) {
    Utils::logMessage("MAIN", "Sensor reading interval reached");

    BatteryManager::update();
    takeEnergyMeasurement();
    EnergySensor::printMetrics(currentMetrics);
    sendData();
    lastDataSendTime = now;

    // if (MqttManager::isConnected()) {
    //   handleTriggers();
    // }

    OtaManager::loop();
    delay(100);
    
    // ✅ CORRECT - Use namespace prefix
    OtaManager::OtaState otaState = OtaManager::getState();
    if (otaState == OtaManager::OTA_STATE_DOWNLOADING || 
        otaState == OtaManager::OTA_STATE_INSTALLING || 
        otaState == OtaManager::OTA_STATE_VERIFYING) {
        // OTA in progress - skip sleep
        Utils::logMessage("MAIN", "⏭ Skipping sleep (OTA in progress)");
        return;
    }
    else {
        Utils::logMessage("MAIN", "No updates in progress");

    }

    Utils::logMessageF("MAIN", "Sleep check: enabled=%d, pairingMode=%d, portalActive=%d, mqttFailed=%d", 
                      sleepEnabled ? 1 : 0, 
                      WifiManager::isInPairingMode() ? 1 : 0,
                      WifiManager::isPortalActive() ? 1 : 0,
                      mqttInFallbackMode ? 1 : 0);

    if (sleepEnabled && !WifiManager::isInPairingMode() && !WifiManager::isPortalActive()) {
        enterSmartLightSleep();
    } else {
      Utils::logMessage("MAIN", "⏭ Skipping sleep (conditions not met)");
    }
  }

  if (MqttManager::isConnected() && OtaManager::shouldCheckNow(lastOtaCheck)) {
    Utils::logMessage("MAIN", "Scheduled OTA check");
    if (OtaManager::checkForUpdate()) {
      OtaManager::performUpdate();
    }
    lastOtaCheck = now;
  }
  
  Utils::delayTask(100);
}

void handleBootMode(ButtonManager::BootMode mode) {
  switch (mode) {
  case ButtonManager::BOOT_HARD_RESET:
    Utils::logMessage("MAIN", "HARD RESET requested!");
    ButtonManager::performHardReset();
    break;

  case ButtonManager::BOOT_WIFI_PAIR:
    Utils::logMessage("MAIN", "Entering WiFi pairing mode...");
    WifiManager::startPortal(true);
    break;

  case ButtonManager::BOOT_NORMAL:
  default:
    break;
  }
}

// void handleTriggers() {
//   if (!MqttManager::hasPendingTrigger()) {
//     return;
//   }

//   String action = MqttManager::getPendingTrigger();
//   Utils::logMessageF("MAIN", "Processing trigger: %s", action.c_str());

//   if (action == "reset") {
//     Utils::logMessage("MAIN", "Trigger: Reset WiFi credentials");
//     WifiManager::resetCredentials();
//   } else if (action == "config") {
//     Utils::logMessage("MAIN", "Trigger: Enter WiFi config mode");
//     WifiManager::startPortal(true);
//   } else if (action == "reboot") {
//     Utils::logMessage("MAIN", "Trigger: Reboot device");
//     Utils::delayTask(500);
//     ESP.restart();
//   } else if (action == "ota") {
//     Utils::logMessage("MAIN", "Trigger: Force OTA check");
//     if (OtaManager::checkForUpdate()) {
//       OtaManager::performUpdate();
//     }
//   }
//   // configurable sleep options added in case in future if we need something
//   // like this
//   else if (action == "sleep_on") {
//     sleepEnabled = true;
//     Utils::logMessage("MAIN", "Sleep enabled");
//   } else if (action == "sleep_off") {
//     sleepEnabled = false;
//     Utils::logMessage("MAIN", "Sleep disabled");
//   }

//   MqttManager::clearPendingTrigger();
// }

void sendData() {
  //START: Tell OTA we're measuring
  OtaManager::setMeasurementActive(true);

  if (WifiManager::isFullyConnected() && MqttManager::isConnected()) {
    //FIRST: Send any buffered data
    sendBufferedData();
    
    //SECOND: Send current reading
    bool success = MqttManager::publishMetrics(currentMetrics);
    if (success) {
      Utils::logMessage("MAIN", "✅ Current data sent via MQTT");
    } else {
      Utils::logMessage("MAIN", "⚠️ Failed to send current data - buffering");
      bufferReading(currentMetrics);
    }
    
  } else {
    //MQTT not available - buffer the data
    bufferReading(currentMetrics);
  }
  
  // Print buffer stats periodically
  static uint8_t statsCounter = 0;
  if (++statsCounter >= 10) {  // Every 10 readings
    printBufferStats();
    statsCounter = 0;
  }

  //Tell OTA measurement is done
  OtaManager::setMeasurementActive(false);
}

/**
 * Check if buttons are held for required duration
 * Returns: 0 = no action, 1 = WiFi pairing, 2 = Hard reset
 */
uint8_t checkButtonsWithDuration() {
  // Initial debounce
  delay(100);
  
  // Sample buttons multiple times to reject noise
  int resetHighCount = 0;
  int wifiHighCount = 0;
  
  for (int i = 0; i < 10; i++) {
    if (digitalRead(HARD_RESET_BTN) == HIGH) resetHighCount++;
    if (digitalRead(WIFI_PAIRING_BTN) == HIGH) wifiHighCount++;
    delay(10);
  }
  
  // Require 80% of samples to be HIGH to proceed
  bool resetStable = (resetHighCount >= 8);
  bool wifiStable = (wifiHighCount >= 8);
  
  if (!resetStable && !wifiStable) {
    Utils::logMessage("SLEEP", "❌ Noise rejected (unstable signal)");
    return 0;  // Noise - ignore
  }
  
  // Hard Reset Button - requires 5 seconds
  if (resetStable) {
    Utils::logMessage("SLEEP", "🔴 Hard Reset detected - verifying 5s hold...");
    uint32_t holdStart = millis();
    bool stillHeld = true;
    
    // Visual feedback while verifying
    LedManager::setMode(LedManager::LED_BATTERY, LedManager::LED_BLINK_FAST);
    
    while (stillHeld && (millis() - holdStart) < 5000) {
      stillHeld = (digitalRead(HARD_RESET_BTN) == HIGH);
      delay(50);
      yield();
    }
    
    LedManager::setMode(LedManager::LED_BATTERY, LedManager::LED_OFF);
    
    if (stillHeld && (millis() - holdStart) >= 5000) {
      Utils::logMessage("SLEEP", "✅ Hard Reset VERIFIED (5s hold)");
      return 2;  // Hard reset confirmed
    } else {
      Utils::logMessageF("SLEEP", "❌ Hard Reset rejected (held only %lums)", millis() - holdStart);
      return 0;  // Released too early
    }
  }
  
  // WiFi Pairing Button - requires 3 seconds
  if (wifiStable) {
    Utils::logMessage("SLEEP", "🔵 WiFi Pairing detected - verifying 3s hold...");
    uint32_t holdStart = millis();
    bool stillHeld = true;
    
    // Visual feedback while verifying
    LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_FAST);
    
    while (stillHeld && (millis() - holdStart) < 3000) {
      stillHeld = (digitalRead(WIFI_PAIRING_BTN) == HIGH);
      delay(50);
      yield();
    }
    
    LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_OFF);
    
    if (stillHeld && (millis() - holdStart) >= 3000) {
      Utils::logMessage("SLEEP", "✅ WiFi Pairing VERIFIED (3s hold)");
      return 1;  // WiFi pairing confirmed
    } else {
      Utils::logMessageF("SLEEP", "❌ WiFi Pairing rejected (held only %lums)", millis() - holdStart);
      return 0;  // Released too early
    }
  }
  
  return 0;
}

/**
 * Enter smart light sleep with periodic button checks
 * Maintains accurate 15-minute sleep duration even with interruptions
 */
void enterSmartLightSleep() {
  const uint64_t MAIN_SLEEP_MS = WAKEUP_TIME_MS;  // 15 minutes (900000ms)
  const uint64_t CHECK_INTERVAL_MS = 1000;  // Check buttons every 1 second
  
  Utils::logMessageF("MAIN", "💤 Entering smart sleep for %lu seconds...", MAIN_SLEEP_MS / 1000);

  // ✅ Set sleep mode flag to disable button callbacks
  inSleepMode = true;
  
  // Turn off LEDs
  LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_OFF);
  LedManager::setMode(LedManager::LED_CHARGING, LedManager::LED_OFF);
  LedManager::setMode(LedManager::LED_BATTERY, LedManager::LED_OFF);
  Utils::delayTask(100);
  
  // Disconnect MQTT gracefully
  if (MqttManager::isConnected()) {
    MqttManager::disconnect();
    Utils::delayTask(100);
  }
  
  // Disconnect WiFi gracefully
  if (WifiManager::isFullyConnected()) {
    WiFi.disconnect(false);  // Don't erase credentials
    delay(500);
  }
  
  // Initialize sleep tracking
  sleepStartTime = millis();
  totalSleepDuration = MAIN_SLEEP_MS;
  sleepElapsed = 0;
  
  uint32_t checkCount = 0;
  uint32_t noiseCount = 0;
  
  while (sleepElapsed < totalSleepDuration) {
    // Calculate remaining sleep time
    uint64_t remaining = totalSleepDuration - sleepElapsed;
    uint64_t thisSleep = min(CHECK_INTERVAL_MS, remaining);
    
    // Disable all wake sources
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    
    // Enable timer wakeup for this interval
    esp_sleep_enable_timer_wakeup(thisSleep * 1000ULL);
    
    // Enable button wakeup for faster response
    esp_sleep_enable_ext0_wakeup((gpio_num_t)HARD_RESET_BTN, 1);
    uint64_t ext1Mask = (1ULL << WIFI_PAIRING_BTN);
    esp_sleep_enable_ext1_wakeup(ext1Mask, ESP_EXT1_WAKEUP_ANY_HIGH);
    
    // Enter light sleep
    Serial.flush();
    uint64_t sleepIterationStart = millis();
    
    esp_light_sleep_start();
    
    // Calculate actual sleep duration
    uint64_t actualSleepTime = millis() - sleepIterationStart;
    sleepElapsed += actualSleepTime;
    
    // Check wake reason
    esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
    checkCount++;
    
    // If woken by button, check if it's valid
    if (wakeReason == ESP_SLEEP_WAKEUP_EXT0 || wakeReason == ESP_SLEEP_WAKEUP_EXT1) {
      const char* buttonName = (wakeReason == ESP_SLEEP_WAKEUP_EXT0) ? "Hard Reset" : "WiFi Pairing";
      Utils::logMessageF("SLEEP", "🔔 Woken by %s button (check %lu, elapsed %llums/%llums)", 
                         buttonName, checkCount, sleepElapsed, totalSleepDuration);
      
      uint8_t action = checkButtonsWithDuration();
      
      if (action == 2) {
        // Hard Reset verified
        Utils::logMessage("SLEEP", "🔴 HARD RESET ACTION - Exiting sleep");
        Utils::logMessageF("SLEEP", "📊 Sleep statistics: %lu checks, %lu noise events, slept %llums/%llums",
                          checkCount, noiseCount, sleepElapsed, totalSleepDuration);
        
        inSleepMode = false;
        ButtonManager::performHardReset();
        return;  // Will restart
        
      } else if (action == 1) {
        // WiFi Pairing verified
        Utils::logMessage("SLEEP", "🔵 WIFI PAIRING ACTION - Exiting sleep");
        Utils::logMessageF("SLEEP", "📊 Sleep statistics: %lu checks, %lu noise events, slept %llums/%llums",
                          checkCount, noiseCount, sleepElapsed, totalSleepDuration);
        
        inSleepMode = false;  // ✅ Clear flag before reset
        WifiManager::startPortal(true);
        return;  
        
      } else {
        noiseCount++;
        Utils::logMessageF("SLEEP", "⏰ Resuming sleep (%llu ms remaining)", totalSleepDuration - sleepElapsed);
      }
    }
    // If woken by timer (normal periodic check), just continue
    
    yield();
  }
  
  // Sleep period complete
  uint64_t actualTotal = millis() - sleepStartTime;
  
  Utils::logMessage("SLEEP", "======================================");
  Utils::logMessageF("SLEEP", "📊 Sleep Summary:");
  Utils::logMessageF("SLEEP", "   Target:       %llu ms (%lu min)", totalSleepDuration, totalSleepDuration / 60000);
  Utils::logMessageF("SLEEP", "   Actual:       %llu ms (%lu min)", actualTotal, actualTotal / 60000);
  Utils::logMessageF("SLEEP", "   Efficiency:   %.1f%%", (sleepElapsed * 100.0) / totalSleepDuration);
  Utils::logMessageF("SLEEP", "   Checks:       %lu", checkCount);
  Utils::logMessageF("SLEEP", "   Noise events: %lu", noiseCount);
  Utils::logMessage("SLEEP", "======================================");
  
  // ✅ Clear sleep mode flag
  inSleepMode = false;
  
  // Wake-up procedure
  Utils::logMessage("MAIN", "🌅 Wake-up sequence starting...");
  
  // Normal wake - reconnect everything
  Utils::logMessage("MAIN", "📡 Phase 1: Reconnecting WiFi...");
  WiFi.mode(WIFI_STA);
  WifiManager::forceReconnect();
  
  uint32_t wifiStart = millis();
  while (!WifiManager::isFullyConnected() && (millis() - wifiStart) < 15000) {
    Utils::delayTask(500);
    yield();
  }
  
  if (!WifiManager::isFullyConnected()) {
    Utils::logMessage("MAIN", "⚠️ WiFi reconnection failed");
    BatteryManager::update();
    return;
  }
  
  Utils::logMessage("MAIN", "✅ WiFi reconnected");
  delay(3000);  // TCP/IP stack stabilization
  
  Utils::logMessage("MAIN", "📡 Phase 2: Reconnecting MQTT...");
  
  uint32_t mqttStart = millis();
  const uint32_t MQTT_TIMEOUT = 20000;
  const uint32_t RETRY_INTERVAL = 3000;
  uint32_t lastAttempt = 0;
  int attempts = 0;
  
  while (!MqttManager::isConnected() && (millis() - mqttStart) < MQTT_TIMEOUT) {
    if (millis() - lastAttempt >= RETRY_INTERVAL) {
      attempts++;
      Utils::logMessageF("MAIN", "MQTT attempt %d...", attempts);
      MqttManager::reconnect();
      lastAttempt = millis();
    }
    
    MqttManager::loop();
    delay(100);
    yield();
  }
  
  if (!MqttManager::isConnected()) {
    Utils::logMessage("MAIN", "⚠️ MQTT reconnection failed");
  } else {
    Utils::logMessage("MAIN", "✅ MQTT reconnected");
  }
  
  BatteryManager::update();
  Utils::logMessage("MAIN", "🎉 Wake-up complete!");
}

// void enterLightSleep() {

//   uint32_t sleepDurationSec = WAKEUP_TIME_MS / 1000;
//   Utils::logMessageF("MAIN", "Entering light sleep for %lu seconds...",
//                      sleepDurationSec);

//   // turn off all led other then power
//   LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_OFF);
//   LedManager::setMode(LedManager::LED_CHARGING, LedManager::LED_OFF);
//   LedManager::setMode(LedManager::LED_BATTERY, LedManager::LED_OFF);
//   Utils::delayTask(100);

//   if (MqttManager::isConnected()) {
//     MqttManager::disconnect();
//     Utils::delayTask(100);
//   }

//   // ✅ Gracefully disconnect WiFi
//   if (WifiManager::isFullyConnected()) {
//     WiFi.disconnect(false);  // Don't erase credentials
//     delay(500);
//   }

//   // Disable all wake sources first
//   esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

//   uint64_t sleepTimeUs = (uint64_t)WAKEUP_TIME_MS * 1000ULL;
//   esp_sleep_enable_timer_wakeup(sleepTimeUs);

//   // wake up on hard reset button and on wifi pairing button
//   esp_sleep_enable_ext0_wakeup((gpio_num_t)HARD_RESET_BTN, 1);
//   uint64_t ext1Mask = (1ULL << WIFI_PAIRING_BTN);
//   esp_sleep_enable_ext1_wakeup(ext1Mask, ESP_EXT1_WAKEUP_ANY_HIGH);

//   Serial.flush();
//   uint64_t sleepStart = millis();

//   esp_light_sleep_start();

//   uint64_t sleepDuration = millis() - sleepStart;
//   esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();

//   const char *reasonStr = "Unknown";
//   switch (wakeReason) {
//   case ESP_SLEEP_WAKEUP_TIMER:
//     reasonStr = "Timer";
//     break;
//   case ESP_SLEEP_WAKEUP_EXT0:
//     reasonStr = "Hard Reset Button";
//     break;
//   case ESP_SLEEP_WAKEUP_EXT1:
//     reasonStr = "WiFi Button";
//     break;
//   default:
//     break;
//   }

//   Utils::logMessageF("MAIN", "Woke up after %lu ms, reason: %s",
//                      (unsigned long)sleepDuration, reasonStr);

// if (wakeReason == ESP_SLEEP_WAKEUP_EXT0) {
//   Utils::logMessage("MAIN", "Woke by Hard Reset button - verifying hold...");
  
//   // ✅ VERIFY the button is STILL held down
//   if (ButtonManager::waitForLongPress(ButtonManager::BTN_HARD_RESET, 5000)) {
//     ButtonManager::performHardReset();
//   } else {
//     Utils::logMessage("MAIN", "False trigger - button not held");
//   }
//   return;
// }

// if (wakeReason == ESP_SLEEP_WAKEUP_EXT1) {
//   Utils::logMessage("MAIN", "Woke by WiFi button - verifying hold...");
  
//   // ✅ VERIFY the button is STILL held down
//   if (ButtonManager::waitForLongPress(ButtonManager::BTN_WIFI_PAIRING, 3000)) {
//     WifiManager::startPortal(true);
//   } else {
//     Utils::logMessage("MAIN", "False trigger - button not held");
//   }
//   return;
// }

//   // Reconnect WiFi
//   Utils::logMessage("MAIN", "Reconnecting WiFi...");
//   WiFi.mode(WIFI_STA);
//   WifiManager::forceReconnect();

//   uint32_t wifiStart = millis();
//   while (!WifiManager::isFullyConnected() && (millis() - wifiStart) < 15000) {
//     Utils::delayTask(500);
//     yield();
//   }

//   // ✅ CHECK if WiFi actually reconnected
//   if (!WifiManager::isFullyConnected()) {
//     Utils::logMessage("MAIN", "⚠️ WiFi reconnection failed after sleep");
//     BatteryManager::update();
//     return;  // Exit and try again on next wake
//   }

//   Utils::logMessage("MAIN", "✅ WiFi reconnected successfully");
//   delay(3000);  // ✅ Give TCP/IP stack time to stabilize

//   // Handle portal mode if button was pressed
//   if (WifiManager::isInPairingMode()) {  // ✅ USE FUNCTION INSTEAD
//     Utils::logMessage("MAIN", "Starting portal (button during sleep)");
//     WifiManager::startPortal(true);
//     return;
//   }

//   // ===== PHASE 2: RECONNECT MQTT =====
//   Utils::logMessage("MAIN", "📡 Phase 2: Reconnecting MQTT...");
  
//   uint32_t mqttStart = millis();
//   const uint32_t MQTT_TIMEOUT = 20000;
//   const uint32_t RETRY_INTERVAL = 3000;
//   uint32_t lastAttempt = 0;
//   int attempts = 0;
  
//   while (!MqttManager::isConnected() && (millis() - mqttStart) < MQTT_TIMEOUT) {
//     if (millis() - lastAttempt >= RETRY_INTERVAL) {
//       attempts++;
//       Utils::logMessageF("MAIN", "MQTT attempt %d...", attempts);
//       MqttManager::reconnect();
//       lastAttempt = millis();
//     }
    
//     MqttManager::loop();
//     delay(100);
//     yield();
//   }

//   if (!MqttManager::isConnected()) {
//     Utils::logMessage("MAIN", "⚠️ MQTT reconnection failed after sleep");
//   } else {
//     Utils::logMessage("MAIN", "✅ MQTT reconnected successfully");
//   }

//   BatteryManager::update();
//   Utils::logMessage("MAIN", "🎉 Wake-up reconnection complete!");
// }
