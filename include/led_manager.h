#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>

namespace LedManager {

/**
 *   LED identifiers
 */
enum LedType { LED_POWER = 0, LED_WIFI, LED_CHARGING, LED_BATTERY, LED_COUNT };

/**
 *   LED modes/states
 */
enum LedMode {
  LED_OFF = 0,
  LED_ON,
  LED_BLINK_FAST, // 200ms interval
  LED_BLINK_SLOW, // 500ms interval
  LED_BLINK_AP    // 1000ms interval (AP mode)
};

/**
 *   Initialize LED manager and start all LED tasks
 * @return true if initialization successful
 */
bool begin();

/**
 *   Stop all LED tasks and cleanup
 */
void stop();

/**
 *   Set LED mode
 * @param led LED identifier
 * @param mode LED mode
 */
void setMode(LedType led, LedMode mode);

/**
 *   Get current LED mode
 * @param led LED identifier
 * @return Current LED mode
 */
LedMode getMode(LedType led);

/**
 *   Set LED state directly (bypass mode)
 * @param led LED identifier
 * @param state true = ON, false = OFF
 */
void setState(LedType led, bool state);

/**
 *   Run startup LED sequence
 */
void runStartupSequence();

/**
 *   Run error indication sequence
 */
void runErrorSequence();

/**
 *   Run success indication sequence
 */
void runSuccessSequence();

/**
 *   Update battery LED based on percentage
 * @param percentage Battery percentage (0-100)
 */
void updateBatteryLed(float percentage);

/**
 *   Update charging LED based on status
 * @param isCharging true if charging
 */
void updateChargingLed(bool isCharging);

/**
 *   Update WiFi LED based on connection status
 * @param isConnected true if WiFi connected
 * @param isAPMode true if in AP mode
 */
void updateWiFiLed(bool isConnected, bool isAPMode);

} // namespace LedManager

#endif // LED_MANAGER_H
