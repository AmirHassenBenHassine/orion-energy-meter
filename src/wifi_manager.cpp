#include "wifi_manager.h"
#include "config.h"
#include "led_manager.h"
#include "mqtt_manager.h"
#include "utils.h"

#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <esp_task_wdt.h>

namespace WifiManager {

// Task handle
static TaskHandle_t _wifiTaskHandle = nullptr;

// State variables
static volatile WifiState _currentState = WIFI_STATE_DISCONNECTED;
static volatile bool _taskShouldRun = false;
static volatile bool _eventsEnabled = false;
static volatile bool _forcePortalOnStart = false;
static volatile bool _freshCredentialsFlag = false;
static volatile bool _isInPairingMode = false;

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
static const uint32_t NOTIFY_START_PORTAL_BUTTON = 7;  // ✅ NEW

// static void _sendCredentialsToGateway(const String& ssid, const String& password); //New sending credentials function   
// Generate device ID DEVICE_ID = Utils::generateDeviceId();
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
      TASK_PRIORITY_HIGH, &_wifiTaskHandle, TASK_CORE_1);

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

bool isInPairingMode() {
  return _isInPairingMode;
}

WifiState getState() { return _currentState; }

IPAddress getLocalIP() { return WiFi.localIP(); }

IPAddress getAPIP() { return WiFi.softAPIP(); }

void startPortal(bool triggeredByButton) {
  if (_wifiTaskHandle != nullptr) {
    uint32_t notification = triggeredByButton ? NOTIFY_START_PORTAL_BUTTON : NOTIFY_START_PORTAL;
    xTaskNotify(_wifiTaskHandle, notification, eSetValueWithOverwrite);
  }
}

bool isPortalActive() {
  return _currentState == WIFI_STATE_PORTAL_ACTIVE ||
         _currentState == WIFI_STATE_AP_MODE;
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

  WiFi.scanNetworks(true);

  int n = -1;
  int attempts = 0;
  while (n < 0 && attempts < 100) {
    vTaskDelay(pdMS_TO_TICKS(100));
    n = WiFi.scanComplete();
    attempts++;
  }

  if (n < 0) {
    Utils::logMessage("WIFI", "Scan timed out");
    WiFi.scanDelete();
    return "[]";
  }
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
  wm.setConnectRetries(1);
  wm.setCleanConnect(true);
  wm.setBreakAfterConfig(true);
  wm.setRemoveDuplicateAPs(true);
  wm.setConfigPortalBlocking(false);  // ✅ CRITICAL
  wm.setCaptivePortalEnable(true);    //Enable captive portal
  wm.setSaveConnect(false);         // ESP32 only

  // ✅ Add callback to feed watchdog
  wm.setPreSaveConfigCallback([]() {
    yield();
    vTaskDelay(pdMS_TO_TICKS(10));
  });

  // Portal callbacks
  wm.setAPCallback([](::WiFiManager *wm) {
    Utils::logMessage("WIFI", "Captive portal started");
    _notifyStateChange(WIFI_STATE_PORTAL_ACTIVE);
    _freshCredentialsFlag = false;  // ✅ Reset when portal starts

  });

  wm.setSaveConfigCallback([]() {
    Utils::logMessage("WIFI", "Configuration saved");
    _freshCredentialsFlag = true;  // ✅ SET THE FLAG HERE
    // LedManager::runSuccessSequence();
    // Utils::delayTask(500);
    // ESP.restart();
  });

  // ✅ Add timeout callback
  wm.setConfigPortalTimeoutCallback([]() {
    Utils::logMessage("WIFI", "⏱ Portal timeout callback triggered");
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

  esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
  
  ::WiFiManager *wm = new ::WiFiManager();
  if (!wm) {
    Utils::logMessage("WIFI", "Failed to allocate WiFiManager");
    _taskShouldRun = false;
    _wifiTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  _setupWiFiManager(*wm);

  String deviceId = Utils::generateDeviceId();
  String portalName = WIFI_CONFIG_PORTAL_SSID;

  _notifyStateChange(WIFI_STATE_CONNECTING);

  bool initialConnectionDone = false;
  bool portalTriggeredByButton = false;
  String previousSSID = "";
  String previousPassword = "";
  uint32_t portalStartTime = 0;
  
  // ✅ NEW: Track credential processing
  bool credentialsReceived = false;
  String newSSID = "";
  String newPassword = "";

  if (_forcePortalOnStart) {
    Utils::logMessage("WIFI", "Forced portal mode (first boot)");
    
    // ✅ ERASE existing credentials first!
    Utils::logMessage("WIFI", "Clearing saved WiFi credentials...");
    wm->resetSettings();  // Clear WiFiManager settings
    
    // ✅ Also clear from Preferences
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE_WIFI, false);
    prefs.clear();
    prefs.end();
    
    // ✅ Disconnect any existing connection
    WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(500));

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);

    _notifyStateChange(WIFI_STATE_PORTAL_ACTIVE);
    _isInPairingMode  = true;
    portalTriggeredByButton = false;
    
    // ✅ Start portal without trying to connect
    wm->startConfigPortal(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
    
    Utils::logMessage("WIFI", "Portal started - ready for pairing");
  }
  else {
    Utils::logMessage("WIFI", "Attempting auto-connect...");
    
    // ✅ Check if ANY credentials exist first
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE_WIFI, true);  // Read-only
    int credCount = prefs.getInt("count", 0);
    String savedSSID = "";
    if (credCount > 0) {
      savedSSID = prefs.getString("ssid0", "");
    }
    prefs.end();
    
    // ✅ If no credentials, go straight to portal
    if (savedSSID.length() == 0) {
      Utils::logMessage("WIFI", "No saved credentials - starting portal");
      
      // Clear WiFiManager cache too
      // wm->resetSettings();
      
      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
      
      _notifyStateChange(WIFI_STATE_PORTAL_ACTIVE);
      _isInPairingMode  = true;
      
      wm->startConfigPortal(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
      Utils::logMessage("WIFI", "Portal started - ready for pairing");
    } else {
      // Try auto-connect only if credentials exist
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
        WiFi.mode(WIFI_AP_STA);
        _isInPairingMode  = true;
        portalTriggeredByButton = false;
        _reconnectAttempts = 0;  // ✅ Reset counter
        wm->startConfigPortal(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
      }
    }
  }

  _eventsEnabled = true;
  WiFi.onEvent(_onWiFiEvent);

  // Main task loop
  while (_taskShouldRun) {
    yield();
    vTaskDelay(pdMS_TO_TICKS(10));

    if (_isInPairingMode  && wm != nullptr) {
        static uint32_t portalStartedAt = 0;
        if (portalStartedAt == 0) {
          portalStartedAt = millis();
        }
        
        const uint32_t MAX_PORTAL_DURATION = 300000; // 5 minutes
        if (millis() - portalStartedAt > MAX_PORTAL_DURATION) {
          Utils::logMessage("WIFI", "⏱ Portal max duration reached - restarting");
          ESP.restart();
        }

      WiFi.mode(WIFI_AP_STA); 
      wm->process();
      yield();
    
      // ✅ If WiFiManager auto-connected, DISCONNECT immediately
      if (WiFi.status() == WL_CONNECTED && !credentialsReceived) {
        Utils::logMessage("WIFI", "⚠️ Aborting WM connection attempt");
        WiFi.disconnect(false, false);  // Don't erase config, don't turn off radio
        vTaskDelay(pdMS_TO_TICKS(100));
      }

      if (_freshCredentialsFlag && !credentialsReceived) {
        String submittedSSID = wm->getWiFiSSID();
        String submittedPass = wm->getWiFiPass();
        
        Utils::logMessage("WIFI", "✅ User submitted credentials");
        Utils::logMessageF("WIFI", "SSID: %s", submittedSSID.c_str());
        
        newSSID = submittedSSID;
        newPassword = submittedPass;
        credentialsReceived = true;
        _freshCredentialsFlag = false;
        
        // ========== PHASE 1: VALIDATE CREDENTIALS (ESP32 CONNECTS) ==========
        Utils::logMessage("WIFI", "🔍 Validating credentials...");
        LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_FAST);
        
        // Stay in AP_STA mode so we keep the AP running
        WiFi.mode(WIFI_AP_STA);
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Disable events temporarily to avoid interference
        _eventsEnabled = false;
        
        // Try to connect to the new WiFi
        WiFi.begin(newSSID.c_str(), newPassword.c_str());
        
        uint32_t connectStart = millis();
        uint32_t VALIDATION_TIMEOUT = 20000; // 20 seconds
        bool validationSuccess = false;
        String failureReason = "";
        
        while ((millis() - connectStart) < VALIDATION_TIMEOUT) {
          wl_status_t status = WiFi.status();
          
          if (status == WL_CONNECTED) {
            // ✅ Connection successful!
            validationSuccess = true;
            Utils::logMessage("WIFI", "✅ Credentials validated!");
            LedManager::runSuccessSequence();
            break;
          }
          else if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
            // ❌ Connection failed
            if (status == WL_NO_SSID_AVAIL) {
              failureReason = "Network not found";
              Utils::logMessage("WIFI", "❌ Network not found");
            } else {
              failureReason = "Connection failed";
              Utils::logMessage("WIFI", "❌ Connection failed");
            }
            break;
          }
          
          vTaskDelay(pdMS_TO_TICKS(500));
          yield();
        }
        
        // Timeout check
        if (!validationSuccess && failureReason.length() == 0) {
          if (WiFi.status() != WL_CONNECTED) {
            failureReason = "Connection timeout (wrong password?)";
            Utils::logMessage("WIFI", "❌ Validation timeout");
          }
        }
        
        _eventsEnabled = true;
        
        // ========== PHASE 2A: IF VALIDATION FAILED ==========
        if (!validationSuccess) {
          Utils::logMessageF("WIFI", "Validation failed: %s", failureReason.c_str());
          LedManager::runErrorSequence();
          
          // Disconnect from the failed network
          WiFi.disconnect(false, false);
          vTaskDelay(pdMS_TO_TICKS(500));
          
          // Make sure AP is still running
          WiFi.mode(WIFI_AP_STA);
          vTaskDelay(pdMS_TO_TICKS(500));
          
          // Show error message on LCD or via portal
          // (You could implement a status endpoint for the portal to poll)
          
          Utils::logMessage("WIFI", "❌ Invalid credentials - staying in portal mode");
          
          // Reset flag to allow retry
          credentialsReceived = false;
          continue; // Stay in portal mode
        }
        
        // ========== PHASE 2B: VALIDATION SUCCESSFUL - SEND TO ORANGEPI ==========
        Utils::logMessage("WIFI", "📡 Sending validated credentials to OrangePi...");
        
        // Wait for OrangePi to connect to our AP (if not already connected)
        uint32_t waitForOrangePi = millis();
        uint32_t WAIT_TIMEOUT = 30000; // 30 seconds
        bool orangePiReady = false;
        
        while ((millis() - waitForOrangePi) < WAIT_TIMEOUT) {
          // Check if any station is connected to our AP
          if (WiFi.softAPgetStationNum() > 0) {
            orangePiReady = true;
            Utils::logMessageF("WIFI", "OrangePi connected (%d clients)", WiFi.softAPgetStationNum());
            break;
          }
          
          if ((millis() - waitForOrangePi) % 5000 < 200) {
            Utils::logMessageF("WIFI", "Waiting for OrangePi... %d/%d clients", 
                              WiFi.softAPgetStationNum(), WAIT_TIMEOUT/1000);
          }
          
          vTaskDelay(pdMS_TO_TICKS(500));
          yield();
        }
        
        if (!orangePiReady) {
          Utils::logMessage("WIFI", "⏱ OrangePi didn't connect to AP");
          // You can decide: restart or continue anyway
          // For now, let's continue since ESP32 is already connected
        }
        
        // ========== PHASE 3: SERVE CREDENTIALS VIA HTTP ==========
        WiFiServer credServer(8080);
        credServer.begin();
        Utils::logMessage("WIFI", "📡 Credential server started on 192.168.4.1:8080");
        
        uint32_t serveStart = millis();
        uint32_t SERVE_TIMEOUT = 60000; // 60 seconds
        bool credentialsSent = false;
        
        while ((millis() - serveStart) < SERVE_TIMEOUT && !credentialsSent) {
          WiFiClient client = credServer.available();
          
          if (client) {
            Utils::logMessage("WIFI", "📥 HTTP request from OrangePi");
            
            String request = "";
            while (client.connected() && client.available()) {
              char c = client.read();
              request += c;
              if (request.endsWith("\r\n\r\n")) break;
            }
            
            if (request.indexOf("GET /credentials") >= 0) {
              Utils::logMessage("WIFI", "✅ Serving validated credentials");
              
              JsonDocument doc;
              doc["ssid"] = newSSID;
              doc["password"] = newPassword;
              doc["validated"] = true;  // ✅ Flag that these are pre-validated
              
              String payload;
              serializeJson(doc, payload);
              
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: application/json");
              client.println("Access-Control-Allow-Origin: *");
              client.print("Content-Length: ");
              client.println(payload.length());
              client.println("Connection: close");
              client.println();
              client.println(payload);
              
              credentialsSent = true;
              Utils::logMessage("WIFI", "✅ Credentials sent to OrangePi!");
            }
            
            client.stop();
          }
          
          if ((millis() - serveStart) % 10000 < 200) {
            uint32_t elapsed = (millis() - serveStart) / 1000;
            Utils::logMessageF("WIFI", "Waiting for OrangePi to fetch... %d/60s", elapsed);
          }
          
          vTaskDelay(pdMS_TO_TICKS(100));
          yield();
        }
        
        credServer.stop();
        
        if (!credentialsSent) {
          Utils::logMessage("WIFI", "⏱ OrangePi didn't fetch credentials");
          // ESP32 is already connected, so we can continue anyway
        }
        
        // ========== PHASE 4: CLEANUP AND FINALIZE ==========
        Utils::logMessage("WIFI", "🎉 Switching to STA mode...");
        
        // Clean up portal
        _isInPairingMode  = false;           // Exit portal mode
        credentialsReceived = false;  // Reset for next time
        initialConnectionDone = true; // Mark as successfully connected

        // Clean up portal
        if (wm != nullptr) {
          delete wm;
          wm = nullptr;
        }
        
        // Disconnect AP
        WiFi.softAPdisconnect(true);
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Switch to pure STA mode (we're already connected from validation)
        WiFi.mode(WIFI_STA);
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Save credentials
        Utils::logMessage("WIFI", "💾 Saving credentials...");
        WiFi.persistent(true);
        
        Preferences prefs;
        if (prefs.begin(PREF_NAMESPACE_WIFI, false)) {
          prefs.putString("ssid0", newSSID);
          prefs.putString("pass0", newPassword);
          prefs.putInt("count", 1);
          prefs.end();
          Utils::logMessage("WIFI", "✅ Credentials saved");
        }
        
        // Verify we're still connected (we should be)
        if (WiFi.status() == WL_CONNECTED) {
          Utils::logMessage("WIFI", "✅ ESP32 connected!");
          Utils::logMessageF("WIFI", "IP: %s", WiFi.localIP().toString().c_str());
          
          _lastConnectedMillis = Utils::millis64();
          _handleSuccessfulConnection();
          initialConnectionDone = true;
          credentialsReceived = false;
          
          Utils::logMessage("WIFI", "🎉 Pairing complete!");
          continue;
        } else {
          Utils::logMessage("WIFI", "⚠️ Lost connection during mode switch");
          // Reconnect
          WiFi.begin(newSSID.c_str(), newPassword.c_str());
          
          uint32_t reconnectStart = millis();
          while (WiFi.status() != WL_CONNECTED && (millis() - reconnectStart) < 20000) {
            vTaskDelay(pdMS_TO_TICKS(500));
            yield();
          }
          
          if (WiFi.status() == WL_CONNECTED) {
            Utils::logMessage("WIFI", "✅ Reconnected successfully");
            _lastConnectedMillis = Utils::millis64();
            _handleSuccessfulConnection();
            initialConnectionDone = true;
            credentialsReceived = false;
            continue;
          } else {
            Utils::logMessage("WIFI", "❌ Failed to reconnect");
            ESP.restart();
          }
        }
      }
                  
      // ✅ Timeout handling for button portal
      if (portalTriggeredByButton && previousSSID.length() > 0) {
        if (portalStartTime == 0) {
          portalStartTime = millis();
        }
        
        const uint32_t BUTTON_PORTAL_TIMEOUT = 180000; // 3 minutes
        
        if (millis() - portalStartTime > BUTTON_PORTAL_TIMEOUT) {
          Utils::logMessage("WIFI", "⏱ Portal timeout - reverting");
          
          delete wm;
          wm = nullptr;
          _isInPairingMode  = false;
          
          WiFi.mode(WIFI_STA);
          WiFi.begin(previousSSID.c_str(), previousPassword.c_str());
          
          uint32_t reconnectStart = millis();
          while (WiFi.status() != WL_CONNECTED && (millis() - reconnectStart) < 20000) {
            vTaskDelay(pdMS_TO_TICKS(500));
            yield();
          }
          
          if (WiFi.status() == WL_CONNECTED) {
            Utils::logMessage("WIFI", "✅ Reconnected to previous WiFi");
            _lastConnectedMillis = Utils::millis64();
            _handleSuccessfulConnection();
            initialConnectionDone = true;
          }
          
          portalTriggeredByButton = false;
        }
      }
      } 

    // Notification handling
    if (xTaskNotifyWait(0, ULONG_MAX, &notification, pdMS_TO_TICKS(100))) {
      yield();

      switch (notification) {
      case NOTIFY_SHUTDOWN:
        _taskShouldRun = false;
        break;

      case NOTIFY_CONNECTED:
        Utils::logMessage("WIFI", "WiFi connected event");
        break;

      case NOTIFY_GOT_IP:
        if (!initialConnectionDone && !credentialsReceived) {
          _lastConnectedMillis = Utils::millis64();
          _handleSuccessfulConnection();
          initialConnectionDone = true;
        }
        break;

      case NOTIFY_DISCONNECTED:
        Utils::logMessage("WIFI", "WiFi disconnected");
        _lastConnectedMillis = 0;
        _notifyStateChange(WIFI_STATE_DISCONNECTED);

        // ✅ Don't process disconnect events if already in portal mode
        if (_isInPairingMode ) {
            break;
        }
    
        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));        

        if (!isFullyConnected()) {
          _reconnectAttempts++;
          if (_reconnectAttempts >= WIFI_MAX_RECONNECT_ATTEMPTS) {
            Utils::logMessage("WIFI", "Max reconnects - starting portal");

            // ✅ STOP trying to connect
            WiFi.disconnect(true);  // true = erase stored credentials from this session
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // ✅ Reset counter to prevent re-triggering
            _reconnectAttempts = 0;

            if (wm == nullptr) {
              wm = new ::WiFiManager();
              _setupWiFiManager(*wm);
            }
            if (wm) {
              WiFi.mode(WIFI_AP_STA);  // ✅ AP_STA mode
              _notifyStateChange(WIFI_STATE_PORTAL_ACTIVE);
              _isInPairingMode  = true;
              credentialsReceived = false;
              wm->startConfigPortal(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
              Utils::logMessage("WIFI", "Portal started after failed reconnects");

            }
          }
        }
        break;

      case NOTIFY_FORCE_RECONNECT:
        Utils::logMessage("WIFI", "Forced reconnect");
        _notifyStateChange(WIFI_STATE_CONNECTING);
        WiFi.reconnect();
        _reconnectAttempts++;
        _lastReconnectAttempt = Utils::millis64();
        break;

      case NOTIFY_START_PORTAL:
        Utils::logMessage("WIFI", "🔧 Manual portal");
        if (wm == nullptr) {
          wm = new ::WiFiManager();
          _setupWiFiManager(*wm);
        }
        if (wm) {
          WiFi.mode(WIFI_AP_STA);  // ✅ AP_STA mode
          _notifyStateChange(WIFI_STATE_PORTAL_ACTIVE);
          _isInPairingMode  = true;
          credentialsReceived = false;
          wm->startConfigPortal(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
        }
        break;

      case NOTIFY_START_PORTAL_BUTTON:
        Utils::logMessage("WIFI", "📱 Button portal");
        previousSSID = WiFi.SSID();
        previousPassword = WiFi.psk();

        // ✅ Disconnect from current WiFi
        WiFi.disconnect(false);  // Erase stored credentials
        vTaskDelay(pdMS_TO_TICKS(500));  

        if (wm == nullptr) {
          wm = new ::WiFiManager();
          _setupWiFiManager(*wm);
        }
        if (wm) {
          // ✅ Clear WiFiManager's cache (temporary, not NVS)
          // wm->resetSettings();
          
          vTaskDelay(pdMS_TO_TICKS(500));
          
          // ✅ Start in AP_STA mode (can still use old WiFi if needed)
          WiFi.mode(WIFI_AP_STA);
          WiFi.softAP(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
          
          _notifyStateChange(WIFI_STATE_PORTAL_ACTIVE);
          _isInPairingMode  = true;
          portalTriggeredByButton = true;
          portalStartTime = 0;
          credentialsReceived = false;
          
          // ✅ Start portal
          wm->startConfigPortal(portalName.c_str(), WIFI_CONFIG_PORTAL_PASSWORD);
              }
        break;
      }
    }

    // Health check
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
} // namespace WifiManager
}