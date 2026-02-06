#include "energy_sensor.h"
#include "battery_manager.h"
#include "config.h"
#include "utils.h"

#include <EmonLib.h>
#include <Preferences.h>

namespace EnergySensor {

static Preferences _calibPrefs;

// Add near the top with other static variables
static bool _ctConnected[MAX_PHASES] = {false, false, false};
static uint32_t _lastCtCheck[MAX_PHASES] = {0, 0, 0};
static const uint32_t CT_CHECK_INTERVAL = 5000; // Check every 5 seconds

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

// Serial number
static String _serialNumber = "UNCALIBRATED";

// NVS CALIBRATION FUNCTIONS

/**
 * Load calibration data from NVS
 * Returns true if calibration data was found, false if using defaults
 */
static bool loadCalibrationFromNVS() {  
  if (!_calibPrefs.begin("calibration", false)) { // Read-only mode
    Utils::logMessage("SENSOR", "⚠️  Failed to open NVS calibration - using defaults");
    return false;
  }
  
  // Check if calibration exists
  if (!_calibPrefs.isKey("vCal")) {
    Utils::logMessage("SENSOR", "⚠️  No calibration found in NVS - using defaults");
    _calibPrefs.end();
    return false;
  }
  
  // Load voltage calibration
  _voltageCal = _calibPrefs.getFloat("vCal", VOLTAGE_CALIBRATION);
  
  // Load current calibration for each phase
  _currentCal[0] = _calibPrefs.getFloat("iCal0", CURRENT_CALIBRATION_DEFAULT);
  _currentCal[1] = _calibPrefs.getFloat("iCal1", CURRENT_CALIBRATION_DEFAULT);
  _currentCal[2] = _calibPrefs.getFloat("iCal2", CURRENT_CALIBRATION_DEFAULT);
  
  // Load serial number
  _serialNumber = _calibPrefs.getString("serialNum", "UNCALIBRATED");
  
  _calibPrefs.end();
  
  // Log calibration values
  Utils::logMessage("SENSOR", "✅ Calibration loaded from NVS:");
  Utils::logMessageF("SENSOR", "   Serial Number : %s", _serialNumber.c_str());
  Utils::logMessageF("SENSOR", "   Voltage Cal   : %.3f", _voltageCal);
  Utils::logMessageF("SENSOR", "   Current R Cal : %.3f", _currentCal[0]);
  Utils::logMessageF("SENSOR", "   Current Y Cal : %.3f", _currentCal[1]);
  Utils::logMessageF("SENSOR", "   Current B Cal : %.3f", _currentCal[2]);

  return true;
}

/**
 * Get serial number from NVS
 */
String getSerialNumber() {
  return _serialNumber;
}

// JSON implementation for EnergyMetrics
String EnergyMetrics::toJson() const {
  StaticJsonDocument<512> doc;

  doc["deviceId"] = deviceId;
  doc["timestamp"] = timestamp;
  doc["battery"] = battery;
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

bool isPhaseConnected(int phase) {
  if (phase >= 0 && phase < MAX_PHASES) {
    return _ctConnected[phase];
  }
  return false;
} 

// Add the detection function
static bool isCtConnected(int phase) {
  if (phase < 0 || phase >= MAX_PHASES) {
    return false;
  }
  
  int pin = _currentPins[phase];
  
  // Take quick samples
  const int samples = 50;
  int readings[samples];
  
  for (int i = 0; i < samples; i++) {
    readings[i] = analogRead(pin);
    delayMicroseconds(100);
  }
  
  // Calculate range (max - min)
  int minVal = 4095;
  int maxVal = 0;
  
  for (int i = 0; i < samples; i++) {
    if (readings[i] < minVal) minVal = readings[i];
    if (readings[i] > maxVal) maxVal = readings[i];
  }
  
  int range = maxVal - minVal;
  
  // Debug output
  // Utils::logMessageF("SENSOR", "Phase %d: range=%d, min=%d, max=%d", 
  //                    phase + 1, range, minVal, maxVal);
  
  // Floating pins have huge range (your data shows 0-3445)
  // Connected CTs should have smaller, consistent range around 1800-1900
  const int MAX_ACCEPTABLE_RANGE = 1200;
  
  if (range > MAX_ACCEPTABLE_RANGE) {
    // Utils::logMessageF("SENSOR", "Phase %d: DISCONNECTED (range too high)", phase + 1);
    return false; // Disconnected/floating
  }
  
  // Utils::logMessageF("SENSOR", "Phase %d: CONNECTED", phase + 1);
  return true;
}

bool begin() {
  if (_initialized) {
    return true;
  }

  analogReadResolution(12);

  // *** LOAD CALIBRATION FROM NVS ***
  bool calibLoaded = loadCalibrationFromNVS();
  
  if (!calibLoaded) {
    Utils::logMessage("SENSOR", "⚠️  WARNING: Using factory default calibration");
    Utils::logMessage("SENSOR", "⚠️  Accuracy may be reduced (±5-10%)");
    Utils::logMessage("SENSOR", "⚠️  Device should be calibrated!");
  }

  // Initialize voltage sensor with loaded/default calibration
  _emonVoltage.voltage(VOLTAGE_PIN, _voltageCal, 1.7);

  // Initialize current sensors with loaded/default calibration
  for (int i = 0; i < MAX_PHASES; i++) {
    _emonCurrent[i].current(_currentPins[i], _currentCal[i]);
  }

  warmup(10);

  _lastMetrics.reset();
  _lastMetrics.deviceId = Utils::generateDeviceId();
  _lastUpdateTime = Utils::millis64();

  // Detect CT sensors on startup
  Utils::logMessage("SENSOR", "Detecting CT sensors...");
  delay(100); // Brief settling time
  
  for (int i = 0; i < MAX_PHASES; i++) {
    _ctConnected[i] = isCtConnected(i);
    _lastCtCheck[i] = millis();
  }

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

  const int numSamples = 500;
  double sumV = 0;
  double offsetV = 2048.0;

  for (int i = 0; i < numSamples; i++) {
    int rawV = analogRead(VOLTAGE_PIN);
    double filteredV = rawV - offsetV;
    sumV += filteredV * filteredV;
    sumV += rawV * rawV;
    delayMicroseconds(200);
  }

  metrics.voltage = sqrt(sumV / numSamples) * _voltageCal / 2048.0;

  // _emonVoltage.calcVI(20, 2000);
  // metrics.voltage = _emonVoltage.Vrms;
  // Read current for each phase
  metrics.totalPower = 0;

  for (int i = 0; i < MAX_PHASES; i++) {
    // Periodically recheck CT connection status
    uint32_t now = millis();
    if (now - _lastCtCheck[i] > CT_CHECK_INTERVAL) {
      _ctConnected[i] = isCtConnected(i);
      _lastCtCheck[i] = now;
    }
    
    // If CT is not connected, set values to zero and skip
    if (!_ctConnected[i]) {
      metrics.current[i] = 0;
      metrics.power[i] = 0;
      continue;
    }

    float current = _emonCurrent[i].calcIrms(1480);

    // Apply noise threshold
    if (current < CURRENT_NOISE_THRESHOLD) {
      current = 0;
    }

    // Clamp to max
    if (current > CURRENT_MAX_READING) {
      current = 0;
      _ctConnected[i] = false; // Mark for immediate recheck
      _lastCtCheck[i] = 0;
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
    for (int k = 0; k < 50; k++) {
      analogRead(VOLTAGE_PIN);
      delayMicroseconds(100);
    }
    
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
    Serial.printf("\n  Phase %d [%s]:\n", 
                  i + 1, 
                  _ctConnected[i] ? "CONNECTED" : "DISCONNECTED");
    Serial.printf("    Current        : %.2f A\n", metrics.current[i]);
    Serial.printf("    Power          : %.2f W\n", metrics.power[i]);
  }
}

} // namespace EnergySensor
