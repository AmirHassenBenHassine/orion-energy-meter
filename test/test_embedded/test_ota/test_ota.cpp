#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <unity.h>

#define TEST_WIFI_SSID "A72"
#define TEST_WIFI_PASS "nono1992"

static const char GITHUB_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIID0zCCArugAwIBAgIQVmcdBOpPmUxvEIFHWdJ1lDANBgkqhkiG9w0BAQwFADB7
MQswCQYDVQQGEwJHQjEbMBkGA1UECAwSR3JlYXRlciBNYW5jaGVzdGVyMRAwDgYD
VQQHDAdTYWxmb3JkMRowGAYDVQQKDBFDb21vZG8gQ0EgTGltaXRlZDEhMB8GA1UE
AwwYQUFBIENlcnRpZmljYXRlIFNlcnZpY2VzMB4XDTE5MDMxMjAwMDAwMFoXDTI4
MTIzMTIzNTk1OVowgYgxCzAJBgNVBAYTAlVTMRMwEQYDVQQIEwpOZXcgSmVyc2V5
MRQwEgYDVQQHEwtKZXJzZXkgQ2l0eTEeMBwGA1UEChMVVGhlIFVTRVJUUlVTVCBO
ZXR3b3JrMS4wLAYDVQQDEyVVU0VSVHJ1c3QgRUNDIENlcnRpZmljYXRpb24gQXV0
aG9yaXR5MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAEGqxUWqn5aCPnetUkb1PGWthL
q8bVttHmc3Gu3ZzWDGH926CJA7gFFOxXzu5dP+Ihs8731Ip54KODfi2X0GHE8Znc
JZFjq38wo7Rw4sehM5zzvy5cU7Ffs30yf4o043l5o4HyMIHvMB8GA1UdIwQYMBaA
FKARCiM+lvEH7OKvKe+CpX/QMKS0MB0GA1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1
xmNjmjAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zARBgNVHSAECjAI
MAYGBFUdIAAwQwYDVR0fBDwwOjA4oDagNIYyaHR0cDovL2NybC5jb21vZG9jYS5j
b20vQUFBQ2VydGlmaWNhdGVTZXJ2aWNlcy5jcmwwNAYIKwYBBQUHAQEEKDAmMCQG
CCsGAQUFBzABhhhodHRwOi8vb2NzcC5jb21vZG9jYS5jb20wDQYJKoZIhvcNAQEM
BQADggEBABns652JLCALBIAdGN5CmXKZFjK9Dpx1WywV4ilAbe7/ctvbq5AfjJXy
ij0IckKJUAfiORVsAYfZFhr1wHUrxeZWEQff2Ji8fJ8ZOd+LygBkc7xGEJuTI42+
FsMuCIKchjN0djsoTI0DQoWz4rIjQtUfenVqGtF8qmchxDM6OW1TyaLtYiKou+JV
bJlsQ2uRl9EMC5MCHdK8aXdJ5htN978UeAOwproLtOGFfy/cQjutdAFI3tZs4RmY
CV4Ks2dH/hzg1cEo70qLRDEmBDeNiXQ2Lu+lIg+DdEmSx/cQwgwp+7e9un/jX9Wf
8qn0dNW44bOwgeThpWOjzOoEeJBuv/c=
-----END CERTIFICATE-----
)EOF";

// Let's Encrypt CA (for your VPS MQTT broker)
static const char LETSENCRYPT_CA_CERT[] PROGMEM = R"EOF(
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
  int attempts = 0;
  while (now < 1700000000 && attempts < 30) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }

  if (now > 1700000000) {
    Serial.printf(" Done! Time: %s", ctime(&now));
  } else {
    Serial.println(" FAILED!");
  }
}

bool isTimeSynced() {
  time_t now = time(nullptr);
  return now > 1700000000;
}

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// VERSION COMPARISON TESTS (No network needed)
// =============================================================================

int compareVersions(const String &v1, const String &v2) {
  int major1 = 0, minor1 = 0, patch1 = 0;
  int major2 = 0, minor2 = 0, patch2 = 0;

  sscanf(v1.c_str(), "%d.%d.%d", &major1, &minor1, &patch1);
  sscanf(v2.c_str(), "%d.%d.%d", &major2, &minor2, &patch2);

  if (major1 != major2)
    return major1 - major2;
  if (minor1 != minor2)
    return minor1 - minor2;
  return patch1 - patch2;
}

void test_version_compare_equal(void) {
  TEST_ASSERT_EQUAL(0, compareVersions("1.0.0", "1.0.0"));
}

void test_version_compare_major_greater(void) {
  TEST_ASSERT_GREATER_THAN(0, compareVersions("2.0.0", "1.0.0"));
}

void test_version_compare_major_less(void) {
  TEST_ASSERT_LESS_THAN(0, compareVersions("1.0.0", "2.0.0"));
}

void test_version_compare_minor_greater(void) {
  TEST_ASSERT_GREATER_THAN(0, compareVersions("1.2.0", "1.1.0"));
}

void test_version_compare_minor_less(void) {
  TEST_ASSERT_LESS_THAN(0, compareVersions("1.1.0", "1.2.0"));
}

void test_version_compare_patch_greater(void) {
  TEST_ASSERT_GREATER_THAN(0, compareVersions("1.0.2", "1.0.1"));
}

void test_version_compare_patch_less(void) {
  TEST_ASSERT_LESS_THAN(0, compareVersions("1.0.1", "1.0.2"));
}

void test_version_compare_complex(void) {
  TEST_ASSERT_GREATER_THAN(0, compareVersions("2.1.0", "1.9.9"));
  TEST_ASSERT_LESS_THAN(0, compareVersions("1.9.9", "2.0.0"));
  TEST_ASSERT_EQUAL(0, compareVersions("10.20.30", "10.20.30"));
}

// =============================================================================
// UPDATE SYSTEM TESTS (No network needed)
// =============================================================================

void test_update_partition_exists(void) {
  const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
  TEST_ASSERT_NOT_NULL_MESSAGE(partition, "No OTA partition found");

  Serial.printf("OTA partition: %s, size: %lu bytes\n", partition->label,
                (unsigned long)partition->size);
}

void test_update_has_enough_space(void) {
  const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
  TEST_ASSERT_NOT_NULL(partition);

  // Should have at least 1MB for firmware
  TEST_ASSERT_GREATER_THAN(1000000, partition->size);
}

void test_update_begin_end(void) {
  size_t testSize = 1024;

  bool beginResult = Update.begin(testSize);
  TEST_ASSERT_TRUE_MESSAGE(beginResult, "Update.begin() failed");

  Update.abort();
}

void test_running_partition_valid(void) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  TEST_ASSERT_NOT_NULL_MESSAGE(running, "Cannot get running partition");

  Serial.printf("Running from: %s\n", running->label);
}

// =============================================================================
// HTTPS CONNECTION TESTS (Verify GitHub will work)
// =============================================================================
void test_https_github_with_correct_ca(void) {
  secureClient.setCACert(GITHUB_ROOT_CA);

  bool connected = secureClient.connect("raw.githubusercontent.com", 443);

  TEST_ASSERT_TRUE_MESSAGE(connected, "GitHub HTTPS connection failed");

  Serial.println("GitHub with DigiCert G2 CA: OK");

  secureClient.stop();
}

void test_https_fetch_from_github(void) {
  HTTPClient http;
  secureClient.setCACert(GITHUB_ROOT_CA);

  http.begin(secureClient, "https://raw.githubusercontent.com/espressif/"
                           "arduino-esp32/master/package.json");

  int httpCode = http.GET();

  TEST_ASSERT_EQUAL_MESSAGE(200, httpCode, "Failed to fetch from GitHub");

  if (httpCode == 200) {
    String payload = http.getString();
    TEST_ASSERT_GREATER_THAN(0, payload.length());
    Serial.printf("Fetched %d bytes from GitHub\n", payload.length());
  }

  http.end();
}
// void test_https_connection_with_ca(void) {
//   // Verify time is synced first
//   TEST_ASSERT_TRUE_MESSAGE(isTimeSynced(),
//                            "Time not synced - cert verification will fail");
//   secureClient.setCACert(ISRG_ROOT_X1);
//
//   bool connected = secureClient.connect("www.google.com", 443);
//
//   TEST_ASSERT_TRUE_MESSAGE(connected, "HTTPS connection failed");
//
//   secureClient.stop();
// }
//
// void test_https_github_connection(void) {
//   // Verify time is synced first
//   TEST_ASSERT_TRUE_MESSAGE(isTimeSynced(),
//                            "Time not synced - cert verification will fail");
//   secureClient.setCACert(ISRG_ROOT_X1);
//   bool connected = secureClient.connect("raw.githubusercontent.com", 443);
//
//   TEST_ASSERT_TRUE_MESSAGE(connected, "GitHub HTTPS connection failed");
//
//   if (connected) {
//     Serial.println("GitHub connection: OK (Let's Encrypt CA works!)");
//   }
//
//   secureClient.stop();
// }
//
// void test_https_fetch_small_file(void) {
//   HTTPClient http;
//   TEST_ASSERT_TRUE_MESSAGE(isTimeSynced(),
//                            "Time not synced - cert verification will fail");
//   secureClient.setCACert(ISRG_ROOT_X1);
//
//   http.begin(secureClient, "https://raw.githubusercontent.com/espressif/"
//                            "arduino-esp32/master/package.json");
//
//   int httpCode = http.GET();
//
//   TEST_ASSERT_EQUAL_MESSAGE(200, httpCode, "Failed to fetch file from
//   GitHub");
//
//   if (httpCode == 200) {
//     String payload = http.getString();
//     TEST_ASSERT_GREATER_THAN(0, payload.length());
//     Serial.printf("Fetched %d bytes from GitHub\n", payload.length());
//   }
//
//   http.end();
// }
//
// =============================================================================
// JSON PARSING TESTS (Simulates version.json parsing)
// =============================================================================

void test_parse_version_json(void) {
  String mockJson = R"({
        "version": "1.1.0",
        "date": "2025-11-29",
        "min_version": "1.0.0",
        "changelog": "Bug fixes and improvements",
        "firmware_url": "https://github.com/user/repo/releases/download/v1.1.0/firmware.bin",
        "checksum": "abc123def456",
        "size": 1048576
    })";

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, mockJson);

  TEST_ASSERT_FALSE_MESSAGE(error, "JSON parse failed");

  const char *version = doc["version"];
  const char *firmwareUrl = doc["firmware_url"];
  size_t size = doc["size"];

  TEST_ASSERT_EQUAL_STRING("1.1.0", version);
  TEST_ASSERT_NOT_NULL(firmwareUrl);
  TEST_ASSERT_EQUAL(1048576, size);
}

void test_parse_minimal_json(void) {
  String mockJson =
      R"({"version":"2.0.0","firmware_url":"https://example.com/fw.bin"})";

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, mockJson);

  TEST_ASSERT_FALSE(error);

  const char *version = doc["version"];
  TEST_ASSERT_EQUAL_STRING("2.0.0", version);
}

void test_parse_invalid_json(void) {
  String badJson = "{ this is not valid json }";

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, badJson);

  TEST_ASSERT_TRUE(error); // Should fail
}

// =============================================================================
// UPDATE DECISION LOGIC TESTS
// =============================================================================

void test_update_decision_logic(void) {
  String currentVersion = "1.0.0";
  String availableVersion = "1.1.0";
  String minVersion = "0.9.0";

  // Should update: available > current
  int cmp = compareVersions(availableVersion, currentVersion);
  TEST_ASSERT_GREATER_THAN(0, cmp);

  // Should be compatible: current >= min_version
  int minCmp = compareVersions(currentVersion, minVersion);
  TEST_ASSERT_GREATER_OR_EQUAL(0, minCmp);

  // Decision: Update available and compatible
  bool shouldUpdate = (cmp > 0) && (minCmp >= 0);
  TEST_ASSERT_TRUE(shouldUpdate);
}

void test_update_skip_when_current(void) {
  String currentVersion = "1.1.0";
  String availableVersion = "1.1.0";

  int cmp = compareVersions(availableVersion, currentVersion);
  TEST_ASSERT_EQUAL(0, cmp);

  bool shouldUpdate = (cmp > 0);
  TEST_ASSERT_FALSE(shouldUpdate);
}

void test_update_skip_when_newer(void) {
  String currentVersion = "1.2.0";
  String availableVersion = "1.1.0";

  int cmp = compareVersions(availableVersion, currentVersion);
  TEST_ASSERT_LESS_THAN(0, cmp);

  bool shouldUpdate = (cmp > 0);
  TEST_ASSERT_FALSE(shouldUpdate);
}

void test_update_blocked_by_min_version(void) {
  String currentVersion = "0.5.0";
  String availableVersion = "2.0.0";
  String minVersion = "1.0.0";

  // Update available
  int cmp = compareVersions(availableVersion, currentVersion);
  TEST_ASSERT_GREATER_THAN(0, cmp);

  // But current < min_version (incompatible)
  int minCmp = compareVersions(currentVersion, minVersion);
  TEST_ASSERT_LESS_THAN(0, minCmp);

  // Should NOT update (incompatible)
  bool shouldUpdate = (cmp > 0) && (minCmp >= 0);
  TEST_ASSERT_FALSE(shouldUpdate);
}

// void test_https_debug(void) {
//   time_t now = time(nullptr);
//   Serial.printf("Time: %s", ctime(&now));
//
//   // Test 1: Try without any CA (insecure) - should work
//   Serial.println("\n1. Testing insecure connection...");
//   secureClient.setInsecure();
//   bool insecureWorks = secureClient.connect("raw.githubusercontent.com",
//   443); Serial.printf("   Insecure: %s\n", insecureWorks ? "OK" : "FAILED");
//   secureClient.stop();
//
//   // Test 2: Try with ISRG Root X1
//   Serial.println("\n2. Testing with ISRG Root X1...");
//   secureClient.setCACert(ISRG_ROOT_X1);
//   bool isrgWorks = secureClient.connect("raw.githubusercontent.com", 443);
//   Serial.printf("   ISRG X1: %s\n", isrgWorks ? "OK" : "FAILED");
//   secureClient.stop();
//
//   // If insecure works but CA doesn't, it's a cert issue
//   if (insecureWorks && !isrgWorks) {
//     Serial.println("\n>>> CA cert mismatch - GitHub may use different CA");
//   }
//
//   TEST_ASSERT_TRUE(insecureWorks);
// }

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  delay(2000);
  Serial.begin(115200);
  Serial.println("\n\n=== OTA TESTS ===\n");

  connectWiFi();
  syncTime();

  UNITY_BEGIN();

  // Version comparison tests
  Serial.println("\n--- Version Comparison ---");
  RUN_TEST(test_version_compare_equal);
  RUN_TEST(test_version_compare_major_greater);
  RUN_TEST(test_version_compare_major_less);
  RUN_TEST(test_version_compare_minor_greater);
  RUN_TEST(test_version_compare_minor_less);
  RUN_TEST(test_version_compare_patch_greater);
  RUN_TEST(test_version_compare_patch_less);
  RUN_TEST(test_version_compare_complex);

  // Update system tests
  Serial.println("\n--- Update System ---");
  RUN_TEST(test_update_partition_exists);
  RUN_TEST(test_update_has_enough_space);
  RUN_TEST(test_update_begin_end);
  RUN_TEST(test_running_partition_valid);

  // HTTPS tests
  Serial.println("\n--- HTTPS Connections ---");
  // RUN_TEST(test_https_debug);
  RUN_TEST(test_https_github_with_correct_ca);
  RUN_TEST(test_https_fetch_from_github);
  // RUN_TEST(test_https_connection_with_ca);
  // RUN_TEST(test_https_github_connection);
  // RUN_TEST(test_https_fetch_small_file);
  //
  // JSON parsing tests
  Serial.println("\n--- JSON Parsing ---");
  RUN_TEST(test_parse_version_json);
  RUN_TEST(test_parse_minimal_json);
  RUN_TEST(test_parse_invalid_json);

  // Update decision tests
  Serial.println("\n--- Update Decision Logic ---");
  RUN_TEST(test_update_decision_logic);
  RUN_TEST(test_update_skip_when_current);
  RUN_TEST(test_update_skip_when_newer);
  RUN_TEST(test_update_blocked_by_min_version);

  UNITY_END();
}

void loop() {}
