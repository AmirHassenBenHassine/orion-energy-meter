#include <Arduino.h>
#include <unity.h>

void setUp(void) {}

void tearDown(void) {}

void test_millis_increases(void) {
  unsigned long t1 = millis();
  delay(100);
  unsigned long t2 = millis();

  TEST_ASSERT_GREATER_THAN(t1, t2);
  TEST_ASSERT_UINT_WITHIN(20, 100, t2 - t1);
}

void test_micros_increases(void) {
  unsigned long t1 = micros();
  delayMicroseconds(1000);
  unsigned long t2 = micros();

  TEST_ASSERT_GREATER_THAN(t1, t2);
}

void setup() {
  delay(2000);
  Serial.begin(115200);

  UNITY_BEGIN();
  RUN_TEST(test_millis_increases);
  RUN_TEST(test_micros_increases);

  UNITY_END();
}

void loop() {}
