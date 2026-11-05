#include <unity.h>

#include <vector>

#include "ConsoleSession.h"
#include "../test_console/console_frames.h"

using namespace klimalogg::console;

void setUp() {}
void tearDown() {}

static std::vector<uint8_t> bytes(const char* hex) {
  std::vector<uint8_t> out;
  for (const char* p = hex; p[0] && p[1]; p += 2) {
    auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    out.push_back((uint8_t)(nib(p[0]) << 4 | nib(p[1])));
  }
  return out;
}

static void put24(std::vector<uint8_t>& f, size_t at, uint32_t v) {
  f[at] = (uint8_t)(v >> 16);
  f[at + 1] = (uint8_t)(v >> 8);
  f[at + 2] = (uint8_t)v;
}

// Real history frame with patched latest/this indexes.
static std::vector<uint8_t> history(uint32_t latest, uint32_t thisIdx) {
  auto f = bytes(kHistoryHex);
  put24(f, 7, historyAddress(latest));
  put24(f, 10, historyAddress(thisIdx));
  return f;
}

static const LocalTime kNow{{2026, 11, 4, 17, 12}, 30, 3, true};

struct Reply {
  size_t len;
  uint8_t b[kMaxReplyBytes];
};

static Reply feed(ConsoleSession& s, const std::vector<uint8_t>& f,
                  const LocalTime& now = kNow) {
  Reply r{};
  r.len = s.onFrame(f.data(), f.size(), now, r.b);
  return r;
}

static uint32_t ackIndex(const Reply& r) {
  return historyIndex((uint32_t)r.b[8] << 16 | r.b[9] << 8 | r.b[10]);
}

// ---- time frame ----

void test_time_frame_matches_kl_py_example() {
  // kl.py: "d5 00 0d 01 07 00 60 1a b1 25 58 21 04 03 41 01"
  Header h{0x0107, 0, 0x53, 100, 0x1AB1};
  const LocalTime t{{2014, 10, 30, 21, 58}, 25, 4, true};  // Thursday
  uint8_t out[kTimeFrameBytes];
  TEST_ASSERT_EQUAL(kTimeFrameBytes, buildTimeFrame(h, t, out));
  const uint8_t expected[] = {0x01, 0x07, 0x00, 0x60, 0x1A, 0xB1, 0x25,
                              0x58, 0x21, 0x04, 0x03, 0x41, 0x01};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, kTimeFrameBytes);
}

// ---- session ----

void test_ignores_other_devices() {
  ConsoleSession s(SessionOptions{0x0BAD});
  TEST_ASSERT_EQUAL(0, feed(s, bytes(kCurrentHex)).len);
}

void test_asks_config_first() {
  ConsoleSession s(SessionOptions{0x01B2});
  Reply r = feed(s, bytes(kCurrentHex));
  TEST_ASSERT_EQUAL(kAckBytes, r.len);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)Action::GetConfig, r.b[3]);
  TEST_ASSERT_EQUAL(SessionState::WaitConfig, s.state());
}

void test_config_then_history_probe() {
  ConsoleSession s(SessionOptions{0x01B2});
  Reply r = feed(s, bytes(kConfigHex));
  TEST_ASSERT_TRUE(s.hasConfig());
  TEST_ASSERT_EQUAL_HEX16(0x1D21, s.config().sensorId[3]);
  TEST_ASSERT_EQUAL_HEX8((uint8_t)Action::GetHistory, r.b[3]);
  TEST_ASSERT_EQUAL(kNoIndex, ackIndex(r));  // latest index not known yet
}

void test_full_download_starts_at_oldest_record() {
  ConsoleSession s(SessionOptions{0x01B2});
  feed(s, bytes(kConfigHex));
  Reply r = feed(s, history(38275, 38406));  // probe answer
  TEST_ASSERT_EQUAL(0, s.takeRecords(nullptr, nullptr, 0));  // not requested
  TEST_ASSERT_EQUAL(SessionState::Download, s.state());
  // The console overwrites the oldest slots while it logs on, so the
  // oldest kOverwriteMargin + 1 slots are left out: records after 38292.
  TEST_ASSERT_EQUAL(38292, ackIndex(r));
  TEST_ASSERT_EQUAL(kHistorySlots - 1 - kOverwriteMargin, s.recordsTotal());
  TEST_ASSERT_EQUAL(0, s.recordsDone());
}

void test_full_download_survives_new_record_after_probe() {
  ConsoleSession s(SessionOptions{0x01B2});
  feed(s, bytes(kConfigHex));
  feed(s, history(38275, 38406));            // probe: asks after 38292
  Reply r = feed(s, history(38276, 38406));  // console logged one more
  TEST_ASSERT_EQUAL(SessionState::Download, s.state());
  TEST_ASSERT_EQUAL(38292, ackIndex(r));
  r = feed(s, history(38277, 38298));        // records 38293..38298
  HistoryRecord recs[kRecordsPerFrame];
  uint32_t idx[kRecordsPerFrame];
  TEST_ASSERT_EQUAL(6, s.takeRecords(recs, idx, kRecordsPerFrame));
  TEST_ASSERT_EQUAL(38293, idx[0]);
  TEST_ASSERT_EQUAL(SessionState::Download, s.state());
  TEST_ASSERT_EQUAL(38298, ackIndex(r));
}

void test_unwrapped_ring_downloads_from_slot_one() {
  // Ring never wrapped: the console answers a request beyond its newest
  // record with the newest frame. Slot 0 holds no record.
  ConsoleSession s(SessionOptions{0x01B2});
  feed(s, bytes(kConfigHex));
  feed(s, history(39348, 39430));            // probe: asks after 39364
  Reply r = feed(s, history(39348, 39348));  // console: only up to 39348
  TEST_ASSERT_EQUAL(SessionState::Download, s.state());
  TEST_ASSERT_EQUAL(0, ackIndex(r));         // records after slot 0
  TEST_ASSERT_EQUAL(39348, s.recordsTotal());
  TEST_ASSERT_EQUAL(0, s.takeRecords(nullptr, nullptr, 0));
  r = feed(s, history(39348, 6));            // records 1..6
  HistoryRecord recs[kRecordsPerFrame];
  uint32_t idx[kRecordsPerFrame];
  TEST_ASSERT_EQUAL(6, s.takeRecords(recs, idx, kRecordsPerFrame));
  TEST_ASSERT_EQUAL(1, idx[0]);
  TEST_ASSERT_EQUAL(6, s.recordsDone());
  TEST_ASSERT_EQUAL(6, ackIndex(r));
}

void test_limited_download_and_progress() {
  ConsoleSession s(SessionOptions{0x01B2, 12});
  feed(s, bytes(kConfigHex));
  Reply r = feed(s, history(1000, 5));
  TEST_ASSERT_EQUAL(988, ackIndex(r));  // last 12 records: 989..1000
  TEST_ASSERT_EQUAL(12, s.recordsTotal());

  r = feed(s, history(1000, 994));  // records 989..994
  HistoryRecord recs[kRecordsPerFrame];
  uint32_t idx[kRecordsPerFrame];
  TEST_ASSERT_EQUAL(6, s.takeRecords(recs, idx, kRecordsPerFrame));
  TEST_ASSERT_EQUAL(989, idx[0]);
  TEST_ASSERT_EQUAL(994, idx[5]);
  TEST_ASSERT_EQUAL(6, s.recordsDone());
  TEST_ASSERT_EQUAL(994, ackIndex(r));

  r = feed(s, history(1000, 1000));  // 995..1000: done
  TEST_ASSERT_EQUAL(6, s.takeRecords(recs, idx, kRecordsPerFrame));
  TEST_ASSERT_EQUAL(12, s.recordsDone());
  TEST_ASSERT_EQUAL(SessionState::SetTime, s.state());
  TEST_ASSERT_EQUAL_HEX8((uint8_t)Action::RequestSetTime, r.b[3]);
}

void test_partial_frame_only_new_records() {
  ConsoleSession s(SessionOptions{0x01B2, 12});
  feed(s, bytes(kConfigHex));
  feed(s, history(1000, 5));        // asks after 988
  feed(s, history(1000, 991));      // only 3 new: 989..991
  HistoryRecord recs[kRecordsPerFrame];
  uint32_t idx[kRecordsPerFrame];
  TEST_ASSERT_EQUAL(3, s.takeRecords(recs, idx, kRecordsPerFrame));
  TEST_ASSERT_EQUAL(989, idx[0]);
  TEST_ASSERT_EQUAL(991, idx[2]);
}

void test_repeated_frame_after_skipped_reply() {
  ConsoleSession s(SessionOptions{0x01B2, 12});
  feed(s, bytes(kConfigHex));
  feed(s, history(1000, 5));
  feed(s, history(1000, 994));
  HistoryRecord recs[kRecordsPerFrame];
  uint32_t idx[kRecordsPerFrame];
  s.takeRecords(recs, idx, kRecordsPerFrame);
  // Our reply was not sent; the console repeats the same frame.
  Reply r = feed(s, history(1000, 994));
  TEST_ASSERT_EQUAL(0, s.takeRecords(recs, idx, kRecordsPerFrame));
  TEST_ASSERT_EQUAL(994, ackIndex(r));
  TEST_ASSERT_EQUAL(6, s.recordsDone());
}

void test_unexpected_index_asks_again() {
  ConsoleSession s(SessionOptions{0x01B2, 12});
  feed(s, bytes(kConfigHex));
  feed(s, history(1000, 5));
  Reply r = feed(s, history(1000, 700));  // not after 988
  HistoryRecord recs[kRecordsPerFrame];
  uint32_t idx[kRecordsPerFrame];
  TEST_ASSERT_EQUAL(0, s.takeRecords(recs, idx, kRecordsPerFrame));
  TEST_ASSERT_EQUAL(988, ackIndex(r));
}

void test_skips_records_the_console_refuses() {
  // The console answers some requests with its newest frame for good
  // (records written while its battery ran out). Skip 1, 2, 4, ... records.
  ConsoleSession s(SessionOptions{0x01B2, 12});
  feed(s, bytes(kConfigHex));
  Reply r = feed(s, history(1000, 5));  // asks after 988
  for (int i = 1; i < kRefusalsBeforeSkip; i++) {
    r = feed(s, history(1000, 1000));
    TEST_ASSERT_EQUAL(988, ackIndex(r));
  }
  r = feed(s, history(1000, 1000));
  TEST_ASSERT_EQUAL(989, ackIndex(r));  // skipped 989
  TEST_ASSERT_EQUAL(1, s.recordsSkipped());
  for (int i = 0; i < kRefusalsBeforeSkip; i++) r = feed(s, history(1000, 1000));
  TEST_ASSERT_EQUAL(991, ackIndex(r));  // skipped 990, 991
  TEST_ASSERT_EQUAL(3, s.recordsSkipped());

  r = feed(s, history(1000, 997));      // 992..997 arrive again
  HistoryRecord recs[kRecordsPerFrame];
  uint32_t idx[kRecordsPerFrame];
  TEST_ASSERT_EQUAL(6, s.takeRecords(recs, idx, kRecordsPerFrame));
  TEST_ASSERT_EQUAL(992, idx[0]);
  TEST_ASSERT_EQUAL(9, s.recordsDone());
  TEST_ASSERT_EQUAL(997, ackIndex(r));
  // After progress a refusal starts again at one record.
  for (int i = 0; i < kRefusalsBeforeSkip; i++) r = feed(s, history(1010, 1010));
  TEST_ASSERT_EQUAL(998, ackIndex(r));
}

void test_wraps_around_the_ring() {
  ConsoleSession s(SessionOptions{0x01B2, 10});
  feed(s, bytes(kConfigHex));
  Reply r = feed(s, history(3, 77));
  TEST_ASSERT_EQUAL(kHistorySlots - 7, ackIndex(r));  // records 51194..3
  HistoryRecord recs[kRecordsPerFrame];
  uint32_t idx[kRecordsPerFrame];
  feed(s, history(3, kHistorySlots - 1));  // 51194..51199
  TEST_ASSERT_EQUAL(6, s.takeRecords(recs, idx, kRecordsPerFrame));
  TEST_ASSERT_EQUAL(kHistorySlots - 6, idx[0]);
  feed(s, history(3, 3));  // 51198..3, new: 0..3
  TEST_ASSERT_EQUAL(4, s.takeRecords(recs, idx, kRecordsPerFrame));
  TEST_ASSERT_EQUAL(0, idx[0]);
  TEST_ASSERT_EQUAL(3, idx[3]);
  TEST_ASSERT_EQUAL(10, s.recordsDone());
  TEST_ASSERT_EQUAL(SessionState::SetTime, s.state());
}

void test_latest_moves_during_download() {
  ConsoleSession s(SessionOptions{0x01B2, 6});
  feed(s, bytes(kConfigHex));
  feed(s, history(1000, 5));           // after 994
  Reply r = feed(s, history(1002, 1000));  // console logged 2 more meanwhile
  TEST_ASSERT_EQUAL(SessionState::Download, s.state());
  TEST_ASSERT_EQUAL(8, s.recordsTotal());
  TEST_ASSERT_EQUAL(1000, ackIndex(r));
  feed(s, history(1002, 1002));
  TEST_ASSERT_EQUAL(SessionState::SetTime, s.state());
}

void test_never_reads_past_latest() {
  ConsoleSession s(SessionOptions{0x01B2, 10});
  feed(s, bytes(kConfigHex));
  feed(s, history(1000, 5));   // after 990
  feed(s, history(1000, 996)); // 991..996
  HistoryRecord recs[kRecordsPerFrame];
  uint32_t idx[kRecordsPerFrame];
  s.takeRecords(recs, idx, kRecordsPerFrame);
  // A frame running past the newest record: 997..1002, of which 1001
  // and 1002 are the previous lap of the ring.
  Reply r = feed(s, history(1000, 1002));
  TEST_ASSERT_EQUAL(4, s.takeRecords(recs, idx, kRecordsPerFrame));
  TEST_ASSERT_EQUAL(997, idx[0]);
  TEST_ASSERT_EQUAL(1000, idx[3]);
  TEST_ASSERT_EQUAL(10, s.recordsDone());
  TEST_ASSERT_EQUAL(SessionState::SetTime, s.state());
  TEST_ASSERT_EQUAL_HEX8((uint8_t)Action::RequestSetTime, r.b[3]);
}

void test_set_time_sequence() {
  ConsoleSession s(SessionOptions{0x01B2, 6});
  feed(s, bytes(kConfigHex));
  feed(s, history(1000, 5));
  feed(s, history(1000, 1000));
  TEST_ASSERT_EQUAL(SessionState::SetTime, s.state());

  std::vector<uint8_t> req = {0x01, 0xB2, 0x00, 0x53, 0x64, 0x14, 0xD1};
  Reply r = feed(s, req);
  TEST_ASSERT_EQUAL(kTimeFrameBytes, r.len);
  TEST_ASSERT_EQUAL_HEX8(0x60, r.b[3]);
  TEST_ASSERT_EQUAL_HEX8(0x30, r.b[6]);  // seconds
  TEST_ASSERT_EQUAL_HEX8(0x12, r.b[7]);  // minutes
  TEST_ASSERT_EQUAL_HEX8(0x17, r.b[8]);  // hour

  std::vector<uint8_t> written = {0x01, 0xB2, 0x00, 0x10, 0x64, 0x14, 0xD1};
  TEST_ASSERT_EQUAL(0, feed(s, written).len);
  TEST_ASSERT_EQUAL(SessionState::Done, s.state());
  TEST_ASSERT_TRUE(s.timeWasSet());
}

void test_skips_set_time_without_clock() {
  ConsoleSession s(SessionOptions{0x01B2, 6});
  feed(s, bytes(kConfigHex));
  feed(s, history(1000, 5));
  LocalTime noClock{};
  Reply r = feed(s, history(1000, 1000), noClock);
  TEST_ASSERT_EQUAL(SessionState::Done, s.state());
  TEST_ASSERT_EQUAL(0, r.len);
}

void test_done_stops_replying() {
  ConsoleSession s(SessionOptions{0x01B2, 6, false});
  feed(s, bytes(kConfigHex));
  feed(s, history(1000, 5));
  feed(s, history(1000, 1000));
  TEST_ASSERT_EQUAL(SessionState::Done, s.state());
  TEST_ASSERT_EQUAL(0, feed(s, bytes(kCurrentHex)).len);
}

void test_keeps_latest_current_weather() {
  ConsoleSession s(SessionOptions{0x01B2});
  feed(s, bytes(kCurrentHex));
  TEST_ASSERT_TRUE(s.hasCurrent());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.9f, s.current().channel[0].temperatureC);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_time_frame_matches_kl_py_example);
  RUN_TEST(test_ignores_other_devices);
  RUN_TEST(test_asks_config_first);
  RUN_TEST(test_config_then_history_probe);
  RUN_TEST(test_full_download_starts_at_oldest_record);
  RUN_TEST(test_full_download_survives_new_record_after_probe);
  RUN_TEST(test_unwrapped_ring_downloads_from_slot_one);
  RUN_TEST(test_limited_download_and_progress);
  RUN_TEST(test_partial_frame_only_new_records);
  RUN_TEST(test_repeated_frame_after_skipped_reply);
  RUN_TEST(test_unexpected_index_asks_again);
  RUN_TEST(test_skips_records_the_console_refuses);
  RUN_TEST(test_wraps_around_the_ring);
  RUN_TEST(test_latest_moves_during_download);
  RUN_TEST(test_never_reads_past_latest);
  RUN_TEST(test_set_time_sequence);
  RUN_TEST(test_skips_set_time_without_clock);
  RUN_TEST(test_done_stops_replying);
  RUN_TEST(test_keeps_latest_current_weather);
  return UNITY_END();
}
