// TFA KlimaLogg Pro sensor protocol (30.3180.IT / 30.3181.IT).
//
// On air: OOK at 868.3 MHz, 38400 baud, bytes sent LSB first (see
// PulseDecoder.h for the bit shape). Telegram:
//
//   2D D4 | II II | sT TT | HH | BB | SS | 56 | CC
//
//   II  15-bit sensor ID (top bit ignored)
//   TTT BCD temperature, 0.1 degC steps, offset +40 degC
//   HH  humidity % (bits 6:0, binary), 0x6A = not available
//   BB  bit 7 = low battery
//   SS  bits 7:4 = sequence number
//   56  type byte
//   CC  CRC8 (poly 0x31, init 0) over ID..56
//
// Sources: github.com/baycom/tfrec, rtl_433 src/devices/klimalogg.c, and
// rtl_433 -A captures of a real 30.3180.IT.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace klimalogg {

constexpr size_t kSyncBytes = 2;
constexpr size_t kPayloadBytes = 9;  // ID..CRC, follows the sync word
constexpr uint8_t kSync[kSyncBytes] = {0x2D, 0xD4};
// 2D D4 sent LSB first, as 16 bits in time order (first bit = MSB).
constexpr uint16_t kSyncBitsTimeOrder = 0xB42B;
constexpr uint8_t kTypeByte = 0x56;
constexpr uint8_t kCrcPoly = 0x31;
constexpr uint8_t kHumidityNotAvailable = 0x6A;
constexpr uint8_t kHumidityMax = 100;
constexpr int kTemperatureOffsetTenths = 400;

enum class Status : uint8_t { Ok, ShortBuffer, BadType, BadCrc, BadTemperature };

struct Reading {
  uint16_t id;
  float temperatureC;
  bool hasHumidity;
  uint8_t humidity;
  bool batteryLow;
  uint8_t sequence;
};

struct DecodeResult {
  Status status;
  Reading reading;  // valid only when status == Status::Ok
};

uint8_t crc8(const uint8_t* data, size_t len);

// Validate and decode the 9 decoded bytes after the sync word.
DecodeResult parsePayload(const uint8_t* payload, size_t len);

const char* statusName(Status status);

}  // namespace klimalogg
