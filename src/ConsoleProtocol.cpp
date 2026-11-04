#include "ConsoleProtocol.h"

namespace klimalogg {
namespace console {

namespace {

constexpr float kTempOffsetC = 40.0f;
constexpr uint8_t kMaxBcdDigit = 9;
constexpr uint32_t kHistoryBase = 0x070000;
constexpr uint32_t kHistoryRecordBytes = 32;
constexpr uint32_t kUnknownAddress = 0xFFFFFF;
constexpr uint8_t kAckFixedByte = 0x80;  // meaning unknown, always 0x80

// Current weather: channel 0 offsets; each channel is 24 bytes further.
constexpr size_t kCurTempByte = 29;  // starts at the low nibble
constexpr size_t kCurHumByte = 17;   // starts at the high nibble
constexpr size_t kCurChannelStride = 24;

// Config: sensor IDs of channels 1..8, big endian, 8 bytes apart.
constexpr size_t kCfgFirstSensorId = 63;
constexpr size_t kCfgSensorStride = 8;

// History: offsets of record 1 (the oldest); each later record sits
// 28 bytes earlier in the frame. Temperatures of even channels start at
// the low nibble, odd channels at the high nibble.
constexpr size_t kHisRecordStride = 28;
constexpr size_t kHisAlarmMarker = 180;  // 0xEE = alarm record
constexpr uint8_t kAlarmRecord = 0xEE;
constexpr size_t kHisTime = 176;  // 10 nibbles YYMMDDhhmm, high nibble first
constexpr size_t kHisTemp[kChannels] = {174, 173, 171, 170, 168,
                                        167, 165, 164, 162};
constexpr size_t kHisHum[kChannels] = {161, 160, 159, 158, 157,
                                       156, 155, 154, 153};

// Nibble n of the frame: byte n/2, high nibble first.
uint8_t nibble(const uint8_t* f, size_t n) {
  return (n % 2 == 0) ? f[n / 2] >> 4 : f[n / 2] & 0x0F;
}

// Reads `count` BCD digits from nibble `start`; false if any is not 0..9.
bool readBcd(const uint8_t* f, size_t start, int count, uint32_t* out) {
  uint32_t v = 0;
  for (int i = 0; i < count; i++) {
    const uint8_t d = nibble(f, start + i);
    if (d > kMaxBcdDigit) return false;
    v = v * 10 + d;
  }
  *out = v;
  return true;
}

size_t nibbleAt(size_t byte, bool highNibble) {
  return byte * 2 + (highNibble ? 0 : 1);
}

Measurement readMeasurement(const uint8_t* f, size_t tempNibble,
                            size_t humNibble) {
  Measurement m{};
  uint32_t t, h;
  if (readBcd(f, tempNibble, 3, &t)) {
    m.hasTemperature = true;
    m.temperatureC = (float)t / 10.0f - kTempOffsetC;
  }
  if (readBcd(f, humNibble, 2, &h)) {
    m.hasHumidity = true;
    m.humidity = (uint8_t)h;
  }
  return m;
}

bool readDateTime(const uint8_t* f, size_t startNibble, DateTime* out) {
  uint32_t v[5];
  for (int i = 0; i < 5; i++) {
    if (!readBcd(f, startNibble + 2 * i, 2, &v[i])) return false;
  }
  if (v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > 31 || v[3] > 23 ||
      v[4] > 59) {
    return false;
  }
  *out = DateTime{(uint16_t)(2000 + v[0]), (uint8_t)v[1], (uint8_t)v[2],
                  (uint8_t)v[3], (uint8_t)v[4]};
  return true;
}

uint32_t read24(const uint8_t* f) {
  return (uint32_t)f[0] << 16 | (uint32_t)f[1] << 8 | f[2];
}

bool parseTyped(const uint8_t* f, size_t len, size_t expectLen, uint8_t type,
                Header* h) {
  return len == expectLen && parseHeader(f, len, h) && h->type == type;
}

}  // namespace

bool parseHeader(const uint8_t* f, size_t len, Header* out) {
  if (f == nullptr || len < kHeaderBytes) return false;
  *out = Header{(uint16_t)(f[0] << 8 | f[1]), f[2], f[3],
                (uint8_t)(f[4] & 0x7F), (uint16_t)(f[5] << 8 | f[6])};
  return true;
}

bool parseCurrentWeather(const uint8_t* f, size_t len, CurrentWeather* out) {
  Header h;
  if (!parseTyped(f, len, kCurrentWeatherBytes, kTypeCurrentWeather, &h)) {
    return false;
  }
  out->header = h;
  for (int c = 0; c < kChannels; c++) {
    const size_t off = c * kCurChannelStride;
    out->channel[c] =
        readMeasurement(f, nibbleAt(kCurTempByte + off, false),
                        nibbleAt(kCurHumByte + off, true));
  }
  return true;
}

bool parseConfig(const uint8_t* f, size_t len, ConsoleConfig* out) {
  Header h;
  if (!parseTyped(f, len, kConfigBytes, kTypeConfig, &h)) return false;
  out->header = h;
  out->sensorId[0] = kNoSensor;
  for (int c = 1; c < kChannels; c++) {
    const size_t at = kCfgFirstSensorId + (c - 1) * kCfgSensorStride;
    out->sensorId[c] = (uint16_t)(f[at] << 8 | f[at + 1]);
  }
  out->checksum = (uint16_t)(f[len - 2] << 8 | f[len - 1]);
  return true;
}

bool parseHistory(const uint8_t* f, size_t len, HistoryFrame* out) {
  Header h;
  if (!parseTyped(f, len, kHistoryBytes, kTypeHistory, &h)) return false;
  out->header = h;
  out->latestIndex = historyIndex(read24(f + 7));
  out->thisIndex = historyIndex(read24(f + 10));
  for (int r = 0; r < kRecordsPerFrame; r++) {
    const size_t back = r * kHisRecordStride;
    HistoryRecord& rec = out->record[r];
    rec = HistoryRecord{};
    if (f[kHisAlarmMarker - back] == kAlarmRecord) continue;
    if (!readDateTime(f, nibbleAt(kHisTime - back, true), &rec.time)) {
      continue;
    }
    rec.valid = true;
    for (int c = 0; c < kChannels; c++) {
      rec.channel[c] = readMeasurement(
          f, nibbleAt(kHisTemp[c] - back, c % 2 == 1),
          nibbleAt(kHisHum[c] - back, true));
    }
  }
  return true;
}

uint32_t historyAddress(uint32_t index) {
  return kHistoryBase + kHistoryRecordBytes * index;
}

uint32_t historyIndex(uint32_t address) {
  if (address < kHistoryBase) return kNoIndex;
  const uint32_t index = (address - kHistoryBase) / kHistoryRecordBytes;
  return index < kHistorySlots ? index : kNoIndex;
}

size_t buildAck(const Header& received, Action action, uint8_t comInterval,
                uint32_t index, uint8_t out[kAckBytes]) {
  const uint32_t addr =
      index < kHistorySlots ? historyAddress(index) : kUnknownAddress;
  out[0] = (uint8_t)(received.deviceId >> 8);
  out[1] = (uint8_t)received.deviceId;
  out[2] = received.loggerId;
  out[3] = (uint8_t)action;
  out[4] = (uint8_t)(received.configChecksum >> 8);
  out[5] = (uint8_t)received.configChecksum;
  out[6] = kAckFixedByte;
  out[7] = comInterval;
  out[8] = (uint8_t)(addr >> 16);
  out[9] = (uint8_t)(addr >> 8);
  out[10] = (uint8_t)addr;
  return kAckBytes;
}

}  // namespace console
}  // namespace klimalogg
