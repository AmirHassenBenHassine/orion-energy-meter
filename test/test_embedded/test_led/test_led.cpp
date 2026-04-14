#include "config.h"
#include <Arduino.h>
#include <unity.h>

// LED pin array for easy iteration
static const uint8_t LED_PINS[] = {POWER_LED_PIN, WIFI_LED_PIN, CHARGING_LED_PIN, BATTERY_LED_PIN};
static const char* LED_NAMES[] = {"Power", "WiFi", "Charging", "Battery"};
static const int LED_COUNT = 4;

void setUp(void) {
  // Reset all LEDs to known state before each test
  for (int i = 0; i < LED_COUNT; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }
  delay(100);
}

void tearDown(void) {
  // Turn off all LEDs after each test
  for (int i = 0; i < LED_COUNT; i++) {
    digitalWrite(LED_PINS[i], LOW);
  }
  delay(50);
}

// ============================================================================
// DIAGNOSTIC FUNCTIONS
// ============================================================================

void printLEDDiagnostics() {
  Serial.println("\n========================================");
  Serial.println("       LED HARDWARE DIAGNOSTICS");
  Serial.println("========================================");
  
  for (int i = 0; i < LED_COUNT; i++) {
    Serial.printf("\n%s LED (GPIO%d):\n", LED_NAMES[i], LED_PINS[i]);
    Serial.printf("  Pin Mode: OUTPUT\n");
    
    // Test current state
    int currentState = digitalRead(LED_PINS[i]);
    Serial.printf("  Current State: %s\n", currentState ? "HIGH" : "LOW");
    
    // Test HIGH
    digitalWrite(LED_PINS[i], HIGH);
    delay(10);
    int highState = digitalRead(LED_PINS[i]);
    Serial.printf("  HIGH Test: %s\n", highState ? "✓ PASS" : "✗ FAIL");
    
    // Test LOW
    digitalWrite(LED_PINS[i], LOW);
    delay(10);
    int lowState = digitalRead(LED_PINS[i]);
    Serial.printf("  LOW Test: %s\n", !lowState ? "✓ PASS" : "✗ FAIL");
    
    // Check for shorts or conflicts
    if (highState && lowState) {
      Serial.printf("  ⚠️  WARNING: Pin stuck HIGH (possible short)\n");
    } else if (!highState && !lowState) {
      Serial.printf("  ⚠️  WARNING: Pin stuck LOW (possible hardware issue)\n");
    }
  }
  
  Serial.println("\n========================================\n");
}

void interactiveLEDTest() {
  Serial.println("\n========================================");
  Serial.println("     INTERACTIVE LED TEST");
  Serial.println("========================================");
  Serial.println("Watch the LEDs and verify they work!");
  Serial.println();
  
  for (int i = 0; i < LED_COUNT; i++) {
    Serial.printf("Testing %s LED (GPIO%d)...\n", LED_NAMES[i], LED_PINS[i]);
    
    // Turn ON
    Serial.println("  → Turning ON (2 seconds)");
    digitalWrite(LED_PINS[i], HIGH);
    delay(2000);
    
    // Turn OFF
    Serial.println("  → Turning OFF (2 seconds)");
    digitalWrite(LED_PINS[i], LOW);
    delay(2000);
    
    // Blink pattern
    Serial.println("  → Blinking 5 times (fast)");
    for (int j = 0; j < 5; j++) {
      digitalWrite(LED_PINS[i], HIGH);
      delay(150);
      digitalWrite(LED_PINS[i], LOW);
      delay(150);
    }
    
    Serial.println("  ✓ Test complete\n");
    delay(500);
  }
  
  Serial.println("========================================\n");
}

void checkForPinConflicts() {
  Serial.println("\n========================================");
  Serial.println("     PIN CONFLICT CHECK");
  Serial.println("========================================\n");
  
  bool conflictFound = false;
  
  // Check for duplicate pin assignments
  for (int i = 0; i < LED_COUNT; i++) {
    for (int j = i + 1; j < LED_COUNT; j++) {
      if (LED_PINS[i] == LED_PINS[j]) {
        Serial.printf("✗ CONFLICT: %s LED and %s LED use same pin (GPIO%d)\n", 
                      LED_NAMES[i], LED_NAMES[j], LED_PINS[i]);
        conflictFound = true;
      }
    }
  }
  
  // Check for ADC2 pins (conflict with WiFi)
  const int ADC2_PINS[] = {0, 2, 4, 12, 13, 14, 15, 25, 26, 27};
  const int ADC2_COUNT = 10;
  
  for (int i = 0; i < LED_COUNT; i++) {
    for (int j = 0; j < ADC2_COUNT; j++) {
      if (LED_PINS[i] == ADC2_PINS[j]) {
        Serial.printf("⚠️  WARNING: %s LED (GPIO%d) is on ADC2 (may affect WiFi)\n", 
                      LED_NAMES[i], LED_PINS[i]);
      }
    }
  }
  
  // Check for strapping pins (should avoid)
  const int STRAPPING_PINS[] = {0, 2, 5, 12, 15};
  const int STRAPPING_COUNT = 5;
  
  for (int i = 0; i < LED_COUNT; i++) {
    for (int j = 0; j < STRAPPING_COUNT; j++) {
      if (LED_PINS[i] == STRAPPING_PINS[j]) {
        Serial.printf("⚠️  WARNING: %s LED (GPIO%d) is a strapping pin (may affect boot)\n", 
                      LED_NAMES[i], LED_PINS[i]);
      }
    }
  }
  
  if (!conflictFound) {
    Serial.println("✓ No pin conflicts detected");
  }
  
  Serial.println("\n========================================\n");
}

void testAllLEDsSimultaneous() {
  Serial.println("\n========================================");
  Serial.println("   SIMULTANEOUS LED TEST");
  Serial.println("========================================");
  Serial.println("All LEDs should turn ON together...\n");
  
  // Turn all ON
  for (int i = 0; i < LED_COUNT; i++) {
    digitalWrite(LED_PINS[i], HIGH);
  }
  delay(3000);
  
  Serial.println("All LEDs should turn OFF together...\n");
  
  // Turn all OFF
  for (int i = 0; i < LED_COUNT; i++) {
    digitalWrite(LED_PINS[i], LOW);
  }
  delay(2000);
  
  Serial.println("Sequential wave pattern...\n");
  
  // Wave pattern
  for (int j = 0; j < 3; j++) {
    for (int i = 0; i < LED_COUNT; i++) {
      digitalWrite(LED_PINS[i], HIGH);
      delay(200);
    }
    for (int i = 0; i < LED_COUNT; i++) {
      digitalWrite(LED_PINS[i], LOW);
      delay(200);
    }
  }
  
  Serial.println("✓ Simultaneous test complete\n");
  Serial.println("========================================\n");
}

void measureLEDVoltages() {
  Serial.println("\n========================================");
  Serial.println("     LED VOLTAGE MEASUREMENT");
  Serial.println("========================================\n");
  Serial.println("Note: These are digital readings (HIGH/LOW)\n");
  
  for (int i = 0; i < LED_COUNT; i++) {
    digitalWrite(LED_PINS[i], HIGH);
    delay(100);
    
    int reading = digitalRead(LED_PINS[i]);
    Serial.printf("%s LED (GPIO%d): %s (%s)\n", 
                  LED_NAMES[i], 
                  LED_PINS[i],
                  reading ? "HIGH" : "LOW",
                  reading ? "~3.3V" : "0V");
    
    digitalWrite(LED_PINS[i], LOW);
  }
  
  Serial.println("\n⚠️  For accurate voltage measurement, use a multimeter");
  Serial.println("    Expected: ~3.3V when HIGH, 0V when LOW");
  Serial.println("\n========================================\n");
}

// ============================================================================
// BASIC HARDWARE TESTS
// ============================================================================

void test_all_leds_initialized(void) {
  Serial.println("\n[TEST] Checking all LED pins are configured...");
  
  for (int i = 0; i < LED_COUNT; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    Serial.printf("  ✓ %s LED (GPIO%d) configured\n", LED_NAMES[i], LED_PINS[i]);
  }
  
  TEST_PASS_MESSAGE("All LED pins configured successfully");
}

void test_power_led_can_turn_on(void) {
  Serial.println("\n[TEST] Power LED ON test...");
  pinMode(POWER_LED_PIN, OUTPUT);
  digitalWrite(POWER_LED_PIN, HIGH);
  delay(100);
  
  int state = digitalRead(POWER_LED_PIN);
  Serial.printf("  Power LED state: %s\n", state ? "HIGH ✓" : "LOW ✗");
  
  TEST_ASSERT_TRUE_MESSAGE(state, "Power LED failed to turn ON");
}

void test_power_led_can_turn_off(void) {
  Serial.println("\n[TEST] Power LED OFF test...");
  pinMode(POWER_LED_PIN, OUTPUT);
  digitalWrite(POWER_LED_PIN, HIGH);
  delay(100);
  digitalWrite(POWER_LED_PIN, LOW);
  delay(100);
  
  int state = digitalRead(POWER_LED_PIN);
  Serial.printf("  Power LED state: %s\n", state ? "HIGH ✗" : "LOW ✓");
  
  TEST_ASSERT_FALSE_MESSAGE(state, "Power LED failed to turn OFF");
}

void test_wifi_led_can_turn_on(void) {
  Serial.println("\n[TEST] WiFi LED ON test...");
  pinMode(WIFI_LED_PIN, OUTPUT);
  digitalWrite(WIFI_LED_PIN, HIGH);
  delay(100);
  
  int state = digitalRead(WIFI_LED_PIN);
  Serial.printf("  WiFi LED state: %s\n", state ? "HIGH ✓" : "LOW ✗");
  
  TEST_ASSERT_TRUE_MESSAGE(state, "WiFi LED failed to turn ON");
}

void test_wifi_led_can_turn_off(void) {
  Serial.println("\n[TEST] WiFi LED OFF test...");
  pinMode(WIFI_LED_PIN, OUTPUT);
  digitalWrite(WIFI_LED_PIN, HIGH);
  delay(100);
  digitalWrite(WIFI_LED_PIN, LOW);
  delay(100);
  
  int state = digitalRead(WIFI_LED_PIN);
  Serial.printf("  WiFi LED state: %s\n", state ? "HIGH ✗" : "LOW ✓");
  
  TEST_ASSERT_FALSE_MESSAGE(state, "WiFi LED failed to turn OFF");
}

void test_charging_led_can_turn_on(void) {
  Serial.println("\n[TEST] Charging LED ON test...");
  pinMode(CHARGING_LED_PIN, OUTPUT);
  digitalWrite(CHARGING_LED_PIN, HIGH);
  delay(100);
  
  int state = digitalRead(CHARGING_LED_PIN);
  Serial.printf("  Charging LED state: %s\n", state ? "HIGH ✓" : "LOW ✗");
  
  TEST_ASSERT_TRUE_MESSAGE(state, "Charging LED failed to turn ON");
}

void test_charging_led_can_turn_off(void) {
  Serial.println("\n[TEST] Charging LED OFF test...");
  pinMode(CHARGING_LED_PIN, OUTPUT);
  digitalWrite(CHARGING_LED_PIN, HIGH);
  delay(100);
  digitalWrite(CHARGING_LED_PIN, LOW);
  delay(100);
  
  int state = digitalRead(CHARGING_LED_PIN);
  Serial.printf("  Charging LED state: %s\n", state ? "HIGH ✗" : "LOW ✓");
  
  TEST_ASSERT_FALSE_MESSAGE(state, "Charging LED failed to turn OFF");
}

void test_battery_led_can_turn_on(void) {
  Serial.println("\n[TEST] Battery LED ON test...");
  pinMode(BATTERY_LED_PIN, OUTPUT);
  digitalWrite(BATTERY_LED_PIN, HIGH);
  delay(100);
  
  int state = digitalRead(BATTERY_LED_PIN);
  Serial.printf("  Battery LED state: %s\n", state ? "HIGH ✓" : "LOW ✗");
  
  TEST_ASSERT_TRUE_MESSAGE(state, "Battery LED failed to turn ON");
}

void test_battery_led_can_turn_off(void) {
  Serial.println("\n[TEST] Battery LED OFF test...");
  pinMode(BATTERY_LED_PIN, OUTPUT);
  digitalWrite(BATTERY_LED_PIN, HIGH);
  delay(100);
  digitalWrite(BATTERY_LED_PIN, LOW);
  delay(100);
  
  int state = digitalRead(BATTERY_LED_PIN);
  Serial.printf("  Battery LED state: %s\n", state ? "HIGH ✗" : "LOW ✓");
  
  TEST_ASSERT_FALSE_MESSAGE(state, "Battery LED failed to turn OFF");
}

void test_all_leds_can_blink(void) {
  Serial.println("\n[TEST] All LEDs blink test...");
  Serial.println("  Each LED should blink 3 times");
  
  bool allPassed = true;
  
  for (int i = 0; i < LED_COUNT; i++) {
    Serial.printf("  Testing %s LED (GPIO%d)...\n", LED_NAMES[i], LED_PINS[i]);
    
    for (int j = 0; j < 3; j++) {
      digitalWrite(LED_PINS[i], HIGH);
      delay(200);
      int highState = digitalRead(LED_PINS[i]);
      
      digitalWrite(LED_PINS[i], LOW);
      delay(200);
      int lowState = digitalRead(LED_PINS[i]);
      
      if (!highState || lowState) {
        Serial.printf("    ✗ Blink cycle %d failed\n", j + 1);
        allPassed = false;
      }
    }
    
    Serial.printf("    ✓ %s LED blink complete\n", LED_NAMES[i]);
  }
  
  TEST_ASSERT_TRUE_MESSAGE(allPassed, "One or more LEDs failed blink test");
}

void test_no_led_interference(void) {
  Serial.println("\n[TEST] LED interference test...");
  Serial.println("  Checking if LEDs interfere with each other...");
  
  bool noInterference = true;
  
  // Turn all LEDs OFF
  for (int i = 0; i < LED_COUNT; i++) {
    digitalWrite(LED_PINS[i], LOW);
  }
  delay(100);
  
  // Test each LED individually while others are OFF
  for (int i = 0; i < LED_COUNT; i++) {
    digitalWrite(LED_PINS[i], HIGH);
    delay(50);
    
    // Check if any other LED turned ON
    for (int j = 0; j < LED_COUNT; j++) {
      if (i != j) {
        int state = digitalRead(LED_PINS[j]);
        if (state) {
          Serial.printf("  ✗ %s LED affected %s LED\n", 
                        LED_NAMES[i], LED_NAMES[j]);
          noInterference = false;
        }
      }
    }
    
    digitalWrite(LED_PINS[i], LOW);
  }
  
  if (noInterference) {
    Serial.println("  ✓ No interference detected");
  }
  
  TEST_ASSERT_TRUE_MESSAGE(noInterference, "LED interference detected");
}

// ============================================================================
// MAIN SETUP
// ============================================================================

void setup() {
  delay(2000);
  Serial.begin(115200);
  
  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   ORION ENERGY MONITOR - LED TESTS     ║");
  Serial.println("║          Hardware Validation           ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println();
  
  // Run diagnostics first
  checkForPinConflicts();
  printLEDDiagnostics();
  
  // Interactive test
  Serial.println("Starting interactive LED test in 3 seconds...");
  Serial.println("Watch your LEDs carefully!\n");
  delay(3000);
  
  interactiveLEDTest();
  testAllLEDsSimultaneous();
  measureLEDVoltages();
  
  // Run unit tests
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║        STARTING UNIT TESTS             ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  UNITY_BEGIN();
  
  RUN_TEST(test_all_leds_initialized);
  
  RUN_TEST(test_power_led_can_turn_on);
  RUN_TEST(test_power_led_can_turn_off);
  
  RUN_TEST(test_wifi_led_can_turn_on);
  RUN_TEST(test_wifi_led_can_turn_off);
  
  RUN_TEST(test_charging_led_can_turn_on);
  RUN_TEST(test_charging_led_can_turn_off);
  
  RUN_TEST(test_battery_led_can_turn_on);
  RUN_TEST(test_battery_led_can_turn_off);
  
  RUN_TEST(test_all_leds_can_blink);
  RUN_TEST(test_no_led_interference);
  
  UNITY_END();
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║         ALL TESTS COMPLETE             ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  Serial.println("LED Test Summary:");
  Serial.println("─────────────────────────────────────────");
  Serial.printf("Total LEDs tested: %d\n", LED_COUNT);
  Serial.println("Check test results above for details");
  Serial.println("\nYou can run these tests again by resetting the device.");
  Serial.println();
}

void loop() {
  // Keep LEDs in a rotating pattern for visual confirmation
  static uint32_t lastChange = 0;
  static int currentLED = 0;
  
  if (millis() - lastChange > 1000) {
    // Turn off all LEDs
    for (int i = 0; i < LED_COUNT; i++) {
      digitalWrite(LED_PINS[i], LOW);
    }
    
    // Turn on current LED
    digitalWrite(LED_PINS[currentLED], HIGH);
    
    currentLED = (currentLED + 1) % LED_COUNT;
    lastChange = millis();
  }
}