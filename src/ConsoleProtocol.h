// KlimaLogg Pro base-station (console) protocol: parse its frames and build
// the transceiver's replies. Pure C++, host tested. Frames are the payloads
// AxFrameDecoder yields (no CRC):
//
//   DevID(2) LoggerID Type Quality CfgChecksum(2) ...
//
// The console starts every exchange; the transceiver answers within
// 10..150 ms (measured) with an 11-byte ACK that says what to send next:
//
//   DevID(2) LoggerID Action CfgChecksum(2) 80 ComInterval HistoryAddr(3)
//
// Values are BCD nibbles: temperature 3 nibbles, 0.1 C, offset +40 C;
// humidity 2 nibbles, %. Nibbles A..E mean "no data", F "overflow"; both
// are reported as missing. Byte offsets follow the format documented in the
// weewx KlimaLogg driver (kl.py) and were checked against real frames.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace klimalogg {
namespace console {

// Frame types (console -> transceiver).
constexpr uint8_t kTypeDataWritten = 0x10;
constexpr uint8_t kTypeConfig = 0x20;
constexpr uint8_t kTypeCurrentWeather = 0x30;
constexpr uint8_t kTypeHistory = 0x40;
constexpr uint8_t kTypeRequestMask = 0xF0;  // 0x5x: console requests
constexpr uint8_t kTypeRequest = 0x50;

constexpr size_t kHeaderBytes = 7;
constexpr size_t kCurrentWeatherBytes = 229;
constexpr size_t kConfigBytes = 125;
constexpr size_t kHistoryBytes = 181;
constexpr size_t kAckBytes = 11;

constexpr int kChannels = 9;  // 0 = the console's own sensor, 1..8 radio
constexpr int kRecordsPerFrame = 6;
constexpr uint32_t kHistorySlots = 51200;  // ring of history records
constexpr uint32_t kNoIndex = 0xFFFFFFFF;
constexpr uint16_t kNoSensor = 0xFFFF;

// What the transceiver asks for next.
enum class Action : uint8_t {
  GetHistory = 0x00,
  RequestSetTime = 0x01,
  RequestSetConfig = 0x02,
  GetConfig = 0x03,
  GetCurrent = 0x04,
};

struct Header {
  uint16_t deviceId;  // the paired transceiver's ID
  uint8_t loggerId;
  uint8_t type;
  uint8_t quality;  // 0..100, drops by 5 for each unanswered frame
  uint16_t configChecksum;
};

struct Measurement {
  bool hasTemperature;
  float temperatureC;
  bool hasHumidity;
  uint8_t humidity;
};

struct CurrentWeather {
  Header header;
  Measurement channel[kChannels];
};

struct ConsoleConfig {
  Header header;
  // Added to the DCF77 (Frankfurt) time for the display and the history
  // time stamps. -12..+12.
  int8_t timeZoneHours;
  uint8_t historyIntervalCode;  // see historyIntervalSeconds()
  bool dcfEnabled;
  uint16_t sensorId[kChannels];  // kNoSensor = none; [0] is the console
  uint16_t checksum;             // echoed in every ACK
};

// History interval code (0 = 1 min, 1 = 5 min, 2 = 10, 3 = 15, 4 = 30,
// 5 = 60 min, 6 = 2 h, 7 = 3 h, 8 = 6 h) in seconds; 0 if unknown.
uint32_t historyIntervalSeconds(uint8_t code);

// Console local time, as kept by the console (DCF77 or set by hand).
struct DateTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
};

struct HistoryRecord {
  bool valid;  // false for alarm records and unreadable timestamps
  DateTime time;
  Measurement channel[kChannels];
};

struct HistoryFrame {
  Header header;
  uint32_t latestIndex;  // newest record the console holds
  uint32_t thisIndex;    // index the console reports for this frame
  HistoryRecord record[kRecordsPerFrame];  // oldest first
};

bool parseHeader(const uint8_t* f, size_t len, Header* out);
bool parseCurrentWeather(const uint8_t* f, size_t len, CurrentWeather* out);
bool parseConfig(const uint8_t* f, size_t len, ConsoleConfig* out);
bool parseHistory(const uint8_t* f, size_t len, HistoryFrame* out);

// History ring index <-> console memory address (0x070000 + 32 * index).
uint32_t historyAddress(uint32_t index);
uint32_t historyIndex(uint32_t address);  // kNoIndex for 0xFFFFFF

// Writes the ACK for a received frame into `out` (kAckBytes). For
// Action::GetHistory, `historyIndex` is the last record already stored;
// the console answers with the six records after it. kNoIndex = unknown.
size_t buildAck(const Header& received, Action action, uint8_t comInterval,
                uint32_t historyIndex, uint8_t out[kAckBytes]);

}  // namespace console
}  // namespace klimalogg
