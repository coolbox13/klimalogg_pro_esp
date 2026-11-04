#include "PulseDecoder.h"

#include <string.h>

namespace klimalogg {

namespace {

constexpr uint32_t kChipUs = PulseDecoder::kBitUs / 2;  // 13 us
constexpr uint32_t kEndOfTransmissionGapUs = 0xFFFFFFFFu;

// Number of 13 us chips in a pulse, rounded.
uint32_t chipsIn(uint32_t pulseUs) { return (pulseUs + kChipUs / 2) / kChipUs; }

}  // namespace

bool PulseDecoder::feedLevel(bool high, uint32_t durationUs) {
  if (!high) {
    if (!havePulse_) return false;  // idle before the first pulse
    gapUs_ += durationUs;
    if (gapUs_ <= kMaxGapUs) return false;
    havePulse_ = false;
    return emitPulse(pulseUs_, gapUs_);
  }

  if (havePulse_ && gapUs_ <= kGlitchUs) {  // dropout inside a pulse
    pulseUs_ += gapUs_ + durationUs;
    gapUs_ = 0;
    return false;
  }
  if (havePulse_ && durationUs <= kGlitchUs) {  // spike inside a gap
    gapUs_ += durationUs;
    return false;
  }

  const bool completed = havePulse_ && emitPulse(pulseUs_, gapUs_);
  pulseUs_ = durationUs;
  gapUs_ = 0;
  havePulse_ = true;
  return completed;
}

bool PulseDecoder::endOfTransmission() {
  if (!havePulse_) return false;
  havePulse_ = false;
  return emitPulse(pulseUs_, kEndOfTransmissionGapUs);
}

// A pulse of 2n+1 chips followed by a short gap is n ones and a zero (the
// gap is the zero's second half). Before a long gap the pulse may also end
// on a one, giving an even chip count and no trailing zero.
bool PulseDecoder::emitPulse(uint32_t pulseUs, uint32_t gapUs) {
  const uint32_t chips = chipsIn(pulseUs);
  const uint32_t ones = chips / 2;
  const bool lastPulse = gapUs > kMaxGapUs;

  // BCD digits and the sync word never produce long runs of ones, so a very
  // long pulse is interference, not a telegram.
  if (chips == 0 || ones > kMaxOnesPerPulse) {
    resetFrame();
    return false;
  }

  bool completed = false;
  for (uint32_t i = 0; i < ones; i++) completed |= pushBit(1);
  if (!lastPulse || (chips % 2) == 1) completed |= pushBit(0);
  if (lastPulse) resetFrame();
  return completed;
}

bool PulseDecoder::pushBit(uint8_t bit) {
  if (!collecting_) {
    shift_ = (uint16_t)((shift_ << 1) | bit);
    if (shift_ == kSyncBitsTimeOrder) {
      collecting_ = true;
      collected_ = 0;
      memset(payload_, 0, sizeof(payload_));
    }
    return false;
  }

  payload_[collected_ / 8] |= (uint8_t)(bit << (collected_ % 8));
  collected_++;
  if (collected_ < kPayloadBytes * 8) return false;

  result_ = parsePayload(payload_, kPayloadBytes);
  resetFrame();
  return true;
}

void PulseDecoder::reset() {
  havePulse_ = false;
  pulseUs_ = 0;
  gapUs_ = 0;
  resetFrame();
}

void PulseDecoder::resetFrame() {
  collecting_ = false;
  collected_ = 0;
  shift_ = 0;
}

}  // namespace klimalogg
