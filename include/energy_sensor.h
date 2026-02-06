#ifndef ENERGY_SENSOR_H
#define ENERGY_SENSOR_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

namespace EnergySensor {

/**
 *   Three-phase energy metrics structure
 */
struct EnergyMetrics {
  String deviceId;
  String timestamp;
  float battery;
  float voltage;
  float current[MAX_PHASES];
  float power[MAX_PHASES];
  float totalPower;
  float energyTotal;

  /**
   *   Convert metrics to JSON string
   * @return JSON formatted string
   */
  String toJson() const;

  /**
   *   Reset all values to zero
   */
  void reset();
};

/**
 *   Initialize energy sensors
 * @return true if initialization successful
 */
bool begin();

/**
 *   Check if CT sensors are connected
 * @param phase Phase index (0-2)
 */
bool isPhaseConnected(int phase);

/**
 *   Stop energy sensor monitoring
 */
void stop();

/**
 *   Read all sensors and update metrics
 * @param metrics Reference to metrics structure to update
 */
void readSensors(EnergyMetrics &metrics);

/**
 *   Get last readings without re-reading sensors
 * @return Copy of last metrics
 */
EnergyMetrics getLastMetrics();

/**
 *   Get accumulated energy (kWh)
 * @return Energy in kWh
 */
float getAccumulatedEnergy();

/**
 *   Reset accumulated energy counter
 */
void resetAccumulatedEnergy();

/**
 *   Set voltage calibration factor
 * @param calibration Calibration value
 */
void setVoltageCalibration(float calibration);

/**
 *   Set current calibration factor for a phase
 * @param phase Phase index (0-2)
 * @param calibration Calibration value
 */
void setCurrentCalibration(int phase, float calibration);

/**
 *   Perform sensor warmup readings
 * @param iterations Number of warmup cycles
 */
void warmup(int iterations = 5);

/**
 *   Print metrics to Serial
 * @param metrics Metrics to print
 */
void printMetrics(const EnergyMetrics &metrics);

/**
 * Get device serial number from NVS calibration
 * @return Serial number string (or "UNCALIBRATED" if not set)
 */
String getSerialNumber();

} // namespace EnergySensor

#endif // ENERGY_SENSOR_H
