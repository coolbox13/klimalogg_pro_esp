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

#include <atomic>

#include "PulseDecoder.h"

namespace klimalogg {

namespace detail {
// The object (Receiver or ConsoleLink) that owns the radio, or nullptr.
const void* radioUser = nullptr;
}  // namespace detail

namespace {

// OOK bitrate only paces the peak-threshold decay; RadioLib caps OOK here.
constexpr float kOokBitrateKbps = 32.768f;
constexpr float kFreqDevKHz = 5.0f;  // unused for OOK, must be legal
constexpr int8_t kTxPowerDbm = 10;   // unused, receive only
constexpr uint16_t kPreambleBits = 16;
// Output low and no new edges for this long = transmission over.
constexpr uint32_t kEndOfTransmissionUs = 200;

constexpr uint32_t kRingSize = 4096;  // power of two: indices wrap with %

// ---- shared between the ISR (producer) and poll() (consumer) ----
// Single-producer/single-consumer ring. The ISR fills a slot, then publishes
// it with a release store of `head`; poll() reads `head` with acquire before
// touching the slot. That stays correct when poll() runs on the other core.
// Edge timestamps use the cycle counter of the ISR's core, so only
// differences between ring entries are meaningful. Counters only grow;
// readers work with differences, so there are no reset races.
uint32_t ringCycles[kRingSize];
uint8_t ringLevel[kRingSize];
std::atomic<uint32_t> head{0};
std::atomic<uint32_t> tail{0};
std::atomic<uint32_t> edgeTotal{0};
std::atomic<uint32_t> droppedTotal{0};
// GPIO input register for DIO2 (GPIO_IN1_REG for pins >= 32). Read
// through a pointer: REG_READ() on a variable address trips an IDF
// compile-time DPORT check.
volatile uint32_t* dataInReg = (volatile uint32_t*)GPIO_IN_REG;
uint32_t dataShift = 0;

// Kept minimal: it can run ~100k times per second on noise.
void IRAM_ATTR onEdge() {
  const uint32_t cycles = XTHAL_GET_CCOUNT();
  const uint8_t level = (*dataInReg >> dataShift) & 1;
  edgeTotal.store(edgeTotal.load(std::memory_order_relaxed) + 1,
                  std::memory_order_relaxed);
  const uint32_t h = head.load(std::memory_order_relaxed);
  if (h - tail.load(std::memory_order_acquire) >= kRingSize) {
    droppedTotal.store(droppedTotal.load(std::memory_order_relaxed) + 1,
                       std::memory_order_relaxed);
    return;
  }
  ringCycles[h % kRingSize] = cycles;
  ringLevel[h % kRingSize] = level;
  head.store(h + 1, std::memory_order_release);
}

// ---- owned by the polling task ----
const Receiver* owner = nullptr;  // the Receiver that called begin()
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
uint32_t activityEdgeTotal = 0;  // edgeTotal when poll() last saw activity
uint32_t activityUs = 0;         // poll()'s esp_timer at that moment

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
  const uint32_t t = tail.load(std::memory_order_relaxed);
  if (t == head.load(std::memory_order_acquire)) return false;
  *cycles = ringCycles[t % kRingSize];
  *level = ringLevel[t % kRingSize];
  tail.store(t + 1, std::memory_order_release);
  return true;
}

// The ISR keeps no wall-clock time; poll() notes when it last saw the edge
// counter move and measures quiet time from there with its own clock.
uint32_t microsSinceActivity() {
  const uint32_t nowUs = (uint32_t)esp_timer_get_time();
  const uint32_t total = edgeTotal.load(std::memory_order_relaxed);
  if (total != activityEdgeTotal) {
    activityEdgeTotal = total;
    activityUs = nowUs;
  }
  return nowUs - activityUs;
}

bool validPins(const ReceiverPins& p) {
  auto isGpio = [](int8_t pin) { return pin >= 0 && pin <= 39; };
  // GPIO 34..39 are input-only: fine for DIO lines, not for SPI out/CS/RST.
  auto isOutput = [](int8_t pin) { return pin >= 0 && pin <= 33; };
  return isOutput(p.sck) && isGpio(p.miso) && isOutput(p.mosi) &&
         isOutput(p.cs) && (p.reset < 0 || isOutput(p.reset)) &&
         isGpio(p.dio0) && isGpio(p.dio2);
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

Receiver::~Receiver() { end(); }

int16_t Receiver::begin(const ReceiverPins& pins, const ReceiverConfig& cfg) {
  if (owner != nullptr || detail::radioUser != nullptr) {
    return kErrAlreadyRunning;  // one radio user only
  }
  if (!validPins(pins)) return kErrInvalidPins;

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
  dataInReg = (volatile uint32_t*)(pins.dio2 >= 32 ? GPIO_IN1_REG : GPIO_IN_REG);
  dataShift = pins.dio2 >= 32 ? pins.dio2 - 32 : pins.dio2;
  tail.store(head.load(std::memory_order_acquire), std::memory_order_release);
  droppedSeen = droppedTotal.load();
  statsEdgesSeen = edgeTotal.load();
  statsDroppedSeen = droppedTotal.load();
  activityEdgeTotal = edgeTotal.load();
  activityUs = (uint32_t)esp_timer_get_time();
  resetDecoding();

  pinMode(dataPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(dataPin), onEdge, CHANGE);
  owner = this;
  detail::radioUser = this;
  return RADIOLIB_ERR_NONE;
}

void Receiver::end() {
  if (owner != this) return;  // never started, or another object owns it
  owner = nullptr;
  detail::radioUser = nullptr;
  detachInterrupt(digitalPinToInterrupt(dataPin));
  radio->standby();
  releaseRadio();
  resetDecoding();
}

bool Receiver::poll(Telegram* out) {
  if (owner != this || out == nullptr) return false;

  // Lost edges leave the level sequence inconsistent: start over.
  const uint32_t dropped = droppedTotal.load(std::memory_order_relaxed);
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

  // Output low and no new edges for a while: the transmission is over.
  const uint32_t quietUs = microsSinceActivity();
  if (!haveLastEdge || flushed || lastLevel) return false;
  if (quietUs < kEndOfTransmissionUs) return false;
  flushed = true;
  if (decoder.endOfTransmission()) {
    *out = finish();
    return true;
  }
  rssiPeak = kRssiUnknown;  // burst without telegram: forget its RSSI
  return false;
}

int16_t Receiver::setOokFloor(uint8_t floor) {
  if (owner != this) return kErrNotRunning;
  return radio->setOokFixedOrFloorThreshold(floor);
}

ReceiverStats Receiver::takeStats() {
  if (owner != this) return ReceiverStats{0, 0};
  const uint32_t edges = edgeTotal.load();
  const uint32_t dropped = droppedTotal.load();
  const ReceiverStats s{edges - statsEdgesSeen, dropped - statsDroppedSeen};
  statsEdgesSeen = edges;
  statsDroppedSeen = dropped;
  return s;
}

float Receiver::instantRssi() {
  return owner == this ? radio->getRSSI(false, true) : kRssiUnknown;
}

}  // namespace klimalogg

#endif  // ESP32 && __XTENSA__
