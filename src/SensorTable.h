// Latest reading per sensor ID, in first-seen order.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "KlimaLoggProtocol.h"

namespace klimalogg {

struct SensorEntry {
  Reading reading;
  int16_t rssi;
  uint32_t lastSeenMs;
  uint32_t packets;
};

class SensorTable {
 public:
  static constexpr size_t kCapacity = 8;       // KlimaLogg base supports 8
  static constexpr uint32_t kStaleMs = 180000;  // sensors send every ~10 s

  // Returns false when the table is full and the ID is new.
  bool update(const Reading& reading, int16_t rssi, uint32_t nowMs);

  size_t size() const { return count_; }
  const SensorEntry& at(size_t index) const { return entries_[index]; }
  bool isStale(size_t index, uint32_t nowMs) const;

 private:
  SensorEntry entries_[kCapacity] = {};
  size_t count_ = 0;
};

// One display line, e.g. "65B0  22.0C 35% BAT". Stale entries end in "?".
void formatLine(const SensorEntry& entry, bool stale, char* out,
                size_t outLen);

}  // namespace klimalogg
