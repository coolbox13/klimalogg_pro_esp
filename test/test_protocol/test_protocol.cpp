#include <string.h>
#include <unity.h>

#include "KlimaLoggProtocol.h"

using namespace klimalogg;

// Real telegrams (decoded domain, incl. sync) from tfrec README and
// rtl_433 issue #2967.
static const uint8_t kFrame22C[11] = {0x2D, 0xD4, 0x65, 0xB0, 0x86, 0x20,
                                      0x23, 0x60, 0xE0, 0x56, 0x97};
static const uint8_t kFrameMinus1_2C[11] = {0x2D, 0xD4, 0x21, 0x5C, 0x83, 0x88,
                                          0x6A, 0x60, 0x00, 0x56, 0x58};

void setUp() {}
void tearDown() {}

void test_crc8_matches_real_telegrams() {
  TEST_ASSERT_EQUAL_HEX8(0x97, crc8(kFrame22C + 2, 8));
  TEST_ASSERT_EQUAL_HEX8(0x58, crc8(kFrameMinus1_2C + 2, 8));
}

void test_parse_decodes_all_fields_positive() {
  DecodeResult r = parsePayload(kFrame22C + 2, kPayloadBytes);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_EQUAL_HEX16(0x65B0, r.reading.id);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, r.reading.temperatureC);
  TEST_ASSERT_TRUE(r.reading.hasHumidity);
  TEST_ASSERT_EQUAL_UINT8(35, r.reading.humidity);
  TEST_ASSERT_FALSE(r.reading.batteryLow);
  TEST_ASSERT_EQUAL_UINT8(14, r.reading.sequence);  // 0xE0
  TEST_ASSERT_EQUAL_UINT8(8, r.reading.learning);   // 0x86: learned
}

void test_parse_decodes_negative_without_humidity() {
  DecodeResult r = parsePayload(kFrameMinus1_2C + 2, kPayloadBytes);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_EQUAL_HEX16(0x215C, r.reading.id);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1.2f, r.reading.temperatureC);
  TEST_ASSERT_FALSE(r.reading.hasHumidity);  // 0x6A
  TEST_ASSERT_EQUAL_UINT8(0, r.reading.humidity);
  TEST_ASSERT_FALSE(r.reading.batteryLow);
  TEST_ASSERT_EQUAL_UINT8(0, r.reading.sequence);
}

void test_parse_exposes_learning_sequence() {
  uint8_t p[kPayloadBytes];
  memcpy(p, kFrame22C + 2, kPayloadBytes);
  p[2] = (uint8_t)((3 << 4) | (p[2] & 0x0F));  // still pairing, step 3
  p[8] = crc8(p, 8);
  DecodeResult r = parsePayload(p, kPayloadBytes);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_EQUAL_UINT8(3, r.reading.learning);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, r.reading.temperatureC);
}

void test_parse_rejects_bad_crc() {
  uint8_t p[kPayloadBytes];
  memcpy(p, kFrame22C + 2, kPayloadBytes);
  p[8] ^= 0x01;
  TEST_ASSERT_EQUAL(Status::BadCrc, parsePayload(p, kPayloadBytes).status);
}

void test_parse_rejects_wrong_type_byte() {
  uint8_t p[kPayloadBytes];
  memcpy(p, kFrame22C + 2, kPayloadBytes);
  p[7] = 0x57;
  p[8] = crc8(p, 8);
  TEST_ASSERT_EQUAL(Status::BadType, parsePayload(p, kPayloadBytes).status);
}

void test_parse_rejects_non_bcd_temperature() {
  uint8_t p[kPayloadBytes];
  memcpy(p, kFrame22C + 2, kPayloadBytes);
  p[3] = 0x2A;  // nibble 0xA is not BCD
  p[8] = crc8(p, 8);
  TEST_ASSERT_EQUAL(Status::BadTemperature,
                    parsePayload(p, kPayloadBytes).status);
}

void test_parse_rejects_short_buffer() {
  TEST_ASSERT_EQUAL(Status::ShortBuffer,
                    parsePayload(kFrame22C + 2, kPayloadBytes - 1).status);
}

void test_parse_reports_low_battery() {
  uint8_t p[kPayloadBytes];
  memcpy(p, kFrame22C + 2, kPayloadBytes);
  p[5] |= 0x80;
  p[8] = crc8(p, 8);
  DecodeResult r = parsePayload(p, kPayloadBytes);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_TRUE(r.reading.batteryLow);
}

void test_id_ignores_top_bit() {
  uint8_t p[kPayloadBytes];
  memcpy(p, kFrame22C + 2, kPayloadBytes);
  p[0] |= 0x80;
  p[8] = crc8(p, 8);
  DecodeResult r = parsePayload(p, kPayloadBytes);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_EQUAL_HEX16(0x65B0, r.reading.id);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc8_matches_real_telegrams);
  RUN_TEST(test_parse_decodes_all_fields_positive);
  RUN_TEST(test_parse_decodes_negative_without_humidity);
  RUN_TEST(test_parse_exposes_learning_sequence);
  RUN_TEST(test_parse_rejects_bad_crc);
  RUN_TEST(test_parse_rejects_wrong_type_byte);
  RUN_TEST(test_parse_rejects_non_bcd_temperature);
  RUN_TEST(test_parse_rejects_short_buffer);
  RUN_TEST(test_parse_reports_low_battery);
  RUN_TEST(test_id_ignores_top_bit);
  return UNITY_END();
}
