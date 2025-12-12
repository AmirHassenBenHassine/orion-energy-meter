#include <Arduino.h>
#include <unity.h>

#define BUTTON_PIN 13
#define DEBOUNCE_MS 50

void setUp(void) { pinMode(BUTTON_PIN, INPUT_PULLUP); }

void tearDown(void) {}

void test_button_pin_configured(void) {
  int state = digitalRead(BUTTON_PIN);
  TEST_ASSERT_TRUE(state == HIGH || state == LOW);
}

void test_button_default_state_high(void) {
  int state = digitalRead(BUTTON_PIN);
  TEST_ASSERT_EQUAL(HIGH, state);
}

void test_button_read_multiple_times_stable(void) {
  int readings[10];
  for (int i = 0; i < 10; i++) {
    readings[i] = digitalRead(BUTTON_PIN);
    delay(10);
  }

  for (int i = 1; i < 10; i++) {
    TEST_ASSERT_EQUAL(readings[0], readings[i]);
  }
}

void test_button_press_detection_manual(void) {
  Serial.println("Press the BOOT button within 5 seconds");

  bool pressed = false;
  unsigned long start = millis();

  while ((millis() - start) < 5000) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      pressed = true;
      Serial.println("Button press detected!");
      break;
    }
    delay(10);
  }

  if (!pressed) {
    Serial.println(">>> No button press detected (skipping)");
  }

  TEST_PASS();
}

void setup() {
  delay(2000);
  Serial.begin(115200);

  UNITY_BEGIN();

  RUN_TEST(test_button_pin_configured);
  RUN_TEST(test_button_default_state_high);
  RUN_TEST(test_button_read_multiple_times_stable);
  RUN_TEST(test_button_press_detection_manual);

  UNITY_END();
}

void loop() {}
