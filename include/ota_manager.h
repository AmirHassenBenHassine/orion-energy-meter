#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace OtaManager {

/**
 *   OTA update states
 */
enum OtaState {
  OTA_STATE_IDLE = 0,
  OTA_STATE_CHECKING,
  OTA_STATE_UPDATE_AVAILABLE,
  OTA_STATE_DOWNLOADING,
  OTA_STATE_VERIFYING,
  OTA_STATE_INSTALLING,
  OTA_STATE_SUCCESS,
  OTA_STATE_FAILED,
  OTA_STATE_NO_UPDATE
};

/**
 *   OTA error codes
 */
enum OtaError {
  OTA_ERROR_NONE = 0,
  OTA_ERROR_NO_WIFI,
  OTA_ERROR_CONNECTION_FAILED,
  OTA_ERROR_HTTP_ERROR,
  OTA_ERROR_INVALID_SIZE,
  OTA_ERROR_NOT_ENOUGH_SPACE,
  OTA_ERROR_WRITE_FAILED,
  OTA_ERROR_VERIFICATION_FAILED,
  OTA_ERROR_FINALIZATION_FAILED,
  OTA_ERROR_TIMEOUT,
  OTA_ERROR_CANCELLED
};

/**
 *   OTA update information
 */
struct OtaUpdateInfo {
  bool available;
  String currentVersion;
  String newVersion;
  String etag;
  uint32_t size;
  String url;
};

/**
 *   OTA progress information
 */
struct OtaProgress {
  OtaState state;
  uint32_t bytesDownloaded;
  uint32_t totalBytes;
  uint8_t percentage;
  OtaError error;
  String errorMessage;
};

/**
 *   Progress callback type
 * @param progress Current progress information
 */
typedef void (*ProgressCallback)(const OtaProgress &progress);

/**
 *   State change callback type
 * @param oldState Previous state
 * @param newState New state
 */
typedef void (*StateChangeCallback)(OtaState oldState, OtaState newState);

/**
 *   Initialize OTA manager
 * @return true if initialization successful
 */
bool begin();

/**
 *   Stop OTA manager
 */
void stop();

/**
 * Process OTA tasks
 */
void loop();

/**
 *   Check for available firmware update
 * @param info Optional pointer to receive update info
 * @return true if update available
 */
bool checkForUpdate(OtaUpdateInfo *info = nullptr);

/**
 *   Perform OTA update if available
 * @return true if update started successfully (will reboot on success)
 */
bool performUpdate();

/**
 *   Check and update in one call
 * @return true if update was performed (will reboot on success)
 */
bool checkAndUpdate();

/**
 *   Cancel ongoing update
 */
void cancelUpdate();

/**
 *   Get current OTA state
 * @return Current state
 */
OtaState getState();

/**
 *   Get current progress information
 * @return Progress structure
 */
OtaProgress getProgress();

/**
 *   Get last error code
 * @return Error code
 */
OtaError getLastError();

/**
 *   Get last error message
 * @return Error message string
 */
String getLastErrorMessage();

/**
 *   Get update information from last check
 * @return Update info structure
 */
OtaUpdateInfo getUpdateInfo();

/**
 * Set measurement active state (OTA waits if true)
 * @param active true if measurement in progress
 */
void setMeasurementActive(bool active);

/**
 * Check if measurement is blocking OTA
 * @return true if OTA is blocked by measurement
 */
bool isMeasurementActive();

/**
 * Get MD5 checksum from version.json
 * @return MD5 string or empty if not available
 */
String fetchChecksumFromGitHub();

/**
 *   Set progress callback
 * @param callback Function to call with progress updates
 */
void setProgressCallback(ProgressCallback callback);

/**
 *   Set state change callback
 * @param callback Function to call on state changes
 */
void setStateChangeCallback(StateChangeCallback callback);

/**
 *   Check if enough time has passed for scheduled check
 * @param lastCheckTime Last check timestamp (millis64)
 * @return true if should check now
 */
bool shouldCheckNow(uint64_t lastCheckTime);

/**
 *   Get current firmware version
 * @return Version string
 */
String getCurrentVersion();

/**
 *   Get firmware build date
 * @return Build date string
 */
String getBuildDate();

/**
 *   Set custom firmware URL
 * @param url URL to firmware binary
 */
void setFirmwareUrl(const String &url);

/**
 *   Get current firmware URL
 * @return Firmware URL
 */
String getFirmwareUrl();

/**
 *   Set update check interval
 * @param intervalMs Interval in milliseconds
 */
void setCheckInterval(uint32_t intervalMs);

/**
 *   Get error string from error code
 * @param error Error code
 * @return Error string
 */
const char *errorToString(OtaError error);

/**
 *   Get state string from state
 * @param state OTA state
 * @return State string
 */
const char *stateToString(OtaState state);

/**
 *   Mark current firmware as valid (rollback protection)
 */
void markFirmwareValid();

/**
 *   Check if running after OTA update
 * @return true if this is first boot after OTA
 */
bool isPostUpdateBoot();

/**
 *   Clear post-update flag
 */
void clearPostUpdateFlag();

} // namespace OtaManager

#endif // OTA_MANAGER_H
