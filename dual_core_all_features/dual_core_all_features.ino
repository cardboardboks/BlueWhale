#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"
#include <AlfredoCRSF.h>
#include <HardwareSerial.h>
#include <Arduino.h>
#include <ESPCpuMonitor.h>
#include <SPI.h>
#include <AlfredoDShot.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <ArduinoJson.h>
#include <Preferences.h>  // flash (NVS) storage for the tuning values
#include "esp_wifi.h"
#include "driver/spi_master.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>  // Required for ESP32

spi_device_handle_t ledDev;

SET_TIME_BEFORE_STARTING_SKETCH_MS(2000);

IPAddress local_IP(192, 168, 10, 1);
IPAddress gateway(192, 168, 10, 1);
IPAddress subnet(255, 255, 255, 0);

const char *ssid = "Whale";
const char *password = "Whale#1234";
uint32_t last_ota_time = 0;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ---------------- Web dashboard variables ----------------
String modeString = "Idle";     // <- set this from anywhere in your code
String bootState = "Starting";  // <- updated during setup()
String bulkText = "";           // <- free text box, put anything in here

#define ARRAY_SIZE 20
volatile bool flagArray[ARRAY_SIZE];  // <- set any slot true to light its green dot

// Volatile variables for the raw readings
volatile float core0 = 0;
volatile float core1 = 0;
volatile float coreA = 0;

// NEW: Persistent peak variables that decay slowly
volatile float core0Peak = 0;
volatile float core1Peak = 0;
volatile float coreAveragePeak = 0;  // Added system average peak-hold tracker

// NEW: Decay rate coefficient (Value between 0.0 and 1.0)
// Lower value = slower decay (e.g., 0.05 is very smooth, 0.20 is faster)
const float PEAK_DECAY_ALPHA = 0.01f;

volatile int16_t imuSpare1 = 0;  // two spare IMU fields
volatile int16_t imuSpare2 = 0;

// ---- Records (tracked here on the ESP so nothing is missed between page updates) ----
#define DMP_ACCEL_LSB_PER_G 16384.0f  // accel counts per g (+/-2 g range). If Max G still reads ~0.5 or ~1 at rest, try 8192.0f
volatile float recMaxG = 0;           // peak acceleration in g with gravity removed (about 0 when sitting still)
volatile float recMaxA = 0;           // highest current seen on any single ESC
volatile float recMinV = 0;           // lowest voltage seen on any ESC (0 = nothing recorded yet)
volatile float recMaxSpd = 0;         // highest AVERAGE speed (average of the ESCs reporting)

void resetRecords() {
  recMaxG = 0;
  recMaxA = 0;
  recMinV = 0;
  recMaxSpd = 0;
}

// ---- Whale Tuning page: 20 placeholder variables ----
// Rename them here. Use tuneVal[n] anywhere in your code; editing a box on the page changes it live.
// Values are saved to flash (see saveTune) and reloaded at boot; 0 is used until something has been saved.
#define NUM_TUNE 20
const char *tuneName[NUM_TUNE] = {
  "Tune Var 01", "Tune Var 02", "Tune Var 03", "Tune Var 04", "Tune Var 05",
  "Tune Var 06", "Tune Var 07", "Tune Var 08", "Tune Var 09", "Tune Var 10",
  "Tune Var 11", "Tune Var 12", "Tune Var 13", "Tune Var 14", "Tune Var 15",
  "Tune Var 16", "Tune Var 17", "Tune Var 18", "Tune Var 19", "Tune Var 20"
};
volatile float tuneVal[NUM_TUNE] = { 0 };

// ---- flash storage for the tuning values ----
// Edits are saved TUNE_SAVE_DELAY_MS after the last change, so typing several values
// causes one flash write instead of many (flash has a limited number of write cycles).
#define TUNE_SAVE_DELAY_MS 2000
Preferences prefs;
volatile bool tuneDirty = false;
volatile uint32_t tuneChangedMs = 0;

// Reboot requested from the web page (done from loop() after a short delay so the page gets its last update)
volatile bool rebootPending = false;
volatile uint32_t rebootAtMs = 0;

void loadTune() {
  float tmp[NUM_TUNE];
  prefs.begin("whale", true);  // read-only
  size_t n = prefs.getBytesLength("tune");
  if (n == sizeof(tmp)) {  // only trust it if the size matches (changing NUM_TUNE falls back to defaults)
    prefs.getBytes("tune", tmp, sizeof(tmp));
    for (int i = 0; i < NUM_TUNE; i++) tuneVal[i] = tmp[i];
  }
  prefs.end();
}

void saveTune() {
  float tmp[NUM_TUNE];
  for (int i = 0; i < NUM_TUNE; i++) tmp[i] = tuneVal[i];
  prefs.begin("whale", false);  // read/write
  prefs.putBytes("tune", tmp, sizeof(tmp));
  prefs.end();
}

#define NUM_OUT 8
// 8 output channels shown on the page. Write 1000-2000 into these from anywhere, e.g. outChannel[0] = 1750;
volatile int outChannel[NUM_OUT] = { 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500 };

#define NUM_ESC 4
float throttleCmd[NUM_ESC] = { 0.00f, 0.00f, 0.00f, 0.00f };  // 0.0 - 1.0, what you send to each ESC

// An ESC that isn't giving telemetry gets re-sent "enable EDT" this often (ms),
// so one plugged in after boot can start reporting. Set to 0 to turn this off.
#define ESC_REDETECT_MS 0

volatile bool otaInProgress = false;
unsigned long lastUpdate = 0;

// Scale dBm to 0-100 %
int getSignalQuality(int dbm) {
  if (dbm == -127) return 0;
  if (dbm >= -50) return 100;
  if (dbm <= -100) return 0;
  return map(dbm, -100, -50, 0, 100);
}

// ---------------- Web page ----------------
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
#c-esc table.num th:first-child, #c-esc table.num td.l { width: 25%; }
#c-esc th:last-child, #c-esc td:last-child { border-left: 1px solid var(--border); }
#c-esc td:last-child { color: #fff; font-weight: 600; }
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
/* hide the up/down spinner arrows on number boxes (desktop browsers) */
.tv input::-webkit-outer-spin-button, .tv input::-webkit-inner-spin-button { -webkit-appearance: none; margin: 0; }
.tv input[type=number] { -moz-appearance: textfield; appearance: textfield; }
#c-rec h2, #c-cpu h2 { display: flex; justify-content: space-between; align-items: center; }
.reset { font: inherit; font-size: .8rem; text-transform: none; letter-spacing: 0; padding: 0 8px; border-radius: 6px;
         border: 1px solid rgba(255,69,58,.5); background: rgba(255,69,58,.15); color: var(--bad); }
.modal { position: fixed; inset: 0; background: rgba(0,0,0,.65); display: none; align-items: center; justify-content: center; z-index: 10; }
.modal.show { display: flex; }
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
      <div class="kv"><span>Avg Amps</span><span class="v" id="v-avga">--</span></div>
    </div>

    <div class="card" id="c-cpu"><h2><span>CPU</span><button class="reset" id="btn-reboot">Reboot</button></h2>
      <div class="kv"><span>Core 0</span><span class="v" id="v-c0">--</span></div>
      <div class="kv"><span>Core 1</span><span class="v" id="v-c1">--</span></div>
      <div class="kv"><span>Average</span><span class="v" id="v-ca">--</span></div>
      <div class="kv"><span>WiFi</span><span class="v" id="v-wifi">--</span></div>
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
          <tr><td class="l">Spare</td><td id="i-s1">--</td><td id="i-s2">--</td><td></td></tr>
        </tbody>
      </table>
    </div>

    <div class="card" id="c-rec"><h2><span>Records</span><button class="reset" id="btn-reset">Reset</button></h2>
      <div class="kv"><span>Max G</span><span class="v" id="v-rg">--</span></div>
      <div class="kv"><span>Max Amps</span><span class="v" id="v-ra">--</span></div>
      <div class="kv"><span>Min Volts</span><span class="v" id="v-rv">--</span></div>
      <div class="kv"><span>Max Avg Spd</span><span class="v" id="v-rs">--</span></div>
    </div>

    <div class="card" id="c-arr"><h2>Array</h2><div class="dots" id="dots"></div></div>

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

const ESC_ROWS = [["Thr %","thr",0],["Volts","v",1],["Amps","a",1],["Temp °C","t",0],["RPM","rpm",0],["Spd km/h","spd",1],["Link %","link",0]];
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
let tHtml = "";
for (let i = 0; i < 20; i++)
  tHtml += `<div class="card tv"><label id="tn${i}">Var ${i + 1}</label><input id="tin${i}" type="number" step="any" placeholder="0"></div>`;
$('tgrid').innerHTML = tHtml;
for (let i = 0; i < 20; i++) {
  $('tin' + i).addEventListener('change', e => {
    const v = parseFloat(e.target.value);
    if (!isNaN(v) && websocket && websocket.readyState === 1) websocket.send(`set,${i},${v}`);
  });
}

// ---- .param file save / load (plain text, one "NAME,value" per line) ----
let tuneNames = [], lastTune = [];
const pname = n => n.trim().replace(/\s+/g, '_');
const tmsg = t => { $('tmsg').textContent = t; };

$('btn-psave').onclick = () => {
  if (!tuneNames.length || !lastTune.length) { tmsg('Not connected yet'); return; }
  let t = "# Whale Tuning parameters\n# NAME,value  (lines starting with # are ignored)\n";
  tuneNames.forEach((n, i) => { t += `${pname(n)},${+Number(lastTune[i]).toPrecision(7)}\n`; });
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
      line = line.replace(/[#;].*$/, '').trim();
      if (!line) return;
      const p = line.split(/[\s,=]+/);
      const i = idx[(p[0] || '').toLowerCase()];
      const v = parseFloat(p[1]);
      if (i === undefined || !isFinite(v)) { skip++; return; }
      websocket.send(`set,${i},${v}`);
      ok++;
    });
    tmsg(`Loaded ${ok} value${ok === 1 ? '' : 's'}` + (skip ? `, skipped ${skip} line${skip === 1 ? '' : 's'}` : ''));
  };
  r.readAsText(file);
});

// ---- websocket ----
let websocket, lastMsg = 0;
function initWebSocket() {
  websocket = new WebSocket(`ws://${window.location.host}/ws`);
  websocket.onclose = () => setTimeout(initWebSocket, 2000);
  websocket.onmessage = onMessage;
}

function onMessage(event) {
  const d = JSON.parse(event.data);
  lastMsg = Date.now();

  set('v-mode', d.mode);
  set('v-boot', d.boot);
  $('v-bulk').textContent = d.bulk || "";

  if (d.cpu) { set('v-c0', f(d.cpu[0],1)+" %"); set('v-c1', f(d.cpu[1],1)+" %"); set('v-ca', f(d.cpu[2],1)+" %"); }

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
    d.esc.forEach((e, i) => {
      const n = i + 1;
      $('eh' + n).classList.toggle('off', !e.ok);
      ESC_ROWS.forEach(([l, k, dec]) => set(`e${n}-${k}`, e.ok ? f(e[k], dec) : ""));  // absent ESC = blank
    });
    // Avg column: only ESCs that are reporting
    const live = d.esc.filter(e => e.ok);
    ESC_ROWS.forEach(([l, k, dec]) =>
      set(`e5-${k}`, live.length ? f(live.reduce((s, e) => s + e[k], 0) / live.length, dec) : ""));
  }

  if (d.rec) {
    set('v-rg', f(d.rec.g, 2) + " g");
    set('v-ra', f(d.rec.a, 1) + " A");
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
    if (inp && document.activeElement !== inp) inp.value = +Number(v).toFixed(4);   // don't overwrite while typing
  });

  if (d.out) d.out.forEach((v, i) => {
    set('out-ch' + (i + 1), v);
    $('ob' + (i + 1)).style.width = Math.max(0, Math.min(100, (v - 1000) / 10)) + "%";
  });

  if (d.imu) {
    const m = d.imu;
    set('i-gx', m.gx); set('i-gy', m.gy); set('i-gz', m.gz);
    set('i-ax', m.ax); set('i-ay', m.ay); set('i-az', m.az);
    set('i-s1', m.s1); set('i-s2', m.s2);
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
$('btn-reset').onclick = () => ask('Max G, Max Amps, Min Volts and Max Avg Speed will all be reset to 0.', 'Yes, reset', () => wsSend('reset'));
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

// ---------------- Hardware ----------------
// ESC signal pins, in web-page order (ESC 1..4).
// ESC 1 = GPIO 2 and ESC 2 = GPIO 1, matching your original (swapped) begin() calls.
// ESC 3 and ESC 4 pins are a guess - change them to wherever you wire them.
const int ESC_PINS[NUM_ESC] = { 2, 1, 9, 10 };
const uint8_t MOTOR_POLES = 14;

AlfredoDShot escs[NUM_ESC];

ESPCpuMonitor cpuMonitor;

#define PIN_RX 5
#define PIN_TX 4

#define SCL_PIN 6
#define SDA_PIN 7
#define INT_PIN 8

#define LED_PIN 11
#define NUM_LEDS 12
#define SPI_FREQ 3200000

#define RESET_BYTES 64
static uint8_t spiBuf[NUM_LEDS * 12 + RESET_BYTES];

struct RGB {
  uint8_t r, g, b;
};
RGB leds[NUM_LEDS];

static const uint8_t lut[4] = {
  0b10001000,
  0b10001110,
  0b11101000,
  0b11101110
};

void encodeByte(uint8_t v, uint8_t *out) {
  out[0] = lut[(v >> 6) & 3];
  out[1] = lut[(v >> 4) & 3];
  out[2] = lut[(v >> 2) & 3];
  out[3] = lut[v & 3];
}

void ledInit() {
  spi_bus_config_t bus = {};
  bus.mosi_io_num = LED_PIN;
  bus.miso_io_num = -1;
  bus.sclk_io_num = 12;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = sizeof(spiBuf);
  spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = SPI_FREQ;
  dev.mode = 0;
  dev.spics_io_num = -1;
  dev.queue_size = 1;
  spi_bus_add_device(SPI3_HOST, &dev, &ledDev);
}

void showLeds() {
  uint8_t *p = spiBuf;
  for (int i = 0; i < NUM_LEDS; i++) {
    encodeByte(leds[i].g, p);
    p += 4;
    encodeByte(leds[i].r, p);
    p += 4;
    encodeByte(leds[i].b, p);
    p += 4;
  }
  memset(p, 0, RESET_BYTES);

  spi_transaction_t t = {};
  t.length = sizeof(spiBuf) * 8;
  t.tx_buffer = spiBuf;
  spi_device_transmit(ledDev, &t);
}

void ledTask(void *pvParameters) {
  ledInit();

  uint8_t hue = 0;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    if (otaInProgress == false) {
      for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t h = hue + i * (256 / NUM_LEDS);
        uint8_t region = h / 43, rem = (h - region * 43) * 6;
        uint8_t v = 64, q = (v * (255 - rem)) >> 8, t = (v * rem) >> 8;
        switch (region) {
          case 0: leds[i] = { v, t, 0 }; break;
          case 1: leds[i] = { q, v, 0 }; break;
          case 2: leds[i] = { 0, v, t }; break;
          case 3: leds[i] = { 0, q, v }; break;
          case 4: leds[i] = { t, 0, v }; break;
          default: leds[i] = { v, 0, q }; break;
        }
      }
      hue++;
      showLeds();
    }
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1));
  }
}

HardwareSerial crsfSerial(1);
AlfredoCRSF crsf;

uint32_t lastHeartbeatMs = 0;
uint32_t lastPrintMs = 0;

int roll = 0;
int pitch = 0;
int yaw = 0;

int roll_flip = 70;

const int RPnumReadings = 50;
int RPreadings[RPnumReadings];
int RPreadIndex = 0;
int RPtotal = 0;

const int YnumReadings = 5;
int Yreadings[YnumReadings];
int YreadIndex = 0;
int Ytotal = 0;

volatile int RP = 0;
volatile int Y = 0;

// Raw IMU values for the web page (written on core 0, read on core 1)
volatile int16_t imuGx = 0, imuGy = 0, imuGz = 0;
volatile int16_t imuAx = 0, imuAy = 0, imuAz = 0;

// volatile float core0 = 0;
// volatile float core1 = 0;
// volatile float coreA = 0;

int gearbox_ratio[NUM_ESC] = { 19, 19, 19, 19 };
int wheel_diamiter[NUM_ESC] = { 101, 101, 101, 101 };  // in mm

const unsigned long INTERVAL_US = 1000;
unsigned long previousMicros = 0;

MPU6050 mpu;

TaskHandle_t imuTaskHandle = nullptr;

void IRAM_ATTR dmpDataReady() {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(imuTaskHandle, &woken);
  if (woken) portYIELD_FROM_ISR();
}

uint8_t fifoBuffer[64];
Quaternion q;
VectorFloat gravity;
VectorInt16 gy;  // gyro
VectorInt16 aa;  // accel (raw, includes gravity)
float ypr[3];
bool dmpReady = false;

void imuTask(void *arg) {
  if (otaInProgress == false) {
    for (;;) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

      if (!mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) continue;

      mpu.dmpGetQuaternion(&q, fifoBuffer);
      mpu.dmpGetGravity(&gravity, &q);
      mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

      pitch = (int)fabsf(ypr[1] * 180.0f / M_PI);
      roll = (int)fabsf(ypr[2] * 180.0f / M_PI);

      RPtotal -= RPreadings[RPreadIndex];
      RPreadings[RPreadIndex] = min(roll + pitch, roll_flip * 2);
      RPtotal += RPreadings[RPreadIndex];
      RPreadIndex = (RPreadIndex + 1) % RPnumReadings;
      RP = RPtotal / RPnumReadings;

      mpu.dmpGetGyro(&gy, fifoBuffer);
      mpu.dmpGetAccel(&aa, fifoBuffer);

      imuGx = gy.x;
      imuGy = gy.y;
      imuGz = gy.z;
      imuAx = aa.x;
      imuAy = aa.y;
      imuAz = aa.z;

      // Remove gravity using the DMP orientation. Done by hand (not dmpGetLinearAccel) because that
      // library function assumes 8192 counts/g, which doesn't match the 16384 this sensor is giving.
      // 'gravity' is a unit vector (computed above from the quaternion), so scale it to counts first.
      float lx = aa.x - gravity.x * DMP_ACCEL_LSB_PER_G;
      float ly = aa.y - gravity.y * DMP_ACCEL_LSB_PER_G;
      float lz = aa.z - gravity.z * DMP_ACCEL_LSB_PER_G;
      float g = sqrtf(lx * lx + ly * ly + lz * lz) / DMP_ACCEL_LSB_PER_G;
      if (g > recMaxG) recMaxG = g;

      yaw = abs(gy.z);

      Ytotal -= Yreadings[YreadIndex];
      Yreadings[YreadIndex] = yaw;
      Ytotal += Yreadings[YreadIndex];
      YreadIndex = (YreadIndex + 1) % YnumReadings;

      int y = Ytotal / YnumReadings;
      Y = (y > 1400) ? 1400 : y;
    }
  }
}

// ---------------- JSON for the web page ----------------
// An ESC counts as present when it is reporting a sensible voltage and its link isn't dead.
// Tweak this one function if your ESCs behave differently when unplugged.
bool escPresent(int i) {
  return escs[i].voltage() > 1.0f && escs[i].lossPercent() < 100;
}

float escSpeed(int i) {
  float rpmOut = escs[i].rpm() / gearbox_ratio[i];
  return (wheel_diamiter[i] / 10) * rpmOut * 0.001885;
}

// Called on every pass of loop() (about every 1 ms): updates max current, min voltage, max average speed
void updateRecords() {
  float sumSpd = 0;
  int n = 0;
  for (int i = 0; i < NUM_ESC; i++) {
    if (!escPresent(i)) continue;
    n++;
    float v = escs[i].voltage();
    float a = escs[i].current();
    if (a > recMaxA) recMaxA = a;
    if (v > 1.0f && (recMinV == 0 || v < recMinV)) recMinV = v;
    sumSpd += fabsf(escSpeed(i));
  }
  if (n > 0) {
    float avgSpd = sumSpd / n;
    if (avgSpd > recMaxSpd) recMaxSpd = avgSpd;
  }
}

void addEsc(JsonArray arr, int i, bool ok) {
  JsonObject o = arr.add<JsonObject>();
  o["ok"] = ok;
  if (!ok) return;  // absent ESC -> page leaves its column blank
  float rpmOut = escs[i].rpm() / gearbox_ratio[i];
  o["thr"] = throttleCmd[i] * 100.0f;
  o["v"] = escs[i].voltage();
  o["a"] = escs[i].current();
  o["t"] = escs[i].temperatureC();
  o["rpm"] = rpmOut;
  o["spd"] = escSpeed(i);
  o["link"] = (float)escs[i].lossPercent();  // adjust if you want a different "link" metric
}

// WiFi signal strength (dBm) of the strongest phone/laptop connected to our access point; -127 = nobody connected
int wifiRssi() {
  wifi_sta_list_t list;
  int best = -127;
  if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) {
    for (int i = 0; i < list.num; i++)
      if (list.sta[i].rssi > best) best = list.sta[i].rssi;
  }
  return best;
}

String getSerializedData() {
  JsonDocument doc;

  doc["mode"] = modeString;
  doc["boot"] = bootState;
  doc["bulk"] = bulkText;

  // CPU: per core + average
  float c0 = core0, c1 = core1, ca = coreA;
  JsonArray cpu = doc["cpu"].to<JsonArray>();
  cpu.add(c0);
  cpu.add(c1);
  cpu.add(ca);

  // ESCs, plus average voltage/current over the ones that are giving data
  JsonArray escArr = doc["esc"].to<JsonArray>();
  float sumV = 0, sumA = 0;
  int nEsc = 0;
  for (int i = 0; i < NUM_ESC; i++) {
    bool ok = escPresent(i);
    addEsc(escArr, i, ok);
    if (ok) {
      sumV += escs[i].voltage();
      sumA += escs[i].current();
      nEsc++;
    }
  }
  JsonObject avg = doc["avg"].to<JsonObject>();
  avg["n"] = nEsc;
  if (nEsc > 0) {
    avg["v"] = sumV / nEsc;
    avg["a"] = sumA / nEsc;
  }

  // Receiver
  JsonObject rx = doc["rx"].to<JsonObject>();
  const crsfLinkStatistics_t *link = crsf.getLinkStatistics();
  int dbm = -(int)(link->active_antenna == 0 ? link->uplink_RSSI_1 : link->uplink_RSSI_2);
  rx["up"] = crsf.isLinkUp();
  rx["lq"] = link->uplink_Link_quality;
  rx["rssi"] = getSignalQuality(dbm);
  rx["dbm"] = dbm;
  JsonArray ch = rx["ch"].to<JsonArray>();
  for (int i = 1; i <= 8; i++) ch.add(crsf.getChannel(i));

  doc["wifi"] = wifiRssi();

  // Tuning values
  JsonArray tune = doc["tune"].to<JsonArray>();
  for (int i = 0; i < NUM_TUNE; i++) tune.add((float)tuneVal[i]);

  // Records
  JsonObject rec = doc["rec"].to<JsonObject>();
  rec["g"] = (float)recMaxG;
  rec["a"] = (float)recMaxA;
  rec["v"] = (float)recMinV;
  rec["s"] = (float)recMaxSpd;

  // Outputs
  JsonArray outArr = doc["out"].to<JsonArray>();
  for (int i = 0; i < NUM_OUT; i++) outArr.add((int)outChannel[i]);

  // IMU
  JsonObject imu = doc["imu"].to<JsonObject>();
  imu["gx"] = (int)imuGx;
  imu["gy"] = (int)imuGy;
  imu["gz"] = (int)imuGz;
  imu["ax"] = (int)imuAx;
  imu["ay"] = (int)imuAy;
  imu["az"] = (int)imuAz;
  imu["s1"] = (int)imuSpare1;
  imu["s2"] = (int)imuSpare2;

  // Bool array -> dots
  JsonArray arr = doc["arr"].to<JsonArray>();
  for (int i = 0; i < ARRAY_SIZE; i++) arr.add(flagArray[i] ? 1 : 0);

  String output;
  serializeJson(doc, output);
  return output;
}

// Names for the tuning page, sent once when a page connects
String getTuneNames() {
  JsonDocument doc;
  JsonArray names = doc["names"].to<JsonArray>();
  for (int i = 0; i < NUM_TUNE; i++) names.add(tuneName[i]);
  String output;
  serializeJson(doc, output);
  return output;
}

void onWsEvent(AsyncWebSocket *s, AsyncWebSocketClient *client, AwsEventType type,
               void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    client->text(getSerializedData());  // send current values immediately
    client->text(getTuneNames());
  } else if (type == WS_EVT_DATA) {
    // the page's "Reset" button sends the text "reset"
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      if (len == 5 && memcmp(data, "reset", 5) == 0) resetRecords();
      else if (len == 6 && memcmp(data, "reboot", 6) == 0) {
        modeString = "Rebooting";
        rebootAtMs = millis() + 500;
        rebootPending = true;
      }
      // tuning box edited on the page: "set,<index>,<value>"
      else if (len > 4 && len < 40 && memcmp(data, "set,", 4) == 0) {
        char buf[40];
        memcpy(buf, data, len);
        buf[len] = 0;
        int idx;
        float val;
        if (sscanf(buf, "set,%d,%f", &idx, &val) == 2 && idx >= 0 && idx < NUM_TUNE) {
          tuneVal[idx] = val;
          tuneChangedMs = millis();
          tuneDirty = true;  // loop() saves it to flash shortly after
        }
      }
    }
  }
}

// Push a web update right now (used during boot, when loop() isn't running yet)
void pushWeb() {
  ws.textAll(getSerializedData());
  ws.cleanupClients();
}

// Peak hold: a new high is taken immediately, otherwise ease down toward the raw reading
float peakHold(float raw, float peak) {
  if (raw > peak) return raw;
  return raw * PEAK_DECAY_ALPHA + peak * (1.0f - PEAK_DECAY_ALPHA);
}

void setup() {
  // Serial first, so nothing printed during setup is lost. On boards using the native USB port
  // this waits (up to 1.5 s) for the serial monitor to attach; on a UART-bridge port it returns at once.
  Serial.begin(115200);
  for (unsigned long t = millis(); !Serial && millis() - t < 1500;) delay(10);
  Serial.println("\n=== Whale boot ===");

  bootState = "GPIO init";

  gpio_config_t cfg = {};
  for (int i = 0; i < NUM_ESC; i++) {
    cfg.pin_bit_mask = 1ULL << ESC_PINS[i];
    cfg.mode = GPIO_MODE_OUTPUT_OD;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    gpio_set_level((gpio_num_t)ESC_PINS[i], 0);
  }

  loadTune();  // restore saved tuning values first

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  Serial.println("ELRS 4.x receiver test");

  bootState = "Receiver init";
  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);
  if (!crsfSerial)
    while (1) Serial.println("Invalid crsfSerial configuration");

  crsf.begin(crsfSerial);
  crsf.setDeviceName("AlfredoCRSF");

  for (int i = 0; i < RPnumReadings; i++) RPreadings[i] = 0;
  for (int i = 0; i < YnumReadings; i++) Yreadings[i] = 0;
  for (int i = 0; i < ARRAY_SIZE; i++) flagArray[i] = false;

  bootState = "IMU init";
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 not found - check wiring/address");
    while (1) delay(1000);
  }

  uint8_t devStatus = mpu.dmpInitialize();

  mpu.setXGyroOffset(-234);
  mpu.setYGyroOffset(24);
  mpu.setZGyroOffset(-9);
  mpu.setXAccelOffset(-698);
  mpu.setYAccelOffset(476);
  mpu.setZAccelOffset(2538);

  if (devStatus == 0) {
    mpu.setDMPEnabled(true);

    xTaskCreatePinnedToCore(imuTask, "imu_0", 4096, nullptr, 2, &imuTaskHandle, 0);
    xTaskCreatePinnedToCore(ledTask, "ledTask", 4096, NULL, 1, NULL, 0);

    pinMode(INT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), dmpDataReady, RISING);

    dmpReady = true;
    Serial.println("DMP ready");
  } else {
    Serial.printf("DMP init failed (code %d)\n", devStatus);
    while (1) delay(1000);
  }

  bootState = "WiFi AP start";
  Serial.println("\nConfiguring Access Point...");

  WiFi.mode(WIFI_AP);

  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Failed to configure AP IP address");
  }
  WiFi.softAP(ssid, password);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.begin();

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Servers initialized smoothly.");

  ArduinoOTA
    .onStart([]() {
      otaInProgress = true;
      modeString = "OTA update";
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {
        type = "filesystem";
      }
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      otaInProgress = false;
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      if (millis() - last_ota_time > 500) {
        Serial.printf("Progress: %u%%\n", (progress / (total / 100)));
        last_ota_time = millis();
      }
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      }
    });

  ArduinoOTA.begin();

  bootState = "Waiting 2s";
  for (unsigned long start = millis(); millis() - start < 2000;) {
  }


  for (int i = 0; i < NUM_ESC; i++) escs[i].begin(ESC_PINS[i], DSHOT600, true, MOTOR_POLES);

  bootState = "Waiting for ESC boot";
  unsigned long lastBootPush = 0;
  for (unsigned long start = millis(); millis() - start < 5000;) {
    unsigned long currentMicros = micros();

    if (currentMicros - previousMicros >= INTERVAL_US) {
      previousMicros += INTERVAL_US;
      for (int i = 0; i < NUM_ESC; i++) escs[i].sendThrottle(0.f);
    }

    // keep the web page updating during the ESC wait (slow, so it won't upset DShot timing much)
    if (millis() - lastBootPush > 250) {
      lastBootPush = millis();
      pushWeb();
    }
  }

  bootState = "Enabling EDT";
  for (int i = 0; i < NUM_ESC; i++) escs[i].command(DSHOT_CMD_EDT_ENABLE);

  bootState = "CPU monitor init";
  CpuMonitorConfig cpucfg;
  cpucfg.sampleIntervalMs = 10;
  cpucfg.calibrationSamples = 5;
  cpucfg.enablePerCore = true;
  cpucfg.enableTemperature = false;

  if (!cpuMonitor.init(cpucfg)) {
    Serial.println("CPU monitor init failed");
    return;
  }

  bootState = "Ready";
  modeString = "Running";
}

void loop() {

  ArduinoOTA.handle();

  if (rebootPending && millis() > rebootAtMs) {
    if (tuneDirty) saveTune();  // don't lose a tuning edit made in the last 2 seconds
    Serial.println("Rebooting...");
    ESP.restart();
  }

  if (otaInProgress) {
    return;
  }


  // ---- diagnostics: longest gap between loop() passes, plus memory, printed once a second ----
  static uint32_t lastLoopUs = micros();
  static uint32_t maxGapUs = 0;
  uint32_t nowUs = micros();
  if (nowUs - lastLoopUs > maxGapUs) maxGapUs = nowUs - lastLoopUs;
  lastLoopUs = nowUs;

  static uint32_t lastDiagMs = 0;
  if (millis() - lastDiagMs >= 1000) {
    lastDiagMs = millis();
    ws.cleanupClients();  // drops dead/stalled web clients so their queued messages don't pile up in RAM
    Serial.printf("diag: heap=%u min=%u maxLoopGap=%lums wsClients=%u link=%s\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                  (unsigned long)(maxGapUs / 1000), (unsigned)ws.count(), crsf.isLinkUp() ? "UP" : "DOWN");
    maxGapUs = 0;
  }

  unsigned long now = millis();
  if (now - lastUpdate > 50) {
    lastUpdate = now;
    if (ws.count() > 0) ws.textAll(getSerializedData());  // nobody watching -> don't build the JSON at all
  }


  // CPU load with peak-hold: jumps up instantly, then decays back toward the live reading.
  // Runs once per CPU monitor sample (every 10 ms, same as cpucfg.sampleIntervalMs) so the decay
  // speed doesn't depend on how fast loop() spins. With alpha 0.01 the fall-off takes about 1 second.
  static uint32_t lastCpuMs = 0;
  if (millis() - lastCpuMs >= 10) {
    lastCpuMs = millis();
    CpuUsageSample sample;
    if (cpuMonitor.getLastSample(sample)) {
      core0Peak = peakHold(sample.perCore[0], core0Peak);
      core1Peak = peakHold(sample.perCore[1], core1Peak);
      coreAveragePeak = peakHold(sample.average, coreAveragePeak);

      // what the web page shows
      core0 = core0Peak;
      core1 = core1Peak;
      coreA = coreAveragePeak;
    }
  }

  crsf.update();

  unsigned long currentMicros = micros();

  if (currentMicros - previousMicros >= INTERVAL_US) {
    previousMicros += INTERVAL_US;
    for (int i = 0; i < NUM_ESC; i++) escs[i].sendThrottle(throttleCmd[i]);
  }

  updateRecords();  // max/min records, checked on every pass so short peaks aren't missed

  if (millis() - lastHeartbeatMs > 1000) {
    lastHeartbeatMs = millis();
    crsf.sendHeartbeat();

    for (int i = 0; i < NUM_ESC; i++) escs[i].stats();

    crsf_sensor_battery_t crsfBatt = { 0 };

    crsfBatt.voltage = htobe16((uint16_t)(escs[0].voltage() * 10.0));
    crsfBatt.current = htobe16((uint16_t)(escs[1].voltage() * 10.0));
    crsfBatt.capacity = htobe24((uint32_t)(escs[0].temperatureC()));
    crsfBatt.remaining = (uint8_t)(escs[1].temperatureC());
    crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_BATTERY_SENSOR, &crsfBatt, sizeof(crsfBatt));
  }

  if (tuneDirty && millis() - tuneChangedMs > TUNE_SAVE_DELAY_MS) {
    tuneDirty = false;
    saveTune();
  }

  // Re-try EDT enable on ESCs that aren't reporting (hot-plugged after boot)
  static uint32_t lastRedetect = 0;
  if (ESC_REDETECT_MS > 0 && millis() - lastRedetect > ESC_REDETECT_MS) {
    lastRedetect = millis();
    for (int i = 0; i < NUM_ESC; i++)
      if (!escPresent(i)) escs[i].command(DSHOT_CMD_EDT_ENABLE);
  }

  // ---- DEMO: remove these once you drive the array / text yourself ----
  static int demoTick = 0;
  if (++demoTick % 100 == 0) {
    int pos = (demoTick / 100) % ARRAY_SIZE;
    for (int i = 0; i < ARRAY_SIZE; i++) flagArray[i] = (i == pos);
    bulkText = "Demo text, tick " + String(demoTick) + "\nTilt: " + String(RP) + "  Yaw: " + String(Y);
  }
  // ----------------------------------------------------------------------


  vTaskDelay(pdMS_TO_TICKS(1));
}
