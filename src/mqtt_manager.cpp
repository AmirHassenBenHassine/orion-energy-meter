#include "mqtt_manager.h"
#include "config.h"
#include "utils.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

namespace MqttManager {

// CONSTANTS
// FOR FUTURE SECURE HOSTED BROKER
static const char *PREF_KEY_USER = "user";
static const char *PREF_KEY_PASS = "pass";

// Network clients
static WiFiClientSecure *_secureClient = nullptr;
static WiFiClient *_insecureClient = nullptr;
static PubSubClient *_mqttClient = nullptr;

// Configuration
static MqttBrokerConfig _brokerConfig;
static MqttSecurityConfig _securityConfig;
static bool _configuredSecurity = false;
static bool _autoReconnectEnabled = true;

// Runtime credentials
static char _username[64] = {0};
static char _password[64] = {0};
static char _brokerHost[128] = {0};
static uint16_t _brokerPort = 0;

// State
static volatile MqttState _currentState = MQTT_STATE_DISCONNECTED;
static volatile bool _initialized = false;
static String _lastError = "";

static MessageCallback _messageCallback = nullptr;
static TriggerCallback _triggerCallback = nullptr;
static ConnectionCallback _connectionCallback = nullptr;

// Pending trigger
static volatile bool _hasTrigger = false;
static String _pendingTrigger = "";

static volatile bool _hasNewCredentials = false;
static String _newSSID = "";
static String _newPassword = "";

// Statistics
static MqttStats _stats;

// Reconnection tracking
static uint64_t _lastReconnectAttempt = 0;
static uint32_t _reconnectAttempts = 0;

// At the top with other static variables
static volatile bool _waitingForConfirmation = false;
static volatile bool _confirmationReceived = false;
static String _confirmedSSID = "";

static void _mqttCallback(char *topic, byte *payload, unsigned int length);
static bool _connect();
static void _subscribeToTopics();
static void _setState(MqttState newState);
static String _generateClientId();
static void _setupTLS();

bool isWaitingForConfirmation() {
  return _waitingForConfirmation;
}

bool hasReceivedConfirmation() {
  return _confirmationReceived;
}

String getConfirmedSSID() {
  return _confirmedSSID;
}

void setAutoReconnect(bool enabled) {
  _autoReconnectEnabled = enabled;
  if (!enabled) {
    // Reset reconnection tracking when disabling
    _reconnectAttempts = 0;
    _lastReconnectAttempt = 0;
  }
}

void clearConfirmation() {
  _waitingForConfirmation = false;
  _confirmationReceived = false;
  _confirmedSSID = "";
}

bool begin() {
  MqttBrokerConfig defaultBroker;
  defaultBroker.host = MQTT_BROKER_HOST;
  defaultBroker.port = MQTT_BROKER_PORT;
  defaultBroker.clientIdPrefix = MQTT_CLIENT_ID_PREFIX;
  defaultBroker.keepAlive = MQTT_KEEPALIVE_SEC;
  defaultBroker.bufferSize = MQTT_BUFFER_SIZE;
  defaultBroker.qos = MQTT_QOS_DEFAULT;
  defaultBroker.cleanSession = true;

  return begin(defaultBroker);
}

bool begin(const MqttBrokerConfig &brokerConfig) {
  if (_initialized) {
    Utils::logMessage("MQTT", "Already initialized");
    return true;
  }

  _brokerConfig = brokerConfig;
  strncpy(_brokerHost, brokerConfig.host, sizeof(_brokerHost) - 1);
  _brokerPort = brokerConfig.port;

  // Load or use default credentials
  if (!loadCredentials(_username, _password, sizeof(_username))) {
    if (_configuredSecurity && _securityConfig.username) {
      strncpy(_username, _securityConfig.username, sizeof(_username) - 1);
    }
    if (_configuredSecurity && _securityConfig.password) {
      strncpy(_password, _securityConfig.password, sizeof(_password) - 1);
    }
    
    // ✅ If still empty, use config.h defaults
    if (strlen(_username) == 0 && strlen(MQTT_DEFAULT_USERNAME) > 0) {
      strncpy(_username, MQTT_DEFAULT_USERNAME, sizeof(_username) - 1);
    }
    if (strlen(_password) == 0 && strlen(MQTT_DEFAULT_PASSWORD) > 0) {
      strncpy(_password, MQTT_DEFAULT_PASSWORD, sizeof(_password) - 1);
    }
  }

  // ✅ Log what credentials we're using (masked password)
  if (strlen(_username) < 0) {
    Utils::logMessage("MQTT", "WARNING: No credentials configured!");
  }

  // Create client
  if (_configuredSecurity && _securityConfig.useTLS) {
    Utils::logMessage("MQTT", "Using TLS connection");
    _secureClient = new WiFiClientSecure();
    _setupTLS();
    _mqttClient = new PubSubClient(*_secureClient);
  } else {
    _insecureClient = new WiFiClient();
    _mqttClient = new PubSubClient(*_insecureClient);
  }

  if (_mqttClient == nullptr) {
    _lastError = "Failed to create MQTT client";
    return false;
  }

  _mqttClient->setServer(_brokerHost, _brokerPort);
  _mqttClient->setCallback(_mqttCallback);
  _mqttClient->setBufferSize(brokerConfig.bufferSize);
  _mqttClient->setKeepAlive(brokerConfig.keepAlive);

  memset(&_stats, 0, sizeof(_stats));

  _initialized = true;
  _currentState = MQTT_STATE_DISCONNECTED;

  Utils::logMessage("MQTT", "MQTT Manager initialized");
  Utils::logMessageF("MQTT", "Broker: %s:%d", _brokerHost, _brokerPort);

  return true;
}

void configureSecurity(const MqttSecurityConfig &securityConfig) {
  _securityConfig = securityConfig;
  _configuredSecurity = true;

  if (securityConfig.username && strlen(securityConfig.username) > 0) {
    strncpy(_username, securityConfig.username, sizeof(_username) - 1);
  }
  if (securityConfig.password && strlen(securityConfig.password) > 0) {
    strncpy(_password, securityConfig.password, sizeof(_password) - 1);
  }
}

bool configure(const MqttBrokerConfig &brokerConfig,
               const MqttSecurityConfig &securityConfig) {
  configureSecurity(securityConfig);
  return begin(brokerConfig);
}

void stop() {
  if (_mqttClient != nullptr) {
    if (_mqttClient->connected()) {
      _mqttClient->disconnect();
    }
    delete _mqttClient;
    _mqttClient = nullptr;
  }

  if (_secureClient != nullptr) {
    delete _secureClient;
    _secureClient = nullptr;
  }

  if (_insecureClient != nullptr) {
    delete _insecureClient;
    _insecureClient = nullptr;
  }

  _initialized = false;
  _currentState = MQTT_STATE_DISCONNECTED;
}

void loop() {
  if (!_initialized || _mqttClient == nullptr) {
    return;
  }

  // ✅ Check if we're in AP mode
  WiFiMode_t mode = WiFi.getMode();
  bool isAPMode = (mode == WIFI_AP || mode == WIFI_AP_STA);
  
  // ✅ In AP mode, we don't need isFullyConnected check
  if (!isAPMode && !WifiManager::isFullyConnected()) {
    if (_currentState == MQTT_STATE_CONNECTED) {
      _setState(MQTT_STATE_DISCONNECTED);
      _stats.disconnects++;
    }
    return;
  }

  if (!_mqttClient->connected()) {
    if (_currentState == MQTT_STATE_CONNECTED) {
      _setState(MQTT_STATE_DISCONNECTED);
      _stats.disconnects++;
    }
    // ✅ Only auto-reconnect if enabled
    if (_autoReconnectEnabled) {
      uint64_t now = Utils::millis64();
      uint32_t backoffDelay =
      MQTT_RECONNECT_DELAY_MS * min(_reconnectAttempts + 1, (uint32_t)5);

    if (now - _lastReconnectAttempt > backoffDelay) {
      _lastReconnectAttempt = now;
      _connect();
    }
  }
 } else {
    _mqttClient->loop();

    if (_reconnectAttempts > 0) {
      _reconnectAttempts = 0;
    }
  }

  // Handle new WiFi credentials
  if (_hasNewCredentials) {
    Utils::logMessageF("MQTT", "New WiFi credentials: %s", _newSSID.c_str());
    publishConfirmation(_newSSID);
    Utils::delayTask(1000);

    WifiManager::saveCredentials(_newSSID.c_str(), _newPassword.c_str());
    _hasNewCredentials = false;

    if (WifiManager::connectWithCredentials(_newSSID.c_str(),
                                            _newPassword.c_str())) {
      _connect();
    } else {
      Utils::delayTask(500);
      ESP.restart();
    }
  }
}

bool isConnected() {
  return _mqttClient != nullptr && _mqttClient->connected();
}

MqttState getState() { return _currentState; }

void reconnect() {
  if (_mqttClient != nullptr && _mqttClient->connected()) {
    _mqttClient->disconnect();
  }
  _setState(MQTT_STATE_DISCONNECTED);
  _reconnectAttempts = 0;
  _lastReconnectAttempt = 0;
  _connect();
}

void disconnect() {
  if (_mqttClient != nullptr && _mqttClient->connected()) {
    _mqttClient->disconnect();
  }
  _setState(MQTT_STATE_DISCONNECTED);
}

void setCredentials(const char *username, const char *password) {
  if (username)
    strncpy(_username, username, sizeof(_username) - 1);
  if (password)
    strncpy(_password, password, sizeof(_password) - 1);
}

void setBroker(const char *host, uint16_t port) {
  if (host)
    strncpy(_brokerHost, host, sizeof(_brokerHost) - 1);
  _brokerPort = port;
  if (_mqttClient != nullptr) {
    _mqttClient->setServer(_brokerHost, _brokerPort);
  }
}

bool publishMetrics(const EnergySensor::EnergyMetrics &metrics) {
  if (!isConnected()) {
    _stats.messagesFailed++;
    return false;
  }

  String payload = metrics.toJson();
  return publish(MQTT_TOPIC_ENERGY_METRICS, payload.c_str(), false,
                 _brokerConfig.qos);
}

bool publish(const char *topic, const char *payload, bool retained,
             uint8_t qos) {
  if (!isConnected()) {
    _stats.messagesFailed++;
    return false;
  }

  bool success = _mqttClient->publish(topic, payload, retained);

  if (success) {
    _stats.messagesPublished++;
  } else {
    _stats.messagesFailed++;
  }

  return success;
}

bool publishStatus(const String &mode, const String &ip) {
  JsonDocument doc;
  doc["deviceId"] = Utils::generateDeviceId();
  doc["mode"] = mode;
  doc["ip"] = ip;
  doc["rssi"] = WiFi.RSSI();

  String payload;
  serializeJson(doc, payload);

  return publish(MQTT_TOPIC_STATUS, payload.c_str(), true, 1);
}

bool publishScanResults(const String &networksJson) {
  return publish(MQTT_TOPIC_SCAN, networksJson.c_str(), false, 0);
}

bool publishConfirmation(const String &ssid) {
  JsonDocument doc;
  doc["status"] = "success";
  doc["ssid"] = ssid;

  String payload;
  serializeJson(doc, payload);

  return publish(MQTT_TOPIC_CONFIRM, payload.c_str(), true, 1);
}

bool publishWiFiCredentials(const String &ssid, const String &password) {
  if (!isConnected()) {
    Utils::logMessage("MQTT", "Cannot send credentials - not connected");
    return false;
  }

  // ✅ Log connection state
  Utils::logMessageF("MQTT", "MQTT state: %s", stateToString(_currentState));
  Utils::logMessageF("MQTT", "WiFi state: %s", WiFi.isConnected() ? "connected" : "disconnected");
  
  JsonDocument doc;
  doc["ssid"] = ssid;
  doc["password"] = password;

  String payload;
  serializeJson(doc, payload);

  Utils::logMessageF("MQTT", "Sending WiFi credentials for SSID: %s", ssid.c_str());
  
  // ✅ Publish credentials (NOT retained)
  bool success = _mqttClient->publish(MQTT_TOPIC_WIFI_CREDENTIALS, payload.c_str(), false); 
  
  if (success) {
    Utils::logMessage("MQTT", "✓ Credentials sent to OrangePi");
    _stats.messagesPublished++;

    // ✅ Set waiting flag
    _waitingForConfirmation = true;
    _confirmationReceived = false;
    _confirmedSSID = "";

    vTaskDelay(pdMS_TO_TICKS(500)); 
    _mqttClient->publish(MQTT_TOPIC_WIFI_CREDENTIALS, "", true);  // Empty + retained = clear
    Utils::logMessage("MQTT", "✓ Credentials topic cleared");
  } else {
    Utils::logMessage("MQTT", "✗ Failed to send credentials");
    _stats.messagesFailed++;
  }
  
  return success;
}

bool subscribe(const char *topic, uint8_t qos) {
  if (!isConnected())
    return false;
  return _mqttClient->subscribe(topic, qos);
}

bool unsubscribe(const char *topic) {
  if (!isConnected())
    return false;
  return _mqttClient->unsubscribe(topic);
}

void setMessageCallback(MessageCallback callback) {
  _messageCallback = callback;
}

void setTriggerCallback(TriggerCallback callback) {
  _triggerCallback = callback;
}

void setConnectionCallback(ConnectionCallback callback) {
  _connectionCallback = callback;
}

bool hasPendingTrigger() { return _hasTrigger; }

String getPendingTrigger() { return _pendingTrigger; }

void clearPendingTrigger() {
  _hasTrigger = false;
  _pendingTrigger = "";
  if (isConnected()) {
    _mqttClient->publish(MQTT_TOPIC_TRIGGER, "", true);
  }
}

MqttStats getStats() { return _stats; }

void resetStats() { memset(&_stats, 0, sizeof(_stats)); }

String getLastError() { return _lastError; }

const char *stateToString(MqttState state) {
  switch (state) {
  case MQTT_STATE_DISCONNECTED:
    return "Disconnected";
  case MQTT_STATE_CONNECTING:
    return "Connecting";
  case MQTT_STATE_CONNECTED:
    return "Connected";
  case MQTT_STATE_TLS_ERROR:
    return "TLS Error";
  case MQTT_STATE_AUTH_ERROR:
    return "Auth Error";
  case MQTT_STATE_ERROR:
    return "Error";
  default:
    return "Unknown";
  }
}

// FOR FUTURE
bool saveCredentials(const char *username, const char *password) {
  Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE_MQTT, false))
    return false;

  prefs.putString(PREF_KEY_USER, username);
  prefs.putString(PREF_KEY_PASS, password);
  prefs.end();

  return true;
}
// FOR FUTURE
bool loadCredentials(char *username, char *password, size_t maxLen) {
  Preferences prefs;
  if (!prefs.begin(PREF_NAMESPACE_MQTT, false))
    return false;

  String storedUser = prefs.getString(PREF_KEY_USER, "");
  String storedPass = prefs.getString(PREF_KEY_PASS, "");
  prefs.end();

  if (storedUser.length() == 0)
    return false;

  strncpy(username, storedUser.c_str(), maxLen - 1);
  strncpy(password, storedPass.c_str(), maxLen - 1);

  return true;
}

void clearStoredCredentials() {
  Preferences prefs;
  if (prefs.begin(PREF_NAMESPACE_MQTT, false)) {
    prefs.clear();
    prefs.end();
  }
  memset(_username, 0, sizeof(_username));
  memset(_password, 0, sizeof(_password));
}

static void _setupTLS() {
  if (_secureClient == nullptr)
    return;

  if (_securityConfig.verifyServer && _securityConfig.caCert != nullptr) {
    _secureClient->setCACert(_securityConfig.caCert);
  } else {
    _secureClient->setInsecure();
  }

  if (_securityConfig.useClientCert && _securityConfig.clientCert != nullptr &&
      _securityConfig.clientKey != nullptr) {
    _secureClient->setCertificate(_securityConfig.clientCert);
    _secureClient->setPrivateKey(_securityConfig.clientKey);
  }
}

static bool _connect() {
  // ✅ Validate MQTT client exists
  if (_mqttClient == nullptr) {
    _lastError = "MQTT client not initialized";
    Utils::logMessage("MQTT", "ERROR: MQTT client is null");
    return false;
  }
  
  // ✅ Validate broker configuration
  if (_brokerHost == nullptr || strlen(_brokerHost) == 0) {
    _lastError = "Broker host not configured";
    Utils::logMessage("MQTT", "ERROR: Broker host is null or empty");
    return false;
  }
  
  if (_brokerPort == 0) {
    _lastError = "Invalid broker port";
    Utils::logMessage("MQTT", "ERROR: Broker port is 0");
    return false;
  }
  
  // ✅ Check if we're in AP mode first
  WiFiMode_t mode = WiFi.getMode();
  bool isAPMode = (mode == WIFI_AP || mode == WIFI_AP_STA);
  
  if (isAPMode) {
    Utils::logMessage("MQTT", "Connecting over AP mode");
    // In AP mode, broker should be reachable directly
    _mqttClient->setServer(_brokerHost, _brokerPort);
  } else if (!WifiManager::isFullyConnected()) {
    _lastError = "WiFi not connected";
    return false;
  }

  // ✅ ADD THIS: Set longer socket timeout
  if (_insecureClient != nullptr) {
    _insecureClient->setTimeout(10000);  // 10 seconds instead of 3
  }
  if (_secureClient != nullptr) {
    _secureClient->setTimeout(10000);
  }

  _setState(MQTT_STATE_CONNECTING);
  _stats.connectAttempts++;
  _reconnectAttempts++;

  String host = String(_brokerHost);
  IPAddress brokerIP;

  if (host.endsWith(".local")) {
    String hostname = host.substring(0, host.length() - 6);
    Utils::logMessageF("MQTT", "Resolving mDNS: %s.local", hostname.c_str());

    brokerIP = MDNS.queryHost(hostname.c_str(), 10000); // 5 second timeout

    if (brokerIP == IPAddress(0, 0, 0, 0)) {
      _lastError = "mDNS resolution failed for " + host;
      Utils::logMessageF("MQTT", "Failed to resolve %s", _brokerHost);
      _setState(MQTT_STATE_ERROR);
      return false;
    }

    Utils::logMessageF("MQTT", "Resolved %s -> %s", _brokerHost,
                       brokerIP.toString().c_str());

    // ✅ Validate client before calling setServer
    if (_mqttClient == nullptr) {
      _lastError = "MQTT client became null";
      Utils::logMessage("MQTT", "ERROR: MQTT client is null before setServer");
      return false;
    }
    
    _mqttClient->setServer(brokerIP, _brokerPort);
  } else {
    // ✅ For non-mDNS hosts, also call setServer
    if (_mqttClient == nullptr) {
      _lastError = "MQTT client became null";
      Utils::logMessage("MQTT", "ERROR: MQTT client is null before setServer");
      return false;
    }
    
    _mqttClient->setServer(_brokerHost, _brokerPort);
  }

  Utils::logMessageF("MQTT", "Connecting to %s:%d...", _brokerHost,
                     _brokerPort);

  // ✅ Final check before connect
  if (_mqttClient == nullptr) {
    _lastError = "MQTT client became null";
    Utils::logMessage("MQTT", "ERROR: MQTT client is null before connect");
    return false;
  }

  String clientId = _generateClientId();
  bool connected = false;

  if (strlen(_username) > 0 && strlen(_password) > 0) {
    connected = _mqttClient->connect(clientId.c_str(), _username, _password);
  } else {
    connected = _mqttClient->connect(clientId.c_str());
  }

  if (connected) {
    Utils::logMessage("MQTT", "Connected to broker");
    _setState(MQTT_STATE_CONNECTED);
    _stats.connectSuccesses++;
    _stats.lastConnectedTime = Utils::millis64();
    _reconnectAttempts = 0;
    _subscribeToTopics();

    // ✅ Only publish status if not in AP mode
    if (!isAPMode) {
      publishStatus("STA", WiFi.localIP().toString());
    }
    
    return true;
  }  else {
    int state = _mqttClient->state();
    _stats.connectFailures++;

    // PubSubClient states:
    // -4 : MQTT_CONNECTION_TIMEOUT
    // -3 : MQTT_CONNECTION_LOST
    // -2 : MQTT_CONNECT_FAILED
    // -1 : MQTT_DISCONNECTED
    //  0 : MQTT_CONNECTED
    //  1 : MQTT_CONNECT_BAD_PROTOCOL
    //  2 : MQTT_CONNECT_BAD_CLIENT_ID
    //  3 : MQTT_CONNECT_UNAVAILABLE
    //  4 : MQTT_CONNECT_BAD_CREDENTIALS
    //  5 : MQTT_CONNECT_UNAUTHORIZED

    Utils::logMessageF("MQTT", "Connection failed, state=%d", state);

    switch (state) {
    case -4:
      _lastError = "Connection timeout";
      break;
    case -2:
      _lastError = "Connect failed (network)";
      break;
    case 1:
      _lastError = "Bad protocol version";
      break;
    case 2:
      _lastError = "Bad client ID";
      break;
    case 3:
      _lastError = "Server unavailable";
      break;
    case 4:
      _lastError = "Bad credentials";
      break;
    case 5:
      _lastError = "Unauthorized";
      break;
    default:
      _lastError = "Unknown error: " + String(state);
      break;
    }

    if (state == 4 || state == 5) {
      _setState(MQTT_STATE_AUTH_ERROR);
    } else {
      _setState(MQTT_STATE_ERROR);
    }

    Utils::logMessageF("MQTT", "Error: %s", _lastError.c_str());
    return false;
  }
}

static void _subscribeToTopics() {
  subscribe(MQTT_TOPIC_TRIGGER, 1);
  subscribe(MQTT_TOPIC_REFRESH, 1);
  subscribe(MQTT_TOPIC_CONFIG, 1);
  subscribe(MQTT_TOPIC_PAIRING_STATUS, 1);  // ✅ Added this

}

static void _mqttCallback(char *topic, byte *payload, unsigned int length) {
  if (length == 0)
    return;

  _stats.messagesReceived++;

  String message;
  message.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  String topicStr = String(topic);

  // ✅ Handle pairing status from OrangePi
  if (topicStr == MQTT_TOPIC_PAIRING_STATUS) {
    JsonDocument doc;
    if (deserializeJson(doc, message) == DeserializationError::Ok) {
      String status = doc["status"].as<String>();
      
      if (status == "starting") {
        Utils::logMessage("MQTT", "📱 OrangePi: Starting pairing process...");
        // LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_FAST);
      }
      else if (status == "paired") {
        Utils::logMessage("MQTT", "✅ OrangePi: Successfully paired!");
        // LedManager::runSuccessSequence();
      }
      else if (status == "failed") {
        Utils::logMessage("MQTT", "❌ OrangePi: Pairing failed");
        // LedManager::runErrorSequence();
      }
      else if (status == "configuring") {
        Utils::logMessage("MQTT", "⚙️ OrangePi: Entering configuration mode");
        // LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_SLOW);
      }
      else if (status == "ready_to_configure") {
        Utils::logMessage("MQTT", "📶 OrangePi: Ready for WiFi configuration");
        // LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_AP);
      }
      else if (status == "wifi_configured") {
        Utils::logMessage("MQTT", "✅ OrangePi confirmed WiFi configuration!");
        String ssid = doc["ssid"].as<String>();  // ✅ Extract SSID from JSON
        _confirmationReceived = true;
        _confirmedSSID = ssid;
        _waitingForConfirmation = false;
        // LedManager::runSuccessSequence();
      }
      else if (status == "connection_failed") {
        Utils::logMessage("MQTT", "❌ OrangePi failed to connect to WiFi");
          String error = doc["error"].as<String>();
          if (error.length() > 0) {
            Utils::logMessageF("MQTT", "Error: %s", error.c_str());
          }
          _waitingForConfirmation = false;
        }
      else if (status == "disconnected") {
        Utils::logMessage("MQTT", "🔌 OrangePi: WiFi disconnected");
        // LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_SLOW);
      }
    }
  } else if (topicStr == MQTT_TOPIC_TRIGGER) {
    JsonDocument doc;
    if (deserializeJson(doc, message) == DeserializationError::Ok) {
      String action = doc["action"].as<String>();
      if (action.length() > 0) {
        _pendingTrigger = action;
        _hasTrigger = true;
        if (_triggerCallback != nullptr) {
          _triggerCallback(action);
        }
      }
    }
  } else if (topicStr == MQTT_TOPIC_REFRESH) {
    JsonDocument doc;
    if (deserializeJson(doc, message) == DeserializationError::Ok) {
      String action = doc["action"].as<String>();
      if (action == "scan") {
        String networks = WifiManager::scanNetworks();
        publishScanResults(networks);
        _mqttClient->publish(MQTT_TOPIC_REFRESH, "", true);
      }
    }
  } else if (topicStr == MQTT_TOPIC_CONFIG) {
    JsonDocument doc;
    if (deserializeJson(doc, message) == DeserializationError::Ok) {
      _newSSID = doc["ssid"].as<String>();
      _newPassword = doc["password"].as<String>();
      if (_newSSID.length() > 0) {
        _hasNewCredentials = true;
      }
    }
  }

  if (_messageCallback != nullptr) {
    _messageCallback(topic, message.c_str(), length);
  }
}

static void _setState(MqttState newState) {
  if (_currentState != newState) {
    _currentState = newState;
    if (_connectionCallback != nullptr) {
      _connectionCallback(newState);
    }
  }
}

static String _generateClientId() {
  String deviceId = Utils::generateDeviceId();
  return String(_brokerConfig.clientIdPrefix) + "-" + deviceId.substring(0, 8);
}

} // namespace MqttManager
