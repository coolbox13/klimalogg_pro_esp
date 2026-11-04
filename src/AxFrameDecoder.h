// Frames from the KlimaLogg Pro base station (console), as sent by its
// AX5051 radio. Pure C++, host tested.
//
// On air (measured 2026-11-04): 2-FSK ~19.9 kbit/s, ~868.28 MHz. Bits are
// NRZI coded (1 = no change), scrambled with 1+x^12+x^17 (G3RUH), and
// framed as HDLC: 0x7E flags, a 0 stuffed after five 1s, bytes LSB first,
// CRC-16/X-25 appended low byte first. The frame bytes are what the USB
// transceiver hands to the PC (see weewx kl.py): DeviceID(2) LoggerID
// Type ...
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace klimalogg {

// CRC-16/X-25 (poly 0x1021 reflected, init and xorout 0xFFFF).
uint16_t crcX25(const uint8_t* data, size_t len);

// Encodes `data` (without CRC) as air bits for the radio: preamble, flag,
// stuffed data + CRC, flag; scrambled and NRZI coded. Writes the levels
// MSB first into `out` (zeroed first). Returns the number of bits, or 0
// when the frame is too long or `out` too small.
size_t encodeAxFrame(const uint8_t* data, size_t len, uint8_t* out,
                     size_t outCapacityBits);

class AxFrameDecoder {
 public:
  // Largest frame (without FCS); current weather is 229 bytes.
  static constexpr size_t kMaxFrameBytes = 300;

  // Feed one demodulated bit (either polarity). Returns true when a frame
  // with a valid CRC completed; frame()/size() then hold it (without the
  // CRC) until the next feedBit().
  bool feedBit(uint8_t bit);
  void reset();

  const uint8_t* frame() const { return buf_; }
  size_t size() const { return frameLen_; }
  uint32_t crcErrors() const { return crcErrors_; }

 private:
  void appendBit(uint8_t bit);
  bool onFlag();

  uint8_t prevRaw_ = 0;
  uint32_t history_ = 0;  // last 17 NRZI-decoded bits, newest in bit 0
  uint8_t ones_ = 0;      // consecutive 1s after descrambling
  bool inFrame_ = false;  // a flag was seen and no abort since
  size_t nbits_ = 0;      // bits collected since the last flag
  size_t frameLen_ = 0;
  uint32_t crcErrors_ = 0;
  uint8_t buf_[kMaxFrameBytes + 2] = {};
};

}  // namespace klimalogg
