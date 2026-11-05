// FLORA - the DREAM Robotics gardener (Field Leaf Observer & Root Assistant). ESP32 DevKit.
//
// She watches one plant (or a bed) and waters it:
//   soil moisture   YL-69 fork: AO -> GPIO34, its VCC from GPIO25 (powered only while reading: the
//                   fork corrodes when it's powered all the time)
//   temp / humidity AHT10 on I2C: SDA GPIO21, SCL GPIO22 (the same sensor and code as NORA and MILA)
//   light           LDR module: AO -> GPIO35 (brightness %, and hours of good light today)
//   UV              analog UV module (GUVA-S12SD, like KIDA-01's): OUT -> GPIO32 (about 100 mV per UV index)
//   pump            relay IN -> GPIO26 (active LOW: most relay modules), the pump on its own 5 V supply
//   buzzer          GPIO27 (the fleet's Brainfuck chirps)    LED  GPIO2
//
// Watering: flora_rules.h (tested on the PC). Automatic watering starts OFF: turn it on from her page once
// the fork is calibrated and the pump tested. Every run is capped (10 s), spaced (30 min), counted (4 a day),
// and skipped when AURA, the weather station, says rain is coming.
//
// Fleet: she joins the highest fleet network she can see (the ladder; she never hosts), registers with
// whoever hosts the fleet list (garden, climate, web:5018, talk:5018, do:water, do:stop), beacons over
// ESP-NOW and Bluetooth LE (fleet_near.h: RITA shows how far she is) and serves her whole web API over
// Bluetooth serial too (fleet_bt.h). Her page: http://<her ip>:5018/
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
#include "fleet_near.h"       // ESP-NOW "I'm here" beacons (RITA's distance, the near map)
#include "fleet_near_ble.h"   // ...and over Bluetooth LE
#include "fleet_bt.h"         // her whole web API over Bluetooth serial (RIFT/Fleet/esp32)
#include "talk_bf.h"          // the fleet's Brainfuck phrasebook
#include "flora_rules.h"      // when she waters, and how much
#include "web_page.h"         // her page (scripts/esp32/web, embedded by scripts/make_web.py)

// ---- pins ----
const int SOIL_PIN = 34, SOIL_PWR = 25, LIGHT_PIN = 35, UV_PIN = 32;
const int PUMP_PIN = 26, BUZZER_PIN = 27, LED_PIN = 2;
const bool PUMP_ACTIVE_LOW = true;
#define AHT10_ADDR 0x38

const uint16_t PORT = 5018;
const char* NAME = "FLORA";
const int PUMP_HARD_CAP_S = 15;          // whatever happens, the pump never runs longer than this
const uint8_t VOICE_PCT = 125;           // her chirps: a little higher than most

// The ladder, highest first (RIFT/Fleet/fleet_ladder.py). She joins, she never hosts.
const char* const LADDER[] = { FLEET_SSID, "DREAM", "NINA", "TINA", "AURA", "NORACTL", "WHIPCTL", "KIDA01CTL",
                               "KIDA00CTL", "ARMCTL", "RITA", "NORA", "WHIP", "KIDA01", "KIDA00", "ARM", "MILA", "NIKA" };
const int LADDER_N = sizeof(LADDER) / sizeof(LADDER[0]);

WebServer web(PORT);
Preferences prefs;
FloraRules rules;
FloraState st;

// ---- readings ----
float temp = NAN, hum = NAN;
int soilRaw = -1, lightPct = -1;
float uvIndex = NAN;
int dryRaw = 4095, wetRaw = 1400;          // the fork's calibration (her page: "this is dry" / "this is wet")
float lightHoursToday = 0;                 // hours of good light (over 40 %) today
const int HIST_N = 96;                     // soil moisture every 15 min: the last 24 hours
int8_t soilHist[HIST_N];
int histCount = 0;

// ---- pump ----
bool pumpOn = false;
unsigned long pumpUntil = 0, pumpStartedAt = 0, lastWaterMs = 0;
bool everWatered = false;
String lastWhy = "starting up", lastWaterBy = "";
int lastDay = -1;

// ---- fleet ----
int rank = -1;                             // which ladder network she's on
String registry;                           // whoever hosts the fleet list (the gateway)
String auraBase;                           // AURA's http://ip:port, from the fleet list
long utcOffsetS = 0;                       // from AURA (Open-Meteo knows the time zone); saved
bool timeSet = false;
unsigned long auraAt = 0;

// ============================================================ sensors
void ahtInit() {
  Wire.beginTransmission(AHT10_ADDR); Wire.write(0x71); Wire.endTransmission();
  Wire.requestFrom(AHT10_ADDR, 1);
  uint8_t status = Wire.available() ? Wire.read() : 0;
  if ((status & 0x68) != 0x08) {
    Wire.beginTransmission(AHT10_ADDR); Wire.write(0xE1); Wire.write(0x08); Wire.write(0x00); Wire.endTransmission();
    delay(10);
  }
}

void ahtRead() {
  Wire.beginTransmission(AHT10_ADDR); Wire.write(0xAC); Wire.write(0x33); Wire.write(0x00);
  if (Wire.endTransmission() != 0) { temp = hum = NAN; ahtInit(); return; }
  delay(80);
  Wire.requestFrom(AHT10_ADDR, 6);
  if (Wire.available() < 6) return;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  if (b[0] & 0x80) return;
  uint32_t rh = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | (b[3] >> 4);
  uint32_t rt = ((uint32_t)(b[3] & 0x0F) << 16) | ((uint32_t)b[4] << 8) | b[5];
  hum = rh / 1048576.0 * 100.0;
  temp = rt / 1048576.0 * 200.0 - 50.0;
}

void soilRead() {
  digitalWrite(SOIL_PWR, HIGH);            // powered only now
  delay(15);
  long sum = 0;
  for (int i = 0; i < 8; i++) { sum += analogRead(SOIL_PIN); delay(2); }
  digitalWrite(SOIL_PWR, LOW);
  soilRaw = sum / 8;
  st.sensorOk = floraSensorOk(soilRaw);
  st.moisture = st.sensorOk ? floraMoisture(soilRaw, dryRaw, wetRaw) : -1;
}

void lightRead() {
  lightPct = 100 - (int)((long)analogRead(LIGHT_PIN) * 100 / 4095);   // the module reads higher when darker
  uvIndex = analogReadMilliVolts(UV_PIN) / 100.0;
}

// ============================================================ the pump
void pumpWrite(bool on) {
  digitalWrite(PUMP_PIN, (on ^ PUMP_ACTIVE_LOW) ? HIGH : LOW);
  digitalWrite(LED_PIN, on ? HIGH : LOW);
}

bool water(int seconds, const String& by) {
  if (pumpOn) return false;
  if (seconds < 1) seconds = 1;
  if (seconds > rules.maxSeconds) seconds = rules.maxSeconds;
  pumpOn = true;
  pumpStartedAt = millis();
  pumpUntil = millis() + seconds * 1000UL;
  pumpWrite(true);
  lastWaterMs = millis();
  everWatered = true;
  st.wateredToday++;
  lastWaterBy = by;
  Serial.printf("[FLORA] watering %d s (%s)\n", seconds, by.c_str());
  return true;
}

void pumpStop() {
  pumpOn = false;
  pumpWrite(false);
}

void pumpLoop() {
  if (!pumpOn) return;
  if ((long)(millis() - pumpUntil) >= 0 || millis() - pumpStartedAt > PUMP_HARD_CAP_S * 1000UL) pumpStop();
}

// ============================================================ time, the day, the decision
int localHour() {
  if (!timeSet) return -1;
  time_t now = time(nullptr) + utcOffsetS;
  struct tm t;
  gmtime_r(&now, &t);
  return t.tm_year > 120 ? t.tm_hour : -1;
}

int localDay() {
  time_t now = time(nullptr) + utcOffsetS;
  struct tm t;
  gmtime_r(&now, &t);
  return t.tm_year > 120 ? t.tm_yday : -1;
}

void decideLoop() {
  int day = localDay();
  if (day >= 0 && day != lastDay) {         // a new day: the counters start again
    if (lastDay >= 0) { st.wateredToday = 0; lightHoursToday = 0; }
    lastDay = day;
  }
  st.hour = localHour();
  st.minutesSinceWater = everWatered ? (long)((millis() - lastWaterMs) / 60000UL) : (1L << 30);
  FloraDecision d = floraDecide(rules, st);
  lastWhy = d.why;
  if (d.water) water(d.seconds, "automatic");
}

// ============================================================ the fleet
void joinLadder() {
  int n = WiFi.scanNetworks();
  int best = -1;
  for (int i = 0; i < n; i++)
    for (int r = 0; r < LADDER_N; r++)
      if (WiFi.SSID(i) == LADDER[r] && (best < 0 || r < best)) best = r;
  WiFi.scanDelete();
  if (best < 0) return;
  WiFi.begin(LADDER[best], best == 0 ? FLEET_PASSWORD : NORA_PASSWORD);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    rank = best;
    registry = WiFi.gatewayIP().toString();
    fleetNearBegin(NAME, WIFI_IF_STA);
    Serial.printf("[FLORA] on %s, %s:%u\n", LADDER[best], WiFi.localIP().toString().c_str(), PORT);
  }
}

void wifiLoop() {
  static unsigned long nextCheck = 0, nextHigher = 0;
  if ((long)(millis() - nextCheck) < 0) return;
  nextCheck = millis() + 30000;
  if (WiFi.status() != WL_CONNECTED) { rank = -1; joinLadder(); return; }
  if (rank > 0 && (long)(millis() - nextHigher) >= 0) {     // a higher network may have come up
    nextHigher = millis() + 5 * 60000UL;
    int n = WiFi.scanNetworks();
    bool higher = false;
    for (int i = 0; i < n && !higher; i++)
      for (int r = 0; r < rank; r++) if (WiFi.SSID(i) == LADDER[r]) higher = true;
    WiFi.scanDelete();
    if (higher) { WiFi.disconnect(); delay(200); joinLadder(); }
  }
}

void registerLoop() {
  static unsigned long next = 0;
  if (WiFi.status() != WL_CONNECTED || (long)(millis() - next) < 0) return;
  next = millis() + 10000;
  HTTPClient http;
  http.begin("http://" + registry + ":5000/register");
  http.setTimeout(1500);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.POST(String("name=FLORA&type=garden&capabilities=garden,climate,web:") + PORT + ",talk:" + PORT + ",do:water,do:stop");
  http.end();
}

// AURA, the weather station: her rain guess, Open-Meteo's next rain, and the time zone.
void auraLoop() {
  if (WiFi.status() != WL_CONNECTED || (auraAt && millis() - auraAt < 10 * 60000UL)) return;
  auraAt = millis();
  if (!auraBase.length()) {
    HTTPClient http;
    http.begin("http://" + registry + ":5000/robots");
    http.setTimeout(2000);
    if (http.GET() == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getString()))
        for (JsonObject r : doc["robots"].as<JsonArray>())
          if (String((const char*)(r["name"] | "")).equalsIgnoreCase("AURA"))
            for (JsonVariant c : r["capabilities"].as<JsonArray>()) {
              String cap = c.as<String>();
              if (cap.startsWith("ask:")) auraBase = "http://" + String((const char*)(r["ip"] | "")) + ":" + cap.substring(4);
            }
    }
    http.end();
    if (!auraBase.length()) { st.rainChance = st.rainInHours = -1; return; }
  }
  HTTPClient http;
  http.begin(auraBase + "/api/now");
  http.setTimeout(4000);
  if (http.GET() != 200) { http.end(); auraBase = ""; st.rainChance = st.rainInHours = -1; return; }
  JsonDocument filter;
  filter["rain_outlook"]["chance"] = true;
  filter["outside"]["soon"]["rain_at"] = true;
  filter["outside"]["utc_offset_s"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) return;
  st.rainChance = doc["rain_outlook"]["chance"] | -1;
  if (!doc["outside"]["utc_offset_s"].isNull()) {
    long off = doc["outside"]["utc_offset_s"];
    if (off != utcOffsetS) { utcOffsetS = off; prefs.putLong("utc", off); }
  }
  st.rainInHours = -1;
  const char* at = doc["outside"]["soon"]["rain_at"] | "";
  int h = localHour();
  if (strlen(at) >= 2 && h >= 0) st.rainInHours = (atoi(at) - h + 24) % 24;
}

// ============================================================ her web API
bool pinOk() { return web.arg("pin") == ROBOT_PIN; }

String statusJson() {
  JsonDocument d;
  d["name"] = NAME;
  d["mode"] = st.autoOn ? "auto" : "manual";
  d["soil"] = st.moisture;
  d["soil_raw"] = soilRaw;
  d["soil_ok"] = st.sensorOk;
  if (!isnan(temp)) d["temp"] = round(temp * 10) / 10.0;
  if (!isnan(hum)) d["hum"] = round(hum * 10) / 10.0;
  d["light"] = lightPct;
  if (!isnan(uvIndex)) d["uv"] = round(uvIndex * 10) / 10.0;
  d["light_hours_today"] = round(lightHoursToday * 10) / 10.0;
  d["pump"] = pumpOn ? 1 : 0;
  d["moving"] = false;
  d["auto"] = st.autoOn;
  d["watered_today"] = st.wateredToday;
  d["last_water_min_ago"] = everWatered ? (long)((millis() - lastWaterMs) / 60000UL) : -1;
  d["last_water_by"] = lastWaterBy;
  d["decision"] = lastWhy;
  d["rain_chance"] = st.rainChance;
  d["rain_in_hours"] = st.rainInHours;
  d["aura"] = auraBase.length() > 0;
  d["hour"] = st.hour;
  JsonObject cal = d["calibration"].to<JsonObject>();
  cal["dry"] = dryRaw; cal["wet"] = wetRaw;
  JsonObject r = d["rules"].to<JsonObject>();
  r["dry_at"] = rules.dryAt; r["critical_at"] = rules.criticalAt; r["max_seconds"] = rules.maxSeconds;
  r["min_gap_min"] = rules.minGapMin; r["max_per_day"] = rules.maxPerDay; r["rain_skip_chance"] = rules.rainSkipChance;
  r["quiet_from"] = rules.quietFrom; r["quiet_to"] = rules.quietTo;
  d["network"] = rank >= 0 ? LADDER[rank] : "";
  d["wifi_rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  d["bluetooth"] = fleetBtConnected() ? 1 : 0;
  d["uptime_s"] = millis() / 1000;
  String out;
  serializeJson(d, out);
  return out;
}

void sendJson(int code, const String& body) { web.send(code, "application/json", body); }
void sendOk(bool ok, const String& error = "") {
  sendJson(ok ? 200 : 400, ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"" + error + "\"}");
}

void routes() {
  web.on("/", []() { web.send_P(200, "text/html", WEB_INDEX); });
  web.on("/style.css", []() { web.send_P(200, "text/css", WEB_STYLE); });
  web.on("/app.js", []() { web.send_P(200, "application/javascript", WEB_APP); });
  web.on("/avatar.jpg", []() {
    web.sendHeader("Cache-Control", "max-age=86400");
    web.send_P(200, "image/jpeg", (const char*)WEB_AVATAR, WEB_AVATAR_LEN);
  });
  web.on("/ping", []() { web.send(200, "text/plain", "FLORA alive"); });
  web.on("/status", []() { sendJson(200, statusJson()); });
  web.on("/sensors", []() { sendJson(200, statusJson()); });
  web.on("/near", []() { sendJson(200, fleetNearJson()); });
  web.on("/history", []() {                 // soil moisture every 15 min, oldest first
    String s = "{\"every_min\":15,\"soil\":[";
    int n = min(histCount, HIST_N);
    for (int i = 0; i < n; i++) s += (i ? "," : "") + String(soilHist[(histCount - n + i) % HIST_N]);
    sendJson(200, s + "]}");
  });
  web.on("/water", HTTP_POST, []() {        // the WATER NOW button: the PIN, a few seconds, capped
    if (!pinOk()) return sendOk(false, "wrong PIN");
    int s = web.hasArg("s") ? web.arg("s").toInt() : 5;
    sendOk(water(s, "by hand"), "already watering");
  });
  web.on("/stop", []() { pumpStop(); sendOk(true); });           // stopping is always allowed
  web.on("/auto", HTTP_POST, []() {
    if (!pinOk()) return sendOk(false, "wrong PIN");
    st.autoOn = web.arg("on") == "1";
    prefs.putBool("auto", st.autoOn);
    sendOk(true);
  });
  web.on("/calibrate", HTTP_POST, []() {    // point=dry (fork in air or dry soil) / point=wet (in a glass of water)
    if (!pinOk()) return sendOk(false, "wrong PIN");
    soilRead();
    if (!st.sensorOk && web.arg("point") == "wet") return sendOk(false, "the fork isn't reading");
    if (web.arg("point") == "dry") { dryRaw = soilRaw; prefs.putInt("dry", dryRaw); }
    else if (web.arg("point") == "wet") { wetRaw = soilRaw; prefs.putInt("wet", wetRaw); }
    else return sendOk(false, "point=dry or point=wet");
    soilRead();
    sendOk(true);
  });
  web.on("/rules", HTTP_POST, []() {        // her settings: dry_at, critical_at, max_seconds, ...
    if (!pinOk()) return sendOk(false, "wrong PIN");
    auto set = [](const char* arg, int& field, int lo, int hi, const char* key) {
      if (!web.hasArg(arg)) return;
      field = constrain(web.arg(arg).toInt(), lo, hi);
      prefs.putInt(key, field);
    };
    set("dry_at", rules.dryAt, 5, 80, "dryAt");
    set("critical_at", rules.criticalAt, 0, 50, "critAt");
    set("max_seconds", rules.maxSeconds, 1, PUMP_HARD_CAP_S, "maxS");
    set("min_gap_min", rules.minGapMin, 5, 720, "gapM");
    set("max_per_day", rules.maxPerDay, 1, 12, "perDay");
    set("rain_skip_chance", rules.rainSkipChance, 10, 101, "rainSkip");
    set("quiet_from", rules.quietFrom, 0, 24, "qFrom");
    set("quiet_to", rules.quietTo, 0, 24, "qTo");
    sendOk(true);
  });
  web.on("/do", HTTP_POST, []() {           // RIFT's Ask & tell (it has a person's yes for "water")
    JsonDocument b;
    deserializeJson(b, web.arg("plain"));
    String a = b["action"] | "";
    if (a == "stop") { pumpStop(); return sendOk(true); }
    if (a == "water") return sendOk(water(b["seconds"] | 5, "RIFT"), "already watering");
    sendOk(false, "FLORA does: water, stop");
  });
  web.on("/chirp", []() {
    int u = web.hasArg("u") ? web.arg("u").toInt() : -1;
    if (u < 0 || u >= TALK_UTTERANCES) return sendOk(false, "use /chirp?u=0-16");
    talkStart(u);
    sendOk(true);
  });
  web.onNotFound([]() { web.send(404, "text/plain", "not here"); });
}

// ============================================================ setup / loop
void setup() {
  Serial.begin(115200);
  pinMode(PUMP_PIN, OUTPUT);
  pumpWrite(false);                         // first thing: the pump is off
  pinMode(SOIL_PWR, OUTPUT); digitalWrite(SOIL_PWR, LOW);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  analogSetAttenuation(ADC_11db);           // 0-3.3 V on the analog pins
  Wire.begin(21, 22);
  delay(40);
  ahtInit();

  prefs.begin("flora", false);
  st.autoOn = prefs.getBool("auto", false);
  dryRaw = prefs.getInt("dry", dryRaw);
  wetRaw = prefs.getInt("wet", wetRaw);
  utcOffsetS = prefs.getLong("utc", 0);
  rules.dryAt = prefs.getInt("dryAt", rules.dryAt);
  rules.criticalAt = prefs.getInt("critAt", rules.criticalAt);
  rules.maxSeconds = prefs.getInt("maxS", rules.maxSeconds);
  rules.minGapMin = prefs.getInt("gapM", rules.minGapMin);
  rules.maxPerDay = prefs.getInt("perDay", rules.maxPerDay);
  rules.rainSkipChance = prefs.getInt("rainSkip", rules.rainSkipChance);
  rules.quietFrom = prefs.getInt("qFrom", rules.quietFrom);
  rules.quietTo = prefs.getInt("qTo", rules.quietTo);

  fleetNearSetState(FLEET_PARKED);          // she's planted: never moves
  fleetNearBleBegin(NAME);                  // Bluetooth LE doesn't wait for the WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("flora");
  joinLadder();
  configTime(0, 0, "pool.ntp.org", "time.google.com");   // UTC; AURA tells her the time zone
  routes();
  web.begin();
  fleetBtBegin(NAME, PORT);                 // Bluetooth serial "FLORA": every command works over it too
  soilRead();
  lightRead();
  ahtRead();
  talkStart(0);                             // "hi"
}

void loop() {
  web.handleClient();
  pumpLoop();
  talkStep(BUZZER_PIN, VOICE_PCT);
  fleetNearLoop();
  fleetNearBleLoop();
  wifiLoop();
  registerLoop();
  auraLoop();
  if (!timeSet && time(nullptr) > 1700000000) timeSet = true;

  static unsigned long nextAht = 0, nextLight = 0, nextSoil = 0, nextHist = 0, nextDecide = 0;
  unsigned long now = millis();
  if ((long)(now - nextAht) >= 0) { nextAht = now + 5000; ahtRead(); }
  if ((long)(now - nextLight) >= 0) {
    nextLight = now + 2000;
    lightRead();
    if (lightPct > 40) lightHoursToday += 2.0 / 3600.0;
  }
  if ((long)(now - nextSoil) >= 0 && !pumpOn) { nextSoil = now + 60000; soilRead(); }   // once a minute
  if ((long)(now - nextHist) >= 0) {
    nextHist = now + 15 * 60000UL;
    soilHist[histCount % HIST_N] = (int8_t)st.moisture;
    histCount++;
  }
  if ((long)(now - nextDecide) >= 0 && !pumpOn) { nextDecide = now + 60000; decideLoop(); }
  if (!pumpOn && !st.autoOn) digitalWrite(LED_PIN, LOW);
  else if (!pumpOn && st.moisture >= 0 && st.moisture < rules.dryAt) digitalWrite(LED_PIN, (now / 1000) % 2);   // thirsty
  delay(2);
}
