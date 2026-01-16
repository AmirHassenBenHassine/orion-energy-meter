#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include "energy_sensor.h"
#include <Arduino.h>

namespace MqttManager {

/**
 * MQTT connection states
 */
enum MqttState {
  MQTT_STATE_DISCONNECTED = 0,
  MQTT_STATE_CONNECTING,
  MQTT_STATE_CONNECTED,
  MQTT_STATE_TLS_ERROR,
  MQTT_STATE_AUTH_ERROR,
  MQTT_STATE_ERROR
};

/**
 * xxxxxxxxxxxxxxxx FOR FUTURE WHEN WE WILL HAVE VPS BROKER SETUP
 * MQTT security configuration
 */
struct MqttSecurityConfig {
  bool useTLS;            // Enable TLS/SSL
  bool verifyServer;      // Verify server certificate
  bool useClientCert;     // Use client certificate (mutual TLS)
  const char *caCert;     // CA certificate (PEM format)
  const char *clientCert; // Client certificate (PEM format)
  const char *clientKey;  // Client private key (PEM format)
  const char *username;   // MQTT username
  const char *password;   // MQTT password
};

/**
 * MQTT broker configuration
 */
struct MqttBrokerConfig {
  const char *host;           // Broker hostname/IP
  uint16_t port;              // Broker port (1883 or 8883 for TLS)
  const char *clientIdPrefix; // Client ID prefix
  uint16_t keepAlive;         // Keep alive interval (seconds)
  uint16_t bufferSize;        // Message buffer size
  uint8_t qos;                // Default QoS level
  bool cleanSession;          // Clean session flag
};

/**
 * MQTT connection statistics
 */
struct MqttStats {
  uint32_t connectAttempts;
  uint32_t connectSuccesses;
  uint32_t connectFailures;
  uint32_t messagesPublished;
  uint32_t messagesFailed;
  uint32_t messagesReceived;
  uint32_t disconnects;
  uint64_t lastConnectedTime;
  uint64_t totalConnectedTime;
};

/**
 * Callback type for incoming messages
 */
typedef void (*MessageCallback)(const char *topic, const char *payload,
                                unsigned int length);

/**
 * Callback type for trigger actions
 */
typedef void (*TriggerCallback)(const String &action);

/**
 * Callback type for connection state changes
 */
typedef void (*ConnectionCallback)(MqttState state);

/**
 * Initialize MQTT manager with default configuration
 * @return true if initialization successful
 */
bool begin();

/**
 * Initialize MQTT manager with custom broker configuration
 * @param brokerConfig Broker configuration
 * @return true if initialization successful
 */
bool begin(const MqttBrokerConfig &brokerConfig);

/**
 * Configure security settings
 * @param securityConfig Security configuration
 */
void configureSecurity(const MqttSecurityConfig &securityConfig);

/**
 * Full configuration with broker and security settings
 * @param brokerConfig Broker configuration
 * @param securityConfig Security configuration
 * @return true if configuration successful
 */
bool configure(const MqttBrokerConfig &brokerConfig,
               const MqttSecurityConfig &securityConfig);

/**
 * Stop MQTT manager and disconnect
 */
void stop();

/**
 * Process MQTT loop (call frequently in main loop)
 */
void loop();

/**
 * Check if connected to broker
 * @return true if connected
 */
bool isConnected();

/**
 * Get current MQTT state
 * @return Current state
 */
MqttState getState();

/**
 * Force reconnection to broker
 */
void reconnect();

/**
 * Disconnect from broker
 */
void disconnect();

// the new getter functions
bool isWaitingForConfirmation();
bool hasReceivedConfirmation();
String getConfirmedSSID();
void clearConfirmation();

/**
 * Set broker credentials at runtime
 * @param username MQTT username
 * @param password MQTT password
 */
void setCredentials(const char *username, const char *password);

/**
 * Set broker host and port at runtime
 * @param host Broker hostname/IP
 * @param port Broker port
 */
void setBroker(const char *host, uint16_t port);

void setAutoReconnect(bool enabled);

/**
 * Publish energy metrics
 * @param metrics Energy metrics to publish
 * @return true if publish successful
 */
bool publishMetrics(const EnergySensor::EnergyMetrics &metrics);

/**
 * Publish raw message to topic
 * @param topic Topic to publish to
 * @param payload Message payload
 * @param retained Retain flag
 * @param qos QoS level (0, 1, or 2)
 * @return true if publish successful
 */
bool publish(const char *topic, const char *payload, bool retained = false,
             uint8_t qos = 0);

/**
 * Publish device status
 * @param mode Current mode (AP/STA)
 * @param ip Current IP address
 * @return true if publish successful
 */
bool publishStatus(const String &mode, const String &ip);

/**
 * Publish WiFi scan results
 * @param networksJson JSON string of networks
 * @return true if publish successful
 */
bool publishScanResults(const String &networksJson);

/**
 * Publish configuration confirmation
 * @param ssid SSID that was configured
 * @return true if publish successful
 */
bool publishConfirmation(const String &ssid);

/**
 * Publish WiFi credentials to gateway
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return true if published successfully
 */
bool publishWiFiCredentials(const String &ssid, const String &password);

/**
 * Subscribe to a topic
 * @param topic Topic to subscribe to
 * @param qos QoS level
 * @return true if subscription successful
 */
bool subscribe(const char *topic, uint8_t qos = 0);

/**
 * Unsubscribe from a topic
 * @param topic Topic to unsubscribe from
 * @return true if unsubscription successful
 */
bool unsubscribe(const char *topic);

/**
 * Set callback for incoming messages
 * @param callback Function to call on messages
 */
void setMessageCallback(MessageCallback callback);

/**
 * Set callback for trigger actions
 * @param callback Function to call on trigger
 */
void setTriggerCallback(TriggerCallback callback);

/**
 * Set callback for connection state changes
 * @param callback Function to call on state change
 */
void setConnectionCallback(ConnectionCallback callback);

/**
 * Check for pending trigger actions
 * @return true if trigger pending
 */
bool hasPendingTrigger();

/**
 * Get pending trigger action
 * @return Trigger action string
 */
String getPendingTrigger();

/**
 * Clear pending trigger
 */
void clearPendingTrigger();

/**
 * Get connection statistics
 * @return MqttStats structure
 */
MqttStats getStats();

/**
 * Reset statistics counters
 */
void resetStats();

/**
 * Get last error message
 * @return Error message string
 */
String getLastError();

/**
 * Get state as string
 * @param state MQTT state
 * @return State string
 */
const char *stateToString(MqttState state);

/**
 * Save MQTT credentials to secure storage
 * @param username MQTT username
 * @param password MQTT password
 * @return true if saved successfully
 */
bool saveCredentials(const char *username, const char *password);

/**
 * Load MQTT credentials from secure storage
 * @param username Output buffer for username
 * @param password Output buffer for password
 * @param maxLen Maximum buffer length
 * @return true if loaded successfully
 */
bool loadCredentials(char *username, char *password, size_t maxLen);

/**
 * Clear stored credentials
 */
void clearStoredCredentials();

} // namespace MqttManager
#endif // MQTT_MANAGER_H
