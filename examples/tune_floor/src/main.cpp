// Find a good ReceiverConfig::ookFloor for your location.
//
// A floor below the noise level makes the radio's OOK slicer toggle on
// noise: ~100k interrupts per second for nothing. A floor too high loses
// weak sensors. This sketch measures both and prints a summary:
//
//   1. Quick scan: idle edges/s for floors 0..250 (lowest of two short
//      windows, so a passing telegram does not skew it).
//   2. Sensor test: around the first quiet floor, run each candidate for
//      60 s and count valid telegrams per sensor (they send every ~10 s).
//
// Pick the highest floor that still receives every sensor you care about,
// then go one step lower for margin.

#include <Arduino.h>
#include <KlimaLoggReceiver.h>

#include <map>
#include <vector>

namespace {

constexpr uint8_t kScanStart = 0;
constexpr uint8_t kScanEnd = 250;
constexpr uint8_t kScanStep = 10;
constexpr uint32_t kScanWindowMs = 1000;
constexpr uint32_t kQuietEdgesPerSecond = 100;
constexpr int kCandidateStep = 10;
constexpr int kCandidatesBelow = 2;
constexpr int kCandidatesAbove = 3;
constexpr uint32_t kTestMs = 60000;

klimalogg::Receiver receiver;

// Keeps the receiver drained while waiting. Returns telegrams seen.
void pollFor(uint32_t ms, std::map<uint16_t, int>* valid, int* bad) {
  const uint32_t start = millis();
  while (millis() - start < ms) {
    klimalogg::Telegram t;
    while (receiver.poll(&t)) {
      if (t.result.status != klimalogg::Status::Ok) {
        if (bad) (*bad)++;
        continue;
      }
      if (valid) (*valid)[t.result.reading.id]++;
    }
    delay(1);
  }
}

uint32_t idleEdgesPerSecond(uint8_t floor) {
  receiver.setOokFloor(floor);
  uint32_t best = UINT32_MAX;
  for (int window = 0; window < 2; window++) {
    receiver.takeStats();
    pollFor(kScanWindowMs, nullptr, nullptr);
    const uint32_t eps = receiver.takeStats().edges * 1000 / kScanWindowMs;
    if (eps < best) best = eps;
  }
  return best;
}

int quickScan() {
  Serial.println("\n== Phase 1: idle edges/s per floor ==");
  int firstQuiet = -1;
  for (int f = kScanStart; f <= kScanEnd; f += kScanStep) {
    const uint32_t eps = idleEdgesPerSecond((uint8_t)f);
    Serial.printf("floor %3d: %6lu edges/s\n", f, (unsigned long)eps);
    if (firstQuiet < 0 && eps < kQuietEdgesPerSecond) firstQuiet = f;
  }
  return firstQuiet;
}

struct Result {
  int floor;
  uint32_t edgesPerSecond;
  std::map<uint16_t, int> valid;
  int bad;
};

Result testFloor(int floor) {
  Result r{floor, 0, {}, 0};
  receiver.setOokFloor((uint8_t)floor);
  receiver.takeStats();
  pollFor(kTestMs, &r.valid, &r.bad);
  r.edgesPerSecond = receiver.takeStats().edges * 1000 / kTestMs;
  return r;
}

void printResult(const Result& r) {
  Serial.printf("floor %3d: %6lu edges/s, bad %d, sensors:", r.floor,
                (unsigned long)r.edgesPerSecond, r.bad);
  for (const auto& kv : r.valid) Serial.printf(" %04X=%d", kv.first, kv.second);
  Serial.println();
}

void run() {
  const int quiet = quickScan();
  if (quiet < 0) {
    Serial.println("No quiet floor found up to 250; check the antenna.");
    return;
  }
  Serial.printf("First quiet floor: %d\n", quiet);

  std::vector<int> candidates;
  for (int i = -kCandidatesBelow; i <= kCandidatesAbove; i++) {
    const int f = quiet + i * kCandidateStep;
    if (f >= 0 && f <= 255) candidates.push_back(f);
  }

  Serial.printf("\n== Phase 2: %u floors x %lu s ==\n",
                (unsigned)candidates.size(), (unsigned long)(kTestMs / 1000));
  std::vector<Result> results;
  for (int f : candidates) {
    results.push_back(testFloor(f));
    printResult(results.back());
  }

  Serial.println("\n== Summary ==");
  for (const auto& r : results) printResult(r);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nKlimaLogg OOK floor tuning");
  const int16_t st = receiver.begin(klimalogg::ReceiverPins::lilygoT3V161());
  if (st != 0) {
    Serial.printf("radio init failed (%d)\n", st);
    return;
  }
  run();
  Serial.println("\nDone. Reset to run again.");
}

void loop() {
  klimalogg::Telegram t;
  while (receiver.poll(&t)) {
  }
  delay(1);
}
