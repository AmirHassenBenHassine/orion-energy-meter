#include "wifi_manager.h"
#include "config.h"
#include "led_manager.h"
#include "utils.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFiManager.h>

namespace WifiManager {

// Task handle
static TaskHandle_t _wifiTaskHandle = nullptr;

// State variables
static volatile WifiState _currentState = WIFI_STATE_DISCONNECTED;
static volatile bool _taskShouldRun = false;
static volatile bool _eventsEnabled = false;
static volatile bool _forcePortalOnStart = false;

// Connection tracking
static uint64_t _lastConnectedMillis = 0;
static uint32_t _reconnectAttempts = 0;
static uint64_t _lastReconnectAttempt = 0;

// Event callback
static WifiEventCallback _eventCallback = nullptr;

// WiFi event notification values
static const uint32_t NOTIFY_CONNECTED = 1;
static const uint32_t NOTIFY_GOT_IP = 2;
static const uint32_t NOTIFY_DISCONNECTED = 3;
static const uint32_t NOTIFY_SHUTDOWN = 4;
static const uint32_t NOTIFY_FORCE_RECONNECT = 5;
static const uint32_t NOTIFY_START_PORTAL = 6;

static void _wifiTask(void *parameter);
static void _onWiFiEvent(WiFiEvent_t event);
static void _setupWiFiManager(::WiFiManager &wm);
static void _handleSuccessfulConnection();
static void _notifyStateChange(WifiState newState);
static void _cleanup();

bool begin(bool forcePortal) {
  if (_wifiTaskHandle != nullptr) {
    Utils::logMessage("WIFI", "Already initialized");
    return true;
  }

  _forcePortalOnStart = forcePortal;

  // Configure WiFi settings
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  // Start WiFi task
  BaseType_t result = xTaskCreatePinnedToCore(
      _wifiTask, TASK_NAME_WIFI, TASK_STACK_SIZE_LARGE, nullptr,
      TASK_PRIORITY_HIGH, &_wifiTaskHandle, TASK_CORE_0);

  if (result != pdPASS) {
    Utils::logMessage("WIFI", "Failed to create WiFi task");
    return false;
  }

  Utils::logMessage("WIFI", "WiFi Manager initialized");
  return true;
}

void stop() {
  if (_wifiTaskHandle == nullptr) {
    return;
  }

  xTaskNotify(_wifiTaskHandle, NOTIFY_SHUTDOWN, eSetValueWithOverwrite);

  uint64_t startTime = Utils::millis64();
  while (_wifiTaskHandle != nullptr && (Utils::millis64() - startTime) < 5000) {
    Utils::delayTask(100);
  }

  // Force cleanup if needed
  if (_wifiTaskHandle != nullptr) {
    vTaskDelete(_wifiTaskHandle);
    _wifiTaskHandle = nullptr;
  }

  WiFi.disconnect(true);
  _cleanup();

  Utils::logMessage("WIFI", "WiFi Manager stopped");
}

bool isFullyConnected() {
  if (!WiFi.isConnected() || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    return false;
  }

  // Ensure network stack has stabilized
  if (_lastConnectedMillis > 0 &&
      (Utils::millis64() - _lastConnectedMillis) < WIFI_LWIP_STABILIZATION_MS) {
    return false;
  }

  return true;
}

bool isAPMode() {
  return _currentState == WIFI_STATE_AP_MODE ||
         _currentState == WIFI_STATE_PORTAL_ACTIVE;
}

WifiState getState() { return _currentState; }

IPAddress getLocalIP() { return WiFi.localIP(); }

IPAddress getAPIP() { return WiFi.softAPIP(); }

void startPortal() {
  if (_wifiTaskHandle != nullptr) {
    xTaskNotify(_wifiTaskHandle, NOTIFY_START_PORTAL, eSetValueWithOverwrite);
  }
}

void forceReconnect() {
  if (_wifiTaskHandle != nullptr) {
    xTaskNotify(_wifiTaskHandle, NOTIFY_FORCE_RECONNECT,
                eSetValueWithOverwrite);
  }
}

bool saveCredentials(const char *ssid, const char *password) {
  if (ssid == nullptr || strlen(ssid) == 0) {
    return false;
  }

  Preferences prefs;
  prefs.begin(PREF_NAMESPACE_WIFI, false);

  int count = prefs.getInt("count", 0);

  // Check if exists
  for (int i = 0; i < count; i++) {
    String savedSSID = prefs.getString(("ssid" + String(i)).c_str(), "");
    if (savedSSID == ssid) {
      prefs.putString(("pass" + String(i)).c_str(), password);
      prefs.end();
      return true;
    }
  }

  // Add new
  prefs.putString(("ssid" + String(count)).c_str(), ssid);
  prefs.putString(("pass" + String(count)).c_str(), password);
  prefs.putInt("count", count + 1);
  prefs.end();
  return true;
}

bool connectWithCredentials(const char *ssid, const char *password) {
  if (ssid == nullptr || strlen(ssid) == 0) {
    return false;
  }

  WiFi.disconnect(true);
  Utils::delayTask(1000);
  WiFi.begin(ssid, password);

  uint64_t startTime = Utils::millis64();
  while (WiFi.status() != WL_CONNECTED &&
         (Utils::millis64() - startTime) < WIFI_CONNECT_TIMEOUT_MS) {
    Utils::delayTask(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    saveCredentials(ssid, password);
    return true;
  }
  return false;
}

void resetCredentials() {
  Utils::logMessage("WIFI", "Resetting all WiFi credentials");

  ::WiFiManager wm;
  wm.resetSettings();

  Preferences prefs;
  prefs.begin(PREF_NAMESPACE_WIFI, false);
  prefs.clear();
  prefs.end();

  Utils::logMessage("WIFI", "Credentials reset, restarting...");
  Utils::delayTask(500);
  ESP.restart();
}

bool testConnectivity() {
  if (!isFullyConnected()) {
    return false;
  }

  // Check gateway
  IPAddress gateway = WiFi.gatewayIP();
  if (gateway == IPAddress(0, 0, 0, 0)) {
    return false;
  }

  // Check DNS
  IPAddress dns1 = WiFi.dnsIP(0);
  IPAddress dns2 = WiFi.dnsIP(1);
  if (dns1 == IPAddress(0, 0, 0, 0) && dns2 == IPAddress(0, 0, 0, 0)) {
    return false;
  }

  // Try HTTP connection
  WiFiClient client;
  client.setTimeout(CONNECTIVITY_TEST_TIMEOUT_MS);

  if (!client.connect(CONNECTIVITY_TEST_HOST, CONNECTIVITY_TEST_PORT)) {
    return false;
  }

  client.print("HEAD / HTTP/1.1\r\nHost: ");
  client.print(CONNECTIVITY_TEST_HOST);
  client.print("\r\nConnection: close\r\n\r\n");

  uint64_t startTime = Utils::millis64();
  while (client.connected() &&
         (Utils::millis64() - startTime) < CONNECTIVITY_TEST_TIMEOUT_MS) {
    if (client.available()) {
      String response = client.readStringUntil('\n');
      client.stop();
      return response.startsWith("HTTP/1.");
    }
    Utils::delayTask(10);
  }

  client.stop();
  return false;
}

String scanNetworks() {
  Utils::logMessage("WIFI", "Scanning networks...");
  int n = WiFi.scanNetworks();

  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0)
      json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" +
            String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
    json += "}";
  }
  json += "]";

  WiFi.scanDelete();
  Utils::logMessageF("WIFI", "Found %d networks", n);

  return json;
}

void setEventCallback(WifiEventCallback callback) { _eventCallback = callback; }

void getStats(uint32_t &reconnectAttempts, uint64_t &lastConnectedTime) {
  reconnectAttempts = _reconnectAttempts;
  lastConnectedTime = _lastConnectedMillis;
}

bool setupMDNS() {
  MDNS.end();
  Utils::delayTask(100);

  String deviceId = Utils::generateDeviceId();
  String hostname = String(MDNS_HOSTNAME);

  if (!MDNS.begin(hostname.c_str())) {
    Utils::logMessage("WIFI", "mDNS setup failed");
    return false;
  }

  MDNS.addService("http", "tcp", 80);
  MDNS.addServiceTxt("http", "tcp", "device_id", deviceId.c_str());
  MDNS.addServiceTxt("http", "tcp", "vendor", COMPANY_NAME);
  MDNS.addServiceTxt("http", "tcp", "model", PRODUCT_NAME);
  MDNS.addServiceTxt("http", "tcp", "version", FIRMWARE_VERSION);

  Utils::logMessageF("WIFI", "mDNS started: %s.local", hostname.c_str());
  return true;
}

static void _notifyStateChange(WifiState newState) {
  _currentState = newState;

  // Update LED
  switch (newState) {
  case WIFI_STATE_DISCONNECTED:
    LedManager::updateWiFiLed(false, false);
    break;
  case WIFI_STATE_CONNECTING:
    LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_FAST);
    break;
  case WIFI_STATE_CONNECTED:
    LedManager::updateWiFiLed(true, false);
    break;
  case WIFI_STATE_AP_MODE:
  case WIFI_STATE_PORTAL_ACTIVE:
    LedManager::updateWiFiLed(false, true);
    break;
  case WIFI_STATE_ERROR:
    LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_FAST);
    break;
  }

  // Call user callback
  if (_eventCallback != nullptr) {
    _eventCallback(newState);
  }
}

static void _setupWiFiManager(::WiFiManager &wm) {
  wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_SECONDS);
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_SECONDS);
  wm.setConnectRetries(3);
  wm.setCleanConnect(true);
  wm.setBreakAfterConfig(true);
  wm.setRemoveDuplicateAPs(true);
  wm.setConfigPortalBlocking(false);  // ✅ CRITICAL

  // ✅ Add callback to feed watchdog
  wm.setPreSaveConfigCallback([]() {
    yield();
    vTaskDelay(pdMS_TO_TICKS(10));
  });

  // Portal callbacks
  wm.setAPCallback([](::WiFiManager *wm) {
    Utils::logMessage("WIFI", "Captive portal started");
    _notifyStateChange(WIFI_STATE_PORTAL_ACTIVE);
  });

  wm.setSaveConfigCallback([]() {
    Utils::logMessage("WIFI", "Configuration saved");
    LedManager::runSuccessSequence();
    Utils::delayTask(500);
    ESP.restart();
  });
}

static void _handleSuccessfulConnection() {
  _reconnectAttempts = 0;
  _lastReconnectAttempt = 0;

  _notifyStateChange(WIFI_STATE_CONNECTED);

  // Setup mDNS
  setupMDNS();

  // Configure NTP
  Utils::configureNTP();

  Utils::logMessageF("WIFI", "Connected! IP: %s",
                     WiFi.localIP().toString().c_str());
}

static void _onWiFiEvent(WiFiEvent_t event) {
  if (!_eventsEnabled || !_taskShouldRun || _wifiTaskHandle == nullptr) {
    return;
  }

  switch (event) {
  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    xTaskNotify(_wifiTaskHandle, NOTIFY_CONNECTED, eSetValueWithOverwrite);
    break;

  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    xTaskNotify(_wifiTaskHandle, NOTIFY_GOT_IP, eSetValueWithOverwrite);
    break;

  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    xTaskNotify(_wifiTaskHandle, NOTIFY_DISCONNECTED, eSetValueWithOverwrite);
    break;

  default:
    break;
  }
}

static void _cleanup() {
  _eventsEnabled = false;
  WiFi.removeEvent(_onWiFiEvent);
  MDNS.end();
}

static void _wifiTask(void *parameter) {
  uint32_t notification;
  _taskShouldRun = true;

  // Create WiFiManager on heap
  ::WiFiManager *wm = new ::WiFiManager();
  if (!wm) {
    Utils::logMessage("WIFI", "Failed to allocate WiFiManager");
    _taskShouldRun = false;
    _wifiTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  _setupWiFiManager(*wm);

  // Generate portal name
  String deviceId = Utils::generateDeviceId();
  String portalName = WIFI_CONFIG_PORTAL_SSID;

  _notifyStateChange(WIFI_STATE_CONNECTING);

  // Track portal state
  bool portalMode = false;
  bool initialConnectionDone = false;

  if (_forcePortalOnStart) {
    Utils::logMessage("WIFI", "Forced portal mode");
    _notifyStateChange(WIFI_STATE_PORTAL_ACTIVE);
    portalMode = true;
    wm->startConfigPortal(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
  } else {
    Utils::logMessage("WIFI", "Attempting auto-connect...");
    // Try auto-connect but don't block
    bool connected = wm->autoConnect(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
    
    if (connected) {
      Utils::logMessage("WIFI", "Auto-connect successful");
      _lastConnectedMillis = Utils::millis64();
      _handleSuccessfulConnection();
      delete wm;
      wm = nullptr;
      initialConnectionDone = true;
    } else {
      Utils::logMessage("WIFI", "Auto-connect failed, entering portal mode");
      portalMode = true;
      wm->startConfigPortal(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
    }
  }

  // Enable event handling
  _eventsEnabled = true;
  WiFi.onEvent(_onWiFiEvent);

  // Main task loop
  while (_taskShouldRun) {
    // ✅ CRITICAL: Feed watchdog every loop iteration
    yield();
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // ✅ Process WiFiManager if portal is active
    if (portalMode && wm != nullptr) {
      wm->process();  // Non-blocking process
      yield();  // Feed watchdog after processing
      
      // Check if connected via portal
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        Utils::logMessage("WIFI", "Connected via portal");
        portalMode = false;
        _lastConnectedMillis = Utils::millis64();
        _handleSuccessfulConnection();
        delete wm;
        wm = nullptr;
        initialConnectionDone = true;
      }
    }

    // Check for notifications with short timeout
    if (xTaskNotifyWait(0, ULONG_MAX, &notification, pdMS_TO_TICKS(100))) {
      
      yield();  // Feed watchdog after notification

      switch (notification) {
      case NOTIFY_SHUTDOWN:
        _taskShouldRun = false;
        break;

      case NOTIFY_CONNECTED:
        Utils::logMessage("WIFI", "WiFi connected event");
        break;

      case NOTIFY_GOT_IP:
        if (!initialConnectionDone) {
          _lastConnectedMillis = Utils::millis64();
          _handleSuccessfulConnection();
          initialConnectionDone = true;
        }
        break;

      case NOTIFY_DISCONNECTED:
        Utils::logMessage("WIFI", "WiFi disconnected");
        _lastConnectedMillis = 0;
        _notifyStateChange(WIFI_STATE_DISCONNECTED);

        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
        yield();

        if (!isFullyConnected()) {
          _reconnectAttempts++;
          _lastReconnectAttempt = Utils::millis64();

          if (_reconnectAttempts >= WIFI_MAX_RECONNECT_ATTEMPTS) {
            Utils::logMessage("WIFI", "Max reconnect attempts, starting portal");

            if (wm == nullptr) {
              wm = new ::WiFiManager();
              if (wm) {
                _setupWiFiManager(*wm);
              }
            }
            
            if (wm) {
              _notifyStateChange(WIFI_STATE_PORTAL_ACTIVE);
              portalMode = true;
              wm->startConfigPortal(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
            }
          }
        }
        break;

      case NOTIFY_FORCE_RECONNECT:
        Utils::logMessage("WIFI", "Forced reconnect");
        WiFi.disconnect(false);
        vTaskDelay(pdMS_TO_TICKS(1000));
        yield();
        WiFi.reconnect();
        _reconnectAttempts++;
        _lastReconnectAttempt = Utils::millis64();
        break;

      case NOTIFY_START_PORTAL:
        Utils::logMessage("WIFI", "Manual portal request");
        if (wm == nullptr) {
          wm = new ::WiFiManager();
          if (wm) {
            _setupWiFiManager(*wm);
          }
        }
        if (wm) {
          _notifyStateChange(WIFI_STATE_PORTAL_ACTIVE);
          portalMode = true;
          wm->startConfigPortal(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
        }
        break;
      }
    }

    // Periodic health check
    if (_taskShouldRun && isFullyConnected()) {
      if (_reconnectAttempts > 0 &&
          (Utils::millis64() - _lastReconnectAttempt) > WIFI_STABLE_CONNECTION_MS) {
        _reconnectAttempts = 0;
      }
    }
  }

  if (wm != nullptr) {
    delete wm;
  }
  _cleanup();
  _wifiTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

} // namespace WifiManager
