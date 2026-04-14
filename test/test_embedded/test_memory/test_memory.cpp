#include <Arduino.h>
#include <unity.h>

void setUp(void) {}

void tearDown(void) {}
void test_free_heap_sufficient(void) {
  size_t freeHeap = ESP.getFreeHeap();
  TEST_ASSERT_GREATER_THAN(50000, freeHeap);
}

void test_free_psram_if_available(void) {
  if (psramFound()) {
    size_t freePsram = ESP.getFreePsram();
    TEST_ASSERT_GREATER_THAN(0, freePsram);
  } else {
    TEST_PASS();
  }
}

void setup() {
  delay(2000);
  Serial.begin(115200);

  UNITY_BEGIN();

  RUN_TEST(test_free_heap_sufficient);
  RUN_TEST(test_free_psram_if_available);

  UNITY_END();
}

void loop() {}
