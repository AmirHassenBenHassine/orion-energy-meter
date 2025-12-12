#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <unity.h>

// WiFi credentials
#define TEST_WIFI_SSID "TUNISIETELECOM-2.4G-nG46_Plus"
#define TEST_WIFI_PASS "KF39UwaM"

#define TEST_MQTT_HOST "192.168.100.235"
#define TEST_MQTT_PORT 8883
#define TEST_MQTT_USER "orion_device"
#define TEST_MQTT_PASS "123456789"

#define USE_INSECURE_TLS true

// Let's Encrypt CA
static const char ISRG_ROOT_X1[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDhTCCAm2gAwIBAgIUEl0Qmz0bBuxrKE88JKj9Wut3gdIwDQYJKoZIhvcNAQEL
BQAwUjELMAkGA1UEBhMCVE4xDzANBgNVBAgMBlNvdXNzZTEPMA0GA1UEBwwGU291
c3NlMQ4wDAYDVQQKDAVPcmlvbjERMA8GA1UEAwwIT3Jpb24gQ0EwHhcNMjUxMjA4
MTkwNDM0WhcNMjYxMjA4MTkwNDM0WjBSMQswCQYDVQQGEwJUTjEPMA0GA1UECAwG
U291c3NlMQ8wDQYDVQQHDAZTb3Vzc2UxDjAMBgNVBAoMBU9yaW9uMREwDwYDVQQD
DAhPcmlvbiBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKqQIhHh
XRKzn/iwBlr2tiPv6kz7AruKoUzYE1kNmadcXjlC5B/iBRN6izey4mqwND2tlRqp
Uuay0BBdnv9Vgz6WwBkAl41iQ7rgPMe3A4qBZyyuMxG7QhjUvkMX4TNNmzNP7v/D
avEMhPem5Lu6wM7byrGqgB1Z4RGNQPr1+cHqsOSWxVlqnj6mIOfd6Rfc+9qFvdLL
/seT3k0aRoih+wq3PwjOow719AN17VdvRcmW7KuXlHM3fU9GCSXdIjcKIwx+6Gju
wXmbl2ytHdHzR1udu31vtln/BL2vQleFKS5AZHUEEfpkaR3frYyvKYnkWizeKXcG
yD9gJJdWtXGibtsCAwEAAaNTMFEwHQYDVR0OBBYEFDLmYyeTcjktFijegq5b8HVI
j+CZMB8GA1UdIwQYMBaAFDLmYyeTcjktFijegq5b8HVIj+CZMA8GA1UdEwEB/wQF
MAMBAf8wDQYJKoZIhvcNAQELBQADggEBAI7VsoVvn/Qns8p1vInhBnY3IOtX3J00
H3cD6Lbdrk5ilo6cAQDXsGVC9aQa0V88Yui5PfN6d9W/R6IMj2naAliNYxAWh0zE
k8vyii1vn/yZESqPESCYesMXOjR6LHvE4qCPg6wuxTe0LalqpKmdMRnbyQSM1qJH
yafF6YFGC8HxLDNkTD+n+PuMbQh9vBMRPt7RR/cceiFdTvzpt541gLnUqdxN2MBI
MCLCePSDCVu1re/NbclHl4pFCGaNLimmqD/qLGi5lHrE5pvVHTqVIVbNBzvZQho5
/fYujvKrm7vtPVH99xaLvFdujs9VLox3wGQ3mQkJt8w9o//8bkFqVjk=
-----END CERTIFICATE-----
)EOF";

WiFiClientSecure secureClient;
PubSubClient mqtt(secureClient);

bool messageReceived = false;
String receivedTopic = "";
String receivedPayload = "";

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  messageReceived = true;
  receivedTopic = String(topic);
  receivedPayload = "";
  for (unsigned int i = 0; i < length; i++) {
    receivedPayload += (char)payload[i];
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED)
    return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(TEST_WIFI_SSID, TEST_WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 30000) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi connected. IP: %s\n",
                WiFi.localIP().toString().c_str());
}

void syncTime() {
  Serial.print("Syncing time...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = time(nullptr);
  while (now < 1700000000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.printf(" Done! %s", ctime(&now));
}

void setupTLS() {
#if USE_INSECURE_TLS
  Serial.println("Using insecure TLS (local testing)");
  secureClient.setInsecure();
#else
  Serial.println("Using Let's Encrypt CA (production)");
  secureClient.setCACert(ISRG_ROOT_X1);
#endif
}

void setUp(void) {
  messageReceived = false;
  receivedTopic = "";
  receivedPayload = "";
}

void tearDown(void) {
  mqtt.disconnect();
  delay(500);
}

// =============================================================================
// CONNECTION TESTS
// =============================================================================

void test_tls_connect(void) {
  setupTLS();
  mqtt.setServer(TEST_MQTT_HOST, TEST_MQTT_PORT);

  bool connected = mqtt.connect("esp32_test", TEST_MQTT_USER, TEST_MQTT_PASS);

  TEST_ASSERT_TRUE_MESSAGE(connected, "TLS connection failed");
  TEST_ASSERT_TRUE(mqtt.connected());
}

void test_auth_fails_without_credentials(void) {
  setupTLS();
  mqtt.setServer(TEST_MQTT_HOST, TEST_MQTT_PORT);

  bool connected = mqtt.connect("esp32_no_auth");

  TEST_ASSERT_FALSE_MESSAGE(connected, "Should fail without credentials");
}

void test_auth_fails_with_wrong_password(void) {
  setupTLS();
  mqtt.setServer(TEST_MQTT_HOST, TEST_MQTT_PORT);

  bool connected = mqtt.connect("esp32_wrong", TEST_MQTT_USER, "wrongpass");

  TEST_ASSERT_FALSE_MESSAGE(connected, "Should fail with wrong password");
}

// PUBLISH TESTS

void test_publish_single_message(void) {
  setupTLS();
  mqtt.setServer(TEST_MQTT_HOST, TEST_MQTT_PORT);
  mqtt.connect("esp32_pub", TEST_MQTT_USER, TEST_MQTT_PASS);

  TEST_ASSERT_TRUE(mqtt.connected());

  bool published = mqtt.publish("energy/metrics", "{\"voltage\":230.5}");
  TEST_ASSERT_TRUE(published);
}

void test_publish_orion_meter_format(void) {
  setupTLS();
  mqtt.setServer(TEST_MQTT_HOST, TEST_MQTT_PORT);
  mqtt.connect("esp32_orion", TEST_MQTT_USER, TEST_MQTT_PASS);

  TEST_ASSERT_TRUE(mqtt.connected());

  // Orion payload
  const char *payload = "{"
                        "\"deviceId\":\"AABBCCDDEEFF\","
                        "\"battery\":85.5,"
                        "\"timestamp\":\"2025-11-29 14:30:00\","
                        "\"voltage\":230.5,"
                        "\"totalPower\":1250.8,"
                        "\"energyTotal\":3.456,"
                        "\"phases\":["
                        "{\"current\":2.5,\"power\":575.0},"
                        "{\"current\":1.8,\"power\":414.0},"
                        "{\"current\":1.1,\"power\":253.5}"
                        "]"
                        "}";

  bool published = mqtt.publish("orion/meter", payload);
  TEST_ASSERT_TRUE(published);
}

void test_publish_multiple_messages(void) {
  setupTLS();
  mqtt.setServer(TEST_MQTT_HOST, TEST_MQTT_PORT);
  mqtt.connect("esp32_multi", TEST_MQTT_USER, TEST_MQTT_PASS);

  TEST_ASSERT_TRUE(mqtt.connected());

  int successCount = 0;
  for (int i = 0; i < 10; i++) {
    String msg = "{\"count\":" + String(i) + "}";
    if (mqtt.publish("test/multi", msg.c_str())) {
      successCount++;
    }
    mqtt.loop();
    delay(100);
  }

  TEST_ASSERT_EQUAL(10, successCount);
}

// SUBSCRIBE TESTS

void test_subscribe_and_receive(void) {
  setupTLS();
  mqtt.setServer(TEST_MQTT_HOST, TEST_MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.connect("esp32_sub", TEST_MQTT_USER, TEST_MQTT_PASS);

  TEST_ASSERT_TRUE(mqtt.connected());

  mqtt.subscribe("orion/trigger");
  mqtt.publish("orion/trigger", "{\"action\":\"test\"}");

  unsigned long start = millis();
  while (!messageReceived && (millis() - start) < 5000) {
    mqtt.loop();
    delay(100);
  }

  TEST_ASSERT_TRUE(messageReceived);
  TEST_ASSERT_EQUAL_STRING("orion/trigger", receivedTopic.c_str());
}

// RECONNECTION TESTS

void test_reconnect_after_disconnect(void) {
  setupTLS();
  mqtt.setServer(TEST_MQTT_HOST, TEST_MQTT_PORT);

  bool connected = mqtt.connect("esp32_reconn", TEST_MQTT_USER, TEST_MQTT_PASS);
  TEST_ASSERT_TRUE(connected);

  mqtt.disconnect();
  delay(1000);
  TEST_ASSERT_FALSE(mqtt.connected());

  connected = mqtt.connect("esp32_reconn", TEST_MQTT_USER, TEST_MQTT_PASS);
  TEST_ASSERT_TRUE(connected);
}

// SETUP AND LOOP

void setup() {
  delay(2000);
  Serial.begin(115200);
  Serial.println("\n\n=== MQTT TLS TESTS ===\n");

  connectWiFi();
  syncTime();

  UNITY_BEGIN();

  RUN_TEST(test_tls_connect);
  RUN_TEST(test_auth_fails_without_credentials);
  RUN_TEST(test_auth_fails_with_wrong_password);

  RUN_TEST(test_publish_single_message);
  RUN_TEST(test_publish_orion_meter_format);
  RUN_TEST(test_publish_multiple_messages);

  RUN_TEST(test_subscribe_and_receive);

  RUN_TEST(test_reconnect_after_disconnect);

  UNITY_END();
}

void loop() {}
