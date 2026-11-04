#include "KlimaLoggProtocol.h"

namespace klimalogg {

namespace {

constexpr uint8_t kMaxBcdDigit = 9;

bool isBcd(uint8_t nibble) { return nibble <= kMaxBcdDigit; }

}  // namespace

uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ kCrcPoly) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

DecodeResult parsePayload(const uint8_t* p, size_t len) {
  DecodeResult result = {};
  if (len < kPayloadBytes) {
    result.status = Status::ShortBuffer;
    return result;
  }
  if (p[7] != kTypeByte) {
    result.status = Status::BadType;
    return result;
  }
  if (crc8(p, 8) != p[8]) {
    result.status = Status::BadCrc;
    return result;
  }

  const uint8_t t2 = p[2] & 0x0F;
  const uint8_t t1 = p[3] >> 4;
  const uint8_t t0 = p[3] & 0x0F;
  if (!isBcd(t2) || !isBcd(t1) || !isBcd(t0)) {
    result.status = Status::BadTemperature;
    return result;
  }
  const int tenths = t2 * 100 + t1 * 10 + t0 - kTemperatureOffsetTenths;
  const uint8_t humidity = p[4] & 0x7F;

  result.status = Status::Ok;
  result.reading.id = (uint16_t)(((p[0] & 0x7F) << 8) | p[1]);
  result.reading.temperatureC = tenths / 10.0f;
  result.reading.hasHumidity =
      humidity != kHumidityNotAvailable && humidity <= kHumidityMax;
  result.reading.humidity = result.reading.hasHumidity ? humidity : 0;
  result.reading.batteryLow = (p[5] & 0x80) != 0;
  result.reading.sequence = p[6] >> 4;
  result.reading.learning = p[2] >> 4;
  return result;
}

const char* statusName(Status status) {
  switch (status) {
    case Status::Ok: return "ok";
    case Status::ShortBuffer: return "short";
    case Status::BadType: return "bad-type";
    case Status::BadCrc: return "bad-crc";
    case Status::BadTemperature: return "bad-temp";
  }
  return "unknown";
}

}  // namespace klimalogg
