// OLED status screen.
#pragma once

#include <stdint.h>

#include "SensorTable.h"

namespace display_view {

struct Status {
  bool radioOk;
  uint32_t frames;  // telegrams with a sync word
  uint32_t valid;   // telegrams that passed CRC
};

bool begin();
void render(const klimalogg::SensorTable& table, const Status& status,
            uint32_t nowMs);

}  // namespace display_view
