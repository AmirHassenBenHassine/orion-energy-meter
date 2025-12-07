#include "energy_sensor.h"
#include "battery_manager.h"
#include "config.h"
#include "utils.h"

#include <EmonLib.h>

namespace EnergySensor {

// EmonLib instances
static EnergyMonitor _emonCurrent[MAX_PHASES];
static EnergyMonitor _emonVoltage;

// Current pin mapping
static const int _currentPins[MAX_PHASES] = {CURRENT_PIN_R, CURRENT_PIN_Y,
                                             CURRENT_PIN_B};

// Calibration values
static float _voltageCal = VOLTAGE_CALIBRATION;
static float _currentCal[MAX_PHASES] = {CURRENT_CALIBRATION_DEFAULT,
                                        CURRENT_CALIBRATION_DEFAULT,
                                        CURRENT_CALIBRATION_DEFAULT};

// Energy accumulator
static float _accumulatedEnergy = 0;
static uint64_t _lastUpdateTime = 0;

// Last metrics
static EnergyMetrics _lastMetrics;

// Initialized flag
static bool _initialized = false;

// JSON implementation for EnergyMetrics
String EnergyMetrics::toJson() const {
  StaticJsonDocument<512> doc;

  doc["deviceId"] = deviceId;
  doc["battery"] = battery;
  doc["timestamp"] = timestamp;
  doc["voltage"] = voltage;
  doc["totalPower"] = totalPower;
  doc["energyTotal"] = energyTotal;

  JsonArray phases = doc.createNestedArray("phases");
  for (int i = 0; i < MAX_PHASES; i++) {
    JsonObject phase = phases.createNestedObject();
    phase["current"] = current[i];
    phase["power"] = power[i];
  }

  String output;
  serializeJson(doc, output);
  return output;
}

void EnergyMetrics::reset() {
  voltage = 0;
  totalPower = 0;
  energyTotal = 0;
  battery = 0;
  timestamp = "";
  deviceId = "";

  for (int i = 0; i < MAX_PHASES; i++) {
    current[i] = 0;
    power[i] = 0;
  }
}

bool begin() {
  if (_initialized) {
    return true;
  }

  analogReadResolution(12);

  _emonVoltage.voltage(VOLTAGE_PIN, _voltageCal, 1.7);

  for (int i = 0; i < MAX_PHASES; i++) {
    _emonCurrent[i].current(_currentPins[i], _currentCal[i]);
  }

  _lastMetrics.reset();
  _lastMetrics.deviceId = Utils::generateDeviceId();
  _lastUpdateTime = Utils::millis64();

  _initialized = true;
  Utils::logMessage("SENSOR", "Energy sensors initialized");

  return true;
}

void stop() {
  _initialized = false;
  Utils::logMessage("SENSOR", "Energy sensors stopped");
}

void readSensors(EnergyMetrics &metrics) {
  if (!_initialized) {
    Utils::logMessage("SENSOR", "Warning: Sensors not initialized");
    return;
  }

  uint64_t currentTime = Utils::millis64();

  // Read voltage
  _emonVoltage.calcVI(20, 2000);
  metrics.voltage = _emonVoltage.Vrms;

  // Read current for each phase
  metrics.totalPower = 0;

  for (int i = 0; i < MAX_PHASES; i++) {
    float current = _emonCurrent[i].calcIrms(1480);

    // Apply noise threshold
    if (current < CURRENT_NOISE_THRESHOLD) {
      current = 0;
    }

    // Clamp to max
    if (current > CURRENT_MAX_READING) {
      current = 0;
    }

    metrics.current[i] = current;
    metrics.power[i] = metrics.voltage * current;
    metrics.totalPower += metrics.power[i];
  }

  // Calculate accumulated energy
  if (_lastUpdateTime > 0) {
    float timeElapsedHours = (currentTime - _lastUpdateTime) / 3600000.0f;
    _accumulatedEnergy += metrics.totalPower * timeElapsedHours;
  }
  _lastUpdateTime = currentTime;

  // Convert to kWh
  metrics.energyTotal = _accumulatedEnergy / 1000.0f;

  // Add metadata
  metrics.battery = BatteryManager::getPercentage();
  metrics.timestamp = Utils::getFormattedTimestamp();
  metrics.deviceId = Utils::generateDeviceId();

  // Store last metrics
  _lastMetrics = metrics;
}

EnergyMetrics getLastMetrics() { return _lastMetrics; }

float getAccumulatedEnergy() {
  return _accumulatedEnergy / 1000.0f; // Return in kWh
}

void resetAccumulatedEnergy() {
  _accumulatedEnergy = 0;
  Utils::logMessage("SENSOR", "Accumulated energy reset");
}

void setVoltageCalibration(float calibration) {
  _voltageCal = calibration;
  _emonVoltage.voltage(VOLTAGE_PIN, _voltageCal, 1.7);
  Utils::logMessageF("SENSOR", "Voltage calibration set to %.2f", calibration);
}

void setCurrentCalibration(int phase, float calibration) {
  if (phase >= 0 && phase < MAX_PHASES) {
    _currentCal[phase] = calibration;
    _emonCurrent[phase].current(_currentPins[phase], _currentCal[phase]);
    Utils::logMessageF("SENSOR", "Phase %d current calibration set to %.2f",
                       phase, calibration);
  }
}

void warmup(int iterations) {
  Utils::logMessage("SENSOR", "Starting sensor warmup...");

  for (int i = 0; i < iterations; i++) {
    _emonVoltage.calcVI(20, 2000);

    for (int j = 0; j < MAX_PHASES; j++) {
      _emonCurrent[j].calcIrms(1480);
    }

    Utils::delayTask(100);
  }

  Utils::logMessage("SENSOR", "Sensor warmup complete");
}

void printMetrics(const EnergyMetrics &metrics) {
  Serial.println("   ENERGY MONITORING DATA");
  Serial.printf("Device ID          : %s\n", metrics.deviceId.c_str());
  Serial.printf("Timestamp          : %s\n", metrics.timestamp.c_str());
  Serial.printf("Battery            : %.1f%%\n", metrics.battery);
  Serial.printf("Voltage            : %.2f V\n", metrics.voltage);
  Serial.printf("Total Power        : %.2f W\n", metrics.totalPower);
  Serial.printf("Energy Total       : %.3f kWh\n", metrics.energyTotal);

  for (int i = 0; i < MAX_PHASES; i++) {
    Serial.printf("\n  Phase %d:\n", i + 1);
    Serial.printf("    Current        : %.2f A\n", metrics.current[i]);
    Serial.printf("    Power          : %.2f W\n", metrics.power[i]);
  }
}

} // namespace EnergySensor
