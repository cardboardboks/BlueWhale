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
#include <Preferences.h>    // flash (NVS) storage for the tuning values
#include <esp_system.h>     // esp_reset_reason()
#include <esp_timer.h>      // esp_timer_get_time() for the uptime counter
#include <esp_heap_caps.h>  // heap_caps_get_largest_free_block() for the diagnostics line
#include "esp_wifi.h"
#include "driver/spi_master.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>  // Required for ESP32

// The web dashboard (WiFi AP, web server, WebSocket, JSON) is in web.ino, and the page itself is in web_page.h.

// Functions defined in the other tabs and used before them. The Arduino IDE normally declares these itself, but its
// scanner can miss some, so they're listed here explicitly.
void escTaskStart();                        // drive.ino
void escTick();                             // drive.ino
void escRequestEdt(int i);                  // drive.ino
void escMonitorStart();                     // drive.ino
int escMonitorUpdate(int i, int rawThr);    // drive.ino
void escRebootAll();                        // drive.ino
bool escAllowed(int i);                     // drive.ino
bool escLedAlert(int i);                    // drive.ino
float escLossPct(int i);                    // drive.ino
void escResetStats(int i);                  // drive.ino
uint32_t escAgeUs(int i);                   // drive.ino: telemetry, from the DShot task's copy
float escVolts(int i);                      // drive.ino
float escAmps(int i);                       // drive.ino
float escTempC(int i);                      // drive.ino
float escRpm(int i);                        // drive.ino
void crsfTaskStart();                       // crsf.ino
void crsfSetStrings(bool force);            // crsf.ino
void loadLevel();                           // tuning.ino
void saveLevel();                           // tuning.ino
void loadImuCal();                          // tuning.ino
void saveImuOffsets();                      // tuning.ino
void saveAccCal();                          // tuning.ino
void calAddPoint(const float sum[3], const float sq[3], uint16_t n);  // imu.ino
void applyWifiPower();                      // web.ino
bool wifiDefaultsHeld();                    // web.ino
float yawStab(float stick, float throttle); // yaw_pid.ino
void yawStabReset();                        // yaw_pid.ino
void yawImuSample(float gx, float gy, float gz, float grx, float gry, float grz);  // yaw_pid.ino
void yawGyroCalSample(float gx, float gy, float gz, float qw, float qx, float qy, float qz, uint32_t us);  // yaw_pid.ino
bool yawKeepOnDefaults(int i);              // yaw_pid.ino
String yawStatusLine();                     // yaw_pid.ino
const char *resetReasonShort();             // status.ino
float smoothCpu(float raw, float avg);      // status.ino
String buildBulkText();                     // status.ino
String getSerializedData(bool full);        // web.ino
static float minOutput(float v, float mo);  // mixer.ino
bool escPresent(int i);                     // drive.ino
void batteryUpdate();                       // battery.ino
float battCap();                            // battery.ino
bool battIsCut();                           // battery.ino
bool battLedFlash();                        // battery.ino
String battStatusLine();                    // battery.ino

spi_device_handle_t ledDev;  // SET_TIME_BEFORE_STARTING_SKETCH_MS(5000);

SET_LOOP_TASK_STACK_SIZE(16 * 1024);  // Set main loop stack size to 16 KB

uint32_t last_ota_time = 0;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ---------------- Web dashboard variables ----------------
String modeString = "Idle";     // <- set this from anywhere in your code
String bootState = "Starting";  // <- updated during setup() with setBoot()

// Why the board last restarted, and how many times it has started, kept on the Boot line the whole time it runs
// (e.g. "Running (PANIC #12)"), so a reboot nobody saw can still be diagnosed.
String bootTag = "";
uint32_t bootCount = 0;
void setBoot(const char *stage) {
  bootState = String(stage) + bootTag;
  crsfSetStrings(true);  // so the radio shows the boot stage straight away
}

// Short version for the radio telemetry (15 characters max): "Run PANIC #12" once running, else the stage name
String bootShort() {
  if (!bootState.startsWith("Running")) return bootState.substring(0, bootState.indexOf(" ("));
  return String("Run ") + resetReasonShort() + " #" + bootCount;
}
String bulkText = "";           // <- your own extra text for the Bulk Text box (added under the built-in lines). Set it
                                //    from loop() only - the page reads it while building its update.

#define ARRAY_SIZE 20
volatile bool modeArray[ARRAY_SIZE];  // <- set any slot true to light its green dot

// variables for the raw readings
volatile float core0 = 0;
volatile float core1 = 0;
volatile float coreA = 0;

// smoothed CPU load (rolling average of the 10 ms samples)
volatile float core0Avg = 0;
volatile float core1Avg = 0;
volatile float coreAllAvg = 0;

// CPU smoothing, per 10 ms sample (0 to 1): lower = smoother but slower. 0.01 ≈ 1 s to settle, 0.005 ≈ 2 s,
// 0.003 ≈ 3 s. Don't go much lower: 0.0001 takes minutes to settle (and starts from 0 at boot, so it reads low).
// Same speed up and down, so a single busy moment barely shows; sustained load still does.
const float CPU_SMOOTH_ALPHA = 0.005f;

// loops stats
static uint32_t maxGapUs = 0;
static uint32_t jsonMaxUs = 0;  // longest time spent building + sending a web update this second
uint32_t wsSkipped = 0;    // page updates skipped this second because the last one hadn't gone out yet
uint32_t wsLastBytes = 0;  // size of the last page update
static volatile uint32_t escMaxUs = 0;  // longest DShot task pass this second (written by the DShot task)

// arrows shown in the IMU box
volatile int8_t imuOrient = 0;     // up/down arrow:    1 = up (arrow up),    -1 = down (arrow down),  0 = blank
volatile int8_t imuDirection = 0;  // left/right arrow: 1 = right (arrow right), -1 = left (arrow left), 0 = blank

// ---- Records ----
#define DMP_ACCEL_LSB_PER_G 16384.0f  // accel counts per g (+/-2 g range)
volatile float recMaxG = 0;           // peak acceleration in g with gravity removed (about 0 when sitting still)
volatile float recMaxTemp = 0;        // highest temperature seen on any ESC (deg C)
volatile float recMaxA = 0;           // highest current seen on any single ESC
volatile float recMaxTotalA = 0;      // highest COMBINED current of all reporting ESCs added together
volatile float recMinV = 0;           // lowest voltage seen on any ESC (0 = nothing recorded yet)
volatile float recMaxSpd = 0;         // highest AVERAGE speed (average of the ESCs reporting)

void resetRecords() {
  recMaxG = 0;
  recMaxTemp = 0;
  recMaxA = 0;
  recMaxTotalA = 0;
  recMinV = 0;
  recMaxSpd = 0;
}

// ---- Battery voltage from a resistor divider ----
// The ESCs (AM32) only send voltage while the motors are running, so the battery is also measured here,
// which works with the motors stopped. Wiring: battery + -> 150k -> VBAT_PIN -> 8.2k -> GND, 100nF across the 8.2k.
// The pin can only take ~3.1 V. Max battery = 3.1 x (R_TOP + R_BOT) / R_BOT: 100k/10k -> 34 V (fine for 6S, NOT for 12S).
// Which voltage is used everywhere (battery protection, Avg Volts, Min Volts record, radio telemetry) is the
// "Batt Source" box on the tuning page: ESCs (default) or Divider. Leave it on ESCs while the pin isn't wired.
#define T_BAT_SRC 35                 // tuning box: 0 = ESCs, 1 = Divider
extern volatile float tuneVal[];     // (defined further down, with the other tuning values)
#define VBAT_PIN 3            // must be an ADC1 pin (GPIO 1-10): ADC2 pins don't work while WiFi is on
#define VBAT_R_TOP 100000.0f  // battery + -> pin (ohms)
#define VBAT_R_BOT 10000.0f   // pin -> GND (ohms)
#define VBAT_CAL 1.000f       // trim against a multimeter: CAL = meter reading / page reading
#define VBAT_MIN_V 5.0f       // below this the reading is treated as "no battery / wire off" and the ESC voltages are used
volatile float vbat = 0;      // battery volts (0 = no reading / divider not enabled)

// true when Batt Source is set to the divider
bool vbatEnabled() {
  return tuneVal[T_BAT_SRC] >= 0.5f;
}

// true when the divider is selected and reading a believable battery voltage (otherwise everything uses the ESCs)
bool vbatValid() {
  return vbatEnabled() && vbat >= VBAT_MIN_V;
}

// Sets the ADC up whatever the setting, so Batt Source can be switched to Divider live from the page
void vbatInit() {
  analogReadResolution(12);
  analogSetPinAttenuation(VBAT_PIN, ADC_11db);  // ~0-3.1 V range
}

// Called from loop(); takes a reading every 20 ms
void vbatUpdate() {
  if (!vbatEnabled()) {
    vbat = 0;  // not selected: no reading (and a fresh start if it's switched on later)
    return;
  }
  static uint32_t last = 0;
  if (millis() - last < 20) return;
  last = millis();
  float v = analogReadMilliVolts(VBAT_PIN) / 1000.0f * (VBAT_R_TOP + VBAT_R_BOT) / VBAT_R_BOT * VBAT_CAL;
  vbat = (vbat == 0) ? v : vbat + 0.1f * (v - vbat);  // light smoothing
}

// ---- Whale Tuning ----
// Rename them here. Use tuneVal[n] anywhere in your code; editing a box on the page changes it live.
// Values are saved to flash (see saveTune) and reloaded at boot; 0 is used until something has been saved.
#define NUM_TUNE 38
#define T_BAT_LIMP 37  // Batt Limp %: drive the battery protection always leaves in IMU Off mode (mixer.ino)
#define T_MIN_OUT 31  // Min Output %: smallest non-zero drive output per wheel (mixer.ino)
const char *tuneName[NUM_TUNE] = {
  "Name", "sysID", "SSID", "Password", "Blank",
  "WiFi Power", "P", "I", "D", "Max Rate °/s",
  "Max Accel °/s²", "FF", "FF Accel", "Expo", "Gyro LPF Hz",
  "D LPF Hz", "ESC1 Dir", "ESC2 Dir", "ESC3 Dir", "ESC4 Dir",
  "I Limit", "Out Limit", "Yaw Dir", "Gyro Scale",
  "Airmode", "Idle Band °/s", "I Bleed",
  "Heading Hold", "Hold Strength", "Hold Max °/s", "Hold Band °",
  "Min Output %",
  "Low Volt V", "Crit Volt V", "Batt Limit", "Batt Source",
  "Hold Settle °/s", "Batt Limp %"
};
// The yaw stabilisation boxes (6-15, 20-30, 36) are used by yaw_pid.ino, which names them (T_YAW_...).
// Shown when the ? next to a box is tapped on the Whale Tuning page. Keep each in step with its name above.
const char *tuneHelp[NUM_TUNE] = {
  "The robot's name. Shown as the page title and heading, used as the receiver's device name in the ELRS Lua script, "
  "and as the OTA upload name (letters, numbers and - only there). Empty = the defaults. Takes effect after a reboot.",
  "Text, up to 31 characters. Not used by the code yet - read it anywhere as tuneStr[1].",
  "The WiFi network name this board makes. Empty = Whale. Takes effect after a reboot. "
  "Locked yourself out? Hold channel 5 high (the ESC reboot switch) with the transmitter on while the board powers up: "
  "it starts with the default network (Whale / Whale#1234) for that boot.",
  "The WiFi password, 8 to 63 characters. Empty, or too short = the default (Whale#1234). Takes effect after a reboot. "
  "See SSID for how to get back in if you forget it.",
  "Text, up to 31 characters. Spare - read it anywhere as tuneStr[4].",
  "WiFi transmit power. Low (8.5 dBm) runs cooler and uses less power, and is plenty within a few metres; "
  "High (19.5 dBm) for range. Changes straight away.",
  "P: how hard the controller corrects a difference between the turn rate you asked for and the real one. "
  "1.0 = full steering for a full stick's worth of error. Raise in steps of about 0.3 until it holds firmly; "
  "back off when it wobbles or buzzes. 0 = off.",
  "I: builds up correction over time, so a steady error (a hit, a dragging wheel) gets fully removed and the robot "
  "turns back to where it was pointing. Per second. Try 2 to 5. Too much = slow swaying. 0 = off.",
  "D: damps the turn rate changing, to stop overshoot. Works on the measured rate only, so stick moves don't kick it. "
  "In seconds - small numbers, try 0.01 to 0.05. Too much = buzzing. 0 = off.",
  "Full stick asks for this turn rate, in °/s. Your choice of how fast full stick should turn - "
  "at or below what the robot can really do flat out. All the gains are relative to it, and FF should be "
  "set to match (see FF). 0 = stabilisation off (plain stick steering in Stab mode).",
  "Most the requested turn rate may change per second, in °/s². Smooths sudden stick flicks so "
  "the wheels don't break traction. e.g. 3000 to 10000. 0 = no limit.",
  "Feedforward: steering straight from the stick, before any correction, so P/I/D only fix what's left. "
  "Set it to Max Rate / the robot's flat-out spin rate (spin at full stick in a non-Stab mode and read Bulk Text): "
  "e.g. 900 / 2300 = 0.4. Too high = overshoot on every turn; too low = laggy, P has to do the work.",
  "Acceleration feedforward, in seconds: an extra push while the requested rate is still changing, to make turns start "
  "and stop crisply. Mostly useful with Max Accel set. Try 0.01 to 0.05. 0 = off.",
  "Stick expo, 0 to 1. 0 = linear. Higher = softer around centre for fine aiming, still full rate at full stick.",
  "Low-pass filter on the measured turn rate, in Hz. Removes vibration noise; lower = smoother but adds delay. "
  "Keep under 50 (the IMU gives about 100 readings a second). 0 = off.",
  "Low-pass filter on the D term only, in Hz. D amplifies noise, so it gets its own filter. Typically about half the "
  "Gyro LPF. 0 = off.",
  "Reverses ESC 1's motor in software (forward and back swap; stop stays stop). Locked while armed. "
  "Reversing a motor changes which way the robot turns, so re-check Yaw Dir after changing this.",
  "Reverses ESC 2's motor in software (forward and back swap; stop stays stop). Locked while armed. "
  "Reversing a motor changes which way the robot turns, so re-check Yaw Dir after changing this.",
  "Reverses ESC 3's motor in software. Only does something once ESC 3 is driven (NUM_ESC and the mixer).",
  "Reverses ESC 4's motor in software. Only does something once ESC 4 is driven (NUM_ESC and the mixer).",
  "Most the I term may add, 0 to 1 of full steering. Stops it building up a huge correction while the robot is stuck "
  "against something. e.g. 0.3.",
  "Most the whole controller may steer, 0 to 1. Start at 0.5 while first tuning, so a wrong sign or gain can't spin "
  "it hard; then raise to 1.0.",
  "CHECK FIRST: in a non-Stab mode steer right - the Bulk Text 'Yaw ... °/s' should go positive. If it goes "
  "negative, set Reversed. Wrong sign = the controller spins the robot instead of holding it.",
  "Gyro counts per °/s. The datasheet says 16.4, but readings that come through the IMU's DMP can be scaled "
  "differently (and clone chips vary), so use Calibrate gyro scale in the IMU Calibration card: it measures this for "
  "you. Check: one full turn by hand in about a second should read about 360 in Bulk Text. Load defaults keeps it.",
  "On: keeps correcting with both sticks centred, so a hit at standstill gets fought and the robot turns back to its "
  "heading (like a quad's airmode). Off: hands off whenever both sticks are centred - the robot can be knocked round "
  "freely, and the controller picks up again as soon as a stick moves.",
  "With both sticks centred: while the robot is turning slower than this (°/s) the controller outputs nothing at all, "
  "so gyro noise can't twitch the motors (in 3D mode even a one-count nudge off 1500 is a real motor pulse), and "
  "there's no slow creep from gyro drift. Knocked faster than this, it corrects at full strength. Only at standstill - "
  "driving is unaffected. 0 = off. Typically 5 to 15: bigger = quieter, but small knocks at rest aren't corrected.",
  "While no turn is asked for, the I term (the 'held heading') slowly fades, per second: 0.1 = down to about a third "
  "in 10 s. Stops it fighting a pin forever (stalled motors, no current limit), while a quick hit still gets "
  "corrected. 0 = holds forever. Higher = lets go sooner. With Heading Hold on it also lets the held heading slowly "
  "accept where the robot has been pushed to.",
  "On: when you centre the steering stick, once the turn has finished it remembers which way the robot is facing and "
  "steers back to exactly that heading after a hit or a shove - at standstill and while driving straight. Moving the "
  "stick takes over; centring it again takes the new heading. Off: only the turn rate is controlled.",
  "How hard heading hold steers back, per second: the return turn rate (°/s) per degree off. 4 = 10° off asks for "
  "40°/s back. Higher = snappier but can overshoot and swing; lower = softer. Try 2 to 8.",
  "The fastest heading hold will turn back, in °/s, so a big knock doesn't whip it round. e.g. 120 to 300.",
  "At standstill, heading hold rests once it's within this many degrees of the held heading (with Idle Band, so the "
  "motors stay quiet). Smaller = more exact but more likely to hunt back and forth. e.g. 1 to 3.",
  "Smallest drive output each wheel gets once it's asked to move at all, in %. The bottom of the range often does "
  "nothing useful (the motor can't overcome friction), so any non-zero output starts here and the rest of the stick "
  "is spread over what's left: at 10, just off centre = 10%, half = 55%, full = still 100%. Applies to throttle, "
  "steering and the yaw controller. 0 = off. Find it by creeping a stick off centre with the wheels on the floor: "
  "the % where the robot starts to move. Too high = jumpy around centre, and Stab corrections at rest get punchy.",
  "Low battery, pack volts (smoothed over ~0.5 s). Below this the red LED flash starts (every 5 s, getting quicker "
  "toward Crit) and, in Ramp mode, the drive output starts being capped. Use the voltage under load you're happy to "
  "go down to. 6S examples: 21.0 = 3.5 V/cell. 0 = battery protection off.",
  "Critical battery, pack volts. Ramp mode caps the drive to 0 by here, so it holds the pack just above this. "
  "Below it for a full second, drive is cut until you disarm (ch3 low). IMU Off mode always keeps Batt Limp % so you can "
  "still show it moving. Must be below Low Volt. 6S example: 19.8 = 3.3 V/cell.",
  "Ramp: below Low Volt the drive is capped in step with the voltage, down to 0 at Crit, so pushing harder can't "
  "sag it past Crit. Hard cut: full power until it's been below Crit for a second, then cut. Either way the cut "
  "stays until you disarm. Voltage is from the battery divider if fitted, otherwise the ESCs (which only report "
  "while the motors turn) - see Batt Source.",
  "Where the battery voltage comes from, for everything that uses it: the battery protection, Avg Volts, the Min "
  "Volts record and the radio telemetry. ESCs: the average the ESCs report - no extra wiring, but AM32 only sends it "
  "while the motors turn, so it freezes at rest. Divider: the resistor divider on VBAT_PIN (see the main sketch for "
  "wiring and VBAT_CAL) - live even at rest; if it reads under 5 V (wire off) the ESCs are used instead. Leave on "
  "ESCs unless the divider is wired: an unconnected pin reads junk. Changes straight away.",
  "After you let go of the steering stick, heading hold waits until the robot is turning slower than this (°/s), "
  "then holds the heading wherever it has come to rest - so it doesn't snap back to where you let go. Knocks with "
  "no stick input still get pulled back to that heading. Lower = waits for a more complete stop. If it still snaps "
  "back a little after a fast turn, lower it; if it's slow to start holding, raise it. 0 = lock as soon as the "
  "requested turn reaches zero (the old behaviour).",
  "In IMU Off mode (ch4 high) the battery protection never caps the drive below this %, even after a cut - so you "
  "can always show the robot moving. Only affects IMU Off mode; the other modes are capped and cut as normal. "
  "0 = IMU Off mode is capped and cut like the rest."
};
volatile float tuneVal[NUM_TUNE] = { 0 };

// Sections on the Whale Tuning page: "Title:box,box,...|Title:...". Boxes appear in the order listed (the yaw ones in
// tuning order). Any box not listed goes in an "Other" section at the end.
const char *tuneGroups =
  "Yaw Tuning:9,14,15,11,6,8,7,20,10,12,13,21,24,25,26,27,28,29,30,36"
  "|Battery:35,32,33,34,37"
  "|Hardware:16,17,18,19,31,22,23"
  "|Software:0,1,2,3,5,4";

// Boxes shown as a dropdown instead of a number: "value:label|value:label". nullptr = a normal number box.
const char *tuneChoice[NUM_TUNE] = {
  nullptr, nullptr, nullptr, nullptr, nullptr,
  "1:Low|2:Medium|3:High", nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, "0:Normal|1:Reversed", "0:Normal|1:Reversed", "0:Normal|1:Reversed", "0:Normal|1:Reversed",
  nullptr, nullptr, "1:Normal|-1:Reversed", nullptr,
  "1:On|0:Off", nullptr, nullptr,
  "1:On|0:Off", nullptr, nullptr, nullptr,
  nullptr,
  nullptr, nullptr, "1:Ramp|0:Hard cut", "0:ESCs|1:Divider",
  nullptr, nullptr
};
#define T_WIFI_POWER 5   // 1 low, 2 medium, 3 high (0 / anything else = high)
#define T_ESC_DIR 16     // 16..19: ESC 1..4 direction, 0 normal, 1 reversed

// Starting values, used for any box that has never been saved and by the page's "Load defaults" button.
// Worked out for this robot: 30 lb, 2 wheels 101 mm on a 345 mm track, 19:1, 1400 KV on 6S (~2300 °/s flat-out
// spin), VytaFlex 20 on steel (grip limits yaw acceleration to roughly 2000-3000 °/s²). Deliberately soft:
// a starting point for tuning, not a finished tune. CHECK YAW DIR BEFORE USING STAB MODE.
const float tuneDefault[NUM_TUNE] = {
  0, 0, 0, 0, 0,
  3,        // WiFi power: high
  1.5f,     // P
  3.0f,     // I
  0.02f,    // D
  900.0f,   // Max Rate: 2.5 turns/s, well under the ~2300 the robot can do
  2500.0f,  // Max Accel: about what the tyres can deliver without spinning
  0.4f,     // FF: Max Rate / the robot's top spin rate (900 / ~2300)
  0.02f,    // FF Accel
  0.3f,     // Expo
  40.0f,    // Gyro LPF Hz
  20.0f,    // D LPF Hz
  0, 0, 0, 0,  // ESC dirs
  0.3f,     // I Limit
  0.6f,     // Out Limit: room for corrections on top of FF, but caps a wrong-sign runaway
  1.0f,     // Yaw Dir
  16.4f,    // Gyro Scale
  1.0f,     // Airmode: on
  8.0f,     // Idle Band °/s: comfortably above gyro noise
  0.1f,     // I Bleed: very slow (about 10 s to let go)
  1.0f,     // Heading Hold: on
  4.0f,     // Hold Strength: 10° off -> 40°/s back
  180.0f,   // Hold Max °/s
  2.0f,     // Hold Band °
  0.0f,     // Min Output %: off
  21.0f,    // Low Volt V: 3.5 V/cell on 6S
  19.8f,    // Crit Volt V: 3.3 V/cell on 6S
  1.0f,     // Batt Limit: Ramp
  0.0f,     // Batt Source: ESCs (as before - set Divider once it's wired)
  30.0f,    // Hold Settle °/s
  10.0f     // Batt Limp %
};


// The FIRST NUM_TUNE_STR tuning variables hold text instead of a number. Read them anywhere as plain C strings,
// e.g. tuneStr[0] (up to 31 characters). They are saved to flash and in the .param file like the numbers.
// (tuneVal[0..4] are unused. Fixed char arrays, not String, because the web server task writes them while loop() reads them.)
#define NUM_TUNE_STR 5
#define TUNE_STR_LEN 32
char tuneStr[NUM_TUNE_STR][TUNE_STR_LEN] = { "", "", "", "", "" };

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

#define NUM_OUT 8
// 8 output channels - write 1000-2000
volatile int outChannel[NUM_OUT] = { 1500 };

// rx intput channels - read 1000-2000
int txchannel[10] = { 1500 };

// Arm switch = receiver channel 3. While it is HIGH, the web page can't reboot the board or change tuning values.
#define ARM_CHANNEL 3
#define ARM_HIGH_US 1500  // above this counts as high

#define MAX_ESC 4                                             // how many ESC slots exist (arrays below always have this many entries)
#define NUM_ESC 2                                             // how many ESCs are actually used: 1 to MAX_ESC. Change only this number.
volatile float throttleCmd[MAX_ESC] = { 0.00f, 0.00f, 0.00f, 0.00f };  // DShot value each ESC gets (0 = stop, 48-2047), sent by the DShot task

// An ESC that isn't giving telemetry gets re-sent "enable EDT" this often (ms),
// so one plugged in after boot can start reporting. Set to 0 to turn this off.
// #define ESC_REDETECT_MS 10

// An ESC with no valid telemetry for this long (microseconds) is shown as not reporting
#define ESC_STALE_US 1000000

// ---- ESC reconnect ----
// After boot, an ESC that stops answering (power glitch, plug pulled, ESC reset) is marked LOST and held at zero
// throttle. When it answers again it is kept at zero for ESC_REARM_MS so AM32 re-arms, then extended telemetry (EDT)
// is re-enabled and its link stats restart from 0. Throttle is then ramped from stopped up to whatever the sticks
// are currently commanding over ESC_RAMP_MS, so the ESC picks up smoothly instead of jumping straight to full command.
#define ESC_LOST_US 250000  // no telemetry for this long (microseconds) = ESC lost
#define ESC_REARM_MS 1000   // zero throttle for this long after it comes back, before re-enabling EDT
#define ESC_EDT_TRIES 3     // EDT enable is sent this many times...
#define ESC_EDT_GAP_MS 300  // ...this far apart, in case the first one arrives before the ESC is ready
#define ESC_RAMP_MS 300     // then throttle ramps from stopped to the live command over this long (0 = no ramp)

// Manual ESC reboot: hold channel 5 above ESC_REBOOT_US for ESC_REBOOT_HOLD_MS and every ESC is reset, whatever
// state it's in. DShot has no reboot command, so the signal is cut for ESC_CUT_MS (long enough for AM32 to
// decide the signal is gone and disarm), then each ESC goes through the normal reconnect: re-arm, EDT, ramp.
#define ESC_REBOOT_CH 5
#define ESC_REBOOT_US 1800
#define ESC_REBOOT_HOLD_MS 1000
#define ESC_CUT_MS 0  // 0 = don't cut the signal, just re-arm + re-enable EDT. A 1000 ms cut left AM32 not
                      // responding for minutes on the bench - only try a cut again if testing shows it's needed.
// The manual reboot is blind: it runs on timers only and hands control back at the end even if telemetry never
// returns. After that, the automatic lost detection only re-engages once the ESC has been heard from again.

// LEDs that belong to each ESC: they blink orange over the normal pattern while that ESC is lost / re-arming.
// Assumes LEDs 0-5 are on the ESC1 (left) side and 6-11 on the ESC2 (right) side. Set a count to 0 for no LEDs.
const int ESC_LED_FIRST[MAX_ESC] = { 0, 6, 0, 0 };
const int ESC_LED_COUNT[MAX_ESC] = { 6, 6, 0, 0 };
#define ESC_LED_COLOR 0xFF5000  // orange
#define ESC_LED_BLINK_MS 125    // on for this long, off for this long

enum : uint8_t { ESC_OK = 0,
                 ESC_LOST,
                 ESC_REARM,
                 ESC_RAMP,
                 ESC_CUT };
volatile uint8_t escState[MAX_ESC] = { 0 };   // ESC_OK / ESC_LOST / ESC_REARM / ESC_RAMP / ESC_CUT
volatile uint32_t escStateMs[MAX_ESC] = { 0 };  // millis() when the current state started (read by the DShot task)
uint8_t escEdtTries[MAX_ESC] = { 0 };         // EDT enables sent during this re-arm
volatile uint32_t escDrops[MAX_ESC] = { 0 };  // times this ESC was lost since boot
volatile uint32_t escReinits[MAX_ESC] = { 0 };  // times it was successfully re-initialised
volatile uint32_t escBootAssists[MAX_ESC] = { 0 };  // times the line was held low to get a re-powered ESC out of its bootloader

// Boot assist (DShot task, drive.ino). AM32's bootloader only starts the motor firmware if it sees the signal line
// LOW while it boots; DShot idles the line HIGH, so an ESC re-powered while the robot runs sat in its bootloader for
// ~20 s. An ESC lost for ESC_BOOT_AFTER_MS gets its line held low for ESC_BOOT_HOLD_MS (just that ESC - the other keeps
// running), then DShot restarts; if it still isn't answering ESC_BOOT_RETRY_MS later, it's tried again.
#define ESC_BOOT_AFTER_MS 300
#define ESC_BOOT_HOLD_MS 2500
#define ESC_BOOT_RETRY_MS 4000
// The ESC library's own reply counters (AlfredoDShot::stats(), totals since boot), copied out by escTick():
// replies decoded OK (eRPM or EDT), no reply at all, and replies that came back garbled (framing / GCR / CRC).
volatile uint32_t escStOk[MAX_ESC] = { 0 }, escStNoRep[MAX_ESC] = { 0 }, escStFrame[MAX_ESC] = { 0 };
volatile uint32_t escStGcr[MAX_ESC] = { 0 }, escStCrc[MAX_ESC] = { 0 };
volatile uint16_t escEcho[MAX_ESC] = { 0 };      // our own frame read back off the wire: 31 = wiring good
uint32_t escBaseOk[MAX_ESC] = { 0 };             // totals at the last (re)connect, for the loss % since then
uint32_t escBaseTried[MAX_ESC] = { 0 };
uint32_t escSecOk[MAX_ESC] = { 0 }, escSecNr[MAX_ESC] = { 0 }, escSecFr[MAX_ESC] = { 0 };  // the last second's counts
uint32_t escSecGcr[MAX_ESC] = { 0 }, escSecCrc[MAX_ESC] = { 0 };
bool escMonitorOn = false;                         // switched on in setup() once the ESCs have booted
bool escBlind[MAX_ESC] = { false };                // true during a manual (channel 5) reboot: ignore telemetry
bool escHeard[MAX_ESC] = { false };                // telemetry seen since the ESC last went to OK

volatile bool otaInProgress = false;
unsigned long lastUpdate = 0;
#define WEB_UPDATE_MS 50  // milliseconds between web page updates (10 = 100 per second). Higher = less CPU, jerkier page

// ---------------- Hardware ----------------
// ESC signal pins, in web-page order (ESC 1..4).
// ESC 1 = GPIO 2 and ESC 2 = GPIO 1, matching your original (swapped) begin() calls.
// ESC 3 and ESC 4 pins are a guess - change them to wherever you wire them.
const int ESC_PINS[MAX_ESC] = { 2, 1, 16, 15 };
const uint8_t MOTOR_POLES = 14;

AlfredoDShot escs[NUM_ESC];

ESPCpuMonitor cpuMonitor;
bool cpuMonitorOk = false;  // only true once init() succeeded; getLastSample() on an un-started monitor would crash

#define PIN_RX 5
#define PIN_TX 4

// Must match DATA_TYPE / STATUS_TYPE / VERSION in /SCRIPTS/WHALE/whale_decode.lua
#define CRSF_TYPE_VI_DATA 0x42
#define CRSF_TYPE_VI_STATUS 0x43
#define VI_PROTO_VERSION 3
#define VI_MAX_STR 15

#define SCL_PIN 6
#define SDA_PIN 7
#define INT_PIN 8

#define LED_PIN 11
#define NUM_LEDS 12
#define SPI_FREQ 3200000

#define RESET_BYTES 64
static uint8_t spiBuf[NUM_LEDS * 12 + RESET_BYTES];

HardwareSerial crsfSerial(1);
AlfredoCRSF crsf;

uint32_t lastHeartbeatMs = 0;
uint32_t lastPrintMs = 0;

int roll = 0;
int pitch = 0;

int roll_flip = 20;

// Up/down detection from the IMU (sets imuOrient to 1 = up, -1 = down)
// "Up" direction in the sensor's own axes, measured by pressing Level on the web page with the robot sitting flat
// and the right way up. Saved to flash and reloaded at boot. Works for any IMU mounting angle.
// Until it has been levelled, up/down is treated as if there were no IMU: always upright.
// Up/down source: 1 = the accelerometer (low-pass filtered: measures gravity directly, unaffected by any DMP
// gyro-scale error), 0 = the DMP's fused orientation. Both are always worked out and shown in Bulk Text for comparison.
#define IMU_UP_FROM_ACCEL 1
#define IMU_ACC_LP 0.1f              // accel filter per packet (~100 Hz): 0.1 = ~0.1 s time constant. Lower = steadier but slower
float imuRef[3] = { 0, 0, 0 };       // upright direction as seen by the DMP gravity vector
float imuRefA[3] = { 0, 0, 0 };      // upright direction as seen by the filtered accelerometer
volatile bool imuLevelled = false;   // true once a valid upright direction has been loaded or measured
volatile float imuTiltDmpDeg = 0;    // tilt from the DMP (diagnostics)
volatile float imuTiltAccDeg = 0;    // tilt from the accelerometer (diagnostics)

// ---- IMU calibration (buttons on the Whale Tuning page) ----
// 1) Offsets: the library's flat calibration for the gyro and accel offset registers. IMU board flat, chip facing up,
//    perfectly still. Saved to flash and written to the chip at every start; with nothing saved the chip's own
//    factory offsets are used. This is what the DMP benefits from.
// 2) 6-side accel: rest the robot still in different positions - its 6 sides is ideal, any order, the IMU needn't be
//    square to the robot - and capture each. Once every axis has been both up and down, a sphere fit works out an
//    offset and scale per accel axis, applied in software to the accel used for up/down and Max G (not the DMP).
// Either calibration changes how the IMU reads, so the saved Level is cleared and must be done again afterwards.
int16_t imuOffs[6] = { 0 };                    // accel X, Y, Z then gyro X, Y, Z offset registers
volatile bool imuOffsValid = false;            // saved offsets loaded / measured
float accOff[3] = { 0, 0, 0 };                 // 6-side accel: zero point per axis (counts)
float accScale[3] = { DMP_ACCEL_LSB_PER_G, DMP_ACCEL_LSB_PER_G, DMP_ACCEL_LSB_PER_G };  // counts per g per axis
volatile bool accCalValid = false;             // 6-side calibration loaded / measured
#define CAL_MAX_POINTS 12                      // most positions kept for the fit
float calPts[CAL_MAX_POINTS][3];               // captured averages, in g
volatile uint8_t calN = 0;                     // how many captured
volatile uint8_t sideMask = 0;                 // which way up each capture was (X+ X- Y+ Y- Z+ Z-); all 6 -> fit
volatile float accCalRms = 0;                  // fit error of the last 6-side calibration, in g
volatile bool imuCalOffReq = false;            // web -> IMU task requests
volatile bool imuCalSideReq = false;
volatile bool imuCalClearReq = false;
volatile bool imuCalFactoryReq = false;
volatile bool imuCalGyroReq = false;          // gyro scale calibration (yaw_pid.ino)
volatile bool imuCalFactoryMsg = false;
volatile bool imuOffSave = false;              // IMU task -> loop(): write to flash
volatile bool accCalSave = false;
const char *volatile imuCalMsg = "";           // status text for the tuning page
#define CAL_SIDE_SAMPLES 100                   // packets averaged per side (~1 s)
#define CAL_MAX_NOISE_G 0.03f                  // a capture is rejected if the reading wobbled more than this (moving)
#define CAL_MIN_SPACING_G 0.5f                 // ...or if it's within this of an earlier capture (about 30 degrees)
#define CAL_MAX_RMS_G 0.03f                    // fit is rejected if the captures disagree by more than this
volatile bool imuLevelReq = false;   // set by the web page; the IMU task takes the reading
volatile bool imuLevelSave = false;  // set by the IMU task; loop() writes it to flash
volatile float imuTiltDeg = 0;       // tilt from upright in degrees (0 = upright, 90 = on its side, 180 = upside down)
// Dead band around the 90 degree flip, in degrees: the robot must tilt this far past sideways before it changes
// between up and down (5 -> becomes "down" at 95 degrees of tilt, "up" again at 85). 0 = no dead band.
#define FLIP_DEADBAND 5
float imuOrientHyst = sinf(FLIP_DEADBAND * (float)M_PI / 180.0f);  // the same dead band as a fraction of 1 g, used in imu.ino

const int RPnumReadings = 10;
int RPreadings[RPnumReadings];
int RPreadIndex = 0;
int RPtotal = 0;

volatile int RP = 0;


// Raw IMU values for the web page (written on core 0, read on core 1)
volatile int16_t imuGx = 0, imuGy = 0, imuGz = 0;
volatile int16_t imuAx = 0, imuAy = 0, imuAz = 0;

//  esc varibles
int throttle1 = 0;
int throttle2 = 0;
int drive1_pwm = 0;
int drive2_pwm = 0;

int gearbox_ratio[MAX_ESC] = { 19, 19, 19, 19 };
int wheel_diamiter[MAX_ESC] = { 101, 101, 101, 101 };  // in mm

// DShot sending (drive.ino): its own task on core 1, woken by a 1000 µs timer - 1000 frames/s per ESC.
#define ESC_TICK_US 1000  // time between DShot frames, µs
// drive_escs() stamps this when it updates throttleCmd
volatile uint32_t escCmdStampMs = 0;
volatile uint32_t escPeriodMinUs = 0xFFFFFFFFUL;  // shortest / longest gap between DShot ticks this second (diag line)
volatile uint32_t escPeriodMaxUs = 0;
float escShowTickMs = 0, escShowMinMs = 0, escShowMaxMs = 0;  // last second's DShot timing, for the Bulk Text ESC line

// ESC telemetry, copied out of the ESC library by the DShot task after every send. Everything else reads these
// (via escAgeUs() / escVolts() / ... in drive.ino), never the library directly: the task may be updating the
// library's values on the other core at that moment, and a 64-bit time read half-old/half-new looked like a lost ESC.
volatile uint32_t escRxUs[MAX_ESC] = { 0 };      // micros() of the last good reply
volatile bool escRxEver[MAX_ESC] = { false };    // a reply has ever been received
volatile float escSnapV[MAX_ESC] = { NAN, NAN, NAN, NAN };
volatile float escSnapA[MAX_ESC] = { 0 };
volatile float escSnapT[MAX_ESC] = { NAN, NAN, NAN, NAN };
volatile float escSnapRpm[MAX_ESC] = { 0 };

// pre arm safe varibles
bool safe_switch = 0;
bool safe_sticks = 0;
bool safe_link = 0;
bool safed = 0;
bool IMU_safe = 1;

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

// Median of the last 3 samples: throws away a single bad reading without smoothing real movement much
struct Med3 {
  int16_t v[3] = { 0, 0, 0 };
  bool primed = false;
  int16_t add(int16_t x) {
    if (!primed) {  // first sample: fill the window with it, otherwise the 0s it starts with look like a zero reading
      v[0] = v[1] = v[2] = x;
      primed = true;
    }
    v[0] = v[1];
    v[1] = v[2];
    v[2] = x;
    int16_t a = v[0], b = v[1], c = v[2];
    return max(min(a, b), min(max(a, b), c));
  }
};

volatile uint32_t dbgMaxLoopGapUs = 0;  // longest pause between loop() passes during the last second (diagnostics line)
char dbgLine[360] = "";                 // diagnostics text shown on the page; rebuilt once a second in loop()
volatile uint32_t imuReadMaxUs = 0;     // longest single IMU FIFO read this second (it can busy-wait after an overflow)
volatile uint32_t imuBadPackets = 0;    // corrupted IMU packets thrown away (printed in the diag line)
volatile uint32_t imuBadOnLate = 0;     // ...of which, how many were read late (took longer than IMU_LATE_US)
volatile uint32_t imuLateReads = 0;     // reads that took longer than one packet interval
volatile float imuLastBadQn = 0;        // quaternion length of the most recent rejected packet (1.0 = good)

// I2C bus speed for the IMU. A 42-byte FIFO read takes ~6 ms at 100 kHz (the DMP makes a packet every ~10 ms),
// so the IMU task spends over half its time on the bus. 400000 cuts that to ~1.6 ms, but needs short wires and
// real pull-up resistors (4.7k on SDA and SCL) - on a breadboard it may make bad packets worse. Try it as an experiment.
#define I2C_CLOCK_HZ 400000
volatile uint32_t imuCountMaxUs = 0;  // longest FIFO-count read (2 bytes, should take ~0.2 ms) this second
volatile uint16_t imuFifoMax = 0;     // most bytes seen waiting in the IMU FIFO this second (one packet = 42)
volatile uint32_t imuMultiPkt = 0;    // times 2 or more packets were waiting (the read fell behind)
#define IMU_CORE 0                    // which core runs the IMU task (0 = with WiFi, 1 = with the main loop). Try 1 as an experiment.
#define IMU_LATE_US 10000             // a read slower than this (10 ms, one packet interval) counts as "late"

// ---- Robust IMU bus access ----
// The library's dmpGetCurrentFIFOPacket() has loops that wait forever if the sensor stops answering (a jumper wire
// comes loose, power glitches), and it can't tell "no data" from "bus error". So the IMU task reads the FIFO itself,
// checks every transfer, and never waits in a loop. A bus that keeps failing is un-stuck and the IMU is restarted.
#define IMU_I2C_ADDR 0x68             // MPU6050 with AD0 low (what the library uses by default)
#define IMU_PKT 42                    // DMP packet size in bytes
#define IMU_FAIL_LIMIT 20             // this many failed transfers in a row -> reset the bus and restart the IMU
#define IMU_I2C_TIMEOUT_MS 20         // give up on a single I2C transfer after this long
#define IMU_WARMUP 150                // ignore the Max G record for this many good packets (~1.5 s) after start / restart
volatile uint32_t imuI2cErrors = 0;   // failed I2C transfers
volatile uint32_t imuRecoveries = 0;  // times the bus was reset and the IMU restarted
volatile uint32_t imuOverflows = 0;   // times the IMU FIFO was found overflowing and was cleared
volatile uint16_t imuWarm = 0;

// IMU dropout: if no good packet has arrived for IMU_DROPOUT_MS (wire out, bus stuck, sensor restarting, no IMU
// fitted) the IMU counts as lost. Use imuAlive() anywhere you shouldn't trust RP / yaw / the IMU values.
#define IMU_DROPOUT_MS 300
volatile uint32_t imuLastGoodMs = 0;  // millis() when the last good IMU packet was processed (0 = none yet)
bool imuAlive() {
  uint32_t t = imuLastGoodMs;
  return t != 0 && (uint32_t)(millis() - t) < IMU_DROPOUT_MS;
}

// ---- Upside-down detection (drives the mixer's direction flip and the disarmed LEDs) ----
// Uses the same up/down as the web page's Orientation arrow (imuOrient, worked out from the DMP gravity vector in
// imu.ino): it flips at a true 90 degree tilt from the levelled upright, with the dead band set by FLIP_DEADBAND.
// No IMU at boot, IMU lost while running, or never levelled: the robot is treated as upright.
bool robotDown() {
  if (!IMU_safe || !imuAlive() || !imuLevelled) return false;  // IMU missing / dropped out / not levelled -> upright
  return imuOrient < 0;                        // 0 (not decided yet) also counts as upright
}

// true while the arm switch (channel 3) is high. Checked on the channel value alone, like mixer.ino does, so a
// stale high reading after the link drops still counts as armed (blocking is the safe side).
bool armSwitchHigh() {
  return txchannel[ARM_CHANNEL] > ARM_HIGH_US;
}

// A reboot is only blocked while armed AND the link is up. With the link down (transmitter off / out of range)
// the switch reading is stale, and rebooting is allowed so you can recover the board. Tuning stays locked by armSwitchHigh().
bool rebootBlocked() {
  return armSwitchHigh() && crsf.isLinkUp();
}

void setup() {
  Serial.begin(115200);
  Serial.printf("Reset reason: %s\n", resetReasonShort());
  prefs.begin("whale", false);  // boot counter, so reboots that nobody saw show up
  bootCount = prefs.getUInt("boots", 0) + 1;
  prefs.putUInt("boots", bootCount);
  prefs.end();
  bootTag = String(" (") + resetReasonShort() + " #" + bootCount + ")";
  Serial.printf("Boot #%u\n", (unsigned)bootCount);

  // CPU monitor first, while nothing else is running: it calibrates "idle" from its first samples, so starting it
  // after the IMU / LED / WiFi / CRSF / DShot tasks made it count their load as idle - and later read 0%.
  setBoot("CPU monitor init");
  CpuMonitorConfig cpucfg;
  cpucfg.sampleIntervalMs = 10;
  cpucfg.calibrationSamples = 5;
  cpucfg.enablePerCore = true;
  cpucfg.enableTemperature = false;
  cpuMonitorOk = cpuMonitor.init(cpucfg);
  if (!cpuMonitorOk) {
    Serial.println("CPU monitor init failed");  // carry on: the CPU figures just stay at 0
  }
  delay(cpucfg.sampleIntervalMs * (cpucfg.calibrationSamples + 3));  // let it take its calibration samples undisturbed

  Serial.println("ELRS 4.x receiver starting");

  setBoot("Receiver init");
  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);
  if (!crsfSerial) {
    Serial.println("Invalid crsfSerial configuration");
  }

  crsf.begin(crsfSerial);
  crsf.setDeviceName("AlfredoCRSF");
  crsfTaskStart();  // receiver + telemetry now run in their own task (crsf.ino)

  ledInit();

  setBoot("GPIO init");
  led(1, 1);

  gpio_config_t cfg = {};
  for (int i = 0; i < NUM_ESC; i++) {
    cfg.pin_bit_mask = 1ULL << ESC_PINS[i];
    cfg.mode = GPIO_MODE_OUTPUT_OD;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    gpio_set_level((gpio_num_t)ESC_PINS[i], 0);
  }

  setBoot("Waiting 2s");
  delay(4000);  // (delay() lets the core idle; an empty loop here kept it at 100%)

  pinMode(INT_PIN, INPUT_PULLDOWN);

  vbatInit();  // battery voltage divider

  Serial.printf("Free stack: %d\n", uxTaskGetStackHighWaterMark(NULL));

  loadTune();   // restore saved tuning values first
  if (tuneStr[0][0]) crsf.setDeviceName(tuneStr[0]);  // the Name box, shown in the ELRS Lua script
  loadLevel();  // and the IMU's levelled "upright" direction (before the IMU task starts)
  loadImuCal();  // and the IMU calibrations (offsets are written to the chip in imuBegin())

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  Wire.setTimeOut(IMU_I2C_TIMEOUT_MS);

  for (int i = 0; i < RPnumReadings; i++) RPreadings[i] = 0;
  for (int i = 0; i < ARRAY_SIZE; i++) modeArray[i] = false;

  setBoot("IMU init");
  // The I2C bus on a breadboard can glitch, so try the IMU several times (restarting the bus between
  // attempts) before giving up. dmpInitialize() returns 0 on success.
  uint8_t devStatus = 1;
  bool mpuFound = false;
  for (int attempt = 1; attempt <= 5 && devStatus != 0; attempt++) {
    devStatus = imuBegin(mpuFound);  // also sets the calibration offsets
    if (devStatus != 0) {
      Serial.printf("IMU init attempt %d failed (found=%d, dmp code=%d)\n", attempt, (int)mpuFound, (int)devStatus);
      Wire.end();
      delay(100);
      Wire.begin(SDA_PIN, SCL_PIN);
      Wire.setClock(I2C_CLOCK_HZ);
      Wire.setTimeOut(IMU_I2C_TIMEOUT_MS);
    }
  }
  if (!mpuFound) {
    Serial.println("MPU6050 not found - check wiring/address");
    IMU_safe = 0;
    led(16, 1);
  }

  if (devStatus == 0) {
    mpu.setDMPEnabled(true);

    // Priorities: IMU highest, LEDs next. The WiFi driver tasks (~23) are built into the core and can
    // not be lowered, so the IMU sits above them and the LEDs level with them; AsyncTCP/web run below.
    xTaskCreatePinnedToCore(imuTask, "imu_0", 4096, nullptr, configMAX_PRIORITIES - 1, &imuTaskHandle, IMU_CORE);

    pinMode(INT_PIN, INPUT_PULLDOWN);  // keep the pull-down: a floating INT wire picks up noise and floods the CPU with interrupts
    attachInterrupt(digitalPinToInterrupt(INT_PIN), dmpDataReady, RISING);

    dmpReady = true;
    Serial.println("DMP ready");
  } else {
    Serial.printf("DMP init failed (code %d)\n", devStatus);
    IMU_safe = 0;
  }

  // The LED task runs with or without an IMU, so the status LEDs (e.g. the purple "no IMU" blink) still work.
  xTaskCreatePinnedToCore(ledTask, "ledTask", 4096, NULL, configMAX_PRIORITIES - 2, NULL, 0);

  setBoot("WiFi AP start");
  webBegin();  // WiFi access point + web server + WebSocket (web.ino)

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

  {  // the Name box as the OTA upload name: letters, numbers and '-' only
    static char host[TUNE_STR_LEN];
    int n = 0;
    for (const char *p = tuneStr[0]; *p && n < TUNE_STR_LEN - 1; p++) {
      char c = *p;
      if (isalnum((unsigned char)c)) host[n++] = tolower((unsigned char)c);
      else if ((c == ' ' || c == '-' || c == '_') && n > 0 && host[n - 1] != '-') host[n++] = '-';
    }
    while (n > 0 && host[n - 1] == '-') n--;
    host[n] = 0;
    if (n > 0) ArduinoOTA.setHostname(host);
  }
  ArduinoOTA.begin();

  setBoot("Waiting 2s");
  delay(2000);

  escTaskStart();  // starts the ESCs and the 1 ms DShot task (drive.ino)

  setBoot("Waiting for ESC boot");
  unsigned long lastBootPush = 0;
  for (unsigned long start = millis(); millis() - start < 5000;) {
    unsigned long currentMicros = micros();

    drive_escs(0, 0);

    // keep the web page updating during the ESC wait (slow, so it won't upset DShot timing much)
    if (millis() - lastBootPush > 250) {
      lastBootPush = millis();
      pushWeb();
    }

    vTaskDelay(pdMS_TO_TICKS(1));  // the DShot task sends the frames; this just keeps them fresh and the page updating
  }

  setBoot("Enabling EDT");
  for (int i = 0; i < NUM_ESC; i++) escRequestEdt(i);  // sent by the DShot task on its next tick
  delay(20);  // let it go out before the ESC monitor starts
  escMonitorStart();  // from here on, an ESC that drops out is re-initialised automatically (drive.ino)

  setBoot("Ready");  // e.g. Ready (PANIC #12), Ready (BROWN #3), Ready (PWR #1)

  led(1, 0);

  pre_arm_safe();

  setBoot("Running");
}

void loop() {

  update_reboot();
  vbatUpdate();     // battery voltage divider
  batteryUpdate();  // low battery protection (battery.ino)
  crsfSetStrings(false);  // Mode / Boot text for the radio telemetry (the CRSF task does the receiving and sending)
  status();
  web();
  mixer(txchannel[1], txchannel[2]);
  drive_escs(drive1_pwm, drive2_pwm);
  updateRecords();  // max/min records, checked on every pass so short peaks aren't missed

  vTaskDelay(pdMS_TO_TICKS(1));
}
