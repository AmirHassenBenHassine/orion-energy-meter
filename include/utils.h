#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <time.h>

namespace Utils {

/**
 *   Get current time in milliseconds
 * @return Current time in milliseconds
 */
uint64_t millis64();

/**
 *   Get formatted timestamp string
 * @return Timestamp in format "YYYY-MM-DD HH:MM:SS"
 */
String getFormattedTimestamp();

/**
 *   Configure NTP time synchronization
 * @return true if time sync successful
 */
bool configureNTP();

/**
 *   Check if time is synchronized
 * @return true if time is valid
 */
bool isTimeSynced();

/**
 *   Generate unique device ID from MAC address
 * @return Device ID string
 */
String generateDeviceId();

/**
 *   Delay with task yield
 * @param ms Milliseconds to delay
 */
void delayTask(uint32_t ms);

/**
 *   Safe string copy with null termination
 */
void safeStrCopy(char *dest, const char *src, size_t destSize);

/**
 *   Log message with timestamp
 */
void logMessage(const char *tag, const char *message);
void logMessageF(const char *tag, const char *format, ...);

} // namespace Utils

#endif // UTILS_H
