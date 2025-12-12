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

// Global device ID
String DEVICE_ID;

RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR uint64_t savedRunTime = 0;

static uint64_t lastDataSendTime = 0;
static uint64_t lastTriggerCheck = 0;
static uint64_t lastOtaCheck = 0;

// Current metrics
static EnergySensor::EnergyMetrics currentMetrics;

void handleBootMode(ButtonManager::BootMode mode);
void handleTriggers();
void sendData();
void enterLightSleep();
void setupMQTT();

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

  Utils::logMessage("MAIN", "Step 2: Checking boot mode...");

  ButtonManager::BootMode bootMode = ButtonManager::detectBootMode();

  // Handle special boot modes
  handleBootMode(bootMode);

  Utils::logMessage("MAIN", "Step 3: Connecting to WiFi...");
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
      Utils::logMessage("MAIN",
                        "WiFi connection timeout, continuing anyway...");
    }
  }

  Utils::logMessage("MAIN", "Step 4: Connecting to MQTT broker...");
  // Initialize MQTT
  setupMQTT();
  // FOR FUTURE WITH VPS BROKER
  // setupSecureMQTT();
  if (WifiManager::isFullyConnected()) {
    uint32_t mqttWaitStart = millis();
    while (!MqttManager::isConnected() && (millis() - mqttWaitStart) < 10000) {
      MqttManager::loop();
      vTaskDelay(pdMS_TO_TICKS(100));
      yield();
    }
    
    if (MqttManager::isConnected()) {
      Utils::logMessage("MAIN", "✓ MQTT connected successfully!");
    } else {
      Utils::logMessage("MAIN", "✗ MQTT connection timeout!");
      Utils::logMessageF("MAIN", "  Error: %s", MqttManager::getLastError().c_str());
    }
  }

  Utils::logMessage("MAIN", "Step 5: Initializing OTA...");
  // Initialize OTA manager
  OtaManager::begin();

  Utils::logMessage("MAIN", "Step 6: Taking initial sensor reading...");
  
  EnergySensor::readSensors(currentMetrics);
  EnergySensor::printMetrics(currentMetrics);

  // ✅ Send initial data (MQTT should be connected now)
  sendData();

  // delay(30000);
  // OtaManager::markFirmwareValid();

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
      LedManager::setMode(LedManager::LED_BATTERY, LedManager::LED_ON);
      break;
    case MqttManager::MQTT_STATE_DISCONNECTED:
      LedManager::setMode(LedManager::LED_BATTERY, LedManager::LED_BLINK_SLOW);
      break;
    case MqttManager::MQTT_STATE_TLS_ERROR:
    case MqttManager::MQTT_STATE_AUTH_ERROR:
      LedManager::setMode(LedManager::LED_BATTERY, LedManager::LED_BLINK_FAST);
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

void printDiagnostics() {
  Serial.println("\n========== DIAGNOSTICS ==========");
  
  // System
  Serial.printf("Uptime: %llu ms\n", Utils::millis64());
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  
  // WiFi
  Serial.println("\n--- WiFi ---");
  Serial.printf("Connected: %s\n", WiFi.isConnected() ? "YES" : "NO");
  Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  Serial.printf("State: %d\n", WifiManager::getState());
  
  // MQTT
  Serial.println("\n--- MQTT ---");
  Serial.printf("Connected: %s\n", MqttManager::isConnected() ? "YES" : "NO");
  Serial.printf("State: %s\n", MqttManager::stateToString(MqttManager::getState()));
  
  // Battery
  Serial.println("\n--- Battery ---");
  Serial.printf("Level: %.1f%%\n", BatteryManager::getPercentage());
  Serial.printf("Charging: %s\n", BatteryManager::isCharging() ? "YES" : "NO");
  Serial.printf("Voltage: %.2f V\n", BatteryManager::getVoltage());
  
  // LEDs
  Serial.println("\n--- LEDs ---");
  const char* ledNames[] = {"Power", "WiFi", "Charging", "Battery"};
  for (int i = 0; i < 4; i++) {
    Serial.printf("%s LED: mode=%d, state=%d\n", 
                  ledNames[i],
                  LedManager::getMode((LedManager::LedType)i),
                  digitalRead(i == 0 ? 14 : (i == 1 ? 27 : (i == 2 ? 26 : 25))));
  }
  
  Serial.println("=================================\n");
}

void loop() {
  uint64_t now = Utils::millis64();
  yield();  // ✅ Feed watchdog

  static bool otaValidated = false;
  if (!otaValidated && millis() > 30000) {
    OtaManager::markFirmwareValid();
    otaValidated = true;
    Utils::logMessage("MAIN", "OTA firmware marked as valid");
  }
  // Process MQTT messages
  MqttManager::loop();
  OtaManager::loop();
  
  // ✅ CHECK BUTTONS IMMEDIATELY (highest priority)
  ButtonManager::ButtonEvent buttonEvent = ButtonManager::checkButtons();

   // ✅ WiFi Pairing - 3 second hold
  if (buttonEvent == ButtonManager::BTN_EVENT_LONG_PRESS) {
    Utils::logMessage("MAIN", "⚡ WIFI PAIRING MODE ACTIVATED!");
    
    // Disconnect MQTT cleanly
    MqttManager::disconnect();
    
    // ✅ Save current WiFi credentials as backup
    String oldSSID = WiFi.SSID();
    String oldPassword = WiFi.psk();
    bool hadCredentials = (oldSSID.length() > 0);
    
    if (hadCredentials) {
      Utils::logMessageF("MAIN", "Current WiFi: %s (keeping as backup)", oldSSID.c_str());
    } else {
      Utils::logMessage("MAIN", "No existing WiFi credentials");
    }
    
  // ✅ CRITICAL: Stop WiFi completely
  WiFi.disconnect(true);  // true = erase flash credentials temporarily
  WiFi.mode(WIFI_OFF);
  vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Start portal
    Utils::logMessage("MAIN", "Starting configuration portal...");
    Utils::logMessage("MAIN", "Connect to 'OrionSetup' AP to configure WiFi");
    WifiManager::startPortal();
    
    // Wait for user to configure WiFi
    while (WifiManager::getState() == WifiManager::WIFI_STATE_PORTAL_ACTIVE ||
           WifiManager::getState() == WifiManager::WIFI_STATE_AP_MODE) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      yield();
    }
    
    // ✅ Check if new WiFi was configured and connected
    if (WifiManager::isFullyConnected()) {
      String newSSID = WiFi.SSID();
      
      Utils::logMessage("MAIN", "✓ WiFi configured successfully!");
      Utils::logMessageF("MAIN", "  Connected to: %s", newSSID.c_str());
      Utils::logMessageF("MAIN", "  IP Address: %s", WiFi.localIP().toString().c_str());
      
      // If we switched to a different network, inform user
      if (hadCredentials && newSSID != oldSSID) {
        Utils::logMessageF("MAIN", "  Switched from '%s' to '%s'", 
                          oldSSID.c_str(), newSSID.c_str());
      }
      
      // Reconnect MQTT
      Utils::logMessage("MAIN", "Reconnecting MQTT...");
      MqttManager::reconnect();
      
      // Wait for MQTT
      uint32_t mqttWaitStart = millis();
      while (!MqttManager::isConnected() && (millis() - mqttWaitStart) < 10000) {
        MqttManager::loop();
        vTaskDelay(pdMS_TO_TICKS(100));
        yield();
      }
      
      if (MqttManager::isConnected()) {
        Utils::logMessage("MAIN", "✓ MQTT reconnected!");
      } else {
        Utils::logMessage("MAIN", "✗ MQTT reconnection failed");
      }
    } 
    // ✅ Portal closed without connecting
    else {
      Utils::logMessage("MAIN", "Portal closed without configuration");
      
      // Try to reconnect to old WiFi if we had one
      if (hadCredentials) {
        Utils::logMessageF("MAIN", "Attempting to reconnect to '%s'...", oldSSID.c_str());
        
        // Try reconnecting to the old network
        WiFi.begin(oldSSID.c_str(), oldPassword.c_str());
        
        uint32_t reconnectStart = millis();
        while (!WifiManager::isFullyConnected() && (millis() - reconnectStart) < 15000) {
          vTaskDelay(pdMS_TO_TICKS(500));
          yield();
        }
        
        if (WifiManager::isFullyConnected()) {
          Utils::logMessage("MAIN", "✓ Reconnected to previous WiFi");
          
          // Reconnect MQTT
          MqttManager::reconnect();
        } else {
          Utils::logMessage("MAIN", "✗ Failed to reconnect to previous WiFi");
        }
      } else {
        Utils::logMessage("MAIN", "No previous WiFi to reconnect to");
      }
    }
  }
  
  // ✅ Hard Reset - 5 second hold
  else if (buttonEvent == ButtonManager::BTN_EVENT_VERY_LONG_PRESS) {
    Utils::logMessage("MAIN", "⚡ FACTORY RESET ACTIVATED!");
    ButtonManager::performHardReset();
    // Device will restart
  }

  // // In loop(), add a button check:
  // if (digitalRead(WIFI_PAIRING_BTN) == LOW) {
  //   delay(50);  // Debounce
  //   if (digitalRead(WIFI_PAIRING_BTN) == LOW) {
  //     printDiagnostics();
  //     while (digitalRead(WIFI_PAIRING_BTN) == LOW) {
  //       delay(10);
  //     }
  //   }
  // }

  // Check for sensor reading interval
  if (now - lastDataSendTime >= DATA_SEND_INTERVAL_MS) {
    Utils::logMessage("MAIN", "Sensor reading interval reached");

    // Update battery status
    BatteryManager::update();

    // Read sensors
    EnergySensor::readSensors(currentMetrics);
    EnergySensor::printMetrics(currentMetrics);

    // Send data
    sendData();

    lastDataSendTime = now;
  }

  // Check for MQTT triggers
  if (now - lastTriggerCheck >= TRIGGER_CHECK_INTERVAL_MS) {
    handleTriggers();
    lastTriggerCheck = now;
  }

  // Check for OTA updates periodically
  if (OtaManager::shouldCheckNow(lastOtaCheck)) {
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
    WifiManager::startPortal();
    break;

  case ButtonManager::BOOT_NORMAL:
  default:
    Utils::logMessage("MAIN", "Normal boot");
    break;
  }
}

void handleTriggers() {
  if (!MqttManager::hasPendingTrigger()) {
    return;
  }

  String action = MqttManager::getPendingTrigger();
  Utils::logMessageF("MAIN", "Processing trigger: %s", action.c_str());

  if (action == "reset") {
    Utils::logMessage("MAIN", "Trigger: Reset WiFi credentials");
    WifiManager::resetCredentials();
  } else if (action == "config") {
    Utils::logMessage("MAIN", "Trigger: Enter WiFi config mode");
    WifiManager::startPortal();
  } else if (action == "reboot") {
    Utils::logMessage("MAIN", "Trigger: Reboot device");
    Utils::delayTask(500);
    ESP.restart();
  } else if (action == "ota") {
    Utils::logMessage("MAIN", "Trigger: Force OTA check");
    if (OtaManager::checkForUpdate()) {
      OtaManager::performUpdate();
    }
  }

  MqttManager::clearPendingTrigger();
}

void sendData() {
  bool sent = false;

  if (WifiManager::isFullyConnected() && MqttManager::isConnected()) {
    sent = MqttManager::publishMetrics(currentMetrics);
    if (sent) {
      Utils::logMessage("MAIN", "Data sent via MQTT");
    }
  }

  if (!sent) {
    Utils::logMessage("MAIN", "Warning: No transmission channel available");
  }
}
