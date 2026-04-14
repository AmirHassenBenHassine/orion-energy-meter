#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include <esp_sleep.h>

namespace ButtonManager {

/**
 *   Boot mode determined by button state
 */
enum BootMode { BOOT_NORMAL = 0, BOOT_WIFI_PAIR, BOOT_HARD_RESET };

/**
 *   Button identifiers
 */
enum ButtonId { BTN_WIFI_PAIRING = 0, BTN_HARD_RESET, BTN_COUNT };

/**
 *   Button event types
 */
enum ButtonEvent {
  BTN_EVENT_NONE = 0,
  BTN_EVENT_PRESSED,
  BTN_EVENT_RELEASED,
  BTN_EVENT_SHORT_PRESS,    // < 1 second
  BTN_EVENT_LONG_PRESS,     // 1-3 seconds
  BTN_EVENT_VERY_LONG_PRESS // > 3 seconds
};

/**
 *   Button state information
 */
struct ButtonState {
  bool isPressed;
  uint32_t pressedTime;   // When button was pressed
  uint32_t releasedTime;  // When button was released
  uint32_t pressDuration; // How long button was held
  ButtonEvent lastEvent;
  uint8_t pressCount; // Number of presses
};

/**
 *   Button event callback type
 */
typedef void (*ButtonEventCallback)(ButtonId button, ButtonEvent event);

/**
 * Immediate action callback - called DURING button press
 * Returns true to consume the event (prevent further processing)
 */
typedef bool (*ImmediateActionCallback)(ButtonId button, uint32_t pressDuration);

/**
 *   Boot mode callback type
 */
typedef void (*BootModeCallback)(BootMode mode);

/**
 *   Initialize button manager
 * @return true if initialization successful
 */
bool begin();

/**
 *   Stop button manager and cleanup
 */
void stop();

/**
 *   Update button states
 */
void update();

/**
 *   Detect boot mode based on button state at startup
 * @return Detected boot mode
 */
BootMode detectBootMode();

/**
 *   Get current boot mode
 * @return Current boot mode
 */
BootMode getBootMode();

/**
 *   Check if a specific button is currently pressed
 * @param button Button identifier
 * @return true if pressed
 */
bool isPressed(ButtonId button);

/**
 *   Get button state information
 * @param button Button identifier
 * @return ButtonState structure
 */
ButtonState getButtonState(ButtonId button);

/**
 *   Check if WiFi pairing button is pressed
 * @return true if pressed
 */
bool isWifiButtonPressed();

/**
 *   Check if hard reset button is pressed
 * @return true if pressed
 */
bool isResetButtonPressed();

/**
 *   Configure wake-up sources for deep/light sleep
 */
void configureWakeupSources();

/**
 *   Get the wake-up cause from sleep
 * @return ESP sleep wake cause
 */
esp_sleep_wakeup_cause_t getWakeupCause();

/**
 *   Get wake-up cause as string
 * @return Human readable wake-up cause
 */
const char *getWakeupCauseString();

/**
 *   Perform hard reset (clear all data and restart)
 */
void performHardReset();

/**
 *   Set callback for button events
 * @param callback Function to call on button events
 */
void setEventCallback(ButtonEventCallback callback);

/**
 *   Set callback for boot mode detection
 * @param callback Function to call when boot mode is detected
 */
void setBootModeCallback(BootModeCallback callback);

/**
 * Set immediate action callback for real-time response
 * This callback is called repeatedly while button is held
 * @param callback Function to call with button and duration
 */
void setImmediateActionCallback(ImmediateActionCallback callback);

/**
 * Check buttons and return any immediate event
 * Call this frequently in main loop for responsive buttons
 * @return Button event if any occurred
 */
ButtonEvent checkButtons();

/**
 *   Check for long press during startup
 * @param button Button to check
 * @param requiredMs Required hold time in milliseconds
 * @return true if button held for required time
 */
bool waitForLongPress(ButtonId button, uint32_t requiredMs);

/**
 *   Get string representation of boot mode
 * @param mode Boot mode
 * @return String name of mode
 */
const char *bootModeToString(BootMode mode);

/**
 *   Get string representation of button event
 * @param event Button event
 * @return String name of event
 */
const char *eventToString(ButtonEvent event);

} // namespace ButtonManager

#endif // BUTTON_MANAGER_H
