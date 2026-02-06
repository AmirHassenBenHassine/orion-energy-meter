#include "button_manager.h"
#include "config.h"
#include "led_manager.h"
#include "utils.h"
#include "wifi_manager.h"  // ✅ Add this

#include <Preferences.h>
#include <nvs_flash.h>  // ✅ Add this for nvs_flash_erase()

namespace ButtonManager {

// Button timing constants (milliseconds)
static const uint32_t DEBOUNCE_DELAY_MS = 100;
static const uint32_t SHORT_PRESS_MAX_MS = 1000;
static const uint32_t LONG_PRESS_MIN_MS = 3000;
static const uint32_t LONG_PRESS_MAX_MS = 5000;
static const uint32_t VERY_LONG_PRESS_MIN_MS = 5000;
static const uint32_t DOUBLE_CLICK_WINDOW_MS = 500;
static const uint32_t STARTUP_HOLD_TIME_MS = 3000;

// Button pin mapping
static const uint8_t _buttonPins[BTN_COUNT] = {WIFI_PAIRING_BTN,
                                               HARD_RESET_BTN};

// Button states
static ButtonState _buttonStates[BTN_COUNT];

// Last raw readings for debounce
static bool _lastRawState[BTN_COUNT] = {false, false};
static uint32_t _lastDebounceTime[BTN_COUNT] = {0, 0};

// ✅ Track if action already triggered
static bool _actionTriggered[BTN_COUNT] = {false, false};

// Current boot mode
static BootMode _currentBootMode = BOOT_NORMAL;

// Callbacks
static ButtonEventCallback _eventCallback = nullptr;
static BootModeCallback _bootModeCallback = nullptr;
static ImmediateActionCallback _immediateActionCallback = nullptr; // ✅ NEW

// Initialized flag
static volatile bool _initialized = false;

// Task handle for button monitoring
static TaskHandle_t _buttonTaskHandle = nullptr;
static volatile bool _taskRunning = false;

static void _buttonTask(void *parameter);
static void _processButton(ButtonId button);
static void _triggerEvent(ButtonId button, ButtonEvent event);
static bool _readButtonDebounced(ButtonId button);
static void _resetButtonState(ButtonId button);
static void _checkImmediateActions(ButtonId button); // ✅ NEW

bool begin() {
  if (_initialized) {
    return true;
  }

  // Configure button pins with pull-down resistors
  for (int i = 0; i < BTN_COUNT; i++) {
    pinMode(_buttonPins[i], INPUT_PULLDOWN);
    _resetButtonState((ButtonId)i);
  }

  // Configure wake-up sources
  configureWakeupSources();

  // Start button monitoring task
  _taskRunning = true;
  BaseType_t result = xTaskCreatePinnedToCore(
      _buttonTask, "ButtonTask", TASK_STACK_SIZE_MEDIUM, nullptr,
      TASK_PRIORITY_HIGH, &_buttonTaskHandle, TASK_CORE_1);

  if (result != pdPASS) {
    Utils::logMessage("BUTTON", "Failed to create button task");
    _taskRunning = false;
  }

  _initialized = true;
  Utils::logMessage("BUTTON", "Button Manager initialized");

  return true;
}

void stop() {
  if (!_initialized) {
    return;
  }

  // Stop task
  _taskRunning = false;
  if (_buttonTaskHandle != nullptr) {
    Utils::delayTask(100);
    vTaskDelete(_buttonTaskHandle);
    _buttonTaskHandle = nullptr;
  }

  _initialized = false;
  Utils::logMessage("BUTTON", "Button Manager stopped");
}

void update() {
  if (!_initialized) {
    return;
  }

  for (int i = 0; i < BTN_COUNT; i++) {
    _processButton((ButtonId)i);
  }
}

// ✅ NEW: Check buttons and return event if threshold reached
ButtonEvent checkButtons() {
  if (!_initialized) {
    return BTN_EVENT_NONE;
  }

  // Check each button
  for (int i = 0; i < BTN_COUNT; i++) {
    ButtonId button = (ButtonId)i;
    ButtonState *state = &_buttonStates[button];
    
    // If button is pressed, check duration
    if (state->isPressed && !_actionTriggered[button]) {
      uint32_t duration = millis() - state->pressedTime;
      
      // ✅ WiFi Pairing Button - LONG PRESS (3 seconds)
      if (button == BTN_WIFI_PAIRING && duration >= LONG_PRESS_MIN_MS && duration < VERY_LONG_PRESS_MIN_MS) {
        _actionTriggered[button] = true;
        Utils::logMessage("BUTTON", "⚡ WiFi Pairing - LONG PRESS detected!");
        LedManager::runSuccessSequence();
        return BTN_EVENT_LONG_PRESS;
      }
      
      // ✅ Hard Reset Button - VERY LONG PRESS (5 seconds)
      if (button == BTN_HARD_RESET && duration >= VERY_LONG_PRESS_MIN_MS) {
        _actionTriggered[button] = true;
        Utils::logMessage("BUTTON", "⚡ Hard Reset - VERY LONG PRESS detected!");
        LedManager::runErrorSequence();
        return BTN_EVENT_VERY_LONG_PRESS;
      }
    }
  }
  
  return BTN_EVENT_NONE;
}

void setImmediateActionCallback(ImmediateActionCallback callback) {
  _immediateActionCallback = callback;
}

BootMode detectBootMode() {
  // Check wake-up cause first
  esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();

  Utils::logMessageF("BUTTON", "Wake-up cause: %s", getWakeupCauseString());

  // Debounce delay for reliable reading
  delay(DEBOUNCE_DELAY_MS * 2);

  // Read button states multiple times for reliability
  int wifiPressCount = 0;
  int resetPressCount = 0;

  for (int i = 0; i < 5; i++) {
    if (digitalRead(WIFI_PAIRING_BTN) == HIGH)
      wifiPressCount++;
    if (digitalRead(HARD_RESET_BTN) == HIGH)
      resetPressCount++;
    delay(10);
  }

  bool wifiPressed = wifiPressCount >= 3;
  bool resetPressed = resetPressCount >= 3;

  // Determine boot mode
  _currentBootMode = BOOT_NORMAL;

  // Check wake-up source first
  if (wakeupCause == ESP_SLEEP_WAKEUP_EXT0) {
    // EXT0 is configured for hard reset button
    _currentBootMode = BOOT_HARD_RESET;
  } else if (wakeupCause == ESP_SLEEP_WAKEUP_EXT1) {
    // EXT1 is configured for WiFi pairing button
    _currentBootMode = BOOT_WIFI_PAIR;
  }
  // Check current button state (for power-on boot)
  else if (resetPressed) {
    // Check for long press to confirm hard reset
    if (waitForLongPress(BTN_HARD_RESET, STARTUP_HOLD_TIME_MS)) {
      _currentBootMode = BOOT_HARD_RESET;
    }
  } else if (wifiPressed) {
    // Check for long press to confirm WiFi pairing
    if (waitForLongPress(BTN_WIFI_PAIRING, STARTUP_HOLD_TIME_MS)) {
      _currentBootMode = BOOT_WIFI_PAIR;
    }
  }

  Utils::logMessageF("BUTTON", "Boot mode detected: %s",
                     bootModeToString(_currentBootMode));

  // Call boot mode callback if registered
  if (_bootModeCallback != nullptr) {
    _bootModeCallback(_currentBootMode);
  }

  return _currentBootMode;
}

BootMode getBootMode() { return _currentBootMode; }

bool isPressed(ButtonId button) {
  if (button >= BTN_COUNT) {
    return false;
  }
  return _buttonStates[button].isPressed;
}

ButtonState getButtonState(ButtonId button) {
  if (button >= BTN_COUNT) {
    ButtonState empty = {0};
    return empty;
  }
  return _buttonStates[button];
}

bool isWifiButtonPressed() { return isPressed(BTN_WIFI_PAIRING); }

bool isResetButtonPressed() { return isPressed(BTN_HARD_RESET); }

void configureWakeupSources() {
  // EXT0: Single GPIO wake-up for Hard Reset button
  // Wake on HIGH level
  esp_err_t err0 = esp_sleep_enable_ext0_wakeup((gpio_num_t)HARD_RESET_BTN, 1);
  if (err0 != ESP_OK) {
    Utils::logMessageF("BUTTON", "EXT0 config failed: %d", err0);
  }

  // EXT1: Multiple GPIO wake-up for WiFi Pairing button
  // Wake when any specified GPIO goes HIGH
  uint64_t ext1Mask = (1ULL << WIFI_PAIRING_BTN);
  esp_err_t err1 =
      esp_sleep_enable_ext1_wakeup(ext1Mask, ESP_EXT1_WAKEUP_ANY_HIGH);
  if (err1 != ESP_OK) {
    Utils::logMessageF("BUTTON", "EXT1 config failed: %d", err1);
  }

  Utils::logMessage("BUTTON", "Wake-up sources configured");
}

esp_sleep_wakeup_cause_t getWakeupCause() {
  return esp_sleep_get_wakeup_cause();
}

const char *getWakeupCauseString() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  switch (cause) {
  case ESP_SLEEP_WAKEUP_UNDEFINED:
    return "Power-on/Reset";
  case ESP_SLEEP_WAKEUP_ALL:
    return "All";
  case ESP_SLEEP_WAKEUP_EXT0:
    return "EXT0 (Reset Button)";
  case ESP_SLEEP_WAKEUP_EXT1:
    return "EXT1 (WiFi Button)";
  case ESP_SLEEP_WAKEUP_TIMER:
    return "Timer";
  case ESP_SLEEP_WAKEUP_ULP:
    return "ULP";
  case ESP_SLEEP_WAKEUP_GPIO:
    return "GPIO";
  case ESP_SLEEP_WAKEUP_UART:
    return "UART";
  default:
    return "Unknown";
  }
}

void performHardReset() {
  Utils::logMessage("BUTTON", "   PERFORMING HARD RESET");

   // Visual indication - rapid blink all LEDs
  for (int cycle = 0; cycle < 10; cycle++) {
    LedManager::setState(LedManager::LED_WIFI, true);
    LedManager::setState(LedManager::LED_CHARGING, true);
    LedManager::setState(LedManager::LED_BATTERY, true);
    delay(100);
    LedManager::setState(LedManager::LED_WIFI, false);
    LedManager::setState(LedManager::LED_CHARGING, false);
    LedManager::setState(LedManager::LED_BATTERY, false);
    delay(100);
  }
  
  // ✅ Also use WifiManager to clear
  Utils::logMessage("BUTTON", "Clearing WiFi via WifiManager...");
  WifiManager::resetCredentials();
  delay(100);

  // Clear all preferences namespaces
  const char *namespaces[] = {
    PREF_NAMESPACE_WIFI,
    PREF_NAMESPACE_MQTT, 
    PREF_NAMESPACE_OTA,
    PREF_NAMESPACE_DEVICE,
    "wifi",
    "nvs.net80211",  // ✅ ESP32 WiFi storage
    "nvs"
  };

  Preferences prefs;
  for (const char *ns : namespaces) {
    if (strlen(ns) > 0 && strcmp(ns, "nvs.net80211") != 0) {
      if (prefs.begin(ns, false)) {
        prefs.clear();
        prefs.end();
        Utils::logMessageF("BUTTON", "Cleared namespace: %s", ns);
      }
    }
  }

  Utils::logMessage("BUTTON", "All data erased. Restarting...");
  delay(1000);
  ESP.restart();
}

void setEventCallback(ButtonEventCallback callback) {
  _eventCallback = callback;
}

void setBootModeCallback(BootModeCallback callback) {
  _bootModeCallback = callback;
}

bool waitForLongPress(ButtonId button, uint32_t requiredMs) {
  if (button >= BTN_COUNT) {
    return false;
  }

  uint8_t pin = _buttonPins[button];
  uint32_t startTime = millis();

  Utils::logMessageF("BUTTON", "Waiting for %dms hold on %s button...",
                     requiredMs, button == BTN_WIFI_PAIRING ? "WiFi" : "Reset");

  // Visual feedback - blink while waiting
  bool ledState = false;
  uint32_t lastBlink = 0;

  while (digitalRead(pin) == HIGH) {
    uint32_t elapsed = millis() - startTime;

    // Blink LED faster as time progresses
    uint32_t blinkInterval = 500 - (elapsed * 400 / requiredMs);
    blinkInterval = max(blinkInterval, (uint32_t)100);

    if (millis() - lastBlink > blinkInterval) {
      ledState = !ledState;
      LedManager::setState(LedManager::LED_WIFI, ledState);
      lastBlink = millis();
    }

    if (elapsed >= requiredMs) {
      // Success - solid LED
      LedManager::setState(LedManager::LED_WIFI, true);
      Utils::logMessage("BUTTON", "Long press confirmed!");
      delay(500);
      return true;
    }

    delay(10);
  }

  // Button released too early
  LedManager::setState(LedManager::LED_WIFI, false);
  Utils::logMessage("BUTTON", "Button released before threshold");
  return false;
}

const char *bootModeToString(BootMode mode) {
  switch (mode) {
  case BOOT_NORMAL:
    return "Normal";
  case BOOT_WIFI_PAIR:
    return "WiFi Pairing";
  case BOOT_HARD_RESET:
    return "Hard Reset";
  default:
    return "Unknown";
  }
}

const char *eventToString(ButtonEvent event) {
  switch (event) {
  case BTN_EVENT_NONE:
    return "None";
  case BTN_EVENT_PRESSED:
    return "Pressed";
  case BTN_EVENT_RELEASED:
    return "Released";
  case BTN_EVENT_SHORT_PRESS:
    return "Short Press";
  case BTN_EVENT_LONG_PRESS:
    return "Long Press";
  case BTN_EVENT_VERY_LONG_PRESS:
    return "Very Long Press";
  default:
    return "Unknown";
  }
}

static void _buttonTask(void *parameter) {
  while (_taskRunning) {
    update();
    yield();  // ✅ Feed watchdog
    Utils::delayTask(20);
  }

  vTaskDelete(nullptr);
}

static void _processButton(ButtonId button) {
  bool currentState = _readButtonDebounced(button);
  ButtonState *state = &_buttonStates[button];
  uint32_t now = millis();

  // Button just pressed
  if (currentState && !state->isPressed) {
    state->isPressed = true;
    state->pressedTime = now;
    state->pressDuration = 0;
    _actionTriggered[button] = false;  // ✅ Reset action flag
    _triggerEvent(button, BTN_EVENT_PRESSED);
  }
  // // Button being held - check thresholds
  // else if (currentState && state->isPressed) {
  //   state->pressDuration = now - state->pressedTime;
    
  //   // ✅ Check for WiFi pairing (3 seconds)
  //   if (button == BTN_WIFI_PAIRING && 
  //       state->pressDuration >= LONG_PRESS_MIN_MS && 
  //       state->pressDuration < VERY_LONG_PRESS_MIN_MS &&
  //       !_actionTriggered[button]) {
      
  //     _actionTriggered[button] = true;
  //     Utils::logMessage("BUTTON", "⚡ WiFi Pairing triggered!");
  //     LedManager::runSuccessSequence();
  //     WifiManager::startPortal();

  //     // ✅ Call immediate callback
  //     if (_immediateActionCallback != nullptr) {
  //       _immediateActionCallback(button, BTN_EVENT_LONG_PRESS);
  //     }
  //   }
    
  //   // ✅ Check for hard reset (5 seconds)
  //   if (button == BTN_HARD_RESET && 
  //       state->pressDuration >= VERY_LONG_PRESS_MIN_MS &&
  //       !_actionTriggered[button]) {
      
  //     _actionTriggered[button] = true;
  //     Utils::logMessage("BUTTON", "⚡ Hard Reset triggered!");
  //     LedManager::runErrorSequence();
  //     performHardReset();
  //     // ✅ Call immediate callback
  //     if (_immediateActionCallback != nullptr) {
  //       _immediateActionCallback(button, BTN_EVENT_VERY_LONG_PRESS);
  //     }
  //   }
  // }  
  // // Button just released
  else if (!currentState && state->isPressed) {
    state->isPressed = false;
    state->releasedTime = now;
    state->pressDuration = now - state->pressedTime;
    _triggerEvent(button, BTN_EVENT_RELEASED);

    // Determine press type
    if (state->pressDuration < SHORT_PRESS_MAX_MS) {
      _triggerEvent(button, BTN_EVENT_SHORT_PRESS);
      if (now - state->releasedTime < DOUBLE_CLICK_WINDOW_MS) {
        state->pressCount++;
      } else {
        state->pressCount = 1;
      }
    } else if (state->pressDuration < VERY_LONG_PRESS_MIN_MS) {
      _triggerEvent(button, BTN_EVENT_LONG_PRESS);
      state->pressCount = 0;
    } else {
      _triggerEvent(button, BTN_EVENT_VERY_LONG_PRESS);
      state->pressCount = 0;
    }
    
    _actionTriggered[button] = false;  // ✅ Reset here when released
  }
  // Button being held
  else if (currentState && state->isPressed) {
    state->pressDuration = now - state->pressedTime;
    _checkImmediateActions(button);  // ✅ Check thresholds while held
  }
}

// ✅ NEW: Check for immediate action thresholds
static void _checkImmediateActions(ButtonId button) {
  ButtonState *state = &_buttonStates[button];
  uint32_t duration = state->pressDuration;
  
  // Only trigger once per press
  if (_actionTriggered[button]) {
    return;
  }
  
  // Call immediate action callback if registered
  if (_immediateActionCallback != nullptr) {
    bool consumed = _immediateActionCallback(button, duration);
    if (consumed) {
      _actionTriggered[button] = true;
    }
  }
}

static void _triggerEvent(ButtonId button, ButtonEvent event) {
  _buttonStates[button].lastEvent = event;

  // Log significant events
  if (event != BTN_EVENT_NONE) {
    Utils::logMessageF("BUTTON", "%s: %s",
                       button == BTN_WIFI_PAIRING ? "WiFi" : "Reset",
                       eventToString(event));
  }

  // Call user callback
  if (_eventCallback != nullptr) {
    _eventCallback(button, event);
  }
}

static bool _readButtonDebounced(ButtonId button) {
  if (button >= BTN_COUNT) {
    return false;
  }

  bool rawState = digitalRead(_buttonPins[button]) == HIGH;
  uint32_t now = millis();

  // If state changed, reset debounce timer
  if (rawState != _lastRawState[button]) {
    _lastDebounceTime[button] = now;
    _lastRawState[button] = rawState;
  }

  // ✅ NEW: Require multiple consecutive stable readings
  static uint8_t stableCount[BTN_COUNT] = {0, 0};

  if ((now - _lastDebounceTime[button]) > DEBOUNCE_DELAY_MS) {
    // Check if state has been stable
    if (rawState == _buttonStates[button].isPressed) {
      stableCount[button] = 0;
      return rawState;
    }
    
    // Count consecutive stable readings
    stableCount[button]++;
    if (stableCount[button] >= 3) {  // Require 3 stable readings
      stableCount[button] = 0;
      return rawState;
    }
  }

  return _buttonStates[button].isPressed;
}

static void _resetButtonState(ButtonId button) {
  if (button >= BTN_COUNT) {
    return;
  }

  _buttonStates[button].isPressed = false;
  _buttonStates[button].pressedTime = 0;
  _buttonStates[button].releasedTime = 0;
  _buttonStates[button].pressDuration = 0;
  _buttonStates[button].lastEvent = BTN_EVENT_NONE;
  _buttonStates[button].pressCount = 0;
}

} // namespace ButtonManager
