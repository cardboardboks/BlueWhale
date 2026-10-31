// The dashboard web page (HTML + CSS + JavaScript), served at http://192.168.10.1/
// Kept in its own file so the main sketch isn't buried under it.
#pragma once
#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
<title>🐋 Whale Stats 🐋</title>
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<style>
:root {
  --bg: #0b0f19; --card: rgba(255,255,255,0.06); --border: rgba(255,255,255,0.10);
  --text: #fff; --muted: #8e8e93; --val: #d1d1d6;
  --accent: #0a84ff; --good: #30d158; --warn: #ff9f0a; --bad: #ff453a;
  --fs: 12px;
}
* { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; }
html { font-size: var(--fs); }
body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  background: var(--bg); color: var(--text);
  min-height: 100vh; min-height: 100dvh;
  padding: calc(env(safe-area-inset-top) + 6px) calc(env(safe-area-inset-right) + 8px)
           calc(env(safe-area-inset-bottom) + 6px) calc(env(safe-area-inset-left) + 8px);
  display: flex; flex-direction: column; align-items: center;
  justify-content: center;   /* centre the whole page vertically as well as horizontally */
}
.wrap { width: 100%; max-width: 560px; }
h1 { display: none; font-size: 1.5rem; font-weight: 700; letter-spacing: -0.3px; margin: 0 0 6px 4px; }

/* ---------- grid: portrait phone (default) ---------- */
.grid {
  display: grid; gap: 5px; grid-template-columns: 1fr 1fr;
  grid-template-areas: "status cpu" "esc esc" "rx rx" "out out" "imu rec" "arr arr" "bulk bulk";
}
#c-status { grid-area: status; } #c-cpu { grid-area: cpu; }
#c-esc { grid-area: esc; }       #c-rx { grid-area: rx; }
#c-imu { grid-area: imu; }       #c-arr { grid-area: arr; }
#c-out { grid-area: out; }  #c-rec { grid-area: rec; }
#c-bulk { grid-area: bulk; }

.card { background: var(--card); border: 1px solid var(--border); border-radius: 10px; padding: 4px 8px 5px; min-width: 0; }
.card h2 { font-size: 0.78rem; font-weight: 600; text-transform: uppercase; letter-spacing: .6px; color: var(--muted); margin-bottom: 2px; }
.kv { display: flex; justify-content: space-between; align-items: baseline; line-height: 1.35; gap: 8px; }
.kv .v, td { color: var(--val); font-variant-numeric: tabular-nums; }
.kv .v { text-align: right; }

table { width: 100%; border-collapse: collapse; }
th { font-size: .8rem; font-weight: 600; color: var(--muted); text-align: right; padding: 0 0 1px; }
td { text-align: right; line-height: 1.3; padding: 0; }
th:first-child, td.l { text-align: left; color: var(--text); }
tr + tr td { border-top: 1px solid var(--border); }
th.off { opacity: .3; }
.stale { color: #636366 !important; font-weight: 400 !important; }   /* last known value, no longer updating */
#c-esc table.num th:first-child, #c-esc table.num td.l { width: 25%; }
#c-esc th:last-child, #c-esc td:last-child { border-left: 1px solid var(--border); }
#c-esc td:last-child { color: #fff; font-weight: 600; }
#c-esc th:last-child { width: 20%; }   /* Avg column a little wider so the units fit */
#c-esc td:last-child small { font-size: .7em; font-weight: 400; color: var(--muted); margin-left: 1px; }
#c-esc th:nth-last-child(2), #c-esc td:nth-last-child(2) { padding-right: .5rem; }   /* gap between ESC 4 and the divider */
#c-esc th:last-child, #c-esc td:last-child { padding-left: .2rem; }

[hidden] { display: none !important; }
#c-status h2 { display: flex; justify-content: space-between; align-items: center; }
.nav { font: inherit; font-size: .8rem; text-transform: none; letter-spacing: 0; padding: 0 8px; border-radius: 6px;
       border: 1px solid rgba(10,132,255,.5); background: rgba(10,132,255,.18); color: #64b0ff; }
.ttop { display: flex; align-items: center; gap: 10px; margin-bottom: 5px; }
.ttop b { font-size: 1.3rem; font-weight: 700; }
.ttop .nav { font-size: 1rem; padding: 3px 12px; }
.tfile { display: flex; align-items: center; gap: 8px; margin-bottom: 5px; flex-wrap: wrap; }
.tfile .nav { font-size: 1rem; padding: 3px 12px; }
#tmsg { color: var(--muted); font-size: .9rem; }
.tgrid { display: grid; grid-template-columns: 1fr 1fr; gap: 5px; }
.tv { display: flex; justify-content: space-between; align-items: center; gap: 6px; padding: 4px 8px; }
.tv label { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.tv input { width: 5.5em; min-width: 0; font: inherit; font-family: ui-monospace, "SF Mono", Menlo, Consolas, monospace; text-align: right;
            color: #fff; background: rgba(255,255,255,.08); border: 1px solid var(--border); border-radius: 6px; padding: 2px 6px; }
.tv input:focus { outline: none; border-color: var(--accent); }
.tv.str label { flex: none; max-width: 45%; }
.tv.str input { flex: 1; width: auto; text-align: left; }   /* text variables: use the whole box, left aligned */
/* hide the up/down spinner arrows on number boxes (desktop browsers) */
.tv input::-webkit-outer-spin-button, .tv input::-webkit-inner-spin-button { -webkit-appearance: none; margin: 0; }
.tv input[type=number] { -moz-appearance: textfield; appearance: textfield; }
#c-rec h2, #c-cpu h2 { display: flex; justify-content: space-between; align-items: center; }
.reset { font: inherit; font-size: .8rem; text-transform: none; letter-spacing: 0; padding: 0 8px; border-radius: 6px;
         border: 1px solid rgba(255,69,58,.5); background: rgba(255,69,58,.15); color: var(--bad); }
.modal { position: fixed; inset: 0; background: rgba(0,0,0,.65); display: none; align-items: center; justify-content: center; z-index: 10; }
.modal.show { display: flex; }
button:disabled { opacity: .35; pointer-events: none; }
.tv input:disabled { opacity: .45; }
.toast { position: fixed; left: 50%; bottom: calc(env(safe-area-inset-bottom) + 14px); transform: translateX(-50%); z-index: 20;
         background: #1c1c1e; border: 1px solid var(--border); border-radius: 10px; padding: 6px 14px; font-size: 1rem;
         opacity: 0; pointer-events: none; transition: opacity .2s; white-space: nowrap; }
.toast.show { opacity: 1; }
.mbox { background: #1c1c1e; border: 1px solid var(--border); border-radius: 14px; padding: 16px; width: min(300px, 85vw); text-align: center; }
.mbox p { font-size: 1.2rem; margin-bottom: 4px; } .mbox small { display: block; color: var(--muted); margin-bottom: 12px; }
.mbtns { display: flex; gap: 8px; }
.mbtns button { flex: 1; font: inherit; font-size: 1.1rem; padding: 8px; border: 0; border-radius: 9px; color: #fff; background: #3a3a3c; }
.mbtns .yes { background: var(--bad); }

/* fixed column widths + monospace digits so values never shift as digit counts change */
table.num { table-layout: fixed; }
table.num th, table.num td { white-space: nowrap; }
table.num th:first-child, table.num td.l { width: 30%; }
#c-imu table.num th:first-child, #c-imu table.num td.l { width: 24%; }
table.num td:not(.l) { font-family: ui-monospace, "SF Mono", Menlo, Consolas, monospace; font-size: .92em; }

.rxtop { display: grid; grid-template-columns: repeat(4, 1fr); column-gap: 8px; margin-bottom: 3px; }
.chs { display: grid; grid-template-columns: repeat(8, 1fr); gap: 3px; }
.ch { position: relative; overflow: hidden; border-radius: 5px; background: rgba(255,255,255,.07);
      padding: 1px 4px; font-size: .88em; display: flex; justify-content: space-between; font-variant-numeric: tabular-nums; }
.ch i { position: absolute; left: 0; top: 0; bottom: 0; width: 0; background: rgba(10,132,255,.5); }
.ch b, .ch span { position: relative; font-weight: 500; }
.ch b { color: var(--muted); }

.dots { display: grid; grid-template-columns: repeat(10, minmax(0, 1fr)); gap: 3px 4px; justify-items: center; padding-top: 2px; min-width: 0; }
.dc { display: flex; align-items: center; gap: 3px; min-width: 0; }
.dc span { font-size: .75rem; color: var(--muted); font-variant-numeric: tabular-nums; }
.dot { flex: none; width: 0.8rem; height: 0.8rem; border-radius: 50%; background: rgba(255,255,255,.12); }
.dot.on { background: var(--good); box-shadow: 0 0 6px var(--good); }

.bulk { font-family: ui-monospace, Menlo, monospace; font-size: .9rem; color: var(--val);
        white-space: pre-wrap; word-break: break-word; min-height: 2.2em; max-height: 5em; overflow-y: auto; }
.good { color: var(--good) !important; } .warn { color: var(--warn) !important; } .bad { color: var(--bad) !important; }

/* ---------- landscape phone: short screen -> 4 columns, no title ---------- */
@media (orientation: landscape) and (max-height: 520px) {
  :root { --fs: 11px; }
  .wrap { max-width: none; }
  h1 { display: none; }
  .grid {
    grid-template-columns: 1fr 1.6fr 1.3fr 1fr;
    grid-template-areas: "status esc rx imu" "cpu esc rx rec" "arr esc out bulk";
  }
  .dots { grid-template-columns: repeat(5, minmax(0, 1fr)); gap: 3px 2px; }
  .dot { width: .7rem; height: .7rem; }
  .dc { gap: 2px; }
  .bulk { max-height: 4.5em; }
  .rxtop { grid-template-columns: 1fr 1fr; }
  .chs { grid-template-columns: repeat(4, 1fr); }
  .tgrid { grid-template-columns: repeat(4, 1fr); }
  .ttop b { font-size: 1.1rem; }
}

/* ---------- desktop browser: mouse + wide window -> use the space ---------- */
@media (hover: hover) and (pointer: fine) and (min-width: 900px) {
  :root { --fs: 15px; }
  body { padding: 18px 24px; }
  .wrap { max-width: 1280px; }
  h1 { display: block; font-size: 2rem; margin-bottom: 12px; }
  .grid {
    gap: 12px; grid-template-columns: repeat(4, 1fr);
    grid-template-areas: "status cpu arr arr" "esc esc rx rx" "esc esc out out" "imu imu rec bulk";
  }
  .card { padding: 10px 14px 12px; border-radius: 14px; }
  .dots { grid-template-columns: repeat(10, minmax(0, 1fr)); gap: 8px 6px; padding-top: 6px; }   /* 2 rows of 10: always fits, whatever the window width */
  .dot { width: 1.1rem; height: 1.1rem; }
  .ch { padding: 4px 8px; }
  .bulk { max-height: 12em; }
  .tgrid { grid-template-columns: repeat(4, 1fr); gap: 12px; }
  .tv { padding: 8px 14px; }
  .ttop b { font-size: 1.8rem; }
}
</style>
</head>
<body>
<div class="wrap" id="page-main">
  <h1>🐋 Whale Stats 🐋</h1>
  <div class="grid">

    <div class="card" id="c-status"><h2><span>🐋 Status</span><button class="nav" id="btn-tune">Whale Tuning</button></h2>
      <div class="kv"><span>Data</span><span class="v" id="v-live">--</span></div>
      <div class="kv"><span>Mode</span><span class="v" id="v-mode">--</span></div>
      <div class="kv"><span>Boot</span><span class="v" id="v-boot">--</span></div>
      <div class="kv"><span>Avg Volts</span><span class="v" id="v-avgv">--</span></div>
      <div class="kv"><span>Total Amps</span><span class="v" id="v-avga">--</span></div>
    </div>

    <div class="card" id="c-cpu"><h2><span>CPU</span><button class="reset" id="btn-reboot">Reboot</button></h2>
      <div class="kv"><span>Core 0</span><span class="v" id="v-c0">--</span></div>
      <div class="kv"><span>Core 1</span><span class="v" id="v-c1">--</span></div>
      <div class="kv"><span>Average</span><span class="v" id="v-ca">--</span></div>
      <div class="kv"><span>WiFi</span><span class="v" id="v-wifi">--</span></div>
      <div class="kv"><span>Uptime</span><span class="v" id="v-up">--</span></div>
    </div>

    <div class="card" id="c-esc"><h2>ESCs</h2>
      <table class="num"><thead><tr><th></th><th id="eh1">1</th><th id="eh2">2</th><th id="eh3">3</th><th id="eh4">4</th><th id="eh5">Avg</th></tr></thead><tbody id="esc-body"></tbody></table>
    </div>

    <div class="card" id="c-rx"><h2>Receiver</h2>
      <div class="rxtop">
        <div class="kv"><span>Link</span><span class="v" id="rx-up">--</span></div>
        <div class="kv"><span>LQ</span><span class="v" id="rx-lq">--</span></div>
        <div class="kv"><span>RSSI</span><span class="v" id="rx-rssi">--</span></div>
        <div class="kv"><span>dBm</span><span class="v" id="rx-dbm">--</span></div>
      </div>
      <div class="chs" id="chs"></div>
    </div>

    <div class="card" id="c-out"><h2>Outputs</h2>
      <div class="chs" id="ochs"></div>
    </div>

    <div class="card" id="c-imu"><h2>IMU</h2>
      <table class="num">
        <thead><tr><th></th><th>X</th><th>Y</th><th>Z</th></tr></thead>
        <tbody>
          <tr><td class="l">Gyro</td><td id="i-gx">--</td><td id="i-gy">--</td><td id="i-gz">--</td></tr>
          <tr><td class="l">Accel</td><td id="i-ax">--</td><td id="i-ay">--</td><td id="i-az">--</td></tr>
        </tbody>
      </table>
      <div class="kv"><span>Max G</span><span class="v" id="v-rg">--</span></div>
      <div class="kv"><span>Orientation</span><span class="v" id="i-ud">--</span></div>
      <div class="kv"><span>Direction</span><span class="v" id="i-lr">--</span></div>
    </div>

    <div class="card" id="c-rec"><h2><span>Records</span><button class="reset" id="btn-reset">Reset</button></h2>
      <div class="kv"><span>Max ESC Temp</span><span class="v" id="v-rtmp">--</span></div>
      <div class="kv"><span>Max ESC Amps</span><span class="v" id="v-ra">--</span></div>
      <div class="kv"><span>Max Total Amps</span><span class="v" id="v-rt">--</span></div>
      <div class="kv"><span>Min Volts</span><span class="v" id="v-rv">--</span></div>
      <div class="kv"><span>Max Avg Spd</span><span class="v" id="v-rs">--</span></div>
    </div>

    <div class="card" id="c-arr"><h2>State</h2><div class="dots" id="dots"></div></div>

    <div class="card" id="c-bulk"><h2>Bulk Text</h2><div class="bulk" id="v-bulk"></div></div>

  </div>
</div>

<div class="wrap" id="page-tune" hidden>
  <div class="ttop"><button class="nav" id="btn-back">‹ Back</button><b>🐋 Whale Tuning</b></div>
  <div class="tfile">
    <button class="nav" id="btn-psave">⬇ Save .param</button>
    <button class="nav" id="btn-pload">⬆ Load .param</button>
    <span id="tmsg"></span>
    <input type="file" id="file-param" accept=".param,.txt,text/plain" hidden>
  </div>
  <div class="tgrid" id="tgrid"></div>
</div>

<div class="toast" id="toast"></div>

<div class="modal" id="modal">
  <div class="mbox">
    <p>Are you sure?</p>
    <small id="m-small"></small>
    <div class="mbtns"><button id="m-no">Cancel</button><button class="yes" id="m-yes">Yes</button></div>
  </div>
</div>

<script>
const $ = id => document.getElementById(id);
const set = (id, v) => { const e = $(id); if (e) e.textContent = v; };
const f = (v, d) => (v === undefined || v === null) ? "--" : Number(v).toFixed(d);

// ---- build repeated pieces ----
// battery: 6S pack. Per-cell limits: below WARN = amber, below LOW = red
const CELLS = 6, CELL_WARN = 3.7, CELL_LOW = 3.5;   // 22.2 V / 21.0 V on 6S

// last good ESC readings, so they can be shown greyed out when an ESC stops reporting
const lastEsc = {}, lastAvgRow = {};
let lastAvgV = "", lastAvgA = "";

const ESC_ROWS = [["Thr %","thr",0,"%"],["Volts","v",1,"V"],["Amps","a",1,"A"],["Temp °C","t",0,"°C"],["RPM","rpm",0,"rpm"],["Spd km/h","spd",1,"km/h"],["Link %","link",0,"%"]];   // [label, key, decimals, unit shown in the Avg column]
$('esc-body').innerHTML = ESC_ROWS.map(([l,k]) =>
  `<tr><td class="l">${l}</td>` + [1,2,3,4,5].map(n => `<td id="e${n}-${k}"></td>`).join("") + `</tr>`).join("");

let chHtml = "";
for (let c = 1; c <= 8; c++) chHtml += `<div class="ch"><i id="cb${c}"></i><b>${c}</b><span id="rx-ch${c}">--</span></div>`;
$('chs').innerHTML = chHtml;

let oHtml = "";
for (let c = 1; c <= 8; c++) oHtml += `<div class="ch"><i id="ob${c}"></i><b>${c}</b><span id="out-ch${c}">--</span></div>`;
$('ochs').innerHTML = oHtml;

let dHtml = "";
for (let i = 0; i < 20; i++) dHtml += `<div class="dc"><div class="dot" id="d${i}"></div><span>${i + 1}</span></div>`;
$('dots').innerHTML = dHtml;

// ---- tuning page ----
const NSTR = 5;   // the first 5 tuning variables hold text (must match NUM_TUNE_STR in the sketch)
let tHtml = "";
for (let i = 0; i < 20; i++)
  tHtml += i < NSTR
    ? `<div class="card tv str"><label id="tn${i}">Var ${i + 1}</label><input id="tin${i}" type="text" maxlength="31" placeholder="text" autocomplete="off" autocapitalize="off" spellcheck="false"></div>`
    : `<div class="card tv"><label id="tn${i}">Var ${i + 1}</label><input id="tin${i}" type="number" step="any" placeholder="0"></div>`;
$('tgrid').innerHTML = tHtml;
for (let i = 0; i < 20; i++) {
  $('tin' + i).addEventListener('change', e => {
    if (!websocket || websocket.readyState !== 1) return;
    if (i < NSTR) { websocket.send(`set,${i},${e.target.value}`); return; }   // text: sent as typed
    const v = parseFloat(e.target.value);
    if (!isNaN(v)) websocket.send(`set,${i},${v}`);
  });
}

// ---- .param file save / load (plain text, one "NAME,value" per line) ----
let tuneNames = [], lastTune = [];
const pname = n => n.trim().replace(/\s+/g, '_');
const tmsg = t => { $('tmsg').textContent = t; };

$('btn-psave').onclick = () => {
  if (!tuneNames.length || !lastTune.length) { tmsg('Not connected yet'); return; }
  let t = "# Whale Tuning parameters\n# NAME,value  (lines starting with # are ignored)\n";
  tuneNames.forEach((n, i) => { t += `${pname(n)},${typeof lastTune[i] === 'string' ? lastTune[i].replace(/[\r\n]+/g, ' ') : +Number(lastTune[i]).toPrecision(7)}\n`; });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([t], { type: 'text/plain' }));
  a.download = 'whale.param';
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1500);
  tmsg('Saved whale.param');
};

$('btn-pload').onclick = () => $('file-param').click();
$('file-param').addEventListener('change', e => {
  const file = e.target.files[0];
  if (!file) return;
  const r = new FileReader();
  r.onload = () => {
    e.target.value = '';
    if (!tuneNames.length || !websocket || websocket.readyState !== 1) { tmsg('Not connected yet'); return; }
    const idx = {};
    tuneNames.forEach((n, i) => { idx[pname(n).toLowerCase()] = i; });
    let ok = 0, skip = 0;
    String(r.result).split(/\r?\n/).forEach(line => {
      line = line.trim();
      if (!line || line[0] === '#' || line[0] === ';') return;        // blank line or comment
      const m = line.match(/^([^\s,=]+)[\s,=]+(.*)$/);                 // NAME, then everything after the first separator
      const i = m ? idx[m[1].toLowerCase()] : undefined;
      if (i === undefined) { skip++; return; }
      if (i < NSTR) { websocket.send(`set,${i},${m[2].trim().slice(0, 31)}`); ok++; return; }   // text variable: the rest of the line
      const v = parseFloat(m[2]);
      if (!isFinite(v)) { skip++; return; }
      websocket.send(`set,${i},${v}`);
      ok++;
    });
    tmsg(`Loaded ${ok} value${ok === 1 ? '' : 's'}` + (skip ? `, skipped ${skip} line${skip === 1 ? '' : 's'}` : ''));
  };
  r.readAsText(file);
});

// ---- arm switch lock: Reboot and tuning changes are blocked while channel 3 is high ----
function toast(t) {
  const e = $('toast');
  e.textContent = t;
  e.classList.add('show');
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => e.classList.remove('show'), 2500);
}
let isLocked = null;
function setLocked(l, rebootLocked) {
  $('btn-reboot').disabled = rebootLocked;                       // reboot: only locked when armed AND the link is up
  if (rebootLocked && $('m-yes').textContent === 'Yes, reboot') $('modal').classList.remove('show');
  if (l === isLocked) return;
  isLocked = l;
  $('btn-pload').disabled = l;                                   // tuning: locked whenever channel 3 is high
  document.querySelectorAll('#tgrid input').forEach(i => { i.disabled = l; });
  if (l) toast(rebootLocked ? 'Armed - reboot and tuning locked' : 'Armed (link down) - tuning locked');
}

// ---- websocket ----
let websocket, lastMsg = 0;
function initWebSocket() {
  websocket = new WebSocket(`ws://${window.location.host}/ws`);
  websocket.onclose = () => setTimeout(initWebSocket, 2000);
  websocket.onmessage = onMessage;
}

function onMessage(event) {
  const d = JSON.parse(event.data);
  if (d.blocked) { toast(d.blocked === 'reboot' ? 'Armed - reboot blocked' : 'Armed - tuning locked'); return; }
  if (d.armed !== undefined) setLocked(!!d.armed, !!d.noreboot);
  lastMsg = Date.now();

  set('v-mode', d.mode);
  set('v-boot', d.boot);
  $('v-bulk').textContent = (d.bulk || "") + (d.dbg ? "\n" + d.dbg : "");

  if (d.cpu) { set('v-c0', f(d.cpu[0],1)+" %"); set('v-c1', f(d.cpu[1],1)+" %"); set('v-ca', f(d.cpu[2],1)+" %"); }

  if (d.up !== undefined) {
    const u = d.up, days = Math.floor(u / 86400), p2 = x => String(x).padStart(2, '0');
    set('v-up', (days ? days + 'd ' : '') + Math.floor(u % 86400 / 3600) + ':' + p2(Math.floor(u % 3600 / 60)) + ':' + p2(u % 60));
  }

  if (d.wifi !== undefined) {
    const wf = $('v-wifi');
    if (d.wifi <= -127) { wf.textContent = "--"; wf.className = "v"; }
    else {
      // -100 dBm (or worse) = 0 %, -50 dBm (or better) = 100 %  (same scale as getSignalQuality() in the sketch)
      const pct = Math.max(0, Math.min(100, Math.round((d.wifi + 100) * 2)));
      wf.textContent = pct + " %";
      wf.className = "v " + (pct >= 80 ? "good" : pct >= 50 ? "warn" : "bad");   // strong / ok / weak
    }
  }

  if (d.esc) {
    for (let n = d.esc.length + 1; n <= 4; n++) $('eh' + n).classList.add('off');   // slots the firmware isn't using
    d.esc.forEach((e, i) => {
      const n = i + 1;
      if (e.ok) lastEsc[n] = e;                 // remember the last good reading
      const shown = e.ok ? e : lastEsc[n];      // not reporting -> keep showing the last one, greyed out
      $('eh' + n).classList.toggle('off', !e.ok);
      ESC_ROWS.forEach(([l, k, dec]) => {
        const c = $(`e${n}-${k}`);
        c.textContent = shown ? f(shown[k], dec) : "";   // an ESC that never reported stays blank
        c.classList.toggle('stale', !e.ok && !!shown);
      });
    });
    // Avg column: only ESCs that are reporting right now; if none are, show the last average greyed out
    const live = d.esc.filter(e => e.ok);
    ESC_ROWS.forEach(([l, k, dec, unit]) => {
      const c = $(`e5-${k}`);
      if (live.length) lastAvgRow[k] = f(live.reduce((s, e) => s + e[k], 0) / live.length, dec);
      c.innerHTML = lastAvgRow[k] ? lastAvgRow[k] + '<small>' + unit + '</small>' : "";   // number plus a small unit
      c.classList.toggle('stale', !live.length && !!lastAvgRow[k]);
    });
  }

  if (d.rec) {
    set('v-rg', f(d.rec.g, 2) + " g");          // Max G lives in the IMU box now
    set('v-rtmp', f(d.rec.t, 0) + " °C");
    set('v-ra', f(d.rec.a, 1) + " A");
    set('v-rt', f(d.rec.ta, 1) + " A");
    set('v-rv', f(d.rec.v, 1) + " V");
    set('v-rs', f(d.rec.s, 1) + " km/h");
  }

  // average over ESCs that are giving data, coloured for 6S battery state
  const av = $('v-avgv');
  if (d.avg && d.avg.n > 0) {
    const perCell = d.avg.v / CELLS;
    av.textContent = f(d.avg.v, 1) + " V";
    av.className = "v " + (perCell < CELL_LOW ? "bad" : perCell < CELL_WARN ? "warn" : "good");
    set('v-avga', f(d.avg.a, 1) + " A");
    $('v-avga').className = "v";
    lastAvgV = av.textContent; lastAvgA = $('v-avga').textContent;
  } else if (lastAvgV) {
    // no ESC reporting: keep the last averages, greyed out
    av.textContent = lastAvgV; av.className = "v stale";
    set('v-avga', lastAvgA); $('v-avga').className = "v stale";
  } else {
    av.textContent = "--"; av.className = "v";
    set('v-avga', "--");
  }

  if (d.rx) {
    const up = $('rx-up');
    up.textContent = d.rx.up ? "UP" : "DOWN";
    up.className = "v " + (d.rx.up ? "good" : "bad");
    set('rx-lq', d.rx.lq + " %");
    set('rx-rssi', d.rx.rssi + " %");
    set('rx-dbm', d.rx.dbm);
    (d.rx.ch || []).forEach((v, i) => {
      set('rx-ch' + (i + 1), v);
      const pct = Math.max(0, Math.min(100, (v - 1000) / 10));
      $('cb' + (i + 1)).style.width = pct + "%";
    });
  }

  if (d.names) { tuneNames = d.names; d.names.forEach((n, i) => set('tn' + i, n)); }
  if (d.tune) lastTune = d.tune;
  if (d.tune) d.tune.forEach((v, i) => {
    const inp = $('tin' + i);
    if (inp && document.activeElement !== inp) inp.value = (typeof v === 'string') ? v : +Number(v).toFixed(4);   // don't overwrite while typing
  });

  if (d.out) d.out.forEach((v, i) => {
    set('out-ch' + (i + 1), v);
    $('ob' + (i + 1)).style.width = Math.max(0, Math.min(100, (v - 1000) / 10)) + "%";
  });

  if (d.imu) {
    const m = d.imu;
    set('i-gx', m.gx); set('i-gy', m.gy); set('i-gz', m.gz);
    set('i-ax', m.ax); set('i-ay', m.ay); set('i-az', m.az);
    // arrows you drive from the sketch: 1 / -1 / 0 (blank)
    set('i-ud', m.ud > 0 ? "⬆️" : m.ud < 0 ? "⬇️" : "--");
    set('i-lr', m.lr > 0 ? "➡️" : m.lr < 0 ? "⬅️" : "--");
  }

  if (d.arr) d.arr.forEach((v, i) => $('d' + i).classList.toggle('on', !!v));
}

setInterval(() => {
  const live = (Date.now() - lastMsg) < 1000;
  const e = $('v-live');
  e.textContent = live ? "LIVE" : "STALE";
  e.className = "v " + (live ? "good" : "bad");
}, 300);

// ---- confirmation box, shared by Reset records and Reboot ----
const modal = $('modal');
let pendingAction = null;
const wsSend = m => { if (websocket && websocket.readyState === 1) websocket.send(m); };
function ask(detail, yesLabel, action) {
  $('m-small').textContent = detail;
  $('m-yes').textContent = yesLabel;
  pendingAction = action;
  modal.classList.add('show');
}
$('btn-reset').onclick = () => ask('All records (Max G, Max ESC Temp, Max ESC Amps, Max Total Amps, Min Volts, Max Avg Speed) will be reset to 0.', 'Yes, reset', () => wsSend('reset'));
$('btn-reboot').onclick = () => ask('The board will restart. Motors lose their signal and the page reconnects in a few seconds.', 'Yes, reboot', () => wsSend('reboot'));
$('m-no').onclick = () => modal.classList.remove('show');
modal.onclick = e => { if (e.target === modal) modal.classList.remove('show'); };
$('m-yes').onclick = () => {
  modal.classList.remove('show');
  if (pendingAction) pendingAction();
  pendingAction = null;
};

// ---- fit everything on one screen: shrink the base font until nothing scrolls ----
function fit() {
  const r = document.documentElement;
  r.style.removeProperty('--fs');
  let fs = parseFloat(getComputedStyle(r).getPropertyValue('--fs')) || 12;
  while ((r.scrollHeight > window.innerHeight + 1 || r.scrollWidth > window.innerWidth + 1) && fs > 7) {
    fs -= 0.25;
    r.style.setProperty('--fs', fs + 'px');
  }
}
window.addEventListener('resize', fit);
window.addEventListener('orientationchange', () => setTimeout(fit, 150));
setInterval(fit, 3000);

// ---- page switching (#tune = tuning page) ----
function route() {
  const t = location.hash === '#tune';
  $('page-main').hidden = t;
  $('page-tune').hidden = !t;
  fit();
}
$('btn-tune').onclick = () => { location.hash = 'tune'; };
$('btn-back').onclick = () => { location.hash = ''; };
window.addEventListener('hashchange', route);

window.addEventListener('load', () => { initWebSocket(); route(); setTimeout(fit, 400); });
</script>
</body>
</html>
)rawliteral";
