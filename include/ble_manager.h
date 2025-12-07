#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include "energy_sensor.h"
#include <Arduino.h>

namespace BleManager {

/**
 *   BLE connection states
 */
enum BleState {
  BLE_STATE_OFF = 0,
  BLE_STATE_ADVERTISING,
  BLE_STATE_CONNECTED,
  BLE_STATE_ERROR
};

/**
 *   Initialize BLE with energy monitor service
 * @return true if initialization successful
 */
bool begin();

/**
 *   Stop BLE and cleanup
 */
void stop();

/**
 *   Check if BLE is enabled
 * @return true if BLE is enabled
 */
bool isEnabled();

/**
 *   Check if a client is connected
 * @return true if client connected
 */
bool isClientConnected();

/**
 *   Get number of connected clients
 * @return Number of connections
 */
int getConnectedCount();

/**
 *   Get current BLE state
 * @return Current state
 */
BleState getState();

/**
 *   Send energy metrics via BLE notification
 * @param metrics Energy metrics to send
 * @return true if sent successfully
 */
bool sendMetrics(const EnergySensor::EnergyMetrics &metrics);

/**
 *   Send raw data via BLE notification
 * @param data Data string to send
 * @return true if sent successfully
 */
bool sendData(const String &data);

/**
 *   Start advertising
 */
void startAdvertising();

/**
 *   Stop advertising
 */
void stopAdvertising();

} // namespace BleManager

#endif // BLE_MANAGER_H
