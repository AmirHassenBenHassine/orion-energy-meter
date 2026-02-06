#include "ota_manager.h"
#include "config.h"
#include "led_manager.h"
#include "utils.h"
#include "wifi_manager.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

namespace OtaManager {

static const uint32_t HTTP_TIMEOUT_MS = 60000;
static const uint32_t DOWNLOAD_BUFFER_SIZE = 512;
static const uint32_t PROGRESS_REPORT_INTERVAL = 5; // Report every 5%
static const uint32_t NETWORK_TIMEOUT_MS = 60000;  // ✅ NEW: Network timeout
static const char *PREF_KEY_ETAG = "fw_etag";
static const char *PREF_KEY_POST_UPDATE = "post_ota";
static const char *PREF_KEY_VERSION = "fw_version";

// Current state
static volatile OtaState _currentState = OTA_STATE_IDLE;
static volatile OtaError _lastError = OTA_ERROR_NONE;
static String _lastErrorMessage = "";

// Progress tracking
static OtaProgress _progress;
static OtaUpdateInfo _updateInfo;

// Configuration
static String _firmwareUrl = FIRMWARE_URL;
static String _versionJsonUrl = VERSION_JSON_URL;
static uint32_t _checkIntervalMs = OTA_CHECK_INTERVAL_MS;

// Callbacks
static ProgressCallback _progressCallback = nullptr;
static StateChangeCallback _stateChangeCallback = nullptr;

// Cancel flag
static volatile bool _cancelRequested = false;

// Initialized flag
static bool _initialized = false;

// ✅ NEW: Measurement blocking
static volatile bool _measurementActive = false;
static bool _updatePending = false;

static void _setState(OtaState newState);
static void _setError(OtaError error, const String &message);
static void _clearError();
static void _updateProgress(uint32_t downloaded, uint32_t total);
static bool _downloadAndInstall();
static void _saveEtag(const String &etag);
static String _loadEtag();
static void _setPostUpdateFlag();
static void _blinkProgress(uint8_t percentage);
static int _compareVersions(const String &v1, const String &v2);  // ✅ NEW

bool begin() {
  if (_initialized) {
    return true;
  }

  // Initialize progress structure
  _progress.state = OTA_STATE_IDLE;
  _progress.bytesDownloaded = 0;
  _progress.totalBytes = 0;
  _progress.percentage = 0;
  _progress.error = OTA_ERROR_NONE;
  _progress.errorMessage = "";

  // Initialize update info
  _updateInfo.available = false;
  _updateInfo.currentVersion = FIRMWARE_VERSION;
  _updateInfo.newVersion = "";
  _updateInfo.etag = "";
  _updateInfo.size = 0;
  _updateInfo.url = _firmwareUrl;

  // ✅ SIMPLE: If we booted after OTA, we're valid (we booted = we work)
  if (isPostUpdateBoot()) {
    Utils::logMessage("OTA", "First boot after OTA - marking firmware as valid");
    markFirmwareValid();
  }

  _initialized = true;
  Utils::logMessage("OTA", "OTA Manager initialized");
  Utils::logMessageF("OTA", "Current version: %s", FIRMWARE_VERSION);

  return true;
}

void loop() {
  static uint64_t lastAutoCheck = 0;

  // ✅ SAFETY: Check if update is pending and measurement is complete
  if (_updatePending && !_measurementActive) {
    Utils::logMessage("OTA", "Measurement complete - installing pending update");
    performUpdate();
    _updatePending = false;
    return;
  }

  if (_checkIntervalMs > 0 && _initialized) {
    uint64_t now = Utils::millis64();
    if (now - lastAutoCheck >= _checkIntervalMs) {
      lastAutoCheck = now;
      checkForUpdate();
    }
  }
}

void stop() {
  cancelUpdate();
  _initialized = false;
  Utils::logMessage("OTA", "OTA Manager stopped");
}

// ✅ NEW: Set measurement active state
void setMeasurementActive(bool active) {
  _measurementActive = active;
  if (active) {
    Utils::logMessage("OTA", "📊 Measurement started - OTA will wait");
  } else {
    Utils::logMessage("OTA", "✅ Measurement complete - OTA can proceed");
  }
}

bool isMeasurementActive() {
  return _measurementActive;
}

// ✅ NEW: Fetch checksum from version.json on GitHub
String fetchChecksumFromGitHub() {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  http.begin(client, _versionJsonUrl);
  http.setTimeout(15000);

  int httpCode = http.GET();

  if (httpCode != 200) {
    Utils::logMessageF("OTA", "Failed to fetch version.json: HTTP %d", httpCode);
    http.end();
    return "";
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Utils::logMessage("OTA", "Failed to parse version.json");
    return "";
  }

  const char* checksum = doc["checksum"];
  const char* version = doc["version"];

  if (checksum && version) {
    Utils::logMessageF("OTA", "Found version %s with checksum %s", version, checksum);
    _updateInfo.newVersion = String(version);
    return String(checksum);
  }

  return "";
}

// ✅ NEW: Compare semantic versions
static int _compareVersions(const String &v1, const String &v2) {
  int major1 = 0, minor1 = 0, patch1 = 0;
  int major2 = 0, minor2 = 0, patch2 = 0;

  sscanf(v1.c_str(), "%d.%d.%d", &major1, &minor1, &patch1);
  sscanf(v2.c_str(), "%d.%d.%d", &major2, &minor2, &patch2);

  if (major1 != major2) return major1 - major2;
  if (minor1 != minor2) return minor1 - minor2;
  return patch1 - patch2;
}

bool checkForUpdate(OtaUpdateInfo *info) {
  if (!WifiManager::isFullyConnected()) {
    _setError(OTA_ERROR_NO_WIFI, "No WiFi connection");
    if (info)
      info->available = false;
    return false;
  }

  _setState(OTA_STATE_CHECKING);
  _clearError();

  Utils::logMessage("OTA", "Checking for firmware update...");
  Utils::logMessageF("OTA", "URL: %s", _firmwareUrl.c_str());

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(HTTP_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(client, _firmwareUrl)) {
    _setError(OTA_ERROR_CONNECTION_FAILED, "Failed to begin HTTP connection");
    _setState(OTA_STATE_FAILED);
    if (info)
      info->available = false;
    return false;
  }

  // Collect headers
  const char *headers[] = {"ETag", "Last-Modified", "Content-Length"};
  http.collectHeaders(headers, 2);

  // Send HEAD request to check for update
  int httpCode = http.sendRequest("HEAD");

  if (httpCode != HTTP_CODE_OK) {
    _setError(OTA_ERROR_HTTP_ERROR, "HTTP " + String(httpCode));
    _setState(OTA_STATE_FAILED);
    Utils::logMessageF("OTA", "HEAD request failed: HTTP %d", httpCode);
    http.end();
    if (info)
      info->available = false;
    return false;
  }

  // Get server ETag
  String serverEtag = http.header("ETag");
  String contentLength = http.header("Content-Length");
  http.end();

  // ✅ NEW: Fetch version info from version.json
  String newVersion = "";
  String checksum = fetchChecksumFromGitHub();
  
  if (_updateInfo.newVersion.length() > 0) {
    newVersion = _updateInfo.newVersion;
  }

  // Load stored ETag
  String storedEtag = _loadEtag();

  Utils::logMessageF("OTA", "Stored ETag: %s", storedEtag.c_str());
  Utils::logMessageF("OTA", "Server ETag: %s", serverEtag.c_str());

  // ✅ NEW: Version comparison
  if (newVersion.length() > 0) {
    int versionCompare = _compareVersions(newVersion, FIRMWARE_VERSION);
    
    if (versionCompare <= 0) {
      Utils::logMessageF("OTA", "Already on latest version (%s >= %s)", 
                        FIRMWARE_VERSION, newVersion.c_str());
      _updateInfo.available = false;
      _setState(OTA_STATE_NO_UPDATE);
      if (info) *info = _updateInfo;
      return false;
    }
    
    Utils::logMessageF("OTA", "New version available: %s -> %s", 
                      FIRMWARE_VERSION, newVersion.c_str());
  }

  // Update info structure
  _updateInfo.currentVersion = FIRMWARE_VERSION;
  _updateInfo.etag = serverEtag;
  _updateInfo.size = contentLength.toInt();
  _updateInfo.url = _firmwareUrl;

  if (storedEtag.length() > 0 && storedEtag.equals(serverEtag)) {
    Utils::logMessage("OTA", "Firmware is up to date");
    _updateInfo.available = false;
    _setState(OTA_STATE_NO_UPDATE);
    if (info)
      *info = _updateInfo;
    return false;
  }

  _updateInfo.available = true;
  _setState(OTA_STATE_UPDATE_AVAILABLE);

  if (info)
    *info = _updateInfo;
  return true;
}

bool performUpdate() {
  if (!_updateInfo.available) {
    Utils::logMessage("OTA",
                      "No update available. Run checkForUpdate() first.");
    return false;
  }

  if (!WifiManager::isFullyConnected()) {
    _setError(OTA_ERROR_NO_WIFI, "No WiFi connection");
    _setState(OTA_STATE_FAILED);
    return false;
  }

  // ✅ SAFETY: Check if measurement is active
  if (_measurementActive) {
    Utils::logMessage("OTA", "⏸️  Measurement in progress - deferring update");
    _updatePending = true;
    return false;
  }

  _cancelRequested = false;

  Utils::logMessage("OTA", "=== STARTING FIRMWARE UPDATE ===");
  Utils::logMessageF("OTA", "Version: %s -> %s", 
                    _updateInfo.currentVersion.c_str(), 
                    _updateInfo.newVersion.c_str());

  LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_BLINK_FAST);

  bool success = _downloadAndInstall();

  if (!success) {
    LedManager::setMode(LedManager::LED_WIFI, LedManager::LED_OFF);
    LedManager::runErrorSequence();
  }

  return success;
}

bool checkAndUpdate() {
  if (checkForUpdate()) {
    return performUpdate();
  }
  return false;
}

void cancelUpdate() {
  if (_currentState == OTA_STATE_DOWNLOADING ||
      _currentState == OTA_STATE_INSTALLING) {
    Utils::logMessage("OTA", "Update cancellation requested");
    _cancelRequested = true;
  }
}

OtaState getState() { return _currentState; }

OtaProgress getProgress() { return _progress; }

OtaError getLastError() { return _lastError; }

String getLastErrorMessage() { return _lastErrorMessage; }

OtaUpdateInfo getUpdateInfo() { return _updateInfo; }

void setProgressCallback(ProgressCallback callback) {
  _progressCallback = callback;
}

void setStateChangeCallback(StateChangeCallback callback) {
  _stateChangeCallback = callback;
}

bool shouldCheckNow(uint64_t lastCheckTime) {
  return (Utils::millis64() - lastCheckTime) >= _checkIntervalMs;
}

String getCurrentVersion() { return String(FIRMWARE_VERSION); }

String getBuildDate() { return String(__DATE__) + " " + String(__TIME__); }

void setFirmwareUrl(const String &url) {
  _firmwareUrl = url;
  _updateInfo.url = url;
  Utils::logMessageF("OTA", "Firmware URL set to: %s", url.c_str());
}

String getFirmwareUrl() { return _firmwareUrl; }

void setCheckInterval(uint32_t intervalMs) {
  _checkIntervalMs = intervalMs;
  Utils::logMessageF("OTA", "Check interval set to: %lu ms", intervalMs);
}

const char *errorToString(OtaError error) {
  switch (error) {
  case OTA_ERROR_NONE:
    return "No error";
  case OTA_ERROR_NO_WIFI:
    return "No WiFi connection";
  case OTA_ERROR_CONNECTION_FAILED:
    return "Connection failed";
  case OTA_ERROR_HTTP_ERROR:
    return "HTTP error";
  case OTA_ERROR_INVALID_SIZE:
    return "Invalid firmware size";
  case OTA_ERROR_NOT_ENOUGH_SPACE:
    return "Not enough space";
  case OTA_ERROR_WRITE_FAILED:
    return "Write failed";
  case OTA_ERROR_VERIFICATION_FAILED:
    return "Verification failed";
  case OTA_ERROR_FINALIZATION_FAILED:
    return "Finalization failed";
  case OTA_ERROR_TIMEOUT:
    return "Timeout";
  case OTA_ERROR_CANCELLED:
    return "Update cancelled";
  default:
    return "Unknown error";
  }
}

const char *stateToString(OtaState state) {
  switch (state) {
  case OTA_STATE_IDLE:
    return "Idle";
  case OTA_STATE_CHECKING:
    return "Checking";
  case OTA_STATE_UPDATE_AVAILABLE:
    return "Update Available";
  case OTA_STATE_DOWNLOADING:
    return "Downloading";
  case OTA_STATE_VERIFYING:
    return "Verifying";
  case OTA_STATE_INSTALLING:
    return "Installing";
  case OTA_STATE_SUCCESS:
    return "Success";
  case OTA_STATE_FAILED:
    return "Failed";
  case OTA_STATE_NO_UPDATE:
    return "No Update";
  default:
    return "Unknown";
  }
}

void markFirmwareValid() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;

  if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
      esp_ota_mark_app_valid_cancel_rollback();
      Utils::logMessage("OTA", "✅ Firmware marked as valid");
    }
  }

  // Clear post-update flag
  clearPostUpdateFlag();
}

bool isPostUpdateBoot() {
  Preferences prefs;
  prefs.begin(PREF_NAMESPACE_OTA, false);
  bool postUpdate = prefs.getBool(PREF_KEY_POST_UPDATE, false);
  prefs.end();
  return postUpdate;
}

void clearPostUpdateFlag() {
  Preferences prefs;
  prefs.begin(PREF_NAMESPACE_OTA, false);
  prefs.putBool(PREF_KEY_POST_UPDATE, false);
  prefs.end();
}

static void _setState(OtaState newState) {
  OtaState oldState = _currentState;
  _currentState = newState;
  _progress.state = newState;

  Utils::logMessageF("OTA", "State: %s -> %s", stateToString(oldState),
                     stateToString(newState));

  if (_stateChangeCallback != nullptr) {
    _stateChangeCallback(oldState, newState);
  }
}

static void _setError(OtaError error, const String &message) {
  _lastError = error;
  _lastErrorMessage = message;
  _progress.error = error;
  _progress.errorMessage = message;

  Utils::logMessageF("OTA", "Error: %s - %s", errorToString(error),
                     message.c_str());
}

static void _clearError() {
  _lastError = OTA_ERROR_NONE;
  _lastErrorMessage = "";
  _progress.error = OTA_ERROR_NONE;
  _progress.errorMessage = "";
}

static void _updateProgress(uint32_t downloaded, uint32_t total) {
  _progress.bytesDownloaded = downloaded;
  _progress.totalBytes = total;
  _progress.percentage = (total > 0) ? (downloaded * 100 / total) : 0;

  _blinkProgress(_progress.percentage);

  if (_progressCallback != nullptr) {
    _progressCallback(_progress);
  }
}

static void _blinkProgress(uint8_t percentage) {
  // Blink pattern based on progress
  static uint32_t lastBlink = 0;
  uint32_t now = millis();

  // Faster blinking as progress increases
  uint32_t interval = 500 - (percentage * 4); // 500ms -> 100ms
  interval = max(interval, (uint32_t)100);

  if (now - lastBlink > interval) {
    static bool ledState = false;
    ledState = !ledState;
    LedManager::setState(LedManager::LED_WIFI, ledState);
    lastBlink = now;
  }
}

static bool _downloadAndInstall() {
  _setState(OTA_STATE_DOWNLOADING);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(HTTP_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);

  // ✅ STEP 1: Get checksum from GitHub
  String expectedMD5 = fetchChecksumFromGitHub();

  if (!http.begin(client, _firmwareUrl)) {
    _setError(OTA_ERROR_CONNECTION_FAILED, "Failed to begin HTTP connection");
    _setState(OTA_STATE_FAILED);
    return false;
  }

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    _setError(OTA_ERROR_HTTP_ERROR, "HTTP " + String(httpCode));
    _setState(OTA_STATE_FAILED);
    http.end();
    return false;
  }

  int contentLength = http.getSize();

  if (contentLength <= 0) {
    _setError(OTA_ERROR_INVALID_SIZE, "Invalid content length: " + String(contentLength));
    _setState(OTA_STATE_FAILED);
    http.end();
    return false;
  }

  Utils::logMessageF("OTA", "Firmware size: %d bytes (%.2f MB)", 
                    contentLength, contentLength / 1024.0 / 1024.0);

  // ✅ SAFETY: Set MD5 checksum BEFORE Update.begin()
  if (expectedMD5.length() == 32) {
    Utils::logMessageF("OTA", "Setting MD5 checksum: %s", expectedMD5.c_str());
    Update.setMD5(expectedMD5.c_str());
  } else {
    Utils::logMessage("OTA", "⚠️  No MD5 checksum available - validation disabled");
  }

  // Check available space
  if (!Update.begin(contentLength)) {
    _setError(OTA_ERROR_NOT_ENOUGH_SPACE, "Not enough space for update");
    _setState(OTA_STATE_FAILED);
    http.end();
    return false;
  }

  _setState(OTA_STATE_INSTALLING);
  Utils::logMessage("OTA", "Installing firmware...");

  // ✅ SAFETY: Download with network timeout
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[DOWNLOAD_BUFFER_SIZE];
  size_t totalWritten = 0;
  uint8_t lastReportedPercent = 0;
  unsigned long lastDataTime = millis();

  while (http.connected() && totalWritten < contentLength) {
    // ✅ SAFETY: Check for cancellation
    if (_cancelRequested) {
      _setError(OTA_ERROR_CANCELLED, "Update cancelled by user");
      Update.abort();
      http.end();
      _setState(OTA_STATE_FAILED);
      return false;
    }

    size_t available = stream->available();

    if (available) {
      lastDataTime = millis();  // ✅ Reset timeout on data received

      size_t toRead = min(available, sizeof(buffer));
      size_t bytesRead = stream->readBytes(buffer, toRead);

      if (bytesRead > 0) {
        size_t bytesWritten = Update.write(buffer, bytesRead);

        if (bytesWritten != bytesRead) {
          _setError(OTA_ERROR_WRITE_FAILED,
                    "Write mismatch: " + String(bytesWritten) + "/" + String(bytesRead));
          Update.abort();
          http.end();
          _setState(OTA_STATE_FAILED);
          return false;
        }

        totalWritten += bytesWritten;

        // Update progress
        _updateProgress(totalWritten, contentLength);

        // Log progress at intervals
        uint8_t currentPercent = (totalWritten * 100) / contentLength;
        if (currentPercent >= lastReportedPercent + PROGRESS_REPORT_INTERVAL) {
          Utils::logMessageF("OTA", "Progress: %d%% (%d/%d bytes)",
                             currentPercent, totalWritten, contentLength);
          lastReportedPercent = currentPercent;
        }
      }
    } else {
      // ✅ SAFETY: Check network timeout
      if (millis() - lastDataTime > NETWORK_TIMEOUT_MS) {
        _setError(OTA_ERROR_TIMEOUT, "Network timeout - no data received");
        Update.abort();
        http.end();
        _setState(OTA_STATE_FAILED);
        Utils::logMessage("OTA", "❌ Network timeout during download");
        return false;
      }
      delay(10);
    }

    yield();
  }

  http.end();

  // ✅ SAFETY: Verify download complete
  if (totalWritten != contentLength) {
    _setError(OTA_ERROR_VERIFICATION_FAILED,
              "Incomplete download: " + String(totalWritten) + "/" + String(contentLength));
    Update.abort();
    _setState(OTA_STATE_FAILED);
    return false;
  }

  _setState(OTA_STATE_VERIFYING);
  Utils::logMessage("OTA", "Verifying firmware...");

  // ✅ SAFETY: Finalize with MD5 check (if set)
  if (!Update.end(true)) {
    _setError(OTA_ERROR_FINALIZATION_FAILED, 
              String("Update.end() failed: ") + Update.errorString());
    Update.printError(Serial);
    _setState(OTA_STATE_FAILED);
    return false;
  }

  if (!Update.isFinished()) {
    _setError(OTA_ERROR_FINALIZATION_FAILED, "Update not finished");
    _setState(OTA_STATE_FAILED);
    return false;
  }

  // Save new ETag
  _saveEtag(_updateInfo.etag);

  // ✅ SAFETY: Set post-update flag for validation on next boot
  _setPostUpdateFlag();

  _setState(OTA_STATE_SUCCESS);

  Utils::logMessage("OTA", "✅ UPDATE SUCCESSFUL!");
  Utils::logMessage("OTA", "Device will reboot in 3 seconds...");

  // Success indication
  LedManager::runSuccessSequence();

  Utils::delayTask(3000);
  ESP.restart();

  return true;
}

static void _saveEtag(const String &etag) {
  Preferences prefs;
  prefs.begin(PREF_NAMESPACE_OTA, false);
  prefs.putString(PREF_KEY_ETAG, etag);
  prefs.end();
  Utils::logMessageF("OTA", "Saved ETag: %s", etag.c_str());
}

static String _loadEtag() {
  Preferences prefs;
  prefs.begin(PREF_NAMESPACE_OTA, false);
  String etag = prefs.getString(PREF_KEY_ETAG, "");
  prefs.end();
  return etag;
}

static void _setPostUpdateFlag() {
  Preferences prefs;
  prefs.begin(PREF_NAMESPACE_OTA, false);
  prefs.putBool(PREF_KEY_POST_UPDATE, true);
  prefs.putString(PREF_KEY_VERSION, FIRMWARE_VERSION);
  prefs.end();
}

} // namespace OtaManager
