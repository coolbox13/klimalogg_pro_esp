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

constexpr uint8_t kFlag = 0x7E;
constexpr uint8_t kPreambleByte = 0xAA;  // the USB stick's preamble pattern
constexpr size_t kPreambleBytes = 8;
constexpr size_t kTrailerBytes = 2;      // lets the closing flag leave the radio

// Air-bit writer: scrambles, NRZI codes and packs MSB first.
class AirWriter {
 public:
  AirWriter(uint8_t* out, size_t capacityBits) : out_(out), cap_(capacityBits) {}

  void bit(uint8_t u) {
    const uint8_t s = u ^ ((history_ >> 11) & 1) ^ ((history_ >> 16) & 1);
    history_ = ((history_ << 1) | s) & 0x1FFFF;
    if (s == 0) level_ ^= 1;  // NRZI: 0 = change
    if (n_ < cap_) {
      if (level_) out_[n_ / 8] |= (uint8_t)(0x80 >> (n_ % 8));
    }
    n_++;
  }
  void rawByte(uint8_t b) {
    for (int i = 0; i < 8; i++) bit((b >> i) & 1);
  }
  void stuffedByte(uint8_t b) {
    for (int i = 0; i < 8; i++) {
      const uint8_t v = (b >> i) & 1;
      bit(v);
      ones_ = v ? ones_ + 1 : 0;
      if (ones_ == kStuffAfterOnes) {
        bit(0);
        ones_ = 0;
      }
    }
  }
  size_t count() const { return n_; }

 private:
  uint8_t* out_;
  size_t cap_;
  size_t n_ = 0;
  uint32_t history_ = 0;
  uint8_t level_ = 0;
  uint8_t ones_ = 0;
};

}  // namespace

size_t encodeAxFrame(const uint8_t* data, size_t len, uint8_t* out,
                     size_t outCapacityBits) {
  if (len > AxFrameDecoder::kMaxFrameBytes) return 0;
  for (size_t i = 0; i < (outCapacityBits + 7) / 8; i++) out[i] = 0;
  AirWriter w(out, outCapacityBits);
  for (size_t i = 0; i < kPreambleBytes; i++) w.rawByte(kPreambleByte);
  w.rawByte(kFlag);
  for (size_t i = 0; i < len; i++) w.stuffedByte(data[i]);
  const uint16_t fcs = crcX25(data, len);
  w.stuffedByte(fcs & 0xFF);
  w.stuffedByte(fcs >> 8);
  w.rawByte(kFlag);
  for (size_t i = 0; i < kTrailerBytes; i++) w.rawByte(kPreambleByte);
  return w.count() <= outCapacityBits ? w.count() : 0;
}

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
