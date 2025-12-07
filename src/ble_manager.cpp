#include "ble_manager.h"
#include "config.h"
#include "utils.h"

#include <NimBLEDevice.h>

namespace BleManager {

static NimBLEServer *_pServer = nullptr;
static NimBLECharacteristic *_pCharacteristic = nullptr;
static NimBLEAdvertising *_pAdvertising = nullptr;

static volatile BleState _currentState = BLE_STATE_OFF;
static volatile bool _initialized = false;
static volatile int _connectedCount = 0;

// Server callbacks class
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) {
    _connectedCount = pServer->getConnectedCount();
    _currentState = BLE_STATE_CONNECTED;
    Utils::logMessageF("BLE", "Client connected (total: %d)", _connectedCount);

    // Continue advertising for multiple connections
    if (_pAdvertising != nullptr) {
      _pAdvertising->start();
    }
  }

  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo,
                    int reason) {
    _connectedCount = pServer->getConnectedCount();
    if (_connectedCount == 0) {
      _currentState = BLE_STATE_ADVERTISING;
    }
    Utils::logMessageF("BLE", "Client disconnected (remaining: %d)",
                       _connectedCount);

    if (_pAdvertising != nullptr) {
      _pAdvertising->start();
    }
  }
};

static ServerCallbacks _serverCallbacks;

bool begin() {
  if (_initialized) {
    return true;
  }

  Utils::logMessage("BLE", "Initializing BLE...");

  NimBLEDevice::init(BLE_DEVICE_NAME);

  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  _pServer = NimBLEDevice::createServer();
  if (_pServer == nullptr) {
    Utils::logMessage("BLE", "Failed to create server");
    return false;
  }

  _pServer->setCallbacks(&_serverCallbacks);

  NimBLEService *pService = _pServer->createService(BLE_SERVICE_UUID);
  if (pService == nullptr) {
    Utils::logMessage("BLE", "Failed to create service");
    return false;
  }

  _pCharacteristic = pService->createCharacteristic(
      BLE_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  if (_pCharacteristic == nullptr) {
    Utils::logMessage("BLE", "Failed to create characteristic");
    return false;
  }

  pService->start();

  _pAdvertising = NimBLEDevice::getAdvertising();
  _pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  _pAdvertising->setName(BLE_DEVICE_NAME);
  _pAdvertising->start();

  _initialized = true;
  _currentState = BLE_STATE_ADVERTISING;

  Utils::logMessage("BLE", "BLE initialized and advertising");
  return true;
}

void stop() {
  if (!_initialized) {
    return;
  }

  if (_pAdvertising != nullptr) {
    _pAdvertising->stop();
  }

  NimBLEDevice::deinit(true);

  _pServer = nullptr;
  _pCharacteristic = nullptr;
  _pAdvertising = nullptr;
  _initialized = false;
  _currentState = BLE_STATE_OFF;
  _connectedCount = 0;

  Utils::logMessage("BLE", "BLE stopped");
}

bool isEnabled() { return _initialized; }

bool isClientConnected() { return _connectedCount > 0; }

int getConnectedCount() { return _connectedCount; }

BleState getState() { return _currentState; }

bool sendMetrics(const EnergySensor::EnergyMetrics &metrics) {
  return sendData(metrics.toJson());
}

bool sendData(const String &data) {
  if (!_initialized || _pCharacteristic == nullptr) {
    return false;
  }

  if (_connectedCount == 0) {
    return false;
  }

  _pCharacteristic->setValue(data);
  _pCharacteristic->notify();

  Utils::logMessage("BLE", "Data sent via BLE notification");
  return true;
}

void startAdvertising() {
  if (_initialized && _pAdvertising != nullptr) {
    _pAdvertising->start();
    _currentState = BLE_STATE_ADVERTISING;
    Utils::logMessage("BLE", "Advertising started");
  }
}

void stopAdvertising() {
  if (_initialized && _pAdvertising != nullptr) {
    _pAdvertising->stop();
    if (_connectedCount == 0) {
      _currentState = BLE_STATE_OFF;
    }
    Utils::logMessage("BLE", "Advertising stopped");
  }
}

} // namespace BleManager
