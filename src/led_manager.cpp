#include "led_manager.h"
#include "config.h"
#include "utils.h"

namespace LedManager {

// LED pin mapping
static const uint8_t _ledPins[LED_COUNT] = {POWER_LED_PIN, WIFI_LED_PIN,
                                            CHARGING_LED_PIN, BATTERY_LED_PIN};

// Current LED modes
static volatile LedMode _ledModes[LED_COUNT] = {
    LED_ON,  // Power LED - always on
    LED_OFF, // WiFi LED
    LED_OFF, // Charging LED
    LED_OFF  // Battery LED
};

// Task handles
static TaskHandle_t _taskHandles[LED_COUNT] = {nullptr};

// Task running flags
static volatile bool _tasksRunning = false;

// Private task functions
static void _powerLedTask(void *parameter);
static void _wifiLedTask(void *parameter);
static void _chargingLedTask(void *parameter);
static void _batteryLedTask(void *parameter);

// Generic blink handler
static void _handleBlink(uint8_t pin, LedMode mode, bool &state);

bool begin() {
  if (_tasksRunning) {
    return true;
  }
  
  // Initialize LED pins
  for (int i = 0; i < LED_COUNT; i++) {
    pinMode(_ledPins[i], OUTPUT);
    digitalWrite(_ledPins[i], LOW);
  }
  
  // ✅ Run startup sequence BEFORE starting tasks
  runStartupSequence();
  
  // ✅ Set initial hardware state based on mode
  for (int i = 0; i < LED_COUNT; i++) {
    if (_ledModes[i] == LED_ON) {
      digitalWrite(_ledPins[i], HIGH);
    } else {
      digitalWrite(_ledPins[i], LOW);
    }
  }

  _tasksRunning = true;  // ✅ Tasks start AFTER startup sequence

  // Create LED tasks on Core 1
  BaseType_t result;

  result = xTaskCreatePinnedToCore(
      _powerLedTask, TASK_NAME_LED_POWER, TASK_STACK_SIZE_SMALL, nullptr,
      TASK_PRIORITY_LOW, &_taskHandles[LED_POWER], TASK_CORE_1);
  if (result != pdPASS) {
    Utils::logMessage("LED", "Failed to create Power LED task");
  }

  result = xTaskCreatePinnedToCore(
      _wifiLedTask, TASK_NAME_LED_WIFI, TASK_STACK_SIZE_SMALL, nullptr,
      TASK_PRIORITY_LOW, &_taskHandles[LED_WIFI], TASK_CORE_1);
  if (result != pdPASS) {
    Utils::logMessage("LED", "Failed to create WiFi LED task");
  }

  result = xTaskCreatePinnedToCore(
      _chargingLedTask, TASK_NAME_LED_CHARGING, TASK_STACK_SIZE_SMALL, nullptr,
      TASK_PRIORITY_LOW, &_taskHandles[LED_CHARGING], TASK_CORE_1);
  if (result != pdPASS) {
    Utils::logMessage("LED", "Failed to create Charging LED task");
  }

  result = xTaskCreatePinnedToCore(
      _batteryLedTask, TASK_NAME_LED_BATTERY, TASK_STACK_SIZE_SMALL, nullptr,
      TASK_PRIORITY_LOW, &_taskHandles[LED_BATTERY], TASK_CORE_1);
  if (result != pdPASS) {
    Utils::logMessage("LED", "Failed to create Battery LED task");
  }

  Utils::logMessage("LED", "LED Manager initialized");
  return true;
}

void stop() {
  _tasksRunning = false;

  // Wait for tasks to finish
  Utils::delayTask(100);

  // Delete tasks
  for (int i = 0; i < LED_COUNT; i++) {
    if (_taskHandles[i] != nullptr) {
      vTaskDelete(_taskHandles[i]);
      _taskHandles[i] = nullptr;
    }
  }

  // Turn off all LEDs
  for (int i = 0; i < LED_COUNT; i++) {
    digitalWrite(_ledPins[i], LOW);
  }

  Utils::logMessage("LED", "LED Manager stopped");
}

void setMode(LedType led, LedMode mode) {
  if (led < LED_COUNT) {
    _ledModes[led] = mode;
  }
}

LedMode getMode(LedType led) {
  if (led < LED_COUNT) {
    return _ledModes[led];
  }
  return LED_OFF;
}

void setState(LedType led, bool state) {
  if (led < LED_COUNT) {
    digitalWrite(_ledPins[led], state ? HIGH : LOW);
  }
}

void runStartupSequence() {
  // Sequential LED blink
  for (int i = 0; i < LED_COUNT; i++) {
    digitalWrite(_ledPins[i], HIGH);
    delay(200);
  }
  delay(500);
  for (int i = LED_COUNT - 1; i >= 0; i--) {
    digitalWrite(_ledPins[i], LOW);
    delay(200);
  }
}

void runErrorSequence() {
  // Fast blink all LEDs 5 times
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < LED_COUNT; j++) {
      digitalWrite(_ledPins[j], HIGH);
    }
    delay(100);
    for (int j = 0; j < LED_COUNT; j++) {
      digitalWrite(_ledPins[j], LOW);
    }
    delay(100);
  }
}

void runSuccessSequence() {
  // Two slow blinks
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < LED_COUNT; j++) {
      digitalWrite(_ledPins[j], HIGH);
    }
    delay(300);
    for (int j = 0; j < LED_COUNT; j++) {
      digitalWrite(_ledPins[j], LOW);
    }
    delay(300);
  }
}

void updateBatteryLed(float percentage) {
  if (percentage > BATTERY_LOW_THRESHOLD) {
    setMode(LED_BATTERY, LED_OFF);
  } else if (percentage > BATTERY_CRITICAL_THRESHOLD) {
    setMode(LED_BATTERY, LED_BLINK_SLOW);
  } else {
    setMode(LED_BATTERY, LED_BLINK_FAST);
  }
}

void updateChargingLed(bool isCharging) {
  setMode(LED_CHARGING, isCharging ? LED_ON : LED_OFF);
}

void updateWiFiLed(bool isConnected, bool isAPMode) {
  if (isAPMode) {
    setMode(LED_WIFI, LED_BLINK_AP);
  } else if (isConnected) {
    setMode(LED_WIFI, LED_ON);
  } else {
    setMode(LED_WIFI, LED_OFF);
  }
}

static void _handleBlink(uint8_t pin, LedMode mode, bool &state) {
  uint32_t interval;

  switch (mode) {
  case LED_BLINK_FAST:
    interval = LED_BLINK_FAST_MS;
    break;
  case LED_BLINK_SLOW:
    interval = LED_BLINK_SLOW_MS;
    break;
  case LED_BLINK_AP:
    interval = LED_BLINK_AP_MS;
    break;
  default:
    return;
  }

  state = !state;
  digitalWrite(pin, state ? HIGH : LOW);
  yield();  // ✅ Feed watchdog before delay
  vTaskDelay(pdMS_TO_TICKS(interval));
  yield();  // ✅ Feed watchdog after delay}
}

static void _powerLedTask(void *parameter) {
  // Ensure it's HIGH from the start
  digitalWrite(_ledPins[LED_POWER], HIGH);
  
  while (_tasksRunning) {
    // ✅ Continuously enforce HIGH state
    digitalWrite(_ledPins[LED_POWER], HIGH);
    yield();  // Feed watchdog
    vTaskDelay(pdMS_TO_TICKS(500));  // Check twice per second
  }
  
  digitalWrite(_ledPins[LED_POWER], LOW);
  vTaskDelete(nullptr);
}

static void _wifiLedTask(void *parameter) {
  bool state = false;
  yield();  // ✅ Feed watchdog at start of loop
  while (_tasksRunning) {

    LedMode mode = _ledModes[LED_WIFI];

    switch (mode) {
    case LED_OFF:
      digitalWrite(_ledPins[LED_WIFI], LOW);
      Utils::delayTask(100);
      break;

    case LED_ON:
      digitalWrite(_ledPins[LED_WIFI], HIGH);
      Utils::delayTask(100);
      break;

    case LED_BLINK_FAST:
    case LED_BLINK_SLOW:
    case LED_BLINK_AP:
      _handleBlink(_ledPins[LED_WIFI], mode, state);
      break;
    }
    yield();  // ✅ Feed watchdog at end of loop
  }
  vTaskDelete(nullptr);
}

static void _chargingLedTask(void *parameter) {
  bool lastState = false;
  
  while (_tasksRunning) {
    // ✅ Read charging status directly
    bool isCharging = digitalRead(CHARGING_STATUS_PIN) == HIGH;  // Active HIGH
    
    LedMode mode = _ledModes[LED_CHARGING];
    
    // ✅ Override mode based on actual hardware state
    if (isCharging) {
      digitalWrite(_ledPins[LED_CHARGING], HIGH);
    } else {
      digitalWrite(_ledPins[LED_CHARGING], LOW);
    }
    
    // ✅ Detect state change
    if (isCharging != lastState) {
      Serial.printf("[LED] Charging status changed: %s\n", isCharging ? "ON" : "OFF");
      lastState = isCharging;
    }
    
    yield();
    vTaskDelay(pdMS_TO_TICKS(100));  // ✅ Check every 100ms instead of 500ms
  }

  vTaskDelete(nullptr);
}

static void _batteryLedTask(void *parameter) {
  bool state = false;
  yield();  // ✅ Feed watchdog

  while (_tasksRunning) {
    LedMode mode = _ledModes[LED_BATTERY];

    switch (mode) {
    case LED_OFF:
      digitalWrite(_ledPins[LED_BATTERY], LOW);
      Utils::delayTask(100);
      break;

    case LED_ON:
      digitalWrite(_ledPins[LED_BATTERY], HIGH);
      Utils::delayTask(100);
      break;

    case LED_BLINK_FAST:
    case LED_BLINK_SLOW:
      _handleBlink(_ledPins[LED_BATTERY], mode, state);
      break;

    default:
      Utils::delayTask(100);
      break;
    }
    yield();  // ✅ Feed watchdog

  }

  vTaskDelete(nullptr);
}

} // namespace LedManager
