// flora_rules.h - when FLORA waters, and how much. Plain C++ (no Arduino calls), so the same rules are
// tested on the PC (tests/test_rules.cpp) and run on the board.
//
// She waters only when all of these hold:
//   * automatic watering is on (it starts off: you turn it on from her page once she's calibrated)
//   * the soil is drier than dryAt (%)
//   * no rain is coming: AURA's guess for the next hours is under rainSkipChance %, and Open-Meteo
//     doesn't have rain in the next rainSkipHours (without AURA she just goes by the soil)
//   * it isn't the hottest part of the day (quietFrom..quietTo: water evaporates before the roots get it)
//     unless the soil is critically dry (under criticalAt %)
//   * at least minGapMin minutes since she last watered (the water needs time to soak in and be read)
//   * fewer than maxPerDay waterings today
// and then for longer the drier it is, never more than maxSeconds. A sensor that reads nonsense
// (a disconnected fork reads 0 or 100 % flat) stops everything.
#pragma once
#include <stdint.h>

struct FloraRules {
  int dryAt = 30;            // % moisture: below this she wants to water
  int criticalAt = 12;       // % moisture: below this the midday pause is skipped
  int maxSeconds = 10;       // longest pump run
  int minSeconds = 3;
  int minGapMin = 30;        // minutes between waterings
  int maxPerDay = 4;
  int rainSkipChance = 60;   // % (AURA's own guess): skip if rain is at least this likely
  int rainSkipHours = 3;     // skip if Open-Meteo has rain within this many hours
  int quietFrom = 11;        // local hours: no watering in the midday heat...
  int quietTo = 15;          // ...from quietFrom up to (not including) quietTo
};

struct FloraState {
  bool autoOn = false;
  bool sensorOk = true;
  int moisture = -1;         // %, -1 = not read yet
  long minutesSinceWater = 1L << 30;
  int wateredToday = 0;
  int hour = -1;             // local hour, -1 = don't know the time yet
  int rainChance = -1;       // AURA's guess, % (-1 = no AURA)
  int rainInHours = -1;      // Open-Meteo: rain within this many hours (-1 = none / unknown)
};

struct FloraDecision {
  bool water;
  int seconds;
  const char* why;           // for her page and her status
};

inline FloraDecision floraDecide(const FloraRules& r, const FloraState& s) {
  if (!s.autoOn) return {false, 0, "automatic watering is off"};
  if (!s.sensorOk || s.moisture < 0) return {false, 0, "the soil sensor isn't reading properly"};
  if (s.moisture >= r.dryAt) return {false, 0, "the soil is moist enough"};
  if (s.wateredToday >= r.maxPerDay) return {false, 0, "watered enough times today"};
  if (s.minutesSinceWater < r.minGapMin) return {false, 0, "waiting for the last watering to soak in"};
  if (s.rainChance >= r.rainSkipChance) return {false, 0, "AURA thinks it will rain"};
  if (s.rainInHours >= 0 && s.rainInHours <= r.rainSkipHours) return {false, 0, "rain is forecast soon"};
  bool critical = s.moisture < r.criticalAt;
  if (!critical && s.hour >= r.quietFrom && s.hour < r.quietTo) return {false, 0, "too hot right now: later"};
  int deficit = r.dryAt - s.moisture;                      // 1..dryAt
  int secs = r.minSeconds + deficit / 4;
  if (secs > r.maxSeconds) secs = r.maxSeconds;
  if (secs < r.minSeconds) secs = r.minSeconds;
  return {true, secs, critical ? "the soil is very dry" : "the soil is dry"};
}

// Raw ADC to moisture %: dryRaw is the fork in air (or dry soil), wetRaw in water. The YL-69 reads
// higher when drier. Outside 0..100 is clamped; a reading stuck at the rails is "not ok".
inline int floraMoisture(int raw, int dryRaw, int wetRaw) {
  if (dryRaw == wetRaw) return -1;
  long pct = (long)(dryRaw - raw) * 100 / (dryRaw - wetRaw);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return (int)pct;
}

inline bool floraSensorOk(int raw) {
  return raw > 20 && raw < 4090;   // 0 = shorted / no power, 4095 = the fork isn't connected
}
