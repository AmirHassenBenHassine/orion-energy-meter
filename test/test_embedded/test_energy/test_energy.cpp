#include <Arduino.h>
#include <unity.h>

#define VOLTAGE_PIN 32
#define CURRENT_PIN_R 39
#define CURRENT_PIN_Y 34
#define CURRENT_PIN_B 35

#define VOLTAGE_CALIBRATION 120.1
#define CURRENT_CALIBRATION 30.0

void setUp(void) {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
}

void tearDown(void) {}

// ADC TESTS

void test_adc_reads_valid_range(void) {
  int reading = analogRead(VOLTAGE_PIN);
  TEST_ASSERT_GREATER_OR_EQUAL(0, reading);
  TEST_ASSERT_LESS_OR_EQUAL(4095, reading);
}

void test_all_current_pins_readable(void) {
  int r = analogRead(CURRENT_PIN_R);
  int y = analogRead(CURRENT_PIN_Y);
  int b = analogRead(CURRENT_PIN_B);

  TEST_ASSERT_GREATER_OR_EQUAL(0, r);
  TEST_ASSERT_GREATER_OR_EQUAL(0, y);
  TEST_ASSERT_GREATER_OR_EQUAL(0, b);

  TEST_ASSERT_LESS_OR_EQUAL(4095, r);
  TEST_ASSERT_LESS_OR_EQUAL(4095, y);
  TEST_ASSERT_LESS_OR_EQUAL(4095, b);
}

void test_adc_readings_stable(void) {
  const int NUM_READINGS = 20;
  int readings[NUM_READINGS];

  for (int i = 0; i < NUM_READINGS; i++) {
    readings[i] = analogRead(VOLTAGE_PIN);
    delay(10);
  }

  // Calculate average
  long sum = 0;
  for (int i = 0; i < NUM_READINGS; i++) {
    sum += readings[i];
  }
  int avg = sum / NUM_READINGS;

  // Check variance is reasonable (< 10% of average)
  int maxDiff = 0;
  for (int i = 0; i < NUM_READINGS; i++) {
    int diff = abs(readings[i] - avg);
    if (diff > maxDiff)
      maxDiff = diff;
  }

  // Allow some noise but not too much
  TEST_ASSERT_LESS_THAN(avg / 5 + 50, maxDiff);
}

// VOLTAGE CALCULATION TESTS

float calculateVoltage(int adcValue) {
  float voltage = (adcValue / 4095.0) * 3.3 * VOLTAGE_CALIBRATION;
  return voltage;
}

void test_voltage_calculation_zero(void) {
  float v = calculateVoltage(0);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 0.0, v);
}

void test_voltage_calculation_mid(void) {
  float v = calculateVoltage(2048); // Mid-scale
  TEST_ASSERT_GREATER_THAN(100.0, v);
  TEST_ASSERT_LESS_THAN(250.0, v);
}

void test_voltage_in_expected_range(void) {
  int reading = analogRead(VOLTAGE_PIN);
  float voltage = calculateVoltage(reading);

  TEST_ASSERT_GREATER_OR_EQUAL(0.0, voltage);
  TEST_ASSERT_LESS_THAN(300.0, voltage);
}

// CURRENT CALCULATION TESTS
float calculateCurrent(int adcValue) {
  float current = ((adcValue - 2048) / 4095.0) * 3.3 * CURRENT_CALIBRATION;
  if (current < 0.1)
    current = 0; // Noise filter
  return abs(current);
}

void test_current_calculation_zero(void) {
  float i = calculateCurrent(2048); // Mid-point = 0 current
  TEST_ASSERT_FLOAT_WITHIN(0.5, 0.0, i);
}

void test_current_noise_filtered(void) {
  float i = calculateCurrent(2050); // Tiny deviation
  TEST_ASSERT_FLOAT_WITHIN(0.1, 0.0, i);
}

// POWER CALCULATION TESTS

float calculatePower(float voltage, float current) { return voltage * current; }

void test_power_calculation(void) {
  float power = calculatePower(230.0, 10.0);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 2300.0, power);
}

void test_power_zero_when_no_current(void) {
  float power = calculatePower(230.0, 0.0);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 0.0, power);
}

void test_total_power_three_phases(void) {
  float p1 = 575.0, p2 = 414.0, p3 = 253.5;
  float total = p1 + p2 + p3;
  TEST_ASSERT_FLOAT_WITHIN(0.1, 1242.5, total);
}

// ENERGY CALCULATION TESTS

float calculateEnergy(float powerWatts, float hours) {
  return (powerWatts * hours) / 1000.0; // kWh
}

void test_energy_one_hour(void) {
  float energy = calculateEnergy(1000.0, 1.0);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, energy);
}

void test_energy_15_minutes(void) {
  float energy = calculateEnergy(1000.0, 0.25);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.25, energy);
}

// SETUP

void setup() {
  delay(2000);
  Serial.begin(115200);

  UNITY_BEGIN();

  // ADC tests
  RUN_TEST(test_adc_reads_valid_range);
  RUN_TEST(test_all_current_pins_readable);
  RUN_TEST(test_adc_readings_stable);

  // Voltage tests
  RUN_TEST(test_voltage_calculation_zero);
  RUN_TEST(test_voltage_calculation_mid);
  RUN_TEST(test_voltage_in_expected_range);

  // Current tests
  RUN_TEST(test_current_calculation_zero);
  RUN_TEST(test_current_noise_filtered);

  // Power tests
  RUN_TEST(test_power_calculation);
  RUN_TEST(test_power_zero_when_no_current);
  RUN_TEST(test_total_power_three_phases);

  // Energy tests
  RUN_TEST(test_energy_one_hour);
  RUN_TEST(test_energy_15_minutes);

  UNITY_END();
}

void loop() {}
