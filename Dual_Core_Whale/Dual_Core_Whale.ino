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

// Memory-corruption hunter. 1 = after crsf.begin(), set a hardware watchpoint on the CRSF object's first
// word. The moment ANY code writes to it, the board panics with a backtrace that shows the guilty function.
// Leave at 0 for normal running.
#define DEBUG_WATCH_CRSF 0
#if DEBUG_WATCH_CRSF
#include "esp_cpu.h"
#include "esp_ipc.h"
#endif

spi_device_handle_t ledDev;  // SET_TIME_BEFORE_STARTING_SKETCH_MS(5000);

SET_LOOP_TASK_STACK_SIZE(16 * 1024);  // Set main loop stack size to 16 KB

uint32_t last_ota_time = 0;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ---------------- Web dashboard variables ----------------
String modeString = "Idle";     // <- set this from anywhere in your code
String bootState = "Starting";  // <- updated during setup()
String bulkText = "";           // <- free text box, put anything in here

#define ARRAY_SIZE 20
volatile bool modeArray[ARRAY_SIZE];  // <- set any slot true to light its green dot

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

static uint32_t maxGapUs = 0;
static uint32_t jsonMaxUs = 0;  // longest time spent building + sending a web update this second
static uint32_t escMaxUs = 0;   // longest time spent in the ESC send block this second

volatile int16_t imuSpare1 = 0;  // two spare IMU fields
volatile int16_t imuSpare2 = 0;

// Arrows shown in the IMU box - you drive these from your own code
volatile int8_t imuOrient = 0;     // up/down arrow:    1 = up (arrow up),    -1 = down (arrow down),  0 = blank
volatile int8_t imuDirection = 0;  // left/right arrow: 1 = right (arrow right), -1 = left (arrow left), 0 = blank

// ---- Records (tracked here on the ESP so nothing is missed between page updates) ----
#define DMP_ACCEL_LSB_PER_G 16384.0f  // accel counts per g (+/-2 g range). If Max G still reads ~0.5 or ~1 at rest, try 8192.0f
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

void loadTune() {
  float tmp[NUM_TUNE];
  prefs.begin("whale", true);  // read-only
  size_t n = prefs.getBytesLength("tune");
  if (n == sizeof(tmp)) {  // only trust it if the size matches (changing NUM_TUNE falls back to defaults)
    prefs.getBytes("tune", tmp, sizeof(tmp));
    for (int i = 0; i < NUM_TUNE; i++) tuneVal[i] = tmp[i];
  }
  for (int i = 0; i < NUM_TUNE_STR; i++) {
    char key[8], txt[TUNE_STR_LEN] = "";
    snprintf(key, sizeof(key), "ts%d", i);
    prefs.getString(key, txt, sizeof(txt));  // stays empty if nothing was saved yet
    strlcpy(tuneStr[i], txt, TUNE_STR_LEN);
  }
  prefs.end();
}

void saveTune() {
  float tmp[NUM_TUNE];
  for (int i = 0; i < NUM_TUNE; i++) tmp[i] = tuneVal[i];
  prefs.begin("whale", false);  // read/write
  prefs.putBytes("tune", tmp, sizeof(tmp));
  for (int i = 0; i < NUM_TUNE_STR; i++) {
    char key[8], txt[TUNE_STR_LEN];
    snprintf(key, sizeof(key), "ts%d", i);
    strlcpy(txt, tuneStr[i], TUNE_STR_LEN);
    prefs.putString(key, txt);
  }
  prefs.end();
}

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
float throttleCmd[MAX_ESC] = { 0.00f, 0.00f, 0.00f, 0.00f };  // 0.0 - 1.0, what you send to each ESC

// An ESC that isn't giving telemetry gets re-sent "enable EDT" this often (ms),
// so one plugged in after boot can start reporting. Set to 0 to turn this off.
// #define ESC_REDETECT_MS 10

// An ESC with no valid telemetry for this long (microseconds) is shown as not reporting
#define ESC_STALE_US 1000000

volatile bool otaInProgress = false;
unsigned long lastUpdate = 0;
#define WEB_UPDATE_MS 50  // milliseconds between web page updates (10 = 100 per second). Higher = less CPU, jerkier page

// ---------------- Hardware ----------------
// ESC signal pins, in web-page order (ESC 1..4).
// ESC 1 = GPIO 2 and ESC 2 = GPIO 1, matching your original (swapped) begin() calls.
// ESC 3 and ESC 4 pins are a guess - change them to wherever you wire them.
const int ESC_PINS[MAX_ESC] = { 2, 1, 9, 10 };
const uint8_t MOTOR_POLES = 14;

AlfredoDShot escs[NUM_ESC];

ESPCpuMonitor cpuMonitor;
bool cpuMonitorOk = false;  // only true once init() succeeded; getLastSample() on an un-started monitor would crash

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

HardwareSerial crsfSerial(1);
AlfredoCRSF crsf;

uint32_t lastHeartbeatMs = 0;
uint32_t lastPrintMs = 0;

int roll = 0;
int pitch = 0;
int yaw = 0;

int roll_flip = 20;

// Up/down detection from the IMU (sets imuOrient to 1 = up, -1 = down)
int imuUpAxis = 2;            // which sensor axis points UP when the robot is the right way up: 0 = X, 1 = Y, 2 = Z
bool imuUpInverted = false;   // true if that axis points DOWN when the robot is the right way up
float imuOrientHyst = 0.15f;  // dead band, in g (0 to 1). The robot must swing past this before the state flips back.
                              // 0.15 = flips when the axis is within about 9 degrees of horizontal, 0.5 = 30 degrees, 0 = no hysteresis.

const int RPnumReadings = 10;
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

//  esc varibles
int throttle1 = 0;
int throttle2 = 0;
int drive1_pwm = 0;
int drive2_pwm = 0;

int gearbox_ratio[MAX_ESC] = { 19, 19, 19, 19 };
int wheel_diamiter[MAX_ESC] = { 101, 101, 101, 101 };  // in mm

const unsigned long INTERVAL_US = 1000;
unsigned long previousMicros = 0;

// pre arm safe varibles
bool safe_switch = 0;
bool safe_sticks = 0;
bool safe_link = 0;
bool safed = 1;
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
char dbgLine[300] = "";                 // diagnostics text shown on the page; rebuilt once a second in loop()
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

bool imuReadFifoCount(uint16_t &count) {
  Wire.beginTransmission(IMU_I2C_ADDR);
  Wire.write(0x72);  // FIFO_COUNT_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)IMU_I2C_ADDR, (size_t)2, true) != 2) return false;
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  count = ((uint16_t)hi << 8) | lo;
  return true;
}

bool imuReadFifo(uint8_t *dst, uint8_t n) {
  Wire.beginTransmission(IMU_I2C_ADDR);
  Wire.write(0x74);  // FIFO_R_W
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)IMU_I2C_ADDR, (size_t)n, true) != n) return false;
  for (uint8_t i = 0; i < n; i++) dst[i] = Wire.read();
  return true;
}

// Start the sensor and load the DMP. Returns 0 on success (same as dmpInitialize()). 'found' says if it answered at all.
uint8_t imuBegin(bool &found) {
  mpu.initialize();
  found = mpu.testConnection();
  if (!found) return 1;
  uint8_t st = mpu.dmpInitialize();
  if (st == 0) {
    mpu.setXGyroOffset(-234);
    mpu.setYGyroOffset(24);
    mpu.setZGyroOffset(-9);
    mpu.setXAccelOffset(-698);
    mpu.setYAccelOffset(476);
    mpu.setZAccelOffset(2538);
  }
  return st;
}

// Called from the IMU task after repeated bus failures.
void imuRecover() {
  imuRecoveries++;
  Wire.end();
  // A device that was mid-transfer when the wire moved can hold SDA low forever. Clocking SCL up to 9 times lets it finish.
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, OUTPUT);
  for (int i = 0; i < 9; i++) {
    digitalWrite(SCL_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(SCL_PIN, HIGH);
    delayMicroseconds(5);
  }
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  Wire.setTimeOut(IMU_I2C_TIMEOUT_MS);
  bool found = false;
  if (imuBegin(found) == 0) {  // the sensor may have power-cycled and lost the DMP, so load it again
    mpu.setDMPEnabled(true);
    mpu.resetFIFO();
    imuWarm = 0;
  }
}

void imuTask(void *arg) {
  Med3 mGx, mGy, mGz, mAx, mAy, mAz;
  uint8_t imuFailStreak = 0;  // failed I2C transfers in a row
  if (otaInProgress == false) {
    for (;;) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

      uint32_t cn0 = micros();
      uint16_t fc = 0;
      bool countOk = imuReadFifoCount(fc);
      uint32_t cnUs = micros() - cn0;
      if (cnUs > imuCountMaxUs) imuCountMaxUs = cnUs;
      if (!countOk) {  // sensor didn't answer: count it, and after a run of failures reset the bus and restart the IMU
        imuI2cErrors++;
        if (++imuFailStreak >= IMU_FAIL_LIMIT) {
          imuRecover();
          imuFailStreak = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      if (fc > imuFifoMax) imuFifoMax = fc;
      if (fc < IMU_PKT) {  // no complete packet yet
        imuFailStreak = 0;
        continue;
      }
      if (fc > 200) {  // FIFO overflowed: its contents can't be trusted, clear it and wait for fresh packets
        imuOverflows++;
        mpu.resetFIFO();
        continue;
      }
      if (fc >= 2 * IMU_PKT) imuMultiPkt++;
      uint32_t rd0 = micros();
      bool gotPacket = true;
      while (gotPacket && fc >= 2 * IMU_PKT) {  // several packets waiting: read and discard the old ones, keep the newest
        gotPacket = imuReadFifo(fifoBuffer, IMU_PKT);
        fc -= IMU_PKT;
      }
      if (gotPacket) gotPacket = imuReadFifo(fifoBuffer, IMU_PKT);
      if (gotPacket) {
        imuFailStreak = 0;
      } else {
        imuI2cErrors++;
        if (++imuFailStreak >= IMU_FAIL_LIMIT) {
          imuRecover();
          imuFailStreak = 0;
        }
      }
      uint32_t rdUs = micros() - rd0;
      if (rdUs > imuReadMaxUs) imuReadMaxUs = rdUs;
      bool lateRead = rdUs > IMU_LATE_US;
      if (lateRead) imuLateReads++;
      if (!gotPacket) continue;

      mpu.dmpGetQuaternion(&q, fifoBuffer);

      // A good packet has a unit-length quaternion. A packet damaged by I2C noise (or a FIFO overflow)
      // doesn't, so drop it instead of letting it spike the tilt, yaw, G and the page values.
      float qn = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
      if (qn < 0.9f || qn > 1.1f) {
        imuBadPackets++;
        if (lateRead) imuBadOnLate++;
        imuLastBadQn = qn;
        mpu.resetFIFO();  // re-sync with the packet boundaries
        continue;
      }

      mpu.dmpGetGravity(&gravity, &q);
      mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

      pitch = (int)fabsf(ypr[1] * 180.0f / M_PI);
      roll = (int)fabsf(ypr[2] * 180.0f / M_PI);

      RPtotal -= RPreadings[RPreadIndex];
      RPreadings[RPreadIndex] = min(roll + pitch, 70 * 2);
      RPtotal += RPreadings[RPreadIndex];
      RPreadIndex = (RPreadIndex + 1) % RPnumReadings;
      RP = RPtotal / RPnumReadings;

      mpu.dmpGetGyro(&gy, fifoBuffer);
      mpu.dmpGetAccel(&aa, fifoBuffer);

      // A half-read packet can have a valid quaternion but all-zero accelerometer bytes. Real readings are never
      // exactly 0,0,0 on every axis, and a zero here makes the gravity-removal below report a false 1.00 G.
      if (aa.x == 0 && aa.y == 0 && aa.z == 0) {
        imuBadPackets++;
        imuLastBadQn = qn;
        mpu.resetFIFO();
        continue;
      }

      imuLastGoodMs = millis();  // this packet passed every check

      // median-of-3 on every axis to remove single-sample spikes
      gy.x = mGx.add(gy.x);
      gy.y = mGy.add(gy.y);
      gy.z = mGz.add(gy.z);
      aa.x = mAx.add(aa.x);
      aa.y = mAy.add(aa.y);
      aa.z = mAz.add(aa.z);

      imuGx = gy.x;
      imuGy = gy.y;
      imuGz = gy.z;
      imuAx = aa.x;
      imuAy = aa.y;
      imuAz = aa.z;

      // Up/down: the DMP gravity vector is +1 on whichever sensor axis currently points up. Inside the dead band
      // (+/- imuOrientHyst) the previous state is kept, so it can't flicker while the robot passes through sideways.
      {
        float up = (imuUpAxis == 0) ? gravity.x : (imuUpAxis == 1) ? gravity.y
                                                                   : gravity.z;
        if (imuUpInverted) up = -up;
        if (up > imuOrientHyst) imuOrient = 1;
        else if (up < -imuOrientHyst) imuOrient = -1;
      }


      // Remove gravity using the DMP orientation. Done by hand (not dmpGetLinearAccel) because that
      // library function assumes 8192 counts/g, which doesn't match the 16384 this sensor is giving.
      // 'gravity' is a unit vector (computed above from the quaternion), so scale it to counts first.
      float lx = aa.x - gravity.x * DMP_ACCEL_LSB_PER_G;
      float ly = aa.y - gravity.y * DMP_ACCEL_LSB_PER_G;
      float lz = aa.z - gravity.z * DMP_ACCEL_LSB_PER_G;
      float g = sqrtf(lx * lx + ly * ly + lz * lz) / DMP_ACCEL_LSB_PER_G;
      if (imuWarm < IMU_WARMUP) imuWarm++;  // let the DMP settle after (re)start before recording
      else if (g > recMaxG) recMaxG = g;

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

// ---------------- ESC telemetry helpers ----------------
// An ESC counts as present while it keeps sending valid telemetry. ageUs() is the time since the last
// good frame (huge if there never was one), so an ESC that goes quiet or is unplugged drops out after
// ESC_STALE_US. voltage() is NaN until the first extended-telemetry frame, so that must have arrived too.
// (Not lossPercent(): that is a running total since boot and almost never reaches 100.)
bool escPresent(int i) {
  return escs[i].ageUs() < ESC_STALE_US && isfinite(escs[i].voltage());
}

// Throttle shown on the page, in %: (output - 1500) / 5. An unset output (0) works out to -300, shown as 0.
int escThrPct(int i) {
  int thr = (outChannel[i] - 1500) / 5;
  return (thr <= -300) ? 0 : thr;
}

// ESC current, forced to 0 while the throttle reads 0. The ESC sends no new readings at zero throttle,
// so its last current would otherwise stay stuck at a non-zero value.
float escCurrent(int i) {
  return (escThrPct(i) == 0) ? 0.0f : escs[i].current();
}

float escSpeed(int i) {
  float rpmOut = escs[i].rpm() / gearbox_ratio[i];
  float spd = (wheel_diamiter[i] / 10) * rpmOut * 0.001885;
  // The ESC's eRPM telemetry has no direction in it, so the sign comes from the command:
  // an output below 1500 means the motor was told to run backwards, so the speed shows negative.
  return (outChannel[i] < 1500) ? -spd : spd;
}

// Called on every pass of loop() (about every 1 ms): updates max temperature, max current, min voltage, max average speed
void updateRecords() {
  float sumSpd = 0;
  float sumA = 0;
  int n = 0;
  for (int i = 0; i < NUM_ESC; i++) {
    if (!escPresent(i)) continue;
    n++;
    float v = escs[i].voltage();
    float a = escCurrent(i);
    float t = escs[i].temperatureC();
    if (isfinite(t) && t > recMaxTemp) recMaxTemp = t;
    if (a > recMaxA) recMaxA = a;
    sumA += a;
    if (v > 1.0f && (recMinV == 0 || v < recMinV)) recMinV = v;
    sumSpd += fabsf(escSpeed(i));
  }
  if (n > 0) {
    if (sumA > recMaxTotalA) recMaxTotalA = sumA;
    float avgSpd = sumSpd / n;
    if (avgSpd > recMaxSpd) recMaxSpd = avgSpd;
  }
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

// Peak hold: a new high is taken immediately, otherwise ease down toward the raw reading
float peakHold(float raw, float peak) {
  if (raw > peak) return raw;
  return raw * PEAK_DECAY_ALPHA + peak * (1.0f - PEAK_DECAY_ALPHA);
}

#if DEBUG_WATCH_CRSF
static void armCrsfWatch(void *) {
  esp_cpu_set_watchpoint(0, (const void *)&crsf, 4, ESP_CPU_WATCHPOINT_STORE);  // per-core, so run it on both
}
#endif

// Why the board last restarted - shown on the page and printed at boot. This is the quickest way to tell
// a brownout (power dip) from a crash (PANIC), a watchdog (IWDT / TWDT) or a normal restart (SW / PWR).
const char *resetReasonShort() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "PWR";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "IWDT";
    case ESP_RST_TASK_WDT: return "TWDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWN";
    case ESP_RST_DEEPSLEEP: return "SLEEP";
    default:

      {  // any other reason (power glitch, USB, CPU lock-up, ...): show the raw number so it can be looked up
        static char other[8];
        snprintf(other, sizeof(other), "R%d", (int)esp_reset_reason());
        return other;
      }
  }
}

void setup() {
  Serial.begin(115200);  // started first so the prints below work
  Serial.printf("Reset reason: %s\n", resetReasonShort());

  bootState = "GPIO init";
  led(1, 1);

  gpio_config_t cfg = {};
  for (int i = 0; i < NUM_ESC; i++) {
    cfg.pin_bit_mask = 1ULL << ESC_PINS[i];
    cfg.mode = GPIO_MODE_OUTPUT_OD;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    gpio_set_level((gpio_num_t)ESC_PINS[i], 0);
  }

  bootState = "Waiting 2s";
  for (unsigned long start = millis(); millis() - start < 4000;) {
  }

  pinMode(INT_PIN, INPUT_PULLDOWN);

  Serial.printf("Free stack: %d\n", uxTaskGetStackHighWaterMark(NULL));

  loadTune();  // restore saved tuning values first


  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  Wire.setTimeOut(IMU_I2C_TIMEOUT_MS);


  Serial.println("ELRS 4.x receiver test");

  bootState = "Receiver init";
  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);
  if (!crsfSerial) {
    Serial.println("Invalid crsfSerial configuration");
  }

  crsf.begin(crsfSerial);
  crsf.setDeviceName("AlfredoCRSF");
#if DEBUG_WATCH_CRSF
  armCrsfWatch(nullptr);                            // this core (setup runs on core 1)
  esp_ipc_call_blocking(0, armCrsfWatch, nullptr);  // and core 0 (IMU / LED tasks)
#endif

  for (int i = 0; i < RPnumReadings; i++) RPreadings[i] = 0;
  for (int i = 0; i < YnumReadings; i++) Yreadings[i] = 0;
  for (int i = 0; i < ARRAY_SIZE; i++) modeArray[i] = false;

  bootState = "IMU init";
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

  bootState = "WiFi AP start";
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

  ArduinoOTA.begin();

  bootState = "Waiting 2s";
  for (unsigned long start = millis(); millis() - start < 2000;) {
  }

  for (int i = 0; i < NUM_ESC; i++) escs[i].begin(ESC_PINS[i], DSHOT600, true, MOTOR_POLES);

  bootState = "Waiting for ESC boot";
  unsigned long lastBootPush = 0;
  for (unsigned long start = millis(); millis() - start < 5000;) {
    unsigned long currentMicros = micros();

    drive_escs(0, 0);

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

  cpuMonitorOk = cpuMonitor.init(cpucfg);
  if (!cpuMonitorOk) {
    Serial.println("CPU monitor init failed");
    return;
  }

  bootState = String("Ready (") + resetReasonShort() + ")";  // e.g. Ready (PANIC), Ready (BROWN), Ready (PWR)

  led(1, 0);

  led(2, 1);
  // safe boot here
  led(2, 0);

  modeString = "Running";
}

void loop() {

  ArduinoOTA.handle();

  if (rebootPending && rebootBlocked()) {  // armed during the 0.5 s countdown: cancel the reboot
    rebootPending = false;
    modeString = "Running";
  }
  if (rebootPending && millis() > rebootAtMs) {
    if (tuneDirty) saveTune();  // don't lose a tuning edit made in the last 2 seconds
    Serial.println("Rebooting...");
    ESP.restart();
  }

  if (otaInProgress) {
    return;
  }


  // longest gap between loop() passes, reported once a second on the page's diagnostics line
  static uint32_t lastLoopUs = micros();
  uint32_t nowUs = micros();
  if (nowUs - lastLoopUs > maxGapUs) maxGapUs = nowUs - lastLoopUs;
  lastLoopUs = nowUs;

  // once a second, drop dead/stalled web clients so their queued messages don't pile up in RAM
  static uint32_t lastCleanupMs = 0;
  if (millis() - lastCleanupMs >= 1000) {
    lastCleanupMs = millis();
    ws.cleanupClients();
    dbgMaxLoopGapUs = maxGapUs;
    snprintf(dbgLine, sizeof(dbgLine), "heap %uk min %uk blk %uk \ngap %.1fms json %.1fms esc %.1fms | ws %u\nimu read %.1fms late %u  cnt %.1fms\nfifo %u multi %u  bad %u (on late reads %u) last qn %.2f\nI2C err %u rec %u ovf %u",
             (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024),
             maxGapUs / 1000.0f, jsonMaxUs / 1000.0f, escMaxUs / 1000.0f, (unsigned)ws.count(),
             imuReadMaxUs / 1000.0f, (unsigned)imuLateReads, imuCountMaxUs / 1000.0f, (unsigned)imuFifoMax, (unsigned)imuMultiPkt, (unsigned)imuBadPackets, (unsigned)imuBadOnLate, (float)imuLastBadQn, (unsigned)imuI2cErrors, (unsigned)imuRecoveries, (unsigned)imuOverflows);
    imuReadMaxUs = 0;
    imuCountMaxUs = 0;
    imuFifoMax = 0;
    maxGapUs = 0;
    jsonMaxUs = 0;
    escMaxUs = 0;
  }

  unsigned long now = millis();
  if (now - lastUpdate >= WEB_UPDATE_MS) {
    lastUpdate = now;

    state();
    if (!imuAlive()) imuOrient = 0;  // IMU dropout: blank the up/down arrow instead of showing a stale value

    if (ws.count() > 0) {  // nobody watching -> don't build the JSON at all
      uint32_t t0 = micros();
      ws.textAll(getSerializedData());
      uint32_t d = micros() - t0;
      if (d > jsonMaxUs) jsonMaxUs = d;
    }
  }


  // CPU load with peak-hold: jumps up instantly, then decays back toward the live reading.
  // Runs once per CPU monitor sample (every 10 ms, same as cpucfg.sampleIntervalMs) so the decay
  // speed doesn't depend on how fast loop() spins. With alpha 0.01 the fall-off takes about 1 second.
  static uint32_t lastCpuMs = 0;
  if (millis() - lastCpuMs >= 10) {
    lastCpuMs = millis();
    CpuUsageSample sample;
    if (cpuMonitorOk && cpuMonitor.getLastSample(sample)) {
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
  for (int i = 1; i <= 8; i++) {
    txchannel[i] = crsf.getChannel(i);
    if (txchannel[i] < 1000) {
      txchannel[i] = 1000;
    }
    if (txchannel[i] > 2000) {
      txchannel[i] = 2000;
    }
  }

  mixer(txchannel[1], txchannel[2]);

  outChannel[0] = throttle1;
  outChannel[1] = throttle2;

  drive_escs(drive1_pwm, drive2_pwm);

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
  // static uint32_t lastRedetect = 0;
  // if (ESC_REDETECT_MS > 0 && millis() - lastRedetect > ESC_REDETECT_MS) {
  //   lastRedetect = millis();
  //   for (int i = 0; i < NUM_ESC; i++)
  //     if (!escPresent(i)) escs[i].command(DSHOT_CMD_EDT_ENABLE);
  // }

  // ---- Bulk text ----


  bulkText = String(escs[0].lossPercent()) + "  " + String(escs[1].lossPercent()) + "\nTilt: " + String(RP) + "  Yaw: " + String(Y);
  // ----------------------------------------------------------------------


  vTaskDelay(pdMS_TO_TICKS(1));
}
