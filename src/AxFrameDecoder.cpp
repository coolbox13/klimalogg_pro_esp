#include "AxFrameDecoder.h"

namespace klimalogg {

namespace {

constexpr uint16_t kCrcPolyReflected = 0x8408;
constexpr uint8_t kStuffAfterOnes = 5;
constexpr uint8_t kFlagOnes = 6;  // 0x7E = 0 111111 0
constexpr uint8_t kAbortOnes = 7;
// The flag's leading 0 and first five 1s were already collected as data.
constexpr size_t kFlagBitsCollected = 6;
constexpr size_t kFcsBytes = 2;
constexpr size_t kMinFrameBytes = 1 + kFcsBytes;
constexpr size_t kBufBits = (AxFrameDecoder::kMaxFrameBytes + kFcsBytes) * 8;

}  // namespace

uint16_t crcX25(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 1) ? (crc >> 1) ^ kCrcPolyReflected : crc >> 1;
    }
  }
  return crc ^ 0xFFFF;
}

void AxFrameDecoder::reset() { *this = AxFrameDecoder(); }

bool AxFrameDecoder::feedBit(uint8_t bit) {
  bit &= 1;
  // NRZI: no level change = 1. Polarity of the FSK slicer doesn't matter.
  const uint8_t d = bit == prevRaw_ ? 1 : 0;
  prevRaw_ = bit;
  // Self-synchronising descrambler, locks after 17 bits.
  const uint8_t u = d ^ ((history_ >> 11) & 1) ^ ((history_ >> 16) & 1);
  history_ = ((history_ << 1) | d) & 0x1FFFF;

  if (u) {
    if (ones_ < kAbortOnes) ones_++;
    if (ones_ == kAbortOnes) inFrame_ = false;
    if (ones_ <= kStuffAfterOnes) appendBit(1);
    return false;
  }
  const uint8_t ones = ones_;
  ones_ = 0;
  if (ones == kStuffAfterOnes) return false;  // stuffed 0
  if (ones == kFlagOnes) return onFlag();
  appendBit(0);
  return false;
}

void AxFrameDecoder::appendBit(uint8_t bit) {
  if (nbits_ >= kBufBits) {
    inFrame_ = false;  // too long: not a frame we know
    return;
  }
  const size_t byte = nbits_ / 8;
  const uint8_t shift = nbits_ % 8;  // bytes arrive LSB first
  if (shift == 0) buf_[byte] = 0;
  buf_[byte] |= (uint8_t)(bit << shift);
  nbits_++;
}

bool AxFrameDecoder::onFlag() {
  const size_t bits = nbits_ >= kFlagBitsCollected ? nbits_ - kFlagBitsCollected : 0;
  const bool wasInFrame = inFrame_;
  inFrame_ = true;  // this flag opens the next frame
  nbits_ = 0;
  if (!wasInFrame || bits % 8 != 0 || bits / 8 < kMinFrameBytes) return false;

  const size_t len = bits / 8;
  const uint16_t fcs = (uint16_t)(buf_[len - 2] | (buf_[len - 1] << 8));
  if (crcX25(buf_, len - kFcsBytes) != fcs) {
    crcErrors_++;
    return false;
  }
  frameLen_ = len - kFcsBytes;
  return true;
}

}  // namespace klimalogg
