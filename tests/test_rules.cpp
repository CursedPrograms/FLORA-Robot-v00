// test_rules.cpp - FLORA's watering rules on the PC. g++ -std=c++17 -I../scripts/esp32 test_rules.cpp && ./a
#include <cstdio>
#include <cstring>
#include "flora_rules.h"

static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { std::printf("FAIL: %s\n", what); fails++; } else std::printf("ok:   %s\n", what); } while (0)

static FloraState dryMorning() {
  FloraState s;
  s.autoOn = true; s.moisture = 18; s.hour = 7; s.rainChance = 20; s.rainInHours = -1;
  s.minutesSinceWater = 600; s.wateredToday = 0;
  return s;
}

int main() {
  FloraRules r;
  FloraDecision d = floraDecide(r, dryMorning());
  CHECK(d.water && d.seconds >= r.minSeconds && d.seconds <= r.maxSeconds, "dry soil in the morning: waters, within limits");

  FloraState s = dryMorning(); s.autoOn = false;
  CHECK(!floraDecide(r, s).water, "automatic watering off: never");
  s = dryMorning(); s.moisture = 45;
  CHECK(!floraDecide(r, s).water, "moist soil: no");
  s = dryMorning(); s.rainChance = 75;
  CHECK(!floraDecide(r, s).water && !std::strcmp(floraDecide(r, s).why, "AURA thinks it will rain"), "AURA expects rain: skip");
  s = dryMorning(); s.rainInHours = 2;
  CHECK(!floraDecide(r, s).water, "rain forecast in 2 h: skip");
  s = dryMorning(); s.rainInHours = 6;
  CHECK(floraDecide(r, s).water, "rain only in 6 h: still waters");
  s = dryMorning(); s.hour = 13;
  CHECK(!floraDecide(r, s).water, "midday heat: later");
  s = dryMorning(); s.hour = 13; s.moisture = 8;
  CHECK(floraDecide(r, s).water, "midday but critically dry: waters");
  s = dryMorning(); s.minutesSinceWater = 10;
  CHECK(!floraDecide(r, s).water, "10 minutes after watering: waits to soak in");
  s = dryMorning(); s.wateredToday = 4;
  CHECK(!floraDecide(r, s).water, "four times today already: no more");
  s = dryMorning(); s.sensorOk = false;
  CHECK(!floraDecide(r, s).water, "broken sensor: never");
  s = dryMorning(); s.moisture = 0;
  CHECK(floraDecide(r, s).seconds == r.maxSeconds, "bone dry: the longest run, capped");
  s = dryMorning(); s.rainChance = -1; s.hour = -1;
  CHECK(floraDecide(r, s).water, "no AURA and no clock: goes by the soil");

  CHECK(floraMoisture(4095, 4095, 1400) == 0 && floraMoisture(1400, 4095, 1400) == 100, "moisture: dry 0 %, wet 100 %");
  CHECK(floraMoisture(2747, 4095, 1400) == 50, "moisture: halfway 50 %");
  CHECK(floraMoisture(1000, 4095, 1400) == 100 && floraMoisture(4200, 4095, 1400) == 0, "moisture: clamped");
  CHECK(!floraSensorOk(4095) && !floraSensorOk(0) && floraSensorOk(2500), "sensor sanity");

  std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
