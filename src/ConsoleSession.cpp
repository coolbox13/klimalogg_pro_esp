#include "ConsoleSession.h"

namespace klimalogg {
namespace console {

namespace {

constexpr uint8_t kTypeRequestSetTime = 0x53;
constexpr uint8_t kSendTimeAction = 0x60;

uint8_t bcd(uint8_t v) { return (uint8_t)((v / 10) << 4 | (v % 10)); }

// Steps from ring index a forward to b.
uint32_t distance(uint32_t a, uint32_t b) {
  return (b + kHistorySlots - a) % kHistorySlots;
}

uint32_t ringAdd(uint32_t index, int32_t delta) {
  return (uint32_t)(((int64_t)index + delta + kHistorySlots) % kHistorySlots);
}

}  // namespace

size_t buildTimeFrame(const Header& h, const LocalTime& t,
                      uint8_t out[kTimeFrameBytes]) {
  const uint8_t year = (uint8_t)(t.date.year - 2000);
  out[0] = (uint8_t)(h.deviceId >> 8);
  out[1] = (uint8_t)h.deviceId;
  out[2] = h.loggerId;
  out[3] = kSendTimeAction;
  out[4] = (uint8_t)(h.configChecksum >> 8);
  out[5] = (uint8_t)h.configChecksum;
  out[6] = bcd(t.second);
  out[7] = bcd(t.date.minute);
  out[8] = bcd(t.date.hour);
  // Nibble-packed, low nibble first: weekday, day, month, year.
  out[9] = (uint8_t)((t.date.day % 10) << 4 | (t.weekday % 10));
  out[10] = (uint8_t)((t.date.month % 10) << 4 | (t.date.day / 10));
  out[11] = (uint8_t)((year % 10) << 4 | (t.date.month / 10));
  out[12] = (uint8_t)(year / 10);
  return kTimeFrameBytes;
}

ConsoleSession::ConsoleSession(const SessionOptions& options) : opt_(options) {}

size_t ConsoleSession::ack(const Header& h, Action a, uint32_t index,
                           uint8_t* out) const {
  return buildAck(h, a, opt_.comInterval, index, out);
}

// Reply for the current state when the frame itself needs nothing special.
size_t ConsoleSession::afterDownload(const Header& h, const LocalTime& now,
                                     uint8_t* out) {
  if (opt_.setTime && now.valid) {
    state_ = SessionState::SetTime;
    return ack(h, Action::RequestSetTime, kNoIndex, out);
  }
  state_ = SessionState::Done;
  return 0;
}

size_t ConsoleSession::onFrame(const uint8_t* f, size_t len,
                               const LocalTime& now,
                               uint8_t out[kMaxReplyBytes]) {
  Header h;
  if (!parseHeader(f, len, &h) || h.deviceId != opt_.deviceId) return 0;
  pendingCount_ = 0;

  if (h.type == kTypeCurrentWeather && parseCurrentWeather(f, len, &current_)) {
    hasCurrent_ = true;
  }
  if (h.type == kTypeConfig && parseConfig(f, len, &config_)) {
    hasConfig_ = true;
    if (state_ == SessionState::WaitConfig) state_ = SessionState::Probe;
  }
  if (h.type == kTypeHistory) return onHistory(f, len, h, now, out);
  if (h.type == kTypeDataWritten && state_ == SessionState::SetTime) {
    timeSet_ = true;
    state_ = SessionState::Done;
    return 0;
  }
  if (h.type == kTypeRequestSetTime && state_ == SessionState::SetTime &&
      now.valid) {
    return buildTimeFrame(h, now, out);
  }

  switch (state_) {
    case SessionState::WaitConfig:
      return ack(h, Action::GetConfig, kNoIndex, out);
    case SessionState::Probe:
      return ack(h, Action::GetHistory, kNoIndex, out);
    case SessionState::Download:
      return ack(h, Action::GetHistory, requested_, out);
    case SessionState::SetTime:
      return now.valid ? ack(h, Action::RequestSetTime, kNoIndex, out)
                       : afterDownload(h, now, out);
    case SessionState::Done:
      return 0;
  }
  return 0;
}

size_t ConsoleSession::onHistory(const uint8_t* f, size_t len, const Header& h,
                                 const LocalTime& now, uint8_t* out) {
  HistoryFrame hf;
  if (state_ == SessionState::WaitConfig) {
    return ack(h, Action::GetConfig, kNoIndex, out);
  }
  if (state_ == SessionState::Done) return 0;
  if (!parseHistory(f, len, &hf) || hf.latestIndex == kNoIndex) {
    return state_ == SessionState::Download
               ? ack(h, Action::GetHistory, requested_, out)
               : ack(h, Action::GetHistory, kNoIndex, out);
  }

  if (state_ == SessionState::Probe) {
    // The oldest slots are overwritten next; leave them out.
    const uint32_t most = kHistorySlots - 1 - kOverwriteMargin;
    const uint32_t count =
        (opt_.maxRecords == 0 || opt_.maxRecords > most) ? most : opt_.maxRecords;
    start_ = ringAdd(hf.latestIndex, 1 - (int32_t)count);
    requested_ = ringAdd(start_, -1);
    total_ = count;
    done_ = 0;
    state_ = SessionState::Download;
    return ack(h, Action::GetHistory, requested_, out);
  }
  if (state_ != SessionState::Download) {
    return afterDownload(h, now, out);
  }

  // Ring never wrapped (e.g. after new batteries): the console answers a
  // request beyond its newest record with the newest frame. Its records
  // then run from slot 1 (slot 0 holds none).
  if (done_ == 0 && hf.thisIndex == hf.latestIndex &&
      distance(requested_, hf.latestIndex) > (uint32_t)kRecordsPerFrame &&
      start_ > hf.latestIndex) {
    start_ = 1;
    requested_ = 0;
  }

  // The console logs on during the download.
  total_ = distance(ringAdd(start_, -1), hf.latestIndex);
  const uint32_t step = distance(requested_, hf.thisIndex);
  if (step >= 1 && step <= (uint32_t)kRecordsPerFrame) {
    // Never past the newest record: beyond it lies the ring's previous lap.
    const uint32_t remaining = distance(requested_, hf.latestIndex);
    const uint32_t take = step < remaining ? step : remaining;
    // record[r] has index thisIndex - 5 + r.
    for (int r = 0; r < kRecordsPerFrame; r++) {
      const uint32_t index = ringAdd(hf.thisIndex, r - (kRecordsPerFrame - 1));
      const uint32_t offset = distance(requested_, index);
      if (offset < 1 || offset > take || !hf.record[r].valid) continue;
      pending_[pendingCount_] = hf.record[r];
      pendingIndex_[pendingCount_] = index;
      pendingCount_++;
    }
    requested_ = ringAdd(requested_, (int32_t)take);
    done_ += take;
    refusals_ = 0;
    skip_ = 1;
  } else if (hf.thisIndex == hf.latestIndex &&
             ++refusals_ >= kRefusalsBeforeSkip) {
    skipAhead(hf.latestIndex);
  }
  if (requested_ == hf.latestIndex) return afterDownload(h, now, out);
  return ack(h, Action::GetHistory, requested_, out);
}

// The console keeps answering with its newest frame: it will not send the
// records after `requested_` (seen on records written while its battery
// ran out). Leave some out, more each time it refuses again.
void ConsoleSession::skipAhead(uint32_t latest) {
  const uint32_t remaining = distance(requested_, latest);
  const uint32_t jump = skip_ < remaining ? skip_ : remaining;
  requested_ = ringAdd(requested_, (int32_t)jump);
  done_ += jump;
  skipped_ += jump;
  skip_ = skip_ * 2 < kMaxSkip ? skip_ * 2 : kMaxSkip;
  refusals_ = 0;
}

size_t ConsoleSession::takeRecords(HistoryRecord* records, uint32_t* indexes,
                                   size_t max) {
  const size_t n = pendingCount_ < max ? pendingCount_ : max;
  for (size_t i = 0; i < n; i++) {
    records[i] = pending_[i];
    indexes[i] = pendingIndex_[i];
  }
  pendingCount_ = 0;
  return n;
}

}  // namespace console
}  // namespace klimalogg
