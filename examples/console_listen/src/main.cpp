// Listen to a KlimaLogg Pro base station (console) and print its current
// readings. Receive only: the console sends a current-weather frame about
// every 330 ms for ~5 s after its USB button is held for 3-4 s.
//
// Set kFrequencyMHz for your board: 868.2815 MHz plus your SX1276's
// crystal error (a T3 V1.6.1 here needed +9 kHz).
#include <Arduino.h>
#include <ConsoleLink.h>
#include <ConsoleProtocol.h>

namespace kc = klimalogg::console;

constexpr float kFrequencyMHz = 868.2905f;
constexpr uint32_t kStatsIntervalMs = 10000;

klimalogg::ConsoleLink consoleLink;

void printCurrent(const kc::CurrentWeather& cw) {
  Serial.printf("console %04X quality %u:", cw.header.deviceId,
                cw.header.quality);
  for (int c = 0; c < kc::kChannels; c++) {
    const kc::Measurement& m = cw.channel[c];
    if (!m.hasTemperature) continue;
    Serial.printf("  ch%d %.1f C", c, m.temperatureC);
    if (m.hasHumidity) Serial.printf(" %u %%", m.humidity);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  klimalogg::ConsoleLinkConfig cfg;
  cfg.frequencyMHz = kFrequencyMHz;
  const int16_t st = consoleLink.begin(klimalogg::ReceiverPins::lilygoT3V161(), cfg);
  Serial.printf("console link: %d. Hold the base station's USB button.\n", st);
}

void loop() {
  static uint32_t lastStatsMs = 0;
  uint8_t frame[klimalogg::AxFrameDecoder::kMaxFrameBytes];
  size_t len;
  uint32_t endUs;
  while (consoleLink.poll(frame, &len, &endUs)) {
    kc::Header h;
    if (!kc::parseHeader(frame, len, &h)) continue;
    kc::CurrentWeather cw;
    if (kc::parseCurrentWeather(frame, len, &cw)) {
      printCurrent(cw);
    } else {
      Serial.printf("frame type %02X, %u bytes\n", h.type, (unsigned)len);
    }
  }
  if (millis() - lastStatsMs >= kStatsIntervalMs) {
    lastStatsMs = millis();
    const klimalogg::ConsoleLinkStats s = consoleLink.takeStats();
    Serial.printf("bit clocks %lu, dropped %lu, frames %lu\n",
                  (unsigned long)s.clocks, (unsigned long)s.droppedBits,
                  (unsigned long)s.frames);
  }
  delay(1);
}
