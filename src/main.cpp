#include <Arduino.h>

#include "battery_manager.h"
#include "ble_manager.h"
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

  // Generate device ID
  DEVICE_ID = Utils::generateDeviceId();
  Utils::logMessageF("MAIN", "Device ID: %s", DEVICE_ID.c_str());

  // Increment boot counter
  bootCount++;
  Utils::logMessageF("MAIN", "Boot count: %lu", bootCount);

  LedManager::begin();
  LedManager::runStartupSequence();

  // Initialize button manager
  ButtonManager::begin();

  // Detect boot mode from buttons/wake source
  ButtonManager::BootMode bootMode = ButtonManager::detectBootMode();

  // Handle special boot modes
  handleBootMode(bootMode);

  // Initialize battery manager
  BatteryManager::begin();
  BatteryManager::update();

  // Initialize energy sensors
  EnergySensor::begin();

  // Initialize BLE
  // BleManager::begin();

  // Initialize WiFi
  bool forcePortal = (bootMode == ButtonManager::BOOT_WIFI_PAIR);
  WifiManager::begin(forcePortal);
  if (!forcePortal) {
    Utils::logMessage("MAIN", "Waiting for WiFi connection...");
    uint32_t wifiWaitStart = millis();
    while (!WifiManager::isFullyConnected() &&
           (millis() - wifiWaitStart) < 30000) {
      Utils::delayTask(500);
    }

    if (WifiManager::isFullyConnected()) {
      Utils::logMessage("MAIN", "WiFi connected!");
    } else {
      Utils::logMessage("MAIN",
                        "WiFi connection timeout, continuing anyway...");
    }
  }
  // Initialize MQTT
  setupMQTT();
  // FOR FUTURE WITH VPS BROKER
  // setupSecureMQTT();

  // Initialize OTA manager
  OtaManager::begin();

  delay(30000);
  OtaManager::markFirmwareValid();

  // Initial data reading
  Utils::logMessage("MAIN", "Taking initial sensor reading...");
  EnergySensor::readSensors(currentMetrics);
  EnergySensor::printMetrics(currentMetrics);
  sendData();

  lastDataSendTime = Utils::millis64();
  lastTriggerCheck = Utils::millis64();

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
      LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_ON);
      break;
    case MqttManager::MQTT_STATE_DISCONNECTED:
      LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_SLOW);
      break;
    case MqttManager::MQTT_STATE_TLS_ERROR:
    case MqttManager::MQTT_STATE_AUTH_ERROR:
      LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_FAST);
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

void loop() {
  uint64_t now = Utils::millis64();

  // Process MQTT messages
  MqttManager::loop();

  OtaManager::loop();

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

  Utils::delayTask(10000);
}

void handleBootMode(ButtonManager::BootMode mode) {
  switch (mode) {
  case ButtonManager::BOOT_HARD_RESET:
    ButtonManager::performHardReset();
    break;

  case ButtonManager::BOOT_WIFI_PAIR:
    Utils::logMessage("MAIN", "Entering WiFi pairing mode...");
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
    // Won't return - device restarts
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

  // if not connected to mqtt
  if (!sent && BleManager::isClientConnected()) {
    sent = BleManager::sendMetrics(currentMetrics);
    if (sent) {
      Utils::logMessage("MAIN", "Data sent via BLE");
    }
  }

  if (!sent) {
    Utils::logMessage("MAIN", "Warning: No transmission channel available");
  }
}
