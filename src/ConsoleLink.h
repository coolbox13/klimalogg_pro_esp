// Radio link to a KlimaLogg Pro base station (console) for an SX1276 on
// ESP32: receives its frames and sends the transceiver's replies.
//
// The SX1276 runs in FSK continuous mode with its bit synchronizer. An
// interrupt on every DCLK rising edge (DIO1) reads the data bit (DIO2)
// while receiving, or sets it while sending. AxFrameDecoder/encodeAxFrame
// do the air format.
//
//   klimalogg::ConsoleLink link;
//   link.begin(klimalogg::ReceiverPins::lilygoT3V161(), cfg);
//   uint8_t f[...]; size_t n; uint32_t endUs;
//   if (link.poll(f, &n, &endUs)) { ... link.send(reply, len) ... }
//
// Rules: the same as for Receiver (one radio owner, begin/poll/send from
// one task, poll every few ms). Receiver and ConsoleLink must not run at
// the same time: end() one before begin() of the other.
//
// Frequency: the console measured 868.2815 MHz with tones at only +-5 kHz,
// so the radio's crystal error matters. Add the board's offset (a T3
// V1.6.1 here needed +9 kHz, i.e. 868.2905 MHz). Measure with an RTL-SDR.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AxFrameDecoder.h"
#include "KlimaLoggReceiver.h"

namespace klimalogg {

struct ConsoleLinkConfig {
  float frequencyMHz = 868.2815f;
  float bitrateKbps = 19.9f;
  float deviationKHz = 12.0f;  // transmit; the console accepts it
  float rxBandwidthKHz = 62.5f;
  int8_t txPowerDbm = 10;
};

struct ConsoleLinkStats {
  uint32_t clocks;       // bit clocks since last takeStats()
  uint32_t droppedBits;  // bits lost to a full buffer
  uint32_t frames;       // valid frames
  uint32_t crcErrors;    // flag-delimited blocks with a bad CRC (noise too)
};

constexpr int16_t kErrMissingClockPin = -1003;
constexpr int16_t kErrSendTimeout = -1004;
constexpr int16_t kErrFrameTooLong = -1005;

class ConsoleLink {
 public:
  ConsoleLink() = default;
  ~ConsoleLink();  // calls end()
  ConsoleLink(const ConsoleLink&) = delete;
  ConsoleLink& operator=(const ConsoleLink&) = delete;

  // Needs pins.dio1. Returns 0, kErrAlreadyRunning, kErrInvalidPins,
  // kErrMissingClockPin or a RadioLib status code.
  int16_t begin(const ReceiverPins& pins, const ConsoleLinkConfig& cfg = {});
  void end();

  // Decodes buffered bits. Returns true with a frame (without CRC, up to
  // AxFrameDecoder::kMaxFrameBytes) in `frame`; `endUs` is the
  // esp_timer time (us) at which the frame ended on air.
  bool poll(uint8_t* frame, size_t* len, uint32_t* endUs);

  // Sends one frame now and returns to receiving. Blocks for the airtime
  // (~11 ms for an ACK). Returns 0 or an error code.
  int16_t send(const uint8_t* frame, size_t len);

  ConsoleLinkStats takeStats();
};

}  // namespace klimalogg
