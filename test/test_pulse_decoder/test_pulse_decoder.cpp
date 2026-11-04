#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "PulseDecoder.h"

using namespace klimalogg;

void setUp() {}
void tearDown() {}

// Real capture of sensor 1D21 (20.6 C, 55 %) from rtl_433 -A at 868.3 MHz.
// One digit per carrier-on pulse: index into kFixturePulseUs. Every gap is
// 12 us except the last, which is the end of the transmission.
static const uint32_t kFixturePulseUs[] = {64, 38, 12, 88};
static const uint32_t kFixtureGapUs = 12;
static const char kFixture[] =
    "012222222222222222222222222222222222222222222222222222222222"
    "222222222222222222222222222222222222222222222222222222222222"
    "222222222222222222222222222222222222222222221012221133221222"
    "122022210222230222222022222221011121322222222222222222222";

// Decoded telegram (sync + payload) from tfrec README: id 65B0, 22.0 C, 35 %.
static const uint8_t kFrame22C[11] = {0x2D, 0xD4, 0x65, 0xB0, 0x86, 0x20,
                                      0x23, 0x60, 0xE0, 0x56, 0x97};

struct Collector {
  int frames = 0;
  int valid = 0;
  DecodeResult last = {};

  void take(const PulseDecoder& d) {
    frames++;
    last = d.result();
    if (last.status == Status::Ok) valid++;
  }
};

static void feed(PulseDecoder& d, Collector& c, bool high, uint32_t us) {
  if (d.feedLevel(high, us)) c.take(d);
}

static void flush(PulseDecoder& d, Collector& c) {
  if (d.endOfTransmission()) c.take(d);
}

static void feedFixture(PulseDecoder& d, Collector& c) {
  const size_t n = strlen(kFixture);
  for (size_t i = 0; i < n; i++) {
    feed(d, c, true, kFixturePulseUs[kFixture[i] - '0']);
    if (i + 1 < n) feed(d, c, false, kFixtureGapUs);
  }
  flush(d, c);
}

// Bit b on air: 13 us carrier on, then 13 us on (b=1) or off (b=0).
// Bytes go LSB first. `jitter` adds +-jitter us to every level.
static void feedSynthetic(PulseDecoder& d, Collector& c, const uint8_t* bytes,
                          size_t len, int preambleZeros, int jitter) {
  bool level = false;
  uint32_t run = 0;
  auto emit = [&](bool chip) {
    if (chip != level && run > 0) {
      int j = jitter ? (rand() % (2 * jitter + 1)) - jitter : 0;
      feed(d, c, level, (uint32_t)((int)run + j));
      run = 0;
    }
    level = chip;
    run += 13;
  };
  for (int i = 0; i < preambleZeros; i++) {
    emit(true);
    emit(false);
  }
  for (size_t i = 0; i < len * 8; i++) {
    uint8_t bit = (bytes[i / 8] >> (i % 8)) & 1;
    emit(true);
    emit(bit != 0);
  }
  // Trailing zeros like the real sensor, then silence.
  for (int i = 0; i < 8; i++) {
    emit(true);
    emit(false);
  }
  feed(d, c, level, run);
  flush(d, c);
}

void test_decodes_real_rtl433_capture() {
  PulseDecoder d;
  Collector c;
  feedFixture(d, c);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL(Status::Ok, c.last.status);
  TEST_ASSERT_EQUAL_HEX16(0x1D21, c.last.reading.id);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.6f, c.last.reading.temperatureC);
  TEST_ASSERT_TRUE(c.last.reading.hasHumidity);
  TEST_ASSERT_EQUAL_UINT8(55, c.last.reading.humidity);
  TEST_ASSERT_FALSE(c.last.reading.batteryLow);
}

void test_decodes_synthetic_frame() {
  PulseDecoder d;
  Collector c;
  feedSynthetic(d, c, kFrame22C, sizeof(kFrame22C), 40, 0);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL(Status::Ok, c.last.status);
  TEST_ASSERT_EQUAL_HEX16(0x65B0, c.last.reading.id);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.0f, c.last.reading.temperatureC);
}

void test_tolerates_timing_jitter() {
  srand(1234);
  for (int run = 0; run < 50; run++) {
    PulseDecoder d;
    Collector c;
    feedSynthetic(d, c, kFrame22C, sizeof(kFrame22C), 40, 5);
    TEST_ASSERT_EQUAL(1, c.frames);
    TEST_ASSERT_EQUAL(Status::Ok, c.last.status);
  }
}

void test_decodes_back_to_back_frames() {
  PulseDecoder d;
  Collector c;
  feedFixture(d, c);
  feedSynthetic(d, c, kFrame22C, sizeof(kFrame22C), 40, 0);
  TEST_ASSERT_EQUAL(2, c.frames);
  TEST_ASSERT_EQUAL_HEX16(0x65B0, c.last.reading.id);
}

void test_reports_bad_crc_as_completed_frame() {
  uint8_t bad[11];
  memcpy(bad, kFrame22C, sizeof(bad));
  bad[10] ^= 0x01;
  PulseDecoder d;
  Collector c;
  feedSynthetic(d, c, bad, sizeof(bad), 40, 0);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL(Status::BadCrc, c.last.status);
}

void test_short_glitches_are_merged() {
  // A 2 us dropout inside a long pulse and a 2 us spike inside a gap.
  PulseDecoder d;
  Collector c;
  const size_t n = strlen(kFixture);
  for (size_t i = 0; i < n; i++) {
    uint32_t p = kFixturePulseUs[kFixture[i] - '0'];
    if (i == 170 && p > 30) {
      feed(d, c, true, p / 2 - 1);
      feed(d, c, false, 2);
      feed(d, c, true, p - p / 2 - 1);
    } else {
      feed(d, c, true, p);
    }
    if (i + 1 == n) break;
    if (i == 180) {
      feed(d, c, false, 5);
      feed(d, c, true, 2);
      feed(d, c, false, 5);
    } else {
      feed(d, c, false, kFixtureGapUs);
    }
  }
  flush(d, c);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL(Status::Ok, c.last.status);
}

void test_truncated_frame_is_dropped_and_decoder_recovers() {
  PulseDecoder d;
  Collector c;
  const size_t cut = 200;  // inside the payload
  for (size_t i = 0; i < cut; i++) {
    feed(d, c, true, kFixturePulseUs[kFixture[i] - '0']);
    feed(d, c, false, kFixtureGapUs);
  }
  feed(d, c, false, 5000);  // carrier gone mid-frame
  TEST_ASSERT_EQUAL(0, c.frames);
  feedFixture(d, c);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL(Status::Ok, c.last.status);
}

void test_random_noise_yields_no_valid_frame() {
  srand(42);
  PulseDecoder d;
  Collector c;
  bool level = true;
  for (int i = 0; i < 200000; i++) {
    feed(d, c, level, 1 + rand() % 120);
    level = !level;
  }
  flush(d, c);
  TEST_ASSERT_EQUAL(0, c.valid);
}

void test_reset_discards_partial_frame() {
  PulseDecoder d;
  Collector c;
  for (size_t i = 0; i < 200; i++) {  // stop inside the payload
    feed(d, c, true, kFixturePulseUs[kFixture[i] - '0']);
    feed(d, c, false, kFixtureGapUs);
  }
  d.reset();
  TEST_ASSERT_FALSE(d.endOfTransmission());
  feedFixture(d, c);
  TEST_ASSERT_EQUAL(1, c.frames);
  TEST_ASSERT_EQUAL(Status::Ok, c.last.status);
}

void test_end_of_transmission_without_pulse_is_noop() {
  PulseDecoder d;
  TEST_ASSERT_FALSE(d.endOfTransmission());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_decodes_real_rtl433_capture);
  RUN_TEST(test_decodes_synthetic_frame);
  RUN_TEST(test_tolerates_timing_jitter);
  RUN_TEST(test_decodes_back_to_back_frames);
  RUN_TEST(test_reports_bad_crc_as_completed_frame);
  RUN_TEST(test_short_glitches_are_merged);
  RUN_TEST(test_truncated_frame_is_dropped_and_decoder_recovers);
  RUN_TEST(test_random_noise_yields_no_valid_frame);
  RUN_TEST(test_reset_discards_partial_frame);
  RUN_TEST(test_end_of_transmission_without_pulse_is_noop);
  return UNITY_END();
}
