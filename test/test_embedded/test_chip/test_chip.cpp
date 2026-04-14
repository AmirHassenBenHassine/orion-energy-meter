#include <Arduino.h>
#include <unity.h>

void setUp(void) {}

void tearDown(void) {}

void test_chip_info_valid(void) {
  TEST_ASSERT_GREATER_THAN(0, ESP.getCpuFreqMHz());
  TEST_ASSERT_GREATER_THAN(0, ESP.getFlashChipSize());
  TEST_ASSERT_NOT_NULL(ESP.getSdkVersion());
}

void setup() {
  delay(2000);
  Serial.begin(115200);

  UNITY_BEGIN();
  RUN_TEST(test_chip_info_valid);

  UNITY_END();
}

void loop() {}
