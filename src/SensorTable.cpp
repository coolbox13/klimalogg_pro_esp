#include "SensorTable.h"

#include <stdio.h>

namespace klimalogg {

bool SensorTable::update(const Reading& reading, int16_t rssi, uint32_t nowMs) {
  size_t index = 0;
  while (index < count_ && entries_[index].reading.id != reading.id) index++;

  if (index == count_) {
    if (count_ < kCapacity) {
      count_++;
    } else {
      index = longestSilent(nowMs);
      if (nowMs - entries_[index].lastSeenMs < kEvictAfterMs) return false;
    }
    entries_[index] = SensorEntry{};
  }

  SensorEntry& e = entries_[index];
  e.reading = reading;
  e.rssi = rssi;
  e.lastSeenMs = nowMs;
  e.packets++;
  return true;
}

size_t SensorTable::longestSilent(uint32_t nowMs) const {
  size_t oldest = 0;
  for (size_t i = 1; i < count_; i++) {
    if (nowMs - entries_[i].lastSeenMs > nowMs - entries_[oldest].lastSeenMs) {
      oldest = i;
    }
  }
  return oldest;
}

bool SensorTable::isStale(size_t index, uint32_t nowMs) const {
  return nowMs - entries_[index].lastSeenMs >= kStaleMs;
}

void formatLine(const SensorEntry& entry, bool stale, char* out,
                size_t outLen) {
  const Reading& r = entry.reading;
  char humidity[5];
  if (r.hasHumidity) {
    snprintf(humidity, sizeof(humidity), "%u%%", r.humidity);
  } else {
    snprintf(humidity, sizeof(humidity), " --");
  }
  snprintf(out, outLen, "%04X %5.1fC %s%s%s", r.id, r.temperatureC, humidity,
           r.batteryLow ? " BAT" : "", stale ? " ?" : "");
}

}  // namespace klimalogg
