#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>

namespace BatteryManager {

/**
 *   Battery status structure
 */
struct BatteryStatus {
  float voltage;
  float percentage;
  bool isCharging;
  bool isLow;
  bool isCritical;
};

/**
 *   Initialize battery monitoring
 * @return true if initialization successful
 */
bool begin();

/**
 *   Stop battery monitoring
 */
void stop();

/**
 *   Get current battery status
 * @return BatteryStatus structure
 */
BatteryStatus getStatus();

/**
 *   Get battery voltage
 * @return Voltage in volts
 */
float getVoltage();

/**
 *   Get battery percentage
 * @return Percentage (0-100)
 */
float getPercentage();

/**
 *   Check if battery is charging
 * @return true if charging
 */
bool isCharging();

/**
 *   Check if battery is low
 * @return true if below low threshold
 */
bool isLow();

/**
 *   Check if battery is critical
 * @return true if below critical threshold
 */
bool isCritical();

/**
 *   Update battery readings and LED status
 */
void update();

} // namespace BatteryManager

#endif // BATTERY_MANAGER_H
