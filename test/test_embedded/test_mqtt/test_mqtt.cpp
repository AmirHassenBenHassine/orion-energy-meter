#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <unity.h>

// WiFi credentials
#define TEST_WIFI_SSID "A72"
#define TEST_WIFI_PASS "nono1992"

#define TEST_MQTT_HOST "10.46.218.203"
#define TEST_MQTT_PORT 8883
#define TEST_MQTT_USER "orion_device"
#define TEST_MQTT_PASS "123456789"

#define USE_INSECURE_TLS true

// Let's Encrypt CA
static const char ISRG_ROOT_X1[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
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
