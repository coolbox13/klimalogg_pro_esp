#include "display_view.h"

#include <Arduino.h>
#include <Wire.h>

#include "SSD1306Wire.h"

namespace display_view {

namespace {

constexpr uint8_t kI2cAddress = 0x3C;
constexpr int kLineHeight = 10;
constexpr int kWidth = 128;
constexpr size_t kMaxSensorLines = 5;  // 64 px - header

// T3 V1.6.1: OLED on SDA 21 / SCL 22.
// Do NOT drive OLED_RST (GPIO16) even though the board variant defines it:
// this board is an ESP32-PICO-D4, where GPIO16 belongs to the internal
// flash. Touching it hangs the chip (TG1WDT_SYS_RESET boot loop).
SSD1306Wire oled(kI2cAddress, OLED_SDA, OLED_SCL);

void drawHeader(const Status& s) {
  char line[32];
  if (!s.radioOk) {
    snprintf(line, sizeof(line), "RADIO ERROR");
  } else {
    snprintf(line, sizeof(line), "KlimaLogg  %lu/%lu ok", (unsigned long)s.valid,
             (unsigned long)s.frames);
  }
  oled.drawString(0, 0, line);
  oled.drawHorizontalLine(0, kLineHeight + 1, kWidth);
}

}  // namespace

bool begin() {
  if (!oled.init()) return false;
  oled.flipScreenVertically();
  oled.setFont(ArialMT_Plain_10);
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  oled.clear();
  oled.drawString(0, 0, "KlimaLogg receiver");
  oled.drawString(0, 2 * kLineHeight, "starting...");
  oled.display();
  return true;
}

void render(const klimalogg::SensorTable& table, const Status& status,
            uint32_t nowMs) {
  oled.clear();
  drawHeader(status);

  if (table.size() == 0) {
    oled.drawString(0, 2 * kLineHeight, "Listening for sensors");
  }
  char line[32];
  for (size_t i = 0; i < table.size() && i < kMaxSensorLines; i++) {
    klimalogg::formatLine(table.at(i), table.isStale(i, nowMs), line,
                          sizeof(line));
    oled.drawString(0, (int)(i + 1) * kLineHeight + 3, line);
  }
  oled.display();
}

}  // namespace display_view
