#include <string.h>
#include <unity.h>

#include "SensorTable.h"

using namespace klimalogg;

void setUp() {}
void tearDown() {}

static Reading makeReading(uint16_t id, float temp) {
  Reading r = {};
  r.id = id;
  r.temperatureC = temp;
  r.hasHumidity = true;
  r.humidity = 50;
  return r;
}

void test_table_adds_and_updates_sensor() {
  SensorTable t;
  TEST_ASSERT_EQUAL(0, t.size());
  t.update(makeReading(0x1234, 20.0f), -80, 100);
  t.update(makeReading(0x1234, 21.5f), -70, 200);
  TEST_ASSERT_EQUAL(1, t.size());
  const SensorEntry& e = t.at(0);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, e.reading.temperatureC);
  TEST_ASSERT_EQUAL(-70, e.rssi);
  TEST_ASSERT_EQUAL_UINT32(200, e.lastSeenMs);
  TEST_ASSERT_EQUAL_UINT32(2, e.packets);
}

void test_table_keeps_first_seen_order() {
  SensorTable t;
  t.update(makeReading(0x0002, 1.0f), -80, 1);
  t.update(makeReading(0x0001, 2.0f), -80, 2);
  t.update(makeReading(0x0002, 3.0f), -80, 3);
  TEST_ASSERT_EQUAL_HEX16(0x0002, t.at(0).reading.id);
  TEST_ASSERT_EQUAL_HEX16(0x0001, t.at(1).reading.id);
}

void test_table_drops_new_sensors_when_full() {
  SensorTable t;
  for (uint16_t i = 0; i < SensorTable::kCapacity; i++) {
    TEST_ASSERT_TRUE(t.update(makeReading(i + 1, 0.0f), -80, i));
  }
  TEST_ASSERT_FALSE(t.update(makeReading(0x7FFF, 0.0f), -80, 99));
  TEST_ASSERT_EQUAL(SensorTable::kCapacity, t.size());
}

void test_table_marks_stale_entries() {
  SensorTable t;
  t.update(makeReading(0x0001, 1.0f), -80, 1000);
  TEST_ASSERT_FALSE(t.isStale(0, 1000 + SensorTable::kStaleMs - 1));
  TEST_ASSERT_TRUE(t.isStale(0, 1000 + SensorTable::kStaleMs));
}

void test_format_line_with_humidity_and_low_battery() {
  SensorEntry e = {};
  e.reading = makeReading(0x65B0, 22.0f);
  e.reading.humidity = 35;
  e.reading.batteryLow = true;
  char buf[32];
  formatLine(e, false, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("65B0  22.0C 35% BAT", buf);
}

void test_format_line_negative_without_humidity_and_stale() {
  SensorEntry e = {};
  e.reading = makeReading(0x215C, -1.2f);
  e.reading.hasHumidity = false;
  char buf[32];
  formatLine(e, true, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("215C  -1.2C  -- ?", buf);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_table_adds_and_updates_sensor);
  RUN_TEST(test_table_keeps_first_seen_order);
  RUN_TEST(test_table_drops_new_sensors_when_full);
  RUN_TEST(test_table_marks_stale_entries);
  RUN_TEST(test_format_line_with_humidity_and_low_battery);
  RUN_TEST(test_format_line_negative_without_humidity_and_stale);
  return UNITY_END();
}
