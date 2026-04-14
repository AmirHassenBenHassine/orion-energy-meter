#include <cstring>
#include <string>
#include <unity.h>

class String {
public:
  std::string _str;
  String() {}
  String(const char *s) : _str(s) {}
  String(const std::string &s) : _str(s) {}
  const char *c_str() const { return _str.c_str(); }
  size_t length() const { return _str.length(); }
  String substring(size_t from, size_t to = std::string::npos) const {
    return String(_str.substr(from, to - from));
  }
  bool operator==(const String &other) const { return _str == other._str; }
  String operator+(const String &other) const {
    return String(_str + other._str);
  }
};

String generateDeviceId(uint8_t mac[6]) {
  char id[18];
  snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  return String(id);
}

void test_device_id_format(void) {
  uint8_t mac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  String id = generateDeviceId(mac);

  TEST_ASSERT_EQUAL(12, id.length());
  TEST_ASSERT_EQUAL_STRING("AABBCCDDEEFF", id.c_str());
}

void test_device_id_all_zeros(void) {
  uint8_t mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  String id = generateDeviceId(mac);

  TEST_ASSERT_EQUAL_STRING("000000000000", id.c_str());
}

void test_device_id_max_values(void) {
  uint8_t mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  String id = generateDeviceId(mac);

  TEST_ASSERT_EQUAL_STRING("FFFFFFFFFFFF", id.c_str());
}

float calculateBatteryPercentage(float voltage, float minV, float maxV) {
  if (voltage <= minV)
    return 0.0f;
  if (voltage >= maxV)
    return 100.0f;
  return ((voltage - minV) / (maxV - minV)) * 100.0f;
}

void test_battery_percentage_full(void) {
  float percent = calculateBatteryPercentage(4.2f, 3.0f, 4.2f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, percent);
}

void test_battery_percentage_empty(void) {
  float percent = calculateBatteryPercentage(3.0f, 3.0f, 4.2f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, percent);
}

void test_battery_percentage_half(void) {
  float percent = calculateBatteryPercentage(3.6f, 3.0f, 4.2f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, percent);
}

void test_battery_percentage_below_min(void) {
  float percent = calculateBatteryPercentage(2.5f, 3.0f, 4.2f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, percent);
}

void test_battery_percentage_above_max(void) {
  float percent = calculateBatteryPercentage(4.5f, 3.0f, 4.2f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, percent);
}

struct EnergyReading {
  float voltage;
  float current[3];
  float power[3];
  float totalPower;
};

float calculatePower(float voltage, float current) { return voltage * current; }

float calculateTotalPower(float power[], int phases) {
  float total = 0;
  for (int i = 0; i < phases; i++) {
    total += power[i];
  }
  return total;
}

float calculateEnergy(float powerWatts, float hoursElapsed) {
  return (powerWatts * hoursElapsed) / 1000.0f; // kWh
}

void test_power_calculation(void) {
  float power = calculatePower(230.0f, 10.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2300.0f, power);
}

void test_power_calculation_zero_current(void) {
  float power = calculatePower(230.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, power);
}

void test_total_power_three_phases(void) {
  float powers[] = {1000.0f, 1500.0f, 2000.0f};
  float total = calculateTotalPower(powers, 3);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 4500.0f, total);
}

void test_energy_calculation_one_hour(void) {
  float energy = calculateEnergy(1000.0f, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, energy);
}

void test_energy_calculation_30_seconds(void) {
  float energy = calculateEnergy(1000.0f, 30.0f / 3600.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.00833f, energy);
}

float filterCurrent(float current, float threshold) {
  if (current < threshold)
    return 0.0f;
  if (current > 100.0f)
    return 100.0f; // Max clamp
  return current;
}

void test_current_below_threshold(void) {
  float filtered = filterCurrent(0.05f, 0.1f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, filtered);
}

void test_current_above_threshold(void) {
  float filtered = filterCurrent(0.5f, 0.1f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, filtered);
}

void test_current_at_threshold(void) {
  float filtered = filterCurrent(0.1f, 0.1f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, filtered);
}

void test_current_max_clamp(void) {
  float filtered = filterCurrent(150.0f, 0.1f);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, filtered);
}

bool isValidTopic(const char *topic) {
  if (topic == nullptr || strlen(topic) == 0)
    return false;
  if (strlen(topic) > 128)
    return false;
  // No wildcards in publish topics
  if (strchr(topic, '#') != nullptr)
    return false;
  if (strchr(topic, '+') != nullptr)
    return false;
  return true;
}

void test_valid_topic(void) {
  TEST_ASSERT_TRUE(isValidTopic("energy/metrics"));
  TEST_ASSERT_TRUE(isValidTopic("orion/status"));
}

void test_invalid_topic_null(void) { TEST_ASSERT_FALSE(isValidTopic(nullptr)); }

void test_invalid_topic_empty(void) { TEST_ASSERT_FALSE(isValidTopic("")); }

void test_invalid_topic_wildcard(void) {
  TEST_ASSERT_FALSE(isValidTopic("energy/#"));
  TEST_ASSERT_FALSE(isValidTopic("orion/+/status"));
}

bool isValidJsonNumber(float value) {
  if (value != value)
    return false; // NaN check
  if (value > 1e38 || value < -1e38)
    return false; // Inf check
  return true;
}

void test_valid_json_number(void) {
  TEST_ASSERT_TRUE(isValidJsonNumber(123.45f));
  TEST_ASSERT_TRUE(isValidJsonNumber(0.0f));
  TEST_ASSERT_TRUE(isValidJsonNumber(-50.0f));
}

void test_invalid_json_nan(void) {
  float nan = 0.0f / 0.0f;
  TEST_ASSERT_FALSE(isValidJsonNumber(nan));
}

void setUp(void) {}

void tearDown(void) {}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_device_id_format);
  RUN_TEST(test_device_id_all_zeros);
  RUN_TEST(test_device_id_max_values);

  RUN_TEST(test_battery_percentage_full);
  RUN_TEST(test_battery_percentage_empty);
  RUN_TEST(test_battery_percentage_half);
  RUN_TEST(test_battery_percentage_below_min);
  RUN_TEST(test_battery_percentage_above_max);

  RUN_TEST(test_power_calculation);
  RUN_TEST(test_power_calculation_zero_current);
  RUN_TEST(test_total_power_three_phases);
  RUN_TEST(test_energy_calculation_one_hour);
  RUN_TEST(test_energy_calculation_30_seconds);

  RUN_TEST(test_current_below_threshold);
  RUN_TEST(test_current_above_threshold);
  RUN_TEST(test_current_at_threshold);
  RUN_TEST(test_current_max_clamp);

  RUN_TEST(test_valid_topic);
  RUN_TEST(test_invalid_topic_null);
  RUN_TEST(test_invalid_topic_empty);
  RUN_TEST(test_invalid_topic_wildcard);

  RUN_TEST(test_valid_json_number);
  RUN_TEST(test_invalid_json_nan);

  return UNITY_END();
}
