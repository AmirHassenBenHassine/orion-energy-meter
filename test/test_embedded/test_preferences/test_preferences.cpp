#include <Arduino.h>
#include <Preferences.h>
#include <unity.h>
void setUp(void) {}

void tearDown(void) {}

void test_preferences_int_write_read(void) {
  Preferences prefs;
  prefs.begin("test_ns", false);

  prefs.putInt("test_int", 12345);
  int value = prefs.getInt("test_int", 0);

  prefs.clear();
  prefs.end();

  TEST_ASSERT_EQUAL(12345, value);
}

void test_preferences_write_read(void) {
  Preferences prefs;
  prefs.begin("test_ns", false);

  prefs.putString("test_key", "test_value");
  String value = prefs.getString("test_key", "");

  prefs.clear();
  prefs.end();

  TEST_ASSERT_EQUAL_STRING("test_value", value.c_str());
}

void setup() {
  delay(2000);
  Serial.begin(115200);

  UNITY_BEGIN();

  RUN_TEST(test_preferences_write_read);
  RUN_TEST(test_preferences_int_write_read);

  UNITY_END();
}

void loop() {}
