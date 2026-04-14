#ifndef WIFI_MANAGER_MODULE_H
#define WIFI_MANAGER_MODULE_H

#include <Arduino.h>
#include <WiFi.h>

namespace WifiManager {

/**
 *   WiFi connection states
 */
enum WifiState {
  WIFI_STATE_DISCONNECTED = 0,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_AP_MODE,
  WIFI_STATE_PORTAL_ACTIVE,
  WIFI_STATE_ERROR
};

/**
 *   WiFi event callback type
 */
typedef void (*WifiEventCallback)(WifiState state);

/**
 *   Initialize WiFi manager
 * @param forcePortal Force captive portal mode on startup
 * @return true if initialization started successfully
 */
bool begin(bool forcePortal = false);

/**
 *   Stop WiFi manager and disconnect
 */
void stop();

/**
 *   Check if WiFi is fully connected (has IP and stable)
 * @return true if fully connected
 */
bool isFullyConnected();

/**
 *   Check if currently in AP/Portal mode
 * @return true if in AP mode
 */
bool isAPMode();

/**
 *   Check if currently in Portal mode
 * @return true if in AP mode
 */
bool isInPairingMode();

/**
 *   Get current WiFi state
 * @return Current state
 */
WifiState getState();

/**
 *   Get local IP address
 * @return IP address or 0.0.0.0 if not connected
 */
IPAddress getLocalIP();

/**
 *   Get AP IP address
 * @return AP IP address
 */
IPAddress getAPIP();

/**
 * Start configuration portal manually
 * @param triggeredByButton true if triggered by button press, false if auto-started
 */
void startPortal(bool triggeredByButton = false);

bool isPortalActive();

/**
 *   Force reconnection attempt
 */
void forceReconnect();

/**
 *   Reset all saved WiFi credentials
 */
void resetCredentials();

/**
 *   Test actual internet connectivity
 * @return true if internet is reachable
 */
bool testConnectivity();

/**
 *   Scan for available WiFi networks
 * @return JSON string of networks
 */
String scanNetworks();

/**
 *   Register callback for WiFi events
 * @param callback Function to call on events
 */
void setEventCallback(WifiEventCallback callback);

/**
 *   Get connection statistics
 * @param reconnectAttempts Output: number of reconnect attempts
 * @param lastConnectedTime Output: timestamp of last connection
 */
void getStats(uint32_t &reconnectAttempts, uint64_t &lastConnectedTime);

/**
 *   Setup mDNS services
 * @return true if successful
 */
bool setupMDNS();

bool saveCredentials(const char *ssid, const char *password);
bool connectWithCredentials(const char *ssid, const char *password);
} // namespace WifiManager

#endif // WIFI_MANAGER_MODULE_H
