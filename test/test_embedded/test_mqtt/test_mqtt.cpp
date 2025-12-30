#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <unity.h>

// WiFi credentials
#define TEST_WIFI_SSID "TUNISIETELECOM-2.4G-nG46_Plus"
#define TEST_WIFI_PASS "KF39UwaM"

#define TEST_MQTT_HOST "orion.local"
#define TEST_MQTT_PORT 1883
#define TEST_MQTT_USER "orion_device"
#define TEST_MQTT_PASS "123456789"

// MQTT Topics
#define TOPIC_WIFI_CREDENTIALS "orion/wifi_credentials"
#define TOPIC_PAIRING_STATUS "orion/pairing/status"
#define TOPIC_ENERGY_METRICS "orion/energy/metrics"
#define TOPIC_TRIGGER "orion/trigger"
#define TOPIC_CONFIG "orion/config"

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

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

bool messageReceived = false;
String receivedTopic = "";
String receivedPayload = "";
int messagesReceived = 0;

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  messagesReceived++;
  messageReceived = true;
  receivedTopic = String(topic);
  receivedPayload = "";
  for (unsigned int i = 0; i < length; i++) {
    receivedPayload += (char)payload[i];
  }
  
  Serial.printf("\n📨 Message received on %s:\n", topic);
  Serial.println(receivedPayload);
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

  if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
      
      // ✅ Wait for valid IP
      start = millis();
      while (WiFi.localIP() == IPAddress(0, 0, 0, 0) && (millis() - start) < 10000) {
        delay(500);
      }
      
      Serial.printf("Final IP: %s\n", WiFi.localIP().toString().c_str());
    }
  }

bool connectMQTT() {
  mqtt.setServer(TEST_MQTT_HOST, TEST_MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(15);  // ✅ Set keepalive

  String clientId = "esp32_test_" + String(random(0xffff), HEX);
  
  if (mqtt.connect(clientId.c_str(), TEST_MQTT_USER, TEST_MQTT_PASS)) {
    Serial.println("✅ MQTT connected");
    return true;
  }
  
  Serial.printf("❌ MQTT connection failed, state: %d\n", mqtt.state());
  return false;
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
  // wifiClient.setInsecure();
#else
  Serial.println("Using Let's Encrypt CA (production)");
  wifiClient.setCACert(ISRG_ROOT_X1);
#endif
}

void setUp(void) {
  messageReceived = false;
  receivedTopic = "";
  receivedPayload = "";

  if (!mqtt.connected()) {
    connectMQTT();
  }
}

void tearDown(void) {
  // mqtt.disconnect();
  // delay(500);
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

void test_mqtt_connect(void) {
  mqtt.disconnect();
  delay(500);
  
  bool connected = connectMQTT();
  TEST_ASSERT_TRUE_MESSAGE(connected, "MQTT connection failed");
  TEST_ASSERT_TRUE(mqtt.connected());
}

void test_mqtt_auth_required(void) {
  mqtt.disconnect();
  delay(500);
  
  mqtt.setServer(TEST_MQTT_HOST, TEST_MQTT_PORT);
  
  bool connected = mqtt.connect("esp32_no_auth");
  TEST_ASSERT_FALSE_MESSAGE(connected, "Should fail without auth");
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

void test_publish_wifi_credentials(void) {
  Serial.println("\n--- Testing WiFi Credentials Publish ---");
  
  TEST_ASSERT_TRUE_MESSAGE(mqtt.connected(), "MQTT not connected");
  
  // Create WiFi credentials JSON
  JsonDocument doc;
  doc["ssid"] = "TestNetwork";
  doc["password"] = "TestPassword123";
  
  String payload;
  serializeJson(doc, payload);
  
  Serial.printf("Publishing to %s:\n", TOPIC_WIFI_CREDENTIALS);
  Serial.println(payload);
  
  bool published = mqtt.publish(TOPIC_WIFI_CREDENTIALS, payload.c_str(), false);
  
  TEST_ASSERT_TRUE_MESSAGE(published, "Failed to publish WiFi credentials");
  Serial.println("✅ WiFi credentials published");
}

void test_wifi_credentials_json_format(void) {
  JsonDocument doc;
  doc["ssid"] = "TestSSID";
  doc["password"] = "TestPass";
  doc["device_id"] = "AABBCCDD";
  
  String payload;
  serializeJson(doc, payload);
  
  // Verify it's valid JSON
  JsonDocument verify;
  DeserializationError error = deserializeJson(verify, payload);
  
  TEST_ASSERT_FALSE(error);
  TEST_ASSERT_EQUAL_STRING("TestSSID", verify["ssid"]);
  TEST_ASSERT_EQUAL_STRING("TestPass", verify["password"]);
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

void test_subscribe_pairing_status(void) {
  Serial.println("\n--- Testing Pairing Status Subscribe ---");
  
  TEST_ASSERT_TRUE(mqtt.connected());
  
  bool subscribed = mqtt.subscribe(TOPIC_PAIRING_STATUS, 1);
  TEST_ASSERT_TRUE_MESSAGE(subscribed, "Failed to subscribe to pairing status");
  
  mqtt.loop();
  delay(100);
  
  Serial.println("✅ Subscribed to pairing/status");
}

void test_receive_pairing_status_starting(void) {
  Serial.println("\n--- Testing Pairing Status: starting ---");
  
  mqtt.subscribe(TOPIC_PAIRING_STATUS);
  
  JsonDocument doc;
  doc["status"] = "starting";
  doc["device_id"] = "F8B3B77FEAF4";
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  
  messageReceived = false;
  mqtt.publish(TOPIC_PAIRING_STATUS, payload.c_str());
  
  unsigned long start = millis();
  while (!messageReceived && (millis() - start) < 3000) {
    mqtt.loop();
    delay(10);
  }
  
  TEST_ASSERT_TRUE_MESSAGE(messageReceived, "Did not receive pairing status");
  TEST_ASSERT_EQUAL_STRING(TOPIC_PAIRING_STATUS, receivedTopic.c_str());
  
  // Parse received message
  JsonDocument received;
  deserializeJson(received, receivedPayload);
  TEST_ASSERT_EQUAL_STRING("starting", received["status"]);
  
  Serial.println("✅ Received 'starting' status");
}

void test_pairing_status_workflow(void) {
  Serial.println("\n--- Testing Full Pairing Workflow ---");
  
  mqtt.subscribe(TOPIC_PAIRING_STATUS);
  
  const char* statuses[] = {
    "starting",
    "ready_to_configure",
    "configuring",
    "wifi_configured",
    "connection_failed",
    "paired",
    "failed",
    "disconnected"
  };
  
  for (int i = 0; i < 5; i++) {
    JsonDocument doc;
    doc["status"] = statuses[i];
    doc["device_id"] = "F8B3B77FEAF4";
    doc["step"] = i + 1;
    
    String payload;
    serializeJson(doc, payload);
    
    messageReceived = false;
    mqtt.publish(TOPIC_PAIRING_STATUS, payload.c_str());
    
    unsigned long start = millis();
    while (!messageReceived && (millis() - start) < 2000) {
      mqtt.loop();
      delay(10);
    }
    
    if (messageReceived) {
      JsonDocument received;
      deserializeJson(received, receivedPayload);
      Serial.printf("✅ Step %d: %s\n", i+1, statuses[i]);
    }
    
    delay(200);
  }
  
  TEST_ASSERT_EQUAL(5, messagesReceived);
  Serial.println("✅ Full pairing workflow completed");
}

void test_pairing_status_error_handling(void) {
  Serial.println("\n--- Testing Pairing Status: Errors ---");
  
  mqtt.subscribe(TOPIC_PAIRING_STATUS);
  
  // Test connection_failed status
  JsonDocument doc;
  doc["status"] = "connection_failed";
  doc["error"] = "Incorrect password";
  doc["ssid"] = "TestNetwork";
  
  String payload;
  serializeJson(doc, payload);
  
  messageReceived = false;
  mqtt.publish(TOPIC_PAIRING_STATUS, payload.c_str());
  
  unsigned long start = millis();
  while (!messageReceived && (millis() - start) < 2000) {
    mqtt.loop();
    delay(10);
  }
  
  TEST_ASSERT_TRUE(messageReceived);
  
  JsonDocument received;
  deserializeJson(received, receivedPayload);
  TEST_ASSERT_EQUAL_STRING("connection_failed", received["status"]);
  TEST_ASSERT_EQUAL_STRING("Incorrect password", received["error"]);
  
  Serial.println("✅ Error status received correctly");
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

void test_complete_wifi_pairing_workflow(void) {
  Serial.println("\n--- Testing Complete WiFi Pairing Workflow ---");
  
  // Step 1: ESP32 publishes WiFi credentials
  Serial.println("Step 1: ESP32 sends WiFi credentials...");
  JsonDocument credDoc;
  credDoc["ssid"] = "HomeNetwork";
  credDoc["password"] = "SecurePass123";
  credDoc["device_id"] = "F8B3B77FEAF4";
  
  String credPayload;
  serializeJson(credDoc, credPayload);
  
  bool credSent = mqtt.publish(TOPIC_WIFI_CREDENTIALS, credPayload.c_str());
  TEST_ASSERT_TRUE(credSent);
  Serial.println("✅ Credentials sent");
  
  mqtt.loop();
  delay(500);
  
  // Step 2: Subscribe to pairing status
  Serial.println("Step 2: Subscribing to pairing status...");
  mqtt.subscribe(TOPIC_PAIRING_STATUS);
  mqtt.loop();
  delay(100);
  
  // Step 3: Simulate OrangePi responses
  Serial.println("Step 3: Simulating OrangePi status updates...");
  
  const char* workflow[] = {
    "starting",
    "ready_to_configure",
    "configuring",
    "wifi_configured",
    "paired"
  };
  
  messagesReceived = 0;
  
  for (const char* status : workflow) {
    JsonDocument statusDoc;
    statusDoc["status"] = status;
    statusDoc["device_id"] = "F8B3B77FEAF4";
    statusDoc["ssid"] = "HomeNetwork";
    
    String statusPayload;
    serializeJson(statusDoc, statusPayload);
    
    mqtt.publish(TOPIC_PAIRING_STATUS, statusPayload.c_str());
    
    unsigned long start = millis();
    while ((millis() - start) < 500) {
      mqtt.loop();
      delay(10);
    }
    
    Serial.printf("  Status: %s\n", status);
  }
  
  Serial.println("✅ Complete pairing workflow tested");
  TEST_ASSERT_GREATER_OR_EQUAL(3, messagesReceived);
}

// =============================================================================
// ADDITIONAL TOPIC TESTS
// =============================================================================

void test_publish_energy_metrics(void) {
  Serial.println("\n--- Testing Energy Metrics Publish ---");
  
  JsonDocument doc;
  doc["deviceId"] = "F8B3B77FEAF4";
  doc["voltage"] = 230.5;
  doc["totalPower"] = 1250.8;
  doc["battery"] = 85.5;
  
  JsonArray phases = doc["phases"].to<JsonArray>();
  for (int i = 0; i < 3; i++) {
    JsonObject phase = phases.add<JsonObject>();
    phase["current"] = 2.5 + i * 0.5;
    phase["power"] = 575.0 + i * 100;
  }
  
  String payload;
  serializeJson(doc, payload);
  
  bool published = mqtt.publish(TOPIC_ENERGY_METRICS, payload.c_str());
  TEST_ASSERT_TRUE(published);
  
  Serial.println("✅ Energy metrics published");
}

void test_subscribe_trigger(void) {
  Serial.println("\n--- Testing Trigger Subscribe ---");
  
  mqtt.subscribe(TOPIC_TRIGGER);
  
  JsonDocument doc;
  doc["action"] = "reboot";
  
  String payload;
  serializeJson(doc, payload);
  
  messageReceived = false;
  mqtt.publish(TOPIC_TRIGGER, payload.c_str());
  
  unsigned long start = millis();
  while (!messageReceived && (millis() - start) < 2000) {
    mqtt.loop();
    delay(10);
  }
  
  TEST_ASSERT_TRUE(messageReceived);
  Serial.println("✅ Trigger message received");
}

// SETUP AND LOOP

void setup() {
  delay(2000);
  Serial.begin(115200);
  Serial.println("\n\n=== MQTT TLS TESTS ===\n");

  connectWiFi();
  connectMQTT();
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

  Serial.println("\n--- Connection Tests ---");
  RUN_TEST(test_mqtt_connect);
  RUN_TEST(test_mqtt_auth_required);

  Serial.println("\n--- WiFi Credentials Tests ---");
  RUN_TEST(test_wifi_credentials_json_format);
  RUN_TEST(test_publish_wifi_credentials);

  Serial.println("\n--- Pairing Status Tests ---");
  RUN_TEST(test_subscribe_pairing_status);
  RUN_TEST(test_receive_pairing_status_starting);
  RUN_TEST(test_pairing_status_workflow);
  RUN_TEST(test_pairing_status_error_handling);

  Serial.println("\n--- Complete Workflow ---");
  RUN_TEST(test_complete_wifi_pairing_workflow);

  Serial.println("\n--- Additional Topics ---");
  RUN_TEST(test_publish_energy_metrics);
  RUN_TEST(test_subscribe_trigger);

  UNITY_END();
  
  mqtt.disconnect();
  UNITY_END();
}

void loop() {}
