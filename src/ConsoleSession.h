// History download from a KlimaLogg Pro console, acting as its USB
// transceiver. Pure C++, host tested; the radio side feeds it frames and
// sends what it returns.
//
//   ConsoleSession s({0x01B2});           // the paired transceiver's ID
//   size_t n = s.onFrame(frame, len, now, reply);
//   if (n) send reply 10..150 ms after the frame ended
//   n = s.takeRecords(recs, idx, 6);      // after every onFrame()
//
// Flow: config -> probe history (learn the newest index) -> history, oldest
// first -> set the console clock (if `now` is valid) -> done (no replies).
// Not answering a frame is safe: the console repeats it and the session
// answers it again. The radio side can use that to keep the duty cycle low.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ConsoleProtocol.h"

namespace klimalogg {
namespace console {

constexpr size_t kTimeFrameBytes = 13;
constexpr size_t kMaxReplyBytes = kTimeFrameBytes;
constexpr uint8_t kDefaultComInterval = 8;  // kl.py default

// Local wall-clock time for the console (it has no time zone).
struct LocalTime {
  DateTime date;
  uint8_t second;
  uint8_t weekday;  // 1 = Monday .. 7 = Sunday
  bool valid;
};

// Writes the 0x60 "send time" frame (kTimeFrameBytes) into `out`.
size_t buildTimeFrame(const Header& received, const LocalTime& now,
                      uint8_t out[kTimeFrameBytes]);

// Frames in a row the console may answer with its newest frame instead of
// the requested records before the session skips ahead (it repeats each
// frame ~4 times per reply we may send, so this is ~3 requests).
constexpr int kRefusalsBeforeSkip = 12;
// Largest skip; skips double from 1 while the console keeps refusing.
constexpr uint32_t kMaxSkip = 256;

struct SessionOptions {
  uint16_t deviceId;           // ID the console is paired with
  uint32_t maxRecords = 0;     // 0 = everything the console holds
  bool setTime = true;         // set the console clock when done
  uint8_t comInterval = kDefaultComInterval;
};

enum class SessionState : uint8_t { WaitConfig, Probe, Download, SetTime, Done };

class ConsoleSession {
 public:
  explicit ConsoleSession(const SessionOptions& options);

  // Handles a received frame; returns the reply length (0 = no reply).
  size_t onFrame(const uint8_t* frame, size_t len, const LocalTime& now,
                 uint8_t out[kMaxReplyBytes]);

  // Copies the new records of the last frame into the arrays (oldest
  // first) and returns how many. Call after every onFrame(); the next
  // frame replaces them. `max` should be kRecordsPerFrame.
  size_t takeRecords(HistoryRecord* records, uint32_t* indexes, size_t max);

  SessionState state() const { return state_; }
  bool hasConfig() const { return hasConfig_; }
  const ConsoleConfig& config() const { return config_; }
  bool hasCurrent() const { return hasCurrent_; }
  const CurrentWeather& current() const { return current_; }
  uint32_t recordsDone() const { return done_; }
  uint32_t recordsTotal() const { return total_; }
  // Records left out because the console refused to send them.
  uint32_t recordsSkipped() const { return skipped_; }
  bool timeWasSet() const { return timeSet_; }

 private:
  size_t ack(const Header& h, Action a, uint32_t index, uint8_t* out) const;
  size_t onHistory(const uint8_t* f, size_t len, const Header& h,
                   const LocalTime& now, uint8_t* out);
  size_t afterDownload(const Header& h, const LocalTime& now, uint8_t* out);
  void skipAhead(uint32_t latest);

  SessionOptions opt_;
  SessionState state_ = SessionState::WaitConfig;
  ConsoleConfig config_{};
  bool hasConfig_ = false;
  CurrentWeather current_{};
  bool hasCurrent_ = false;
  uint32_t start_ = 0;       // index of the first record to fetch
  uint32_t requested_ = 0;   // last index stored; console sends after it
  uint32_t done_ = 0;
  uint32_t total_ = 0;
  uint32_t skipped_ = 0;
  int refusals_ = 0;     // newest-frame answers since the last progress
  uint32_t skip_ = 1;    // next skip size
  bool timeSet_ = false;
  HistoryRecord pending_[kRecordsPerFrame];
  uint32_t pendingIndex_[kRecordsPerFrame];
  size_t pendingCount_ = 0;
};

}  // namespace console
}  // namespace klimalogg
