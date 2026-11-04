#include <unity.h>

#include <vector>

#include "ConsoleProtocol.h"
#include "console_frames.h"

using namespace klimalogg::console;

void setUp() {}
void tearDown() {}

static std::vector<uint8_t> bytes(const char* hex) {
  std::vector<uint8_t> out;
  for (const char* p = hex; p[0] && p[1]; p += 2) {
    auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    out.push_back((uint8_t)(nib(p[0]) << 4 | nib(p[1])));
  }
  return out;
}

static void assertChannel(const Measurement& m, float t, int h) {
  TEST_ASSERT_TRUE(m.hasTemperature);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, t, m.temperatureC);
  TEST_ASSERT_TRUE(m.hasHumidity);
  TEST_ASSERT_EQUAL(h, m.humidity);
}

static void assertEmpty(const Measurement& m) {
  TEST_ASSERT_FALSE(m.hasTemperature);
  TEST_ASSERT_FALSE(m.hasHumidity);
}

// ---- header ----

void test_header() {
  auto f = bytes(kCurrentHex);
  Header h;
  TEST_ASSERT_TRUE(parseHeader(f.data(), f.size(), &h));
  TEST_ASSERT_EQUAL_HEX16(0x01B2, h.deviceId);
  TEST_ASSERT_EQUAL(0, h.loggerId);
  TEST_ASSERT_EQUAL_HEX8(kTypeCurrentWeather, h.type);
  TEST_ASSERT_EQUAL(100, h.quality);
  TEST_ASSERT_EQUAL_HEX16(0x14D1, h.configChecksum);
}

void test_header_rejects_short_frame() {
  const uint8_t f[6] = {0x01, 0xB2, 0, 0x30, 0x64, 0x14};
  Header h;
  TEST_ASSERT_FALSE(parseHeader(f, sizeof(f), &h));
}

// ---- current weather ----

void test_current_weather() {
  auto f = bytes(kCurrentHex);
  CurrentWeather cw;
  TEST_ASSERT_TRUE(parseCurrentWeather(f.data(), f.size(), &cw));
  assertChannel(cw.channel[0], 19.9f, 51);  // console's own sensor
  assertChannel(cw.channel[1], 19.0f, 60);
  assertEmpty(cw.channel[2]);  // sensor 3304, battery dead
  assertChannel(cw.channel[3], 19.8f, 54);
  assertChannel(cw.channel[4], 19.7f, 55);
  for (int i = 5; i < 9; i++) assertEmpty(cw.channel[i]);
}

void test_current_weather_rejects_wrong_type_or_length() {
  auto f = bytes(kCurrentHex);
  CurrentWeather cw;
  TEST_ASSERT_FALSE(parseCurrentWeather(f.data(), f.size() - 1, &cw));
  auto h = bytes(kHistoryHex);
  TEST_ASSERT_FALSE(parseCurrentWeather(h.data(), h.size(), &cw));
}

void test_negative_temperature() {
  auto f = bytes(kCurrentHex);
  // ch0 temperature: 3 BCD nibbles from the low nibble of byte 29 (+40 C).
  f[29] = (uint8_t)((f[29] & 0xF0) | 0x3);
  f[30] = 0x45;  // 34.5 - 40 = -5.5
  CurrentWeather cw;
  TEST_ASSERT_TRUE(parseCurrentWeather(f.data(), f.size(), &cw));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.5f, cw.channel[0].temperatureC);
}

// ---- config: channel -> sensor ID ----

void test_config_sensor_ids() {
  auto f = bytes(kConfigHex);
  ConsoleConfig cfg;
  TEST_ASSERT_TRUE(parseConfig(f.data(), f.size(), &cfg));
  TEST_ASSERT_EQUAL_HEX16(0x19CA, cfg.sensorId[1]);
  TEST_ASSERT_EQUAL_HEX16(0x3304, cfg.sensorId[2]);
  TEST_ASSERT_EQUAL_HEX16(0x1D21, cfg.sensorId[3]);
  TEST_ASSERT_EQUAL_HEX16(0x26C7, cfg.sensorId[4]);
  for (int i = 5; i < 9; i++) TEST_ASSERT_EQUAL_HEX16(kNoSensor, cfg.sensorId[i]);
  TEST_ASSERT_EQUAL_HEX16(kNoSensor, cfg.sensorId[0]);  // the console itself
  TEST_ASSERT_EQUAL_HEX16(0x14D1, cfg.checksum);
}

// ---- history ----

void test_history_frame() {
  auto f = bytes(kHistoryHex);
  HistoryFrame hf;
  TEST_ASSERT_TRUE(parseHistory(f.data(), f.size(), &hf));
  TEST_ASSERT_EQUAL(38275, hf.latestIndex);
  TEST_ASSERT_EQUAL(38406, hf.thisIndex);
  for (int i = 0; i < kRecordsPerFrame; i++) {
    const HistoryRecord& r = hf.record[i];
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL(2025, r.time.year);
    TEST_ASSERT_EQUAL(12, r.time.month);
    TEST_ASSERT_EQUAL(4, r.time.day);
    TEST_ASSERT_EQUAL(5, r.time.hour);
    TEST_ASSERT_EQUAL(1 + i, r.time.minute);  // oldest first
  }
  assertChannel(hf.record[0].channel[0], 17.7f, 51);
  assertChannel(hf.record[0].channel[1], 16.6f, 56);
  assertEmpty(hf.record[0].channel[2]);
  assertChannel(hf.record[0].channel[3], 15.9f, 58);
  assertChannel(hf.record[0].channel[4], 12.8f, 71);
  assertChannel(hf.record[1].channel[3], 16.0f, 58);
  assertChannel(hf.record[3].channel[4], 12.7f, 71);
}

void test_history_alarm_record_is_not_valid() {
  auto f = bytes(kHistoryHex);
  f[180] = 0xEE;  // marks record 1 as an alarm record
  HistoryFrame hf;
  TEST_ASSERT_TRUE(parseHistory(f.data(), f.size(), &hf));
  TEST_ASSERT_FALSE(hf.record[0].valid);
  TEST_ASSERT_TRUE(hf.record[1].valid);
}

void test_history_bad_date_is_not_valid() {
  auto f = bytes(kHistoryHex);
  f[176] = 0xAA;  // year nibbles not BCD
  HistoryFrame hf;
  TEST_ASSERT_TRUE(parseHistory(f.data(), f.size(), &hf));
  TEST_ASSERT_FALSE(hf.record[0].valid);
}

// ---- history index <-> address ----

void test_history_address() {
  TEST_ASSERT_EQUAL_HEX32(0x070000, historyAddress(0));
  TEST_ASSERT_EQUAL_HEX32(0x19B060, historyAddress(38275));
  TEST_ASSERT_EQUAL(38275, historyIndex(0x19B060));
  TEST_ASSERT_EQUAL(kNoIndex, historyIndex(0x06FFFF));  // below range
  TEST_ASSERT_EQUAL(kNoIndex, historyIndex(historyAddress(kHistorySlots)));
  TEST_ASSERT_EQUAL(kNoIndex, historyIndex(0xFFFFFF));  // "unknown"
}

// ---- ACK ----

void test_ack_frame() {
  Header h{0x01B2, 0, kTypeCurrentWeather, 100, 0x14D1};
  uint8_t out[kAckBytes];
  TEST_ASSERT_EQUAL(kAckBytes, buildAck(h, Action::GetHistory, 8, 38275, out));
  const uint8_t expected[kAckBytes] = {0x01, 0xB2, 0x00, 0x00, 0x14, 0xD1,
                                       0x80, 0x08, 0x19, 0xB0, 0x60};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, kAckBytes);
}

void test_ack_unknown_history_index() {
  Header h{0x01B2, 0, kTypeCurrentWeather, 100, 0x14D1};
  uint8_t out[kAckBytes];
  buildAck(h, Action::GetConfig, 8, kNoIndex, out);
  TEST_ASSERT_EQUAL_HEX8(0x03, out[3]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[8]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[9]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[10]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_header);
  RUN_TEST(test_header_rejects_short_frame);
  RUN_TEST(test_current_weather);
  RUN_TEST(test_current_weather_rejects_wrong_type_or_length);
  RUN_TEST(test_negative_temperature);
  RUN_TEST(test_config_sensor_ids);
  RUN_TEST(test_history_frame);
  RUN_TEST(test_history_alarm_record_is_not_valid);
  RUN_TEST(test_history_bad_date_is_not_valid);
  RUN_TEST(test_history_address);
  RUN_TEST(test_ack_frame);
  RUN_TEST(test_ack_unknown_history_index);
  return UNITY_END();
}
