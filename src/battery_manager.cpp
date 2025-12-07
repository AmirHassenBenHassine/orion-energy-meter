#include "battery_manager.h"
#include "config.h"
#include "led_manager.h"
#include "utils.h"

namespace BatteryManager {

static BatteryStatus _status = {0, 0, false, false, false};

static bool _initialized = false;

static float _readVoltage();
static float _calculatePercentage(float voltage);

bool begin() {
  if (_initialized) {
    return true;
  }

  // Configure pins
  pinMode(BATTERY_VOLTAGE_PIN, INPUT);
  pinMode(CHARGING_STATUS_PIN, INPUT);

  // Set ADC resolution
  analogReadResolution(12);

  // Initial reading
  update();

  _initialized = true;
  Utils::logMessage("BATTERY", "Battery Manager initialized");

  return true;
}

void stop() {
  _initialized = false;
  Utils::logMessage("BATTERY", "Battery Manager stopped");
}

BatteryStatus getStatus() { return _status; }

float getVoltage() { return _status.voltage; }

float getPercentage() { return _status.percentage; }

bool isCharging() { return _status.isCharging; }

bool isLow() { return _status.isLow; }

bool isCritical() { return _status.isCritical; }

void update() {
  _status.voltage = _readVoltage();

  _status.percentage = _calculatePercentage(_status.voltage);

  _status.isCharging = digitalRead(CHARGING_STATUS_PIN) == HIGH;

  _status.isLow = _status.percentage <= BATTERY_LOW_THRESHOLD;
  _status.isCritical = _status.percentage <= BATTERY_CRITICAL_THRESHOLD;

  LedManager::updateBatteryLed(_status.percentage);
  LedManager::updateChargingLed(_status.isCharging);
}

// Private implementation

static float _readVoltage() {
  float sum = 0;

  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    sum += analogReadMilliVolts(BATTERY_VOLTAGE_PIN);
    delayMicroseconds(100);
  }

  float avgMilliVolts = sum / BATTERY_SAMPLES;
  float voltage = (avgMilliVolts / 1000.0f) * BATTERY_VOLTAGE_DIVIDER_RATIO;

  return voltage;
}

static float _calculatePercentage(float voltage) {
  if (voltage >= BATTERY_MAX_VOLTAGE) {
    return 100.0f;
  }
  if (voltage <= BATTERY_MIN_VOLTAGE) {
    return 0.0f;
  }

  float percentage = ((voltage - BATTERY_MIN_VOLTAGE) /
                      (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) *
                     100.0f;

  return constrain(percentage, 0.0f, 100.0f);
}

} // namespace BatteryManager
