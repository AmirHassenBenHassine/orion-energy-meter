
#include <Arduino.h>
#include <WiFi.h>
#include <unity.h>

#define TEST_SSID "TUNISIETELECOM-2.4G-nG46_Plus"
#define TEST_PASSWORD "KF39UwaM"
#define CONNECTION_TIMEOUT_MS 30000

void setUp(void) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(1000);
}

void tearDown(void) { WiFi.disconnect(true); }

void test_wifi_scan_finds_networks(void) {
  int networks = WiFi.scanNetworks();
  TEST_ASSERT_GREATER_THAN(0, networks);
  WiFi.scanDelete();
}

void test_wifi_connects_to_network(void) {
  WiFi.begin(TEST_SSID, TEST_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < CONNECTION_TIMEOUT_MS) {
    delay(500);
  }

  TEST_ASSERT_EQUAL(WL_CONNECTED, WiFi.status());
  TEST_ASSERT_NOT_EQUAL(IPAddress(0, 0, 0, 0), WiFi.localIP());
}

void test_wifi_gets_valid_ip(void) {
  WiFi.begin(TEST_SSID, TEST_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < CONNECTION_TIMEOUT_MS) {
    delay(500);
  }

  IPAddress ip = WiFi.localIP();
  TEST_ASSERT_NOT_EQUAL(0, ip[0]);
}

void test_wifi_gets_gateway(void) {
  WiFi.begin(TEST_SSID, TEST_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < CONNECTION_TIMEOUT_MS) {
    delay(500);
  }

  IPAddress gw = WiFi.gatewayIP();
  TEST_ASSERT_NOT_EQUAL(IPAddress(0, 0, 0, 0), gw);
}

void test_wifi_rssi_valid(void) {
  WiFi.begin(TEST_SSID, TEST_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < CONNECTION_TIMEOUT_MS) {
    delay(500);
  }

  int rssi = WiFi.RSSI();
  TEST_ASSERT_LESS_THAN(0, rssi);
  TEST_ASSERT_GREATER_THAN(-100, rssi);
}

void setup() {
  delay(2000);
  Serial.begin(115200);

  UNITY_BEGIN();

  RUN_TEST(test_wifi_scan_finds_networks);
  RUN_TEST(test_wifi_connects_to_network);
  RUN_TEST(test_wifi_gets_valid_ip);
  RUN_TEST(test_wifi_gets_gateway);
  RUN_TEST(test_wifi_rssi_valid);

  UNITY_END();
}

void loop() {}
