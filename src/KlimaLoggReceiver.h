// KlimaLogg Pro sensor receiver for an SX1276 (RFM95/LoRa module) on ESP32.
//
// The SX1276 runs in OOK continuous mode. An interrupt timestamps every edge
// of the raw demodulator output on DIO2; poll() turns those timings into
// telegrams with PulseDecoder.
//
//   klimalogg::Receiver rx;
//   rx.begin(klimalogg::ReceiverPins::lilygoT3V161());
//   ...
//   klimalogg::Telegram t;
//   while (rx.poll(&t)) { if (t.result.status == klimalogg::Status::Ok) ... }
//
// Rules:
// - One Receiver per board (it owns the radio and a global interrupt).
// - Call begin() and poll() from the same task. The interrupt runs on the
//   core that called begin(); poll() may run on either core.
// - Call poll() often (every few ms). Blocking the polling task for tens of
//   milliseconds overflows the edge buffer and loses telegrams; watch
//   ReceiverStats::droppedEdges. Run WiFi/MQTT in another task.
// - Keep the CPU frequency fixed after begin().
//
// Supported: classic ESP32 (Xtensa). Not ESP32-C3/C6/H2 (RISC-V).
#pragma once

#include <stdint.h>

#include "KlimaLoggProtocol.h"

namespace klimalogg {

struct ReceiverPins {
  int8_t sck;
  int8_t miso;
  int8_t mosi;
  int8_t cs;
  int8_t reset;
  int8_t dio0;
  int8_t dio2;  // raw OOK data, must be interrupt capable

  // LilyGO T3 V1.6.1 / TTGO LoRa32 V2.1_1.6 (868 MHz). Tested.
  static constexpr ReceiverPins lilygoT3V161() {
    return ReceiverPins{5, 19, 27, 18, 23, 26, 32};
  }
};

struct ReceiverConfig {
  float frequencyMHz = 868.30f;  // sensors measured at 868.31 MHz
  float bandwidthKHz = 125.0f;   // single-sided; keep >= 62.5 for 13 us pulses
  uint8_t ookFloor = 15;         // RegOokFix; raise to cut idle noise edges
};

constexpr float kRssiUnknown = -200.0f;

struct Telegram {
  DecodeResult result;  // check result.status before using result.reading
  float rssi;           // dBm, approximate peak during reception;
                        // kRssiUnknown if not sampled
};

struct ReceiverStats {
  uint32_t edges;         // demodulator edges since last takeStats()
  uint32_t droppedEdges;  // edges lost to a full buffer since last takeStats()
};

class Receiver {
 public:
  Receiver() = default;
  Receiver(const Receiver&) = delete;
  Receiver& operator=(const Receiver&) = delete;

  // Returns a RadioLib status code (0 = ok). Safe to retry after a failure.
  // Fails with RADIOLIB_ERR_UNKNOWN (-1) if a receiver is already running.
  int16_t begin(const ReceiverPins& pins, const ReceiverConfig& config = {});

  // Stops reception and releases the radio. begin() may be called again.
  void end();

  // Processes recorded edges. Returns true and fills `out` when a telegram
  // with a sync word completed (valid or not). Never waits for the radio.
  bool poll(Telegram* out);

  ReceiverStats takeStats();
  float instantRssi();
};

}  // namespace klimalogg
