// web_page.h - FLORA's own web page. Written by scripts/make_web.py from scripts/esp32/web/:
// edit the files there, not this.
#pragma once
#include <Arduino.h>

const char WEB_INDEX[] PROGMEM = R"=====(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>FLORA · the gardener</title>
<meta name="color-scheme" content="dark">
<link rel="stylesheet" href="/style.css">
<script src="/app.js" defer></script>
</head>
<body>
<header>
  <div class="brand">FLORA</div>
  <div class="where" id="net">connecting...</div>
</header>

<main>
  <p class="says" id="says">Checking the soil...</p>

  <section class="soil">
    <div class="gauge"><div class="fill" id="fill"></div><span id="soil">--</span></div>
    <div class="facts">
      <div><dt>Temperature</dt><dd id="temp">--</dd></div>
      <div><dt>Humidity</dt><dd id="hum">--</dd></div>
      <div><dt>Light</dt><dd id="light">--</dd></div>
      <div><dt>UV index</dt><dd id="uv">--</dd></div>
      <div><dt>Good light today</dt><dd id="lighth">--</dd></div>
      <div><dt>Watered today</dt><dd id="today">--</dd></div>
      <div><dt>Last watered</dt><dd id="last">--</dd></div>
      <div><dt>Rain (AURA)</dt><dd id="rain">--</dd></div>
    </div>
  </section>

  <section>
    <h2>Watering</h2>
    <p class="decision" id="decision">--</p>
    <div class="buttons">
      <button id="auto">Automatic: --</button>
      <button id="water">Water now (5 s)</button>
      <button id="stop" class="stop">Stop</button>
    </div>
  </section>

  <section>
    <h2>The last 24 hours</h2>
    <canvas id="chart" height="160"></canvas>
  </section>

  <section>
    <h2>Calibrate the fork</h2>
    <p class="muted">Hold the fork in the air (or push it into bone-dry soil) and press <b>Dry</b>; put it in a glass of water
      and press <b>Wet</b>. Then automatic watering knows what dry means. Raw now: <span id="raw">--</span>
      (dry <span id="cdry">--</span>, wet <span id="cwet">--</span>).</p>
    <div class="buttons">
      <button id="cal-dry">This is dry</button>
      <button id="cal-wet">This is wet</button>
    </div>
  </section>

  <section>
    <h2>Her rules</h2>
    <form class="rules" id="rules">
      <label>Water below <input name="dry_at" type="number" min="5" max="80"> %</label>
      <label>Even at midday below <input name="critical_at" type="number" min="0" max="50"> %</label>
      <label>At most <input name="max_seconds" type="number" min="1" max="15"> s a time</label>
      <label>At least <input name="min_gap_min" type="number" min="5" max="720"> min apart</label>
      <label>At most <input name="max_per_day" type="number" min="1" max="12"> times a day</label>
      <label>Skip if AURA says rain from <input name="rain_skip_chance" type="number" min="10" max="101"> %</label>
      <label>No watering from <input name="quiet_from" type="number" min="0" max="24"> to <input name="quiet_to" type="number" min="0" max="24"> h</label>
      <button type="submit">Save</button>
    </form>
  </section>
  <p class="toast" id="toast"></p>
</main>
</body>
</html>
)=====";

const char WEB_STYLE[] PROGMEM = R"=====(/* FLORA's page: soil green, her leaf green, sunflower yellow, water blue (colour_scheme.xml). */
:root {
  --bg: #070D09; --panel: #0E1810; --border: #1F3424; --text: #EAF2E6; --sub: #93A88F;
  --leaf: #7BC96F; --sun: #F2C14E; --water: #5EB8F2; --bad: #E86A5A;
  --mono: ui-monospace, Consolas, monospace; --sans: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 16px/1.5 var(--sans); }
header { display: flex; justify-content: space-between; align-items: center; padding: 14px 20px; border-bottom: 1px solid var(--border); }
.brand { font: 700 22px var(--mono); letter-spacing: 4px; color: var(--leaf); }
.where { color: var(--sub); font-size: 14px; }
main { max-width: 900px; margin: 0 auto; padding: 18px 20px 40px; }
h2 { font: 600 13px var(--mono); letter-spacing: 2px; text-transform: uppercase; color: var(--sub); margin: 26px 0 10px; }
.muted { color: var(--sub); font-size: 14px; }
.says { font-size: 19px; margin: 4px 0 16px; }

.soil { display: grid; grid-template-columns: 150px 1fr; gap: 20px; align-items: center; }
.gauge { position: relative; height: 150px; border: 2px solid var(--border); border-radius: 14px; overflow: hidden; background: var(--panel); }
.gauge .fill { position: absolute; left: 0; right: 0; bottom: 0; height: 0; background: linear-gradient(var(--water), #2C6E99); transition: height .6s; }
.gauge span { position: absolute; inset: 0; display: grid; place-items: center; font: 300 40px var(--sans); }
.facts { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 8px; margin: 0; }
.facts div { background: var(--panel); border: 1px solid var(--border); border-radius: 10px; padding: 8px 10px; }
.facts dt { color: var(--sub); font-size: 11px; text-transform: uppercase; letter-spacing: 1px; }
.facts dd { margin: 2px 0 0; font-family: var(--mono); }

.decision { margin: 0 0 10px; color: var(--sun); }
.buttons { display: flex; gap: 8px; flex-wrap: wrap; }
button { background: var(--panel); color: var(--leaf); border: 1px solid var(--leaf); border-radius: 8px; padding: 9px 16px; font: 15px var(--sans); cursor: pointer; }
button.on { background: var(--leaf); color: var(--bg); }
button.stop { color: var(--bad); border-color: var(--bad); }
canvas { width: 100%; height: 160px; background: var(--panel); border: 1px solid var(--border); border-radius: 12px; }
.rules { display: grid; gap: 8px; }
.rules label { color: var(--sub); }
.rules input { width: 64px; background: var(--panel); color: var(--text); border: 1px solid var(--border); border-radius: 6px; padding: 4px 6px; font: 15px var(--mono); }
.rules button { justify-self: start; }
.toast { min-height: 1.4em; color: var(--sun); }
@media (max-width: 520px) { .soil { grid-template-columns: 1fr; } }
)=====";

const char WEB_APP[] PROGMEM = R"=====(/* FLORA's page: /status every 3 s, /history for the chart; buttons that change things ask for the PIN. */
const $ = id => document.getElementById(id);
let last = {};
let pin = sessionStorage.getItem('flora.pin') || '';

function askPin() {
  if (!pin) pin = (prompt("FLORA's PIN") || '').trim();
  if (pin) sessionStorage.setItem('flora.pin', pin);
  return pin;
}

async function post(path, params = {}) {
  if (!askPin()) return;
  const body = new URLSearchParams({ ...params, pin });
  const r = await fetch(path, { method: 'POST', body });
  const d = await r.json().catch(() => ({}));
  if (!r.ok) {
    if (d.error === 'wrong PIN') { pin = ''; sessionStorage.removeItem('flora.pin'); }
    $('toast').textContent = d.error || 'She said no.';
  } else $('toast').textContent = '';
  refresh();
}

function words(d) {
  if (!d.soil_ok) return "I can't feel the soil: check the fork's wires.";
  const s = d.soil;
  const how = s < 15 ? "I'm parched" : s < d.rules.dry_at ? "The soil's getting dry" : s < 70 ? 'The soil feels just right' : "I'm well watered";
  const extra = d.pump ? ' Drinking now...' : d.rain_chance >= d.rules.rain_skip_chance ? ' AURA says rain is coming, so I\'ll wait.' : '';
  return `${how} (${s}%).${extra}`;
}

async function refresh() {
  let d;
  try { d = await (await fetch('/status')).json(); } catch { $('net').textContent = 'not answering'; return; }
  last = d;
  $('net').textContent = d.network ? `${d.network} · ${d.wifi_rssi} dBm` : 'no network';
  $('says').textContent = words(d);
  $('soil').textContent = d.soil_ok ? `${d.soil}%` : '?';
  $('fill').style.height = `${d.soil_ok ? d.soil : 0}%`;
  $('temp').textContent = d.temp !== undefined ? `${d.temp}°C` : '--';
  $('hum').textContent = d.hum !== undefined ? `${d.hum}%` : '--';
  $('light').textContent = d.light >= 0 ? `${d.light}%` : '--';
  $('uv').textContent = d.uv !== undefined ? d.uv : '--';
  $('lighth').textContent = `${d.light_hours_today} h`;
  $('today').textContent = `${d.watered_today} of ${d.rules.max_per_day}`;
  $('last').textContent = d.last_water_min_ago < 0 ? 'not yet' : `${d.last_water_min_ago} min ago (${d.last_water_by})`;
  $('rain').textContent = d.aura ? `${d.rain_chance}%` + (d.rain_in_hours >= 0 ? ` · in ~${d.rain_in_hours} h` : '') : 'no AURA';
  $('decision').textContent = d.pump ? 'Watering now.' : `Now: ${d.decision}.`;
  $('auto').textContent = `Automatic: ${d.auto ? 'on' : 'off'}`;
  $('auto').classList.toggle('on', d.auto);
  $('raw').textContent = d.soil_raw;
  $('cdry').textContent = d.calibration.dry;
  $('cwet').textContent = d.calibration.wet;
  const form = $('rules');
  for (const [k, v] of Object.entries(d.rules)) {
    const input = form.elements[k];
    if (input && document.activeElement !== input) input.value = v;
  }
}

async function chart() {
  let d;
  try { d = await (await fetch('/history')).json(); } catch { return; }
  const c = $('chart'), dpr = window.devicePixelRatio || 1, W = c.clientWidth, H = c.clientHeight;
  c.width = W * dpr; c.height = H * dpr;
  const g = c.getContext('2d');
  g.scale(dpr, dpr);
  const pts = d.soil.filter(v => v >= 0);
  g.font = '12px system-ui'; g.fillStyle = '#93A88F';
  if (pts.length < 2) { g.fillText('A reading every 15 minutes: the line starts soon.', 12, H / 2); return; }
  const x = i => 30 + i / (d.soil.length - 1) * (W - 40), y = v => 8 + (100 - v) / 100 * (H - 24);
  g.strokeStyle = '#1F3424';
  for (const v of [0, 50, 100]) { g.beginPath(); g.moveTo(30, y(v)); g.lineTo(W - 10, y(v)); g.stroke(); g.fillText(`${v}`, 4, y(v) + 4); }
  if (last.rules) {   // where she starts watering
    g.strokeStyle = '#F2C14E'; g.setLineDash([4, 4]);
    g.beginPath(); g.moveTo(30, y(last.rules.dry_at)); g.lineTo(W - 10, y(last.rules.dry_at)); g.stroke(); g.setLineDash([]);
  }
  g.strokeStyle = '#5EB8F2'; g.lineWidth = 2; g.beginPath();
  d.soil.forEach((v, i) => { if (v >= 0) (i ? g.lineTo : g.moveTo).call(g, x(i), y(v)); });
  g.stroke();
}

$('auto').addEventListener('click', () => post('/auto', { on: last.auto ? '0' : '1' }));
$('water').addEventListener('click', () => { if (confirm('Water now, 5 seconds?')) post('/water', { s: 5 }); });
$('stop').addEventListener('click', () => fetch('/stop').then(refresh));
$('cal-dry').addEventListener('click', () => post('/calibrate', { point: 'dry' }));
$('cal-wet').addEventListener('click', () => post('/calibrate', { point: 'wet' }));
$('rules').addEventListener('submit', e => {
  e.preventDefault();
  post('/rules', Object.fromEntries(new FormData(e.target)));
});
window.addEventListener('resize', chart);
refresh(); chart();
setInterval(refresh, 3000);
setInterval(chart, 60000);
)=====";

