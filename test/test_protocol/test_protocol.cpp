#include <string.h>
#include <unity.h>

#include "KlimaLoggProtocol.h"

using namespace klimalogg;

// Real telegrams (decoded domain, incl. sync) from tfrec README and
// rtl_433 issue #2967.
static const uint8_t kFrame22C[11] = {0x2D, 0xD4, 0x65, 0xB0, 0x86, 0x20,
                                      0x23, 0x60, 0xE0, 0x56, 0x97};
static const uint8_t kFrameMinus1C[11] = {0x2D, 0xD4, 0x21, 0x5C, 0x83, 0x88,
                                          0x6A, 0x60, 0x00, 0x56, 0x58};

void setUp() {}
void tearDown() {}

void test_crc8_matches_real_telegrams() {
  TEST_ASSERT_EQUAL_HEX8(0x97, crc8(kFrame22C + 2, 8));
  TEST_ASSERT_EQUAL_HEX8(0x58, crc8(kFrameMinus1C + 2, 8));
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
  RUN_TEST(test_parse_rejects_bad_crc);
  RUN_TEST(test_parse_rejects_wrong_type_byte);
  RUN_TEST(test_parse_rejects_non_bcd_temperature);
  RUN_TEST(test_parse_rejects_short_buffer);
  RUN_TEST(test_parse_reports_low_battery);
  RUN_TEST(test_id_ignores_top_bit);
  return UNITY_END();
}
