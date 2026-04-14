#include "utils.h"
#include "config.h"
#include <WiFi.h>
#include <stdarg.h>

namespace Utils {

static uint64_t _millisOffset = 0;
static uint32_t _lastMillis = 0;

uint64_t millis64() {
  uint32_t currentMillis = millis();

  // Handle overflow
  if (currentMillis < _lastMillis) {
    _millisOffset += 0x100000000ULL;
  }
  _lastMillis = currentMillis;

  return _millisOffset + currentMillis;
}

String getFormattedTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time Not Available";
  }

  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

bool configureNTP() {
  // added extra ntp servers in case the configured is down or something
  configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC, NTP_SERVER,
             "time.google.com", "time.nist.gov");

  Serial.print("NTP Syncing time");
  time_t now = time(nullptr);
  int attempts = 0;

  while (now < 1000000000 && attempts < 30) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }

  if (now > 1000000000) {
    Serial.println("Synchronized");
    return true;
  }

  Serial.println("Failed to sync");
  return false;
}

bool isTimeSynced() {
  time_t now = time(nullptr);
  return now > 1000000000;
}

String generateDeviceId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);

  char deviceId[18];
  snprintf(deviceId, sizeof(deviceId), "%02X%02X%02X%02X%02X%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);

  return String(deviceId);
}

void delayTask(uint32_t ms) { 
  yield();  // ✅ Feed watchdog BEFORE delay
  vTaskDelay(pdMS_TO_TICKS(ms));
  yield();  // ✅ Feed watchdog AFTER delay
}
void safeStrCopy(char *dest, const char *src, size_t destSize) {
  if (dest == nullptr || src == nullptr || destSize == 0)
    return;

  strncpy(dest, src, destSize - 1);
  dest[destSize - 1] = '\0';
}

void logMessage(const char *tag, const char *message) {
  Serial.printf("[%s] %s\n", tag, message);
}

void logMessageF(const char *tag, const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  Serial.printf("[%s] %s\n", tag, buffer);
}

} // namespace Utils
