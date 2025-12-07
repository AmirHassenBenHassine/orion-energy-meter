#include "config.h"
#include <Arduino.h>
#include <unity.h>
void setUp(void) {}

void tearDown(void) {}

void test_power_led_can_turn_on(void) {
  pinMode(POWER_LED_PIN, OUTPUT);
  digitalWrite(POWER_LED_PIN, HIGH);
  delay(100);
  TEST_PASS();
}

void test_wifi_led_can_turn_on(void) {
  pinMode(WIFI_LED_PIN, OUTPUT);
  digitalWrite(WIFI_LED_PIN, HIGH);
  delay(100);
  TEST_PASS();
}

void setup() {
  delay(2000);
  Serial.begin(115200);

  UNITY_BEGIN();

  RUN_TEST(test_power_led_can_turn_on);
  RUN_TEST(test_wifi_led_can_turn_on);

  UNITY_END();
}

void loop() {}
