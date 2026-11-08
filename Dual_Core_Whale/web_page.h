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
/* iPhone full-screen: a solid strip behind the status bar, so pages that scroll (tuning, guide) slide under it
   instead of showing through the translucent bar */
body::before { content: ""; position: fixed; top: 0; left: 0; right: 0; height: env(safe-area-inset-top); background: var(--bg); z-index: 30; pointer-events: none; }
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
.tv input { width: 4.4em; flex: none; min-width: 0; font: inherit; font-family: ui-monospace, "SF Mono", Menlo, Consolas, monospace; text-align: right;
            color: #fff; background: rgba(255,255,255,.08); border: 1px solid var(--border); border-radius: 6px; padding: 2px 6px; }
.tv input:focus, .tv select:focus { outline: none; border-color: var(--accent); }
.tv select { width: auto; max-width: 8em; flex: none; min-width: 0; font: inherit; color: #fff; background: #2c2c2e; border: 1px solid var(--border);
             border-radius: 6px; padding: 2px 4px; }
.tv select:disabled { opacity: .45; }
#page-guide { max-width: 760px; align-self: stretch; margin: 0 auto; }
#page-guide .card { padding: 10px 14px 12px; margin-bottom: 8px; }
#page-guide h3 { font-size: 1.15rem; margin: 2px 0 6px; }
#page-guide p, #page-guide li { font-size: 1.05rem; line-height: 1.5; color: var(--val); }
#page-guide p + p { margin-top: 6px; }
#page-guide ol, #page-guide ul { padding-left: 1.4em; }
#page-guide li + li { margin-top: 6px; }
#page-guide b { color: #fff; }
#page-guide table { margin-top: 4px; }
#page-guide td, #page-guide th { text-align: left; padding: 4px 6px 4px 0; font-size: 1rem; vertical-align: top; color: var(--val); }
#page-guide th { color: var(--muted); }
#page-guide code { font-family: ui-monospace, Menlo, monospace; font-size: .95em; color: #fff; }

.tv .lab { display: flex; align-items: center; gap: 4px; min-width: 0; flex: 1 1 auto; overflow: hidden; }
.tv.str .lab { flex: none; max-width: 50%; }
.tv .lab label { min-width: 0; flex: 0 1 auto; }
.q { flex: none; font: inherit; font-size: .8rem; line-height: 1; width: 1.35em; height: 1.35em; border-radius: 50%; padding: 0;
     border: 1px solid rgba(10,132,255,.5); background: rgba(10,132,255,.18); color: #64b0ff; }
.mbox.help { text-align: left; width: min(380px, 90vw); }
.mbox.help p { font-weight: 600; margin-bottom: 8px; }
.mbox.help div { color: var(--val); font-size: 1rem; line-height: 1.4; margin-bottom: 12px; white-space: pre-wrap; }
.tv.str input { flex: 1; width: auto; text-align: left; }   /* text variables: use the whole box, left aligned */
/* hide the up/down spinner arrows on number boxes (desktop browsers) */
.tv input::-webkit-outer-spin-button, .tv input::-webkit-inner-spin-button { -webkit-appearance: none; margin: 0; }
.tv input[type=number] { -moz-appearance: textfield; appearance: textfield; }
#c-rec h2, #c-cpu h2, #c-imu h2 { display: flex; justify-content: space-between; align-items: center; }
.reset { font: inherit; font-size: .8rem; text-transform: none; letter-spacing: 0; padding: 0 8px; border-radius: 6px;
         border: 1px solid rgba(255,69,58,.5); background: rgba(255,69,58,.15); color: var(--bad); }
.modal { position: fixed; inset: 0; background: rgba(0,0,0,.65); display: none; align-items: center; justify-content: center; z-index: 10; }
.modal.show { display: flex; }
button:disabled { opacity: .35; pointer-events: none; }
.tv input:disabled { opacity: .45; }
#c-cal { margin-top: 5px; }
.tsec + .tsec { margin-top: 12px; }
.tsec > h3 { font-size: .85rem; font-weight: 600; text-transform: uppercase; letter-spacing: .6px; color: var(--muted); margin: 0 0 5px 4px; }
#c-cal .tfile { margin: 4px 0 0; }
.sides span { margin-left: 6px; color: var(--muted); } .sides span.done { color: var(--good); font-weight: 600; }
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
        white-space: pre-wrap; word-break: break-word; min-height: 2.2em; max-height: 9em; overflow-y: auto;
        -webkit-overflow-scrolling: touch; overscroll-behavior: contain; }
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
  .bulk { max-height: 7em; }
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
  .bulk { max-height: 18em; }
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

    <div class="card" id="c-imu"><h2><span>IMU <span id="i-state"></span></span><button class="nav" id="btn-level">Level</button></h2>
      <table class="num">
        <thead><tr><th></th><th>X</th><th>Y</th><th>Z</th></tr></thead>
        <tbody>
          <tr><td class="l">Gyro</td><td id="i-gx">--</td><td id="i-gy">--</td><td id="i-gz">--</td></tr>
          <tr><td class="l">Accel</td><td id="i-ax">--</td><td id="i-ay">--</td><td id="i-az">--</td></tr>
        </tbody>
      </table>
      <div class="kv"><span>Tilt</span><span class="v" id="i-tilt">--</span></div>
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
    <button class="nav" id="btn-guide">? Tuning guide</button>
    <button class="reset" id="btn-pdef">Load defaults</button>
    <span id="tmsg"></span>
    <input type="file" id="file-param" accept=".param,.txt,text/plain" hidden>
  </div>
  <div id="tgrid"></div>
  <div class="card" id="c-cal"><h2>IMU Calibration</h2>
    <div class="kv"><span>Gyro + accel offsets</span><span class="v" id="cal-offs">--</span></div>
    <div class="kv"><span>6-side accel</span><span class="v" id="cal-acal">--</span></div>
    <div class="kv"><span>Sides captured</span><span class="v sides" id="cal-sides">--</span></div>
    <div class="kv"><span>Status</span><span class="v" id="cal-msg">--</span></div>
    <div class="tfile">
      <button class="nav" id="btn-caloff">Calibrate offsets (flat)</button>
      <button class="nav" id="btn-calside">Capture position</button>
      <button class="nav" id="btn-calgyro">Calibrate gyro scale</button>
      <button class="reset" id="btn-calfac">Factory offsets</button>
      <button class="reset" id="btn-calclr">Clear 6-side</button>
    </div>
  </div>
</div>

<div class="wrap" id="page-guide" hidden>
  <div class="ttop"><button class="nav" id="btn-gback">‹ Tuning</button><b>🐋 Yaw Tuning Guide</b></div>

  <div class="card"><h3>Quick start - first time with the defaults</h3>
    <p>The defaults are worked out for this robot (30 lb, 101 mm wheels on a 345 mm track, 19:1, 1400 KV on 6S, which spins at about 2300 °/s flat out). They're deliberately soft: a safe starting point, not a finished tune.</p>
    <ol>
      <li><b>Load defaults</b> - press it on the tuning page after flashing (Name and WiFi boxes are kept).</li>
      <li><b>Check Yaw Dir before anything else.</b> Wheels off the ground, in a non-Stab mode, steer right: Bulk Text <code>Yaw … °/s</code> must go <b>positive</b>.
        If it goes negative, set Yaw Dir to Reversed. The defaults include P and I, so a wrong sign makes the controller fight you - the 0.6 Out Limit keeps that to 60% steering.</li>
      <li><b>Measure the flat-out spin.</b> Still in a non-Stab mode, full stick, read the same Bulk Text figure. If it isn't about 2300, set
        <b>FF = 900 ÷ that figure</b> (900 being the default Max Rate). The 2300 estimate assumes the 101 mm wheels and 19:1 gearbox set in the code are right.</li>
      <li><b>Tune in the order below.</b> Mainly: raise P until it wobbles, then back off by about a third, and raise Out Limit toward 1.0 once it behaves.</li>
    </ol>
  </div>

  <div class="card"><h3>How it fits together</h3>
    <p>In the Stab modes the steering stick asks for a <b>turn rate</b>. The controller compares it with the gyro and steers to make them match:</p>
    <p><code>stick → Expo → × Max Rate → Max Accel limit → requested rate</code><br>
       <code>steering = FF + FF Accel + P + I + D</code>, capped at <b>Out Limit</b></p>
    <p>Settings early in that chain change how the later ones behave, so tune them in the order below. Each step lists what it affects.</p>
  </div>

  <div class="card"><h3>Before you start</h3>
    <ul>
      <li><b>IMU calibrated and levelled</b> (Calibration card on the tuning page). A bad calibration makes everything below wrong.</li>
      <li>Set <b>Out Limit to 0.5</b> and do the first tests with the <b>wheels off the ground</b>.</li>
      <li>Watch Bulk Text on the main page. In a Stab mode it shows <code>Yaw sp / meas / out / I</code>:
        <b>sp</b> = rate asked for, <b>meas</b> = real rate (°/s), <b>out</b> = steering (−1..1), <b>I</b> = the I term's share.
        Good tuning: meas follows sp, and out doesn't sit at ±1.</li>
      <li>Change one thing at a time, and save a .param when something works.</li>
    </ul>
  </div>

  <div class="card"><h3>Tuning order</h3>
    <ol>
      <li><b>ESC1 / ESC2 Dir</b> - get forward and turning the right way in a non-Stab mode first.
        <br><i>Affects:</i> the sign of every turn, so Yaw Dir must be checked after any change.</li>
      <li><b>Yaw Dir</b> - in a non-Stab mode, steer right: Bulk Text <code>Yaw … °/s</code> must go <b>positive</b>. If negative, set Reversed.
        <br><i>Affects:</i> everything. Wrong = the controller spins the robot up instead of holding it.</li>
      <li><b>Gyro Scale</b> - press <b>Calibrate gyro scale</b> (IMU Calibration card) and twist the robot back and forth by hand; it measures this for you.
        Check: one full turn by hand in about a second should read about 360 in Bulk Text.
        <br><i>Affects:</i> the measured rate, so every setting below.</li>
      <li><b>Max Rate</b> - first find the robot's <b>flat-out spin rate</b>: in a non-Stab mode, spin at full stick and read Bulk Text <code>Yaw … °/s</code>.
        Then pick Max Rate: how fast full stick should turn, at or below that (e.g. 900 when flat-out is 2300).
        <br><i>Affects:</i> FF (next-but-one step) and P, I, D, FF Accel, which all work on the error as a fraction of Max Rate - raising Max Rate makes them gentler.</li>
      <li><b>Gyro LPF Hz</b>, then <b>D LPF Hz</b> - start at 40 / 20. Only lower them if the readings are noisy (meas jumping around while driving straight).
        <br><i>Affects:</i> more filtering = more delay = it wobbles at a lower P and D. Set these before P and D, and re-check P/D if you change them.</li>
      <li><b>FF</b> - set to Max Rate ÷ flat-out spin rate (e.g. 900 ÷ 2300 = 0.4). Check with P/I/D at 0: meas should roughly follow sp on its own.
        Too high = it overshoots every turn (the most common cause of overshoot). Too low = laggy, and P has to do the work.
        <br><i>Affects:</i> how much P has to do. Lower FF = raise P to match.</li>
      <li><b>P</b> - raise in steps of about 0.3 until the robot wobbles or buzzes in a straight line or after a quick turn, then back off by about a third.
        Push it sideways by hand: it should snap back to a stop.
        <br><i>Affects:</i> how much D you need. Limited by the filters (step 5).</li>
      <li><b>D</b> - only if it overshoots when you let go of a turn. Start at 0.01, go up to about 0.05. Often lets you take P a little higher.
        If motors buzz or get warm, D is too high or D LPF too high.
        <br><i>Affects:</i> P (may allow more), noise (D LPF).</li>
      <li><b>I</b> and <b>I Limit</b> - I removes a steady error and turns the robot back to where it was pointing after a hit. Try 2 to 5 with I Limit 0.3.
        Too much I = a slow sway back and forth. If it fights hard while pinned against a wall, lower I Limit.
        <br><i>Affects:</i> I Limit caps it; P and D need to be settled first or I hides their problems.</li>
      <li><b>Max Accel</b> with <b>FF Accel</b> - Max Accel softens stick flicks (stops wheelspin), e.g. 3000 to 10000. Then add FF Accel (0.01 to 0.05) if turns feel slow to start or stop.
        <br><i>Affects:</i> FF Accel only does much with Max Accel set. Both change how hard P is worked during quick turns.</li>
      <li><b>Expo</b> - feel only, for finer aiming around centre. Doesn't change the loop.</li>
      <li><b>Out Limit</b> - raise to 1.0 once it behaves. Below the FF value it caps full-stick turns too.</li>
      <li><b>Standstill: Airmode, Idle Band, I Bleed</b> - last, with the robot sitting still and armed. Airmode on = it fights hits with the sticks centred.
        If it twitches or slowly creeps round while still, raise Idle Band (5 to 15 °/s). If it fights a pin too long, raise I Bleed.
        <br><i>Affects:</i> standstill only (I Bleed: whenever no turn is asked for). Idle Band too big = small knocks at standstill aren't corrected.</li>
      <li><b>Heading Hold: Hold Strength, Hold Max, Hold Band</b> - once the rate loop above is good (heading hold rides on top of it).
        Knock the robot round with the sticks centred: it should turn back to where it was pointing and stop. Raise Hold Strength until it returns briskly;
        if it swings past and back, lower it (or add a little D). Hold Max caps how fast it turns back after a big knock. If it hunts back and forth at rest, raise Hold Band.
        Bulk Text shows <code>hdg</code> = degrees off the held heading.
        <br><i>Affects:</i> needs P set first - a weak rate loop can't follow the hold. I Bleed also lets the held heading slowly give in to a pin.</li>
    </ol>
  </div>

  <div class="card"><h3>If you change this, re-check that</h3>
    <table>
      <tr><th>Changed</th><th>Re-check</th></tr>
      <tr><td>ESC Dir</td><td>Yaw Dir</td></tr>
      <tr><td>IMU calibration / remounting</td><td>Level, Gyro Scale, then all of it</td></tr>
      <tr><td>Gyro Scale</td><td>Max Rate, then P / I / D</td></tr>
      <tr><td>Max Rate</td><td>FF, then P, I, D, FF Accel</td></tr>
      <tr><td>Gyro LPF / D LPF</td><td>P and D (wobble point moves)</td></tr>
      <tr><td>FF</td><td>P</td></tr>
      <tr><td>P</td><td>D, then I</td></tr>
      <tr><td>Max Accel</td><td>FF Accel</td></tr>
      <tr><td>New wheels / weight / battery</td><td>Max Rate, then P</td></tr>
    </table>
  </div>

  <div class="card"><h3>What you see → what to change</h3>
    <table>
      <tr><th>Symptom</th><th>Likely fix</th></tr>
      <tr><td>Spins up and won't stop as soon as Stab is on</td><td>Yaw Dir wrong - switch Stab off and fix it first</td></tr>
      <tr><td>Fast wobble / shaking driving straight</td><td>P too high, or filters too low (too much delay)</td></tr>
      <tr><td>Buzzing motors, warm ESCs</td><td>D too high, or D LPF too high</td></tr>
      <tr><td>Slow sway, overshoots its heading</td><td>I too high</td></tr>
      <tr><td>Overshoots every turn</td><td>FF too high for the Max Rate (FF = Max Rate ÷ flat-out spin)</td></tr>
      <tr><td>Overshoots when you stop a turn</td><td>Add D, or lower Max Accel to what the tyres can grip</td></tr>
      <tr><td>Gets knocked round and stays there</td><td>More I (or I Limit too low)</td></tr>
      <tr><td>Turns feel laggy</td><td>FF too low, Max Accel too low, or add FF Accel</td></tr>
      <tr><td>Full stick turns slower than it used to</td><td>Max Rate too low, or Out Limit below 1</td></tr>
      <tr><td>Twitches or creeps round while sitting still</td><td>Raise Idle Band; if it creeps, redo Calibrate offsets</td></tr>
      <tr><td>Keeps fighting long after being pinned</td><td>Raise I Bleed, or lower I Limit</td></tr>
      <tr><td>Swings past its heading and back after a knock</td><td>Lower Hold Strength, or add a little D</td></tr>
      <tr><td>Slowly curves while driving straight with the stick centred</td><td>Heading hold following gyro drift - redo Calibrate offsets (flat), robot still</td></tr>
      <tr><td>out sits at ±1 a lot</td><td>Max Rate higher than the robot can do - lower it</td></tr>
    </table>
  </div>
</div>

<div class="toast" id="toast"></div>

<div class="modal" id="hmodal">
  <div class="mbox help">
    <p id="h-title"></p>
    <div id="h-text"></div>
    <div class="mbtns"><button id="h-ok">OK</button></div>
  </div>
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
// The boxes are built from the names the firmware sends, so adding tuning variables in the sketch needs no page change.
let tuneCount = 0, tuneHelp = [];
// ? buttons: show what a box does (text comes from tuneHelp[] in the sketch)
$('tgrid').addEventListener('click', e => {
  const b = e.target.closest('.q');
  if (!b) return;
  const i = +b.dataset.i;
  set('h-title', tuneNames[i] || ('Var ' + (i + 1)));
  set('h-text', tuneHelp[i] || '');
  $('hmodal').classList.add('show');
});
$('h-ok').onclick = () => $('hmodal').classList.remove('show');
$('hmodal').onclick = e => { if (e.target === $('hmodal')) $('hmodal').classList.remove('show'); };
function buildTune(n, choice, groups) {
  if (n === tuneCount) return;
  tuneCount = n;
  const box = i => (choice && choice[i])   // dropdown: "value:label|value:label"
      ? `<div class="card tv"><span class="lab"><label id="tn${i}">Var ${i + 1}</label><button class="q" data-i="${i}" hidden>?</button></span><select id="tin${i}">` +
        choice[i].split('|').map(o => { const [v, l] = o.split(':'); return `<option value="${v}">${l}</option>`; }).join('') + `</select></div>`
      : i < NSTR
      ? `<div class="card tv str"><span class="lab"><label id="tn${i}">Var ${i + 1}</label><button class="q" data-i="${i}" hidden>?</button></span><input id="tin${i}" type="text" maxlength="31" placeholder="text" autocomplete="off" autocapitalize="off" spellcheck="false"></div>`
      : `<div class="card tv"><span class="lab"><label id="tn${i}">Var ${i + 1}</label><button class="q" data-i="${i}" hidden>?</button></span><input id="tin${i}" type="number" step="any" placeholder="0"></div>`;
  // sections: "Title:1,2,3|Title:..." from the sketch; anything not listed goes under Other
  const secs = [], used = new Set();
  String(groups || '').split('|').forEach(g => {
    const c = g.indexOf(':');
    if (c < 0) return;
    const idx = g.slice(c + 1).split(',').map(Number).filter(i => i >= 0 && i < n && !used.has(i));
    idx.forEach(i => used.add(i));
    if (idx.length) secs.push([g.slice(0, c).trim(), idx]);
  });
  const rest = [];
  for (let i = 0; i < n; i++) if (!used.has(i)) rest.push(i);
  if (rest.length) secs.push([secs.length ? 'Other' : '', rest]);
  $('tgrid').innerHTML = secs.map(([t, idx]) =>
    `<div class="tsec" data-t="${t}">${t ? `<h3>${t}</h3>` : ''}<div class="tgrid">${idx.map(box).join('')}</div></div>`).join('');
  const hw = document.querySelector('#tgrid .tsec[data-t="Hardware"]');   // IMU calibration sits with the hardware
  if (hw) hw.appendChild($('c-cal'));
  for (let i = 0; i < n; i++) {
    const inp = $('tin' + i);
    inp.addEventListener('change', e => {
      if (!websocket || websocket.readyState !== 1) return;
      if (i < NSTR) { websocket.send(`set,${i},${e.target.value}`); return; }   // text: sent as typed
      const v = parseFloat(e.target.value);
      if (!isNaN(v)) websocket.send(`set,${i},${v}`);
    });
  }
  setLockedInputs();
  fit();
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
function setLockedInputs() { document.querySelectorAll('#tgrid input, #tgrid select').forEach(i => { i.disabled = !!isLocked; }); }
function setLocked(l, rebootLocked) {
  $('btn-reboot').disabled = rebootLocked;                       // reboot: only locked when armed AND the link is up
  if (rebootLocked && $('m-yes').textContent === 'Yes, reboot') $('modal').classList.remove('show');
  if (l === isLocked) return;
  isLocked = l;
  $('btn-pload').disabled = l;
  $('btn-pdef').disabled = l;
  $('btn-level').disabled = l;
  ['btn-caloff','btn-calside','btn-calclr','btn-calfac','btn-calgyro'].forEach(b => { $(b).disabled = l; });   // calibration: locked with the tuning
  setLockedInputs();
  if (l) toast(rebootLocked ? 'Armed - reboot and tuning locked' : 'Armed (link down) - tuning locked');
}

// ---- websocket ----
let websocket, lastMsg = 0, lastDbg = "";
function initWebSocket() {
  websocket = new WebSocket(`ws://${window.location.host}/ws`);
  websocket.onclose = () => setTimeout(initWebSocket, 2000);
  websocket.onmessage = onMessage;
}

function onMessage(event) {
  const d = JSON.parse(event.data);
  if (d.blocked) { toast({ reboot: 'Armed - reboot blocked', level: 'Armed - level locked', cal: 'Armed - calibration locked', imu: 'No IMU - can\'t level' }[d.blocked] || 'Armed - tuning locked'); return; }
  if (d.armed !== undefined) setLocked(!!d.armed, !!d.noreboot);
  lastMsg = Date.now();

  set('v-mode', d.mode);
  set('v-boot', d.boot);
  if (d.dbg !== undefined) lastDbg = d.dbg;   // diagnostics only come in every 10th update: keep showing the last ones
  {  // keep the box's scroll position (replacing the text jumps it back to the top on iPhone), and only touch it if it changed
    const vb = $('v-bulk'), bt = (d.bulk || "") + (lastDbg ? "\n" + lastDbg : "");
    if (vb.textContent !== bt) { const st = vb.scrollTop; vb.textContent = bt; vb.scrollTop = st; }
  }

  if (d.cpu) { set('v-c0', f(d.cpu[0],3)+" %"); set('v-c1', f(d.cpu[1],3)+" %"); set('v-ca', f(d.cpu[2],3)+" %"); }   // 3 decimals

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

  // Avg Volts: from the battery divider when fitted (live even with the motors stopped), else the ESC average.
  // Coloured for 6S battery state. Total Amps: only from ESCs that are reporting.
  // Each keeps its last value, greyed out, when its source goes quiet.
  const av = $('v-avgv'), aa = $('v-avga');
  if (d.avg && d.avg.v !== undefined) {
    const perCell = d.avg.v / CELLS;
    av.textContent = f(d.avg.v, 1) + " V";
    av.className = "v " + (perCell < CELL_LOW ? "bad" : perCell < CELL_WARN ? "warn" : "good");
    lastAvgV = av.textContent;
  } else if (lastAvgV) {
    av.textContent = lastAvgV; av.className = "v stale";
  } else {
    av.textContent = "--"; av.className = "v";
  }
  if (d.avg && d.avg.n > 0) {
    aa.textContent = f(d.avg.a, 1) + " A"; aa.className = "v";
    lastAvgA = aa.textContent;
  } else if (lastAvgA) {
    aa.textContent = lastAvgA; aa.className = "v stale";
  } else {
    aa.textContent = "--"; aa.className = "v";
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

  if (d.names) { tuneNames = d.names; buildTune(d.names.length, d.choice, d.groups); d.names.forEach((n, i) => set('tn' + i, n)); }
  if (d.help) { tuneHelp = d.help; document.querySelectorAll('#tgrid .q').forEach(b => { b.hidden = !tuneHelp[+b.dataset.i]; }); }
  if (d.tune) lastTune = d.tune;
  if (d.tune) d.tune.forEach((v, i) => {
    const inp = $('tin' + i);
    if (!inp || document.activeElement === inp) return;   // don't overwrite while typing
    inp.value = (typeof v === 'string') ? v : (inp.tagName === 'SELECT' ? String(Math.round(v)) : +Number(v).toFixed(4));
  });
  if (d.tune && typeof d.tune[0] === 'string') {   // Name box -> page title and heading
    const nm = d.tune[0].trim() || 'Whale Stats';
    const t = '🐋 ' + nm + ' 🐋';
    if (document.title !== t) { document.title = t; document.querySelector('#page-main h1').textContent = t; }
  }

  if (d.out) d.out.forEach((v, i) => {
    set('out-ch' + (i + 1), v);
    $('ob' + (i + 1)).style.width = Math.max(0, Math.min(100, (v - 1000) / 10)) + "%";
  });

  if (d.imu) {
    const m = d.imu;
    set('i-gx', m.gx); set('i-gy', m.gy); set('i-gz', m.gz);
    set('i-ax', m.ax); set('i-ay', m.ay); set('i-az', m.az);
    // arrows you drive from the sketch: 1 / -1 / 0 (blank)
    // IMU dropout: show LOST in the heading and grey out the numbers until packets come back
    const imuOk = m.ok !== false;
    const lvl = m.lvl !== false;
    set('i-state', !imuOk ? "LOST" : !lvl ? "NOT LEVELLED" : "");
    $('i-state').className = !imuOk ? "bad" : !lvl ? "warn" : "";
    ['i-gx','i-gy','i-gz','i-ax','i-ay','i-az','v-rg','i-tilt'].forEach(id => $(id).classList.toggle('stale', !imuOk));
    set('i-ud', m.ud > 0 ? "⬆️" : m.ud < 0 ? "⬇️" : "--");
    if (m.cal) {
      const c = m.cal;
      set('cal-offs', c.offs ? "saved" : "factory");
      set('cal-acal', c.acal ? "saved" + (c.rms !== undefined ? ` (±${(c.rms * 1000).toFixed(0)} mg)` : "") : "none");
      $('cal-sides').innerHTML = ["X+","X-","Y+","Y-","Z+","Z-"].map((n, b) => `<span class="${(c.sides >> b) & 1 ? 'done' : ''}">${n}</span>`).join("") + ` <span>(${c.n || 0})</span>`;
      set('cal-msg', c.busy ? "working..." : (c.msg || "--"));
    }
    set('i-tilt', m.tilt === undefined ? '--' : f(m.tilt, 0) + ' °');
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
$('btn-level').onclick = () => ask('Sit the robot flat and the right way up, and keep it still. Its current position becomes upright, and up/down flips at 90 degrees from it.', 'Yes, level', () => { wsSend('level'); toast('Levelled'); });
$('btn-pdef').onclick = () => ask('Every number box goes back to its default (yaw tuning, ESC and Yaw Dir, WiFi power). Name, SSID, Password and the other text boxes are kept. Save a .param first if you want to keep the current values.', 'Yes, load defaults', () => { wsSend('defaults'); tmsg('Defaults loaded'); });
$('btn-caloff').onclick = () => ask('Put the IMU board flat with the chip facing up and keep everything perfectly still for a few seconds. The IMU stops reporting while it runs. Clears the 6-side calibration and the Level.', 'Yes, calibrate', () => wsSend('cal_off'));
$('btn-calside').onclick = () => wsSend('cal_side');   // rest the robot on a side, keep still ~1 s
$('btn-calgyro').onclick = () => ask('After pressing Yes, pick the robot up (or lift its wheels) and twist it back and forth quickly - at least a couple of full turns in total, within 10 seconds. It sets Gyro Scale from the IMU\'s own orientation, and saves it.', 'Yes, start', () => wsSend('cal_gyro'));
$('btn-calfac').onclick = () => ask('Forget the saved gyro + accel offsets and go back to the chip\'s own. The IMU restarts. Clears the 6-side calibration and the Level.', 'Yes, factory', () => wsSend('cal_fac'));
$('btn-calclr').onclick = () => ask('Forget the 6-side accel calibration (and the Level, which depends on it).', 'Yes, clear', () => wsSend('cal_clr'));
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
  if ($('page-main').hidden) return;   // only the main page shrinks to fit; tuning and the guide scroll at normal size
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
  const h = location.hash;
  $('page-main').hidden = (h === '#tune' || h === '#guide');
  $('page-tune').hidden = h !== '#tune';
  $('page-guide').hidden = h !== '#guide';
  if (h === '#guide') window.scrollTo(0, 0);
  fit();
}
// Buttons swap the page straight away, then update the address (so Back still works). Waiting for the browser's
// hashchange instead left a frame or two of a half-drawn page on some phones.
function go(h) {
  history.pushState(null, '', h ? '#' + h : location.pathname + location.search);
  route();
}
$('btn-guide').onclick = () => go('guide');
$('btn-gback').onclick = () => go('tune');
$('btn-tune').onclick = () => go('tune');
$('btn-back').onclick = () => go('');
window.addEventListener('hashchange', route);
window.addEventListener('popstate', route);

window.addEventListener('load', () => { initWebSocket(); route(); setTimeout(fit, 400); });
</script>
</body>
</html>
)rawliteral";
