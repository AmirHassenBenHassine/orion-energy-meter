#include <Arduino.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <unity.h>

#define TEST_WIFI_SSID "TUNISIETELECOM-2.4G-nG46_Plus"
#define TEST_WIFI_PASS "KF39UwaM"
#define TEST_MQTT_HOST "192.168.100.235"
#define TEST_MQTT_PORT 1883
#define TEST_MQTT_USER "orion_device"
#define TEST_MQTT_PASS "123456789"

WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);

void setUp(void) {}
void tearDown(void) {}

void test_free_heap_sufficient(void) {
  size_t freeHeap = ESP.getFreeHeap();
  Serial.printf("Free heap: %d bytes\n", freeHeap);
  TEST_ASSERT_GREATER_THAN(50000, freeHeap);
}

void test_chip_info_valid(void) {
  TEST_ASSERT_GREATER_THAN(0, ESP.getCpuFreqMHz());
  TEST_ASSERT_GREATER_THAN(0, ESP.getFlashChipSize());
}

// PREFERENCES TESTS

void test_preferences_write_read(void) {
  Preferences prefs;
  prefs.begin("test_ns", false);

  prefs.putString("test_key", "test_value");
  String value = prefs.getString("test_key", "");

  prefs.clear();
  prefs.end();

  TEST_ASSERT_EQUAL_STRING("test_value", value.c_str());
}

void test_preferences_persist_credentials(void) {
  Preferences prefs;
  prefs.begin("wifi", false);

  prefs.putString("ssid", "TestSSID");
  prefs.putString("pass", "TestPass");

  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");

  prefs.clear();
  prefs.end();

  TEST_ASSERT_EQUAL_STRING("TestSSID", ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("TestPass", pass.c_str());
}

void test_complete_data_flow(void) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(TEST_WIFI_SSID, TEST_WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 30000) {
    delay(500);
  }
  TEST_ASSERT_EQUAL_MESSAGE(WL_CONNECTED, WiFi.status(), "WiFi failed");
  Serial.println("WiFi: OK");

  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  time_t now = time(nullptr); 
  start = millis();
  while (now < 1700000000 && (millis() - start) < 20000) { 
    delay(500);
    now = time(nullptr);
  }
  TEST_ASSERT_GREATER_THAN_MESSAGE(1700000000, now, "Time sync failed");
  Serial.println("Time sync: OK");

  secureClient.setInsecure();
  mqtt.setServer(TEST_MQTT_HOST, TEST_MQTT_PORT);

  bool connected =
      mqtt.connect("esp32_integration", TEST_MQTT_USER, TEST_MQTT_PASS);
  TEST_ASSERT_TRUE_MESSAGE(connected, "MQTT failed");
  Serial.println("MQTT: OK");

  float voltage = 230.5;
  float current[3] = {2.5, 1.8, 1.1};
  float power[3];
  float totalPower = 0;

  for (int i = 0; i < 3; i++) {
    power[i] = voltage * current[i];
    totalPower += power[i];
  }
  Serial.printf("Sensor reading: %.1fW\n", totalPower);

  char payload[512];
  snprintf(payload, sizeof(payload),
           "{"
           "\"deviceId\":\"TESTDEVICE01\","
           "\"battery\":85.5,"
           "\"timestamp\":\"%s\","
           "\"voltage\":%.1f,"
           "\"totalPower\":%.1f,"
           "\"energyTotal\":3.456,"
           "\"phases\":["
           "{\"current\":%.1f,\"power\":%.1f},"
           "{\"current\":%.1f,\"power\":%.1f},"
           "{\"current\":%.1f,\"power\":%.1f}"
           "]"
           "}",
           "2025-11-29 15:00:00", voltage, totalPower, current[0], power[0],
           current[1], power[1], current[2], power[2]);

  bool published = mqtt.publish("energy/metrics", payload);
  TEST_ASSERT_TRUE_MESSAGE(published, "Publish failed");
  Serial.println("Publish: OK");

  mqtt.disconnect();
  WiFi.disconnect();

  Serial.println("\nCOMPLETE DATA FLOW: PASSED ");
}

// SETUP

void setup() {
  delay(2000);
  Serial.begin(115200);

  UNITY_BEGIN();

  // System tests
  RUN_TEST(test_free_heap_sufficient);
  RUN_TEST(test_chip_info_valid);

  // Preferences tests
  RUN_TEST(test_preferences_write_read);
  RUN_TEST(test_preferences_persist_credentials);

  // Full workflow
  RUN_TEST(test_complete_data_flow);

  UNITY_END();
}

void loop() {}
