// Example: receive KlimaLogg Pro sensors on a LilyGO T3 V1.6.1, show them on
// the OLED and print one JSON line per valid telegram on the serial port.

#include <Arduino.h>
#include <KlimaLoggReceiver.h>
#include <SensorTable.h>

#include "config.h"
#include "display_view.h"

namespace {

klimalogg::Receiver receiver;
klimalogg::SensorTable sensors;
display_view::Status status = {};
uint32_t ledOffAtMs = 0;

void printReading(const klimalogg::Reading& r, float rssi) {
  Serial.printf("{\"model\":\"KlimaLogg-Pro\",\"id\":\"%04X\","
                "\"temperature_C\":%.1f,",
                r.id, r.temperatureC);
  if (r.hasHumidity) Serial.printf("\"humidity\":%u,", r.humidity);
  Serial.printf("\"battery_ok\":%s,\"sequence\":%u,\"rssi\":%.1f}\n",
                r.batteryLow ? "false" : "true", r.sequence, rssi);
}

void handleTelegram(const klimalogg::Telegram& t, uint32_t nowMs) {
  status.frames++;
  if (t.result.status != klimalogg::Status::Ok) {
    if (KL_DEBUG_FRAMES) {
      Serial.printf("frame rejected: %s rssi=%.1f\n",
                    klimalogg::statusName(t.result.status), t.rssi);
    }
    return;
  }

  status.valid++;
  const klimalogg::Reading& r = t.result.reading;
  if (!sensors.update(r, (int16_t)t.rssi, nowMs)) {
    Serial.printf("sensor table full, ignoring %04X\n", r.id);
  }
  printReading(r, t.rssi);
  digitalWrite(LED_BUILTIN, HIGH);
  ledOffAtMs = nowMs + KL_LED_FLASH_MS;
}

void printStats(uint32_t nowMs) {
  static uint32_t lastMs = 0;
  if (nowMs - lastMs < KL_STATS_INTERVAL_MS) return;
  const float seconds = (nowMs - lastMs) / 1000.0f;
  lastMs = nowMs;
  const klimalogg::ReceiverStats s = receiver.takeStats();
  Serial.printf("stats: edges/s=%.0f dropped=%lu frames=%lu valid=%lu "
                "sensors=%u rssi_now=%.1f\n",
                s.edges / seconds, (unsigned long)s.droppedEdges,
                (unsigned long)status.frames, (unsigned long)status.valid,
                (unsigned)sensors.size(), receiver.instantRssi());
}

}  // namespace

void setup() {
  Serial.begin(KL_SERIAL_BAUD);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println("\nKlimaLogg Pro receiver (SX1276 OOK 868.3 MHz)");

  if (!display_view::begin()) Serial.println("display init failed");

  const klimalogg::ReceiverConfig config;
  const int16_t st =
      receiver.begin(klimalogg::ReceiverPins::lilygoT3V161(), config);
  status.radioOk = (st == 0);
  Serial.printf("radio init %s (%d), %.2f MHz, bw %.1f kHz, floor %u\n",
                status.radioOk ? "ok" : "FAILED", st, config.frequencyMHz,
                config.bandwidthKHz, config.ookFloor);
}

void loop() {
  static uint32_t lastRenderMs = 0;
  const uint32_t now = millis();

  klimalogg::Telegram t;
  while (receiver.poll(&t)) handleTelegram(t, now);
  printStats(now);

  if ((int32_t)(now - ledOffAtMs) >= 0) digitalWrite(LED_BUILTIN, LOW);

  if (now - lastRenderMs >= KL_DISPLAY_REFRESH_MS) {
    lastRenderMs = now;
    display_view::render(sensors, status, now);
  }
}
