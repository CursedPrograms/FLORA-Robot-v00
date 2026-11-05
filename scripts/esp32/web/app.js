/* FLORA's page: /status every 3 s, /history for the chart; buttons that change things ask for the PIN. */
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
