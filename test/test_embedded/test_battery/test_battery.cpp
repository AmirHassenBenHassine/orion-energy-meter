#include <Arduino.h>
#include <unity.h>

#define BATTERY_PIN 36
#define BATTERY_MIN_VOLTAGE 3.0
#define BATTERY_MAX_VOLTAGE 4.2
#define VOLTAGE_DIVIDER 2.0

void setUp(void) { analogReadResolution(12); }

void tearDown(void) {}

float readBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);
  float voltage = (raw / 4095.0) * 3.3 * VOLTAGE_DIVIDER;
  return voltage;
}

float calculatePercentage(float voltage) {
  if (voltage <= BATTERY_MIN_VOLTAGE)
    return 0.0;
  if (voltage >= BATTERY_MAX_VOLTAGE)
    return 100.0;
  return ((voltage - BATTERY_MIN_VOLTAGE) /
          (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) *
         100.0;
}

void test_battery_pin_readable(void) {
  int reading = analogRead(BATTERY_PIN);
  TEST_ASSERT_GREATER_OR_EQUAL(0, reading);
  TEST_ASSERT_LESS_OR_EQUAL(4095, reading);
}

void test_battery_voltage_in_range(void) {
  float voltage = readBatteryVoltage();
  TEST_ASSERT_GREATER_OR_EQUAL(0.0, voltage);
  TEST_ASSERT_LESS_THAN(5.0, voltage);
}

void test_percentage_at_full(void) {
  float pct = calculatePercentage(4.2);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 100.0, pct);
}

void test_percentage_at_empty(void) {
  float pct = calculatePercentage(3.0);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 0.0, pct);
}

void test_percentage_at_half(void) {
  float pct = calculatePercentage(3.6);
  TEST_ASSERT_FLOAT_WITHIN(1.0, 50.0, pct);
}

void test_percentage_clamps_below_min(void) {
  float pct = calculatePercentage(2.5);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 0.0, pct);
}

void test_percentage_clamps_above_max(void) {
  float pct = calculatePercentage(4.5);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 100.0, pct);
}

void test_battery_readings_stable(void) {
  float readings[10];
  for (int i = 0; i < 10; i++) {
    readings[i] = readBatteryVoltage();
    delay(50);
  }

  float sum = 0;
  for (int i = 0; i < 10; i++)
    sum += readings[i];
  float avg = sum / 10.0;

  for (int i = 0; i < 10; i++) {
    TEST_ASSERT_FLOAT_WITHIN(avg * 0.05 + 0.1, avg, readings[i]);
  }
}

void setup() {
  delay(2000);
  Serial.begin(115200);

  UNITY_BEGIN();

  RUN_TEST(test_battery_pin_readable);
  RUN_TEST(test_battery_voltage_in_range);
  RUN_TEST(test_percentage_at_full);
  RUN_TEST(test_percentage_at_empty);
  RUN_TEST(test_percentage_at_half);
  RUN_TEST(test_percentage_clamps_below_min);
  RUN_TEST(test_percentage_clamps_above_max);
  RUN_TEST(test_battery_readings_stable);

  UNITY_END();
}

void loop() {}
