#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "AxFrameDecoder.h"
#include "console_capture.h"

using namespace klimalogg;

void setUp() {}
void tearDown() {}

static std::vector<uint8_t> captureBits() {
  std::vector<uint8_t> bits;
  for (size_t i = 0; i < kConsoleCaptureBits; i++) {
    const char c = kConsoleCaptureHex[i / 4];
    const int nibble = c <= '9' ? c - '0' : c - 'a' + 10;
    bits.push_back((nibble >> (3 - i % 4)) & 1);
  }
  return bits;
}

// Feeds bits; returns the frames that completed.
static std::vector<std::vector<uint8_t>> run(AxFrameDecoder& d,
                                             const std::vector<uint8_t>& bits) {
  std::vector<std::vector<uint8_t>> frames;
  for (uint8_t b : bits) {
    if (d.feedBit(b)) {
      frames.emplace_back(d.frame(), d.frame() + d.size());
    }
  }
  return frames;
}

// ---- encoder for synthetic tests: bytes -> HDLC -> scramble -> NRZI ----

struct Encoder {
  std::vector<uint8_t> out;
  uint32_t hist = 0;  // scrambler output history
  uint8_t level = 0;

  void airBit(uint8_t u) {
    const uint8_t s = u ^ ((hist >> 11) & 1) ^ ((hist >> 16) & 1);
    hist = ((hist << 1) | s) & 0x1FFFF;
    if (s == 0) level ^= 1;  // NRZI: 0 = change
    out.push_back(level);
  }
  void rawByte(uint8_t b) {  // no stuffing (flags, preamble)
    for (int i = 0; i < 8; i++) airBit((b >> i) & 1);
  }
  void stuffedBytes(const std::vector<uint8_t>& data) {
    int ones = 0;
    for (uint8_t b : data) {
      for (int i = 0; i < 8; i++) {
        const uint8_t bit = (b >> i) & 1;
        airBit(bit);
        ones = bit ? ones + 1 : 0;
        if (ones == 5) {
          airBit(0);
          ones = 0;
        }
      }
    }
  }
  void frame(std::vector<uint8_t> data) {
    const uint16_t fcs = crcX25(data.data(), data.size());
    data.push_back(fcs & 0xFF);
    data.push_back(fcs >> 8);
    for (int i = 0; i < 6; i++) rawByte(0xAA);
    rawByte(0x7E);
    stuffedBytes(data);
    rawByte(0x7E);
    rawByte(0xAA);
  }
};

void test_crc_x25_check_value() {
  const char* s = "123456789";
  TEST_ASSERT_EQUAL_HEX16(0x906E, crcX25((const uint8_t*)s, strlen(s)));
}

void test_decodes_real_console_capture() {
  AxFrameDecoder d;
  auto frames = run(d, captureBits());
  TEST_ASSERT_EQUAL(1, frames.size());
  const auto& f = frames[0];
  TEST_ASSERT_EQUAL(229, f.size());
  TEST_ASSERT_EQUAL_HEX8(0x01, f[0]);  // DeviceID 01B2
  TEST_ASSERT_EQUAL_HEX8(0xB2, f[1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, f[2]);  // logger 0
  TEST_ASSERT_EQUAL_HEX8(0x30, f[3]);  // current weather
}

void test_capture_decodes_with_inverted_polarity() {
  auto bits = captureBits();
  for (auto& b : bits) b ^= 1;
  AxFrameDecoder d;
  TEST_ASSERT_EQUAL(1, run(d, bits).size());
}

void test_bit_error_fails_crc() {
  auto bits = captureBits();
  bits[1200] ^= 1;
  AxFrameDecoder d;
  TEST_ASSERT_EQUAL(0, run(d, bits).size());
  TEST_ASSERT_GREATER_THAN(0, d.crcErrors());
}

void test_round_trip_with_stuffing() {
  // 0xFF bytes force bit stuffing; 0x7E inside data must survive.
  const std::vector<uint8_t> data = {0x01, 0xB2, 0x00, 0xFF, 0xFF,
                                     0x7E, 0x3F, 0xF8, 0x00, 0x7E};
  Encoder e;
  e.frame(data);
  AxFrameDecoder d;
  auto frames = run(d, e.out);
  TEST_ASSERT_EQUAL(1, frames.size());
  TEST_ASSERT_EQUAL(data.size(), frames[0].size());
  TEST_ASSERT_EQUAL_MEMORY(data.data(), frames[0].data(), data.size());
}

void test_two_frames_back_to_back() {
  Encoder e;
  e.frame({0x01, 0xB2, 0x00, 0x30});
  e.frame({0x01, 0xB2, 0x00, 0x40, 0x55});
  AxFrameDecoder d;
  auto frames = run(d, e.out);
  TEST_ASSERT_EQUAL(2, frames.size());
  TEST_ASSERT_EQUAL(4, frames[0].size());
  TEST_ASSERT_EQUAL(5, frames[1].size());
}

void test_oversized_frame_is_dropped() {
  Encoder e;
  e.frame(std::vector<uint8_t>(AxFrameDecoder::kMaxFrameBytes + 10, 0x12));
  e.frame({0x01, 0xB2, 0x00, 0x30});
  AxFrameDecoder d;
  auto frames = run(d, e.out);
  TEST_ASSERT_EQUAL(1, frames.size());  // only the small one
  TEST_ASSERT_EQUAL(4, frames[0].size());
}

void test_noise_gives_no_frames() {
  std::vector<uint8_t> bits;
  uint32_t x = 12345;
  for (int i = 0; i < 200000; i++) {
    x = x * 1103515245u + 12345u;
    bits.push_back((x >> 16) & 1);
  }
  AxFrameDecoder d;
  TEST_ASSERT_EQUAL(0, run(d, bits).size());
}

// ---- library encoder ----

static std::vector<uint8_t> unpack(const uint8_t* packed, size_t bits) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < bits; i++) out.push_back((packed[i / 8] >> (7 - i % 8)) & 1);
  return out;
}

void test_encoder_round_trip() {
  const std::vector<uint8_t> data = {0x0B, 0xAD, 0x00, 0x30, 0xFF, 0xFF, 0x7E,
                                     0x00, 0x80, 0x12, 0x34};
  uint8_t packed[256];
  const size_t bits = encodeAxFrame(data.data(), data.size(), packed,
                                    sizeof(packed) * 8);
  TEST_ASSERT_GREATER_THAN(0, bits);
  AxFrameDecoder d;
  auto frames = run(d, unpack(packed, bits));
  TEST_ASSERT_EQUAL(1, frames.size());
  TEST_ASSERT_EQUAL_MEMORY(data.data(), frames[0].data(), data.size());
}

void test_encoder_matches_capture_frame() {
  // Re-encode the real console frame; decoding must give it back.
  AxFrameDecoder d;
  auto frames = run(d, captureBits());
  TEST_ASSERT_EQUAL(1, frames.size());
  static uint8_t packed[600];
  const size_t bits = encodeAxFrame(frames[0].data(), frames[0].size(), packed,
                                    sizeof(packed) * 8);
  AxFrameDecoder d2;
  auto again = run(d2, unpack(packed, bits));
  TEST_ASSERT_EQUAL(1, again.size());
  TEST_ASSERT_EQUAL_MEMORY(frames[0].data(), again[0].data(), frames[0].size());
}

void test_encoder_rejects_small_buffer() {
  const uint8_t data[20] = {};
  uint8_t packed[16];
  TEST_ASSERT_EQUAL(0, encodeAxFrame(data, sizeof(data), packed, sizeof(packed) * 8));
}

void test_encoder_rejects_oversized_frame() {
  static uint8_t data[AxFrameDecoder::kMaxFrameBytes + 1];
  static uint8_t packed[1024];
  TEST_ASSERT_EQUAL(0, encodeAxFrame(data, sizeof(data), packed, sizeof(packed) * 8));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc_x25_check_value);
  RUN_TEST(test_decodes_real_console_capture);
  RUN_TEST(test_capture_decodes_with_inverted_polarity);
  RUN_TEST(test_bit_error_fails_crc);
  RUN_TEST(test_round_trip_with_stuffing);
  RUN_TEST(test_two_frames_back_to_back);
  RUN_TEST(test_oversized_frame_is_dropped);
  RUN_TEST(test_noise_gives_no_frames);
  RUN_TEST(test_encoder_round_trip);
  RUN_TEST(test_encoder_matches_capture_frame);
  RUN_TEST(test_encoder_rejects_small_buffer);
  RUN_TEST(test_encoder_rejects_oversized_frame);
  return UNITY_END();
}
