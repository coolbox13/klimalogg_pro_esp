// Classic ESP32 (Xtensa) + RadioLib only. Elsewhere (host unit tests, RISC-V
// ESP32 variants) this file compiles to nothing.
#if defined(ESP32) && defined(__XTENSA__)

#include "KlimaLoggReceiver.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <esp_timer.h>
#include <soc/gpio_reg.h>
#include <xtensa/core-macros.h>

#include "PulseDecoder.h"

namespace klimalogg {

namespace {

// OOK bitrate only paces the peak-threshold decay; RadioLib caps OOK here.
constexpr float kOokBitrateKbps = 32.768f;
constexpr float kFreqDevKHz = 5.0f;  // unused for OOK, must be legal
constexpr int8_t kTxPowerDbm = 10;   // unused, receive only
constexpr uint16_t kPreambleBits = 16;
constexpr uint32_t kEndOfTransmissionUs = 200;  // low this long = done

constexpr uint32_t kRingSize = 4096;  // power of two: indices wrap with %

// ---- written by the ISR, read by poll() ----
// Edge timestamps use the cycle counter of the ISR's core, so only
// differences between ring entries are meaningful. Counters only grow;
// readers work with differences, so no cross-core reset races.
volatile uint32_t ringCycles[kRingSize];
volatile uint8_t ringLevel[kRingSize];
volatile uint32_t head = 0;
volatile uint32_t tail = 0;  // written by poll() only
volatile uint32_t edgeTotal = 0;
volatile uint32_t droppedTotal = 0;
volatile uint32_t lastEdgeUs = 0;  // esp_timer, same clock on both cores
uint32_t dataInReg = GPIO_IN_REG;  // GPIO_IN1_REG for pins >= 32
uint32_t dataShift = 0;

void IRAM_ATTR onEdge() {
  const uint32_t cycles = XTHAL_GET_CCOUNT();
  const uint8_t level = (REG_READ(dataInReg) >> dataShift) & 1;
  lastEdgeUs = (uint32_t)esp_timer_get_time();
  edgeTotal = edgeTotal + 1;
  const uint32_t h = head;
  if (h - tail >= kRingSize) {
    droppedTotal = droppedTotal + 1;
    return;
  }
  ringCycles[h % kRingSize] = cycles;
  ringLevel[h % kRingSize] = level;
  head = h + 1;
}

// ---- owned by the polling task ----
SPIClass* spi = nullptr;
Module* module = nullptr;
SX1276* radio = nullptr;
int8_t dataPin = -1;
PulseDecoder decoder;
uint32_t lastCycles = 0;
uint8_t lastLevel = 0;
bool haveLastEdge = false;
bool flushed = true;
float rssiPeak = kRssiUnknown;
uint32_t cyclesPerUs = 240;
uint32_t droppedSeen = 0;
uint32_t statsEdgesSeen = 0;
uint32_t statsDroppedSeen = 0;

void resetDecoding() {
  decoder.reset();
  haveLastEdge = false;
  flushed = true;
  rssiPeak = kRssiUnknown;
}

void releaseRadio() {
  delete radio;
  delete module;
  if (spi != nullptr) spi->end();
  delete spi;
  radio = nullptr;
  module = nullptr;
  spi = nullptr;
}

bool popEdge(uint32_t* cycles, uint8_t* level) {
  const uint32_t t = tail;
  if (t == head) return false;
  *cycles = ringCycles[t % kRingSize];
  *level = ringLevel[t % kRingSize];
  tail = t + 1;
  return true;
}

void sampleRssi() {
  const float rssi = radio->getRSSI(false, true);
  if (rssi > rssiPeak) rssiPeak = rssi;
}

Telegram finish() {
  Telegram t;
  t.result = decoder.result();
  t.rssi = rssiPeak;
  rssiPeak = kRssiUnknown;
  return t;
}

int16_t configureRadio(SX1276& r, const ReceiverConfig& cfg) {
  int16_t st;
  // RadioLib runs OOK on the FSK/OOK modem: beginFSK(..., enableOOK = true).
  if ((st = r.beginFSK(cfg.frequencyMHz, kOokBitrateKbps, kFreqDevKHz,
                       cfg.bandwidthKHz, kTxPowerDbm, kPreambleBits, true)))
    return st;
  if ((st = r.setOokThresholdType(RADIOLIB_SX127X_OOK_THRESH_PEAK))) return st;
  if ((st = r.setOokPeakThresholdDecrement(
           RADIOLIB_SX127X_OOK_PEAK_THRESH_DEC_1_1_CHIP)))
    return st;
  if ((st = r.setOokPeakThresholdStep(
           RADIOLIB_SX127X_OOK_PEAK_THRESH_STEP_0_5_DB)))
    return st;
  if ((st = r.setOokFixedOrFloorThreshold(cfg.ookFloor))) return st;
  if ((st = r.setRSSIConfig(RADIOLIB_SX127X_RSSI_SMOOTHING_SAMPLES_2)))
    return st;
  // Raw slicer output on DIO2; bit timing is recovered in software.
  if ((st = r.disableBitSync())) return st;
  return r.receiveDirect();
}

}  // namespace

int16_t Receiver::begin(const ReceiverPins& pins, const ReceiverConfig& cfg) {
  if (radio != nullptr) return RADIOLIB_ERR_UNKNOWN;  // one receiver only

  spi = new SPIClass(VSPI);
  spi->begin(pins.sck, pins.miso, pins.mosi, pins.cs);
  module = new Module(pins.cs, pins.dio0, pins.reset, RADIOLIB_NC, *spi);
  radio = new SX1276(module);

  const int16_t st = configureRadio(*radio, cfg);
  if (st != RADIOLIB_ERR_NONE) {
    releaseRadio();
    return st;
  }

  cyclesPerUs = getCpuFrequencyMhz();
  dataPin = pins.dio2;
  dataInReg = pins.dio2 >= 32 ? GPIO_IN1_REG : GPIO_IN_REG;
  dataShift = pins.dio2 >= 32 ? pins.dio2 - 32 : pins.dio2;
  tail = head;
  droppedSeen = droppedTotal;
  statsEdgesSeen = edgeTotal;
  statsDroppedSeen = droppedTotal;
  resetDecoding();

  pinMode(dataPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(dataPin), onEdge, CHANGE);
  return RADIOLIB_ERR_NONE;
}

void Receiver::end() {
  if (radio == nullptr) return;
  detachInterrupt(digitalPinToInterrupt(dataPin));
  radio->standby();
  releaseRadio();
  resetDecoding();
}

bool Receiver::poll(Telegram* out) {
  if (radio == nullptr) return false;

  // Lost edges leave the level sequence inconsistent: start over.
  const uint32_t dropped = droppedTotal;
  if (dropped != droppedSeen) {
    droppedSeen = dropped;
    resetDecoding();
  }

  uint32_t cycles;
  uint8_t level;
  bool sampled = false;
  while (popEdge(&cycles, &level)) {
    if (!sampled) {  // carrier is (or just was) on: worth an RSSI read
      sampleRssi();
      sampled = true;
    }
    bool completed = false;
    if (haveLastEdge) {
      const uint32_t us = (cycles - lastCycles) / cyclesPerUs;
      completed = decoder.feedLevel(lastLevel != 0, us);
    }
    lastCycles = cycles;
    lastLevel = level;
    haveLastEdge = true;
    flushed = false;
    if (completed) {
      *out = finish();
      return true;  // remaining edges are handled by the next poll()
    }
  }

  // Output low for a while: the transmission is over.
  if (!haveLastEdge || flushed || lastLevel) return false;
  const uint32_t idleUs = (uint32_t)esp_timer_get_time() - lastEdgeUs;
  if (idleUs < kEndOfTransmissionUs) return false;
  flushed = true;
  if (decoder.endOfTransmission()) {
    *out = finish();
    return true;
  }
  rssiPeak = kRssiUnknown;  // burst without telegram: forget its RSSI
  return false;
}

ReceiverStats Receiver::takeStats() {
  const uint32_t edges = edgeTotal;
  const uint32_t dropped = droppedTotal;
  const ReceiverStats s{edges - statsEdgesSeen, dropped - statsDroppedSeen};
  statsEdgesSeen = edges;
  statsDroppedSeen = dropped;
  return s;
}

float Receiver::instantRssi() {
  return radio ? radio->getRSSI(false, true) : kRssiUnknown;
}

}  // namespace klimalogg

#endif  // ESP32 && __XTENSA__
