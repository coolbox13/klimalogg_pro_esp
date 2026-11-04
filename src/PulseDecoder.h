// Turns raw OOK carrier on/off durations into KlimaLogg telegrams.
//
// On air every bit lasts 26 us (38400 baud): 13 us carrier on, then 13 us
// carrier on for a 1 or off for a 0. So a carrier-on pulse of 13 + 26*n us
// followed by a 13 us gap is n one-bits and then a zero-bit. Bits arrive
// LSB first; the decoder hunts for sync 2D D4, takes the next 9 bytes and
// validates them with parsePayload().
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "KlimaLoggProtocol.h"

namespace klimalogg {

class PulseDecoder {
 public:
  static constexpr uint32_t kBitUs = 26;          // 1e6 / 38400
  static constexpr uint32_t kGlitchUs = 4;        // shorter levels are merged
  static constexpr uint32_t kMaxGapUs = 40;       // longer gap = end of frame
  static constexpr uint32_t kMaxOnesPerPulse = 24;  // longer = not our signal

  // Feed one level of the demodulator output and how long it lasted.
  // Returns true when a telegram completed; read it with result().
  bool feedLevel(bool high, uint32_t durationUs);

  // Call when the output has stayed low for a while (> kMaxGapUs).
  // Flushes the last pulse. Returns true when a telegram completed.
  bool endOfTransmission();

  const DecodeResult& result() const { return result_; }

  // Drop any half-received pulse or telegram, e.g. after lost edges.
  void reset();

 private:
  bool emitPulse(uint32_t pulseUs, uint32_t gapUs);
  bool pushBit(uint8_t bit);
  void resetFrame();

  // Pulse assembly
  uint32_t pulseUs_ = 0;
  uint32_t gapUs_ = 0;
  bool havePulse_ = false;

  // Bit assembly
  uint16_t shift_ = 0;
  bool collecting_ = false;
  size_t collected_ = 0;
  uint8_t payload_[kPayloadBytes] = {};

  DecodeResult result_ = {};
};

}  // namespace klimalogg
