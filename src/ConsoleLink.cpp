// Classic ESP32 (Xtensa) + RadioLib only, like KlimaLoggReceiver.cpp.
#if defined(ESP32) && defined(__XTENSA__)

#include "ConsoleLink.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <esp_timer.h>
#include <soc/gpio_reg.h>

#include <atomic>

namespace klimalogg {

namespace detail {
extern const void* radioUser;  // KlimaLoggReceiver.cpp
}  // namespace detail

namespace {

constexpr uint16_t kPreambleBits = 16;  // unused in continuous mode
constexpr uint32_t kRingBits = 65536;   // ~3.3 s at 19.9 kbit/s
constexpr uint32_t kRingWords = kRingBits / 32;
constexpr size_t kTxMaxBits = 4096;     // ~400 bytes on air
constexpr uint32_t kSendTimeoutMs = 300;

// ---- shared with the ISR ----
uint32_t ring[kRingWords];
std::atomic<uint32_t> head{0};
std::atomic<uint32_t> tail{0};
std::atomic<uint32_t> clocks{0};
std::atomic<uint32_t> dropped{0};
volatile uint32_t* dataInReg = (volatile uint32_t*)GPIO_IN_REG;
uint32_t dataMask = 0;
// Written through pointers: REG_WRITE() on a variable address trips an IDF
// compile-time DPORT check.
volatile uint32_t* setReg = (volatile uint32_t*)GPIO_OUT_W1TS_REG;
volatile uint32_t* clearReg = (volatile uint32_t*)GPIO_OUT_W1TC_REG;
uint8_t txBits[kTxMaxBits / 8];
volatile uint32_t txLen = 0;
volatile uint32_t txPos = 0;
volatile bool sending = false;

// Receive: store the data bit. Send: set the level for the next edge (the
// radio samples DIO2 on the rising edge).
void IRAM_ATTR onClock() {
  clocks.store(clocks.load(std::memory_order_relaxed) + 1,
               std::memory_order_relaxed);
  if (sending) {
    const uint32_t p = txPos;
    if (p < txLen) {
      const bool level = (txBits[p / 8] >> (7 - p % 8)) & 1;
      *(level ? setReg : clearReg) = dataMask;
      txPos = p + 1;
    }
    return;
  }
  const bool bit = (*dataInReg & dataMask) != 0;
  const uint32_t h = head.load(std::memory_order_relaxed);
  if (h - tail.load(std::memory_order_acquire) >= kRingBits) {
    dropped.store(dropped.load(std::memory_order_relaxed) + 1,
                  std::memory_order_relaxed);
    return;
  }
  const uint32_t w = (h / 32) % kRingWords;
  const uint32_t mask = 1u << (h % 32);
  if (bit) {
    ring[w] |= mask;
  } else {
    ring[w] &= ~mask;
  }
  head.store(h + 1, std::memory_order_release);
}

// ---- owned by the polling task ----
const ConsoleLink* owner = nullptr;
SPIClass* spi = nullptr;
Module* module = nullptr;
SX1276* radio = nullptr;
int8_t clockPin = -1;
int8_t dataPin = -1;
float bitrateBps = 19900.0f;
AxFrameDecoder decoder;
uint32_t framesTotal = 0;
uint32_t statsClocksSeen = 0;
uint32_t statsDroppedSeen = 0;
uint32_t statsFramesSeen = 0;
uint32_t statsCrcSeen = 0;

void releaseRadio() {
  delete radio;
  delete module;
  if (spi != nullptr) spi->end();
  delete spi;
  radio = nullptr;
  module = nullptr;
  spi = nullptr;
}

void flushReceived() {
  tail.store(head.load(std::memory_order_acquire), std::memory_order_release);
  decoder.reset();
}

bool validPins(const ReceiverPins& p) {
  auto isGpio = [](int8_t pin) { return pin >= 0 && pin <= 39; };
  auto isOutput = [](int8_t pin) { return pin >= 0 && pin <= 33; };
  // DIO2 is driven while sending, so it must be an output pin here.
  return isOutput(p.sck) && isGpio(p.miso) && isOutput(p.mosi) &&
         isOutput(p.cs) && (p.reset < 0 || isOutput(p.reset)) &&
         isGpio(p.dio0) && isGpio(p.dio1) && isOutput(p.dio2);
}

int16_t configureRadio(SX1276& r, const ConsoleLinkConfig& cfg) {
  int16_t st;
  if ((st = r.beginFSK(cfg.frequencyMHz, cfg.bitrateKbps, cfg.deviationKHz,
                       cfg.rxBandwidthKHz, cfg.txPowerDbm, kPreambleBits,
                       false)))
    return st;
  if ((st = r.setRSSIConfig(RADIOLIB_SX127X_RSSI_SMOOTHING_SAMPLES_8)))
    return st;
  if ((st = r.enableBitSync())) return st;
  return r.receiveDirect();
}

}  // namespace

ConsoleLink::~ConsoleLink() { end(); }

int16_t ConsoleLink::begin(const ReceiverPins& pins,
                           const ConsoleLinkConfig& cfg) {
  if (owner != nullptr || detail::radioUser != nullptr) {
    return kErrAlreadyRunning;
  }
  if (pins.dio1 < 0) return kErrMissingClockPin;
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

  clockPin = pins.dio1;
  dataPin = pins.dio2;
  bitrateBps = cfg.bitrateKbps * 1000.0f;
  const bool high = pins.dio2 >= 32;
  dataInReg = (volatile uint32_t*)(high ? GPIO_IN1_REG : GPIO_IN_REG);
  setReg = (volatile uint32_t*)(high ? GPIO_OUT1_W1TS_REG : GPIO_OUT_W1TS_REG);
  clearReg = (volatile uint32_t*)(high ? GPIO_OUT1_W1TC_REG : GPIO_OUT_W1TC_REG);
  dataMask = 1u << (high ? pins.dio2 - 32 : pins.dio2);
  sending = false;
  flushReceived();
  statsClocksSeen = clocks.load();
  statsDroppedSeen = dropped.load();
  statsFramesSeen = framesTotal;
  statsCrcSeen = decoder.crcErrors();

  pinMode(dataPin, INPUT);
  pinMode(clockPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(clockPin), onClock, RISING);
  owner = this;
  detail::radioUser = this;
  return RADIOLIB_ERR_NONE;
}

void ConsoleLink::end() {
  if (owner != this) return;
  owner = nullptr;
  detail::radioUser = nullptr;
  detachInterrupt(digitalPinToInterrupt(clockPin));
  sending = false;
  pinMode(dataPin, INPUT);
  radio->standby();
  releaseRadio();
  decoder.reset();
}

bool ConsoleLink::poll(uint8_t* frame, size_t* len, uint32_t* endUs) {
  if (owner != this || frame == nullptr || len == nullptr) return false;
  const uint32_t h = head.load(std::memory_order_acquire);
  uint32_t t = tail.load(std::memory_order_relaxed);
  bool got = false;
  while (t != h) {
    const uint8_t bit = (ring[(t / 32) % kRingWords] >> (t % 32)) & 1;
    t++;
    if (decoder.feedBit(bit)) {
      got = true;
      break;
    }
  }
  tail.store(t, std::memory_order_release);
  if (!got) return false;

  framesTotal++;
  *len = decoder.size();
  memcpy(frame, decoder.frame(), *len);
  if (endUs != nullptr) {
    // Bits after the closing flag are already buffered: back-date by them.
    const uint32_t laterBits = h - t;
    *endUs = (uint32_t)esp_timer_get_time() -
             (uint32_t)(laterBits * 1e6f / bitrateBps);
  }
  return true;
}

int16_t ConsoleLink::send(const uint8_t* frame, size_t len) {
  if (owner != this) return kErrNotRunning;
  const size_t bits = encodeAxFrame(frame, len, txBits, kTxMaxBits);
  if (bits == 0) return kErrFrameTooLong;
  txLen = bits;
  txPos = 0;
  // Radio to transmit first, so it no longer drives DIO2; then take the
  // pin. Bits clocked in meanwhile are flushed below.
  int16_t st = radio->transmitDirect();
  pinMode(dataPin, OUTPUT);
  *clearReg = dataMask;
  sending = true;
  if (st == RADIOLIB_ERR_NONE) {
    const uint32_t start = millis();
    while (txPos < txLen && millis() - start < kSendTimeoutMs) delay(1);
    if (txPos < txLen) st = kErrSendTimeout;
  }
  radio->standby();
  sending = false;
  pinMode(dataPin, INPUT);
  const int16_t rx = radio->receiveDirect();
  flushReceived();
  return st != RADIOLIB_ERR_NONE ? st : rx;
}

ConsoleLinkStats ConsoleLink::takeStats() {
  if (owner != this) return ConsoleLinkStats{0, 0, 0, 0};
  const uint32_t c = clocks.load(), d = dropped.load();
  const uint32_t crc = decoder.crcErrors();
  const ConsoleLinkStats s{c - statsClocksSeen, d - statsDroppedSeen,
                           framesTotal - statsFramesSeen, crc - statsCrcSeen};
  statsClocksSeen = c;
  statsDroppedSeen = d;
  statsFramesSeen = framesTotal;
  statsCrcSeen = crc;
  return s;
}

}  // namespace klimalogg

#endif  // ESP32 && __XTENSA__
