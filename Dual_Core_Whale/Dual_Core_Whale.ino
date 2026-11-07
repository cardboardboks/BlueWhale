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

// variables for the raw readings
volatile float core0 = 0;
volatile float core1 = 0;
volatile float coreA = 0;

// variables that decay slowly
volatile float core0Peak = 0;
volatile float core1Peak = 0;
volatile float coreAveragePeak = 0;  // Added system average peak-hold tracker

// decay rate coefficient (Value between 0.0 and 1.0)
// lower value = slower decay (e.g., 0.05 is very smooth, 0.20 is faster)
const float PEAK_DECAY_ALPHA = 0.01f;

// loops stats
static uint32_t maxGapUs = 0;
static uint32_t jsonMaxUs = 0;  // longest time spent building + sending a web update this second
static uint32_t escMaxUs = 0;   // longest time spent in the ESC send block this second

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

// ---- Whale Tuning ----
// Rename them here. Use tuneVal[n] anywhere in your code; editing a box on the page changes it live.
// Values are saved to flash (see saveTune) and reloaded at boot; 0 is used until something has been saved.
#define NUM_TUNE 20
const char *tuneName[NUM_TUNE] = {
  "Name", "sysID", "SSID", "Password", "Blank",
  "WiFi Power", "P", "I", "D", "Rate Yaw Max",
  "Accl Yaw Max", "Tune Var 12", "Tune Var 13", "Tune Var 14", "Tune Var 15",
  "Tune Var 16", "ESC1 Dir", "ESC2 Dir", "ESC3 Dir", "ESC4 Dir"
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
const int ESC_PINS[MAX_ESC] = { 2, 1, 16, 15 };
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

  Serial.println("ELRS 4.x receiver starting");

  bootState = "Receiver init";
  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);
  if (!crsfSerial) {
    Serial.println("Invalid crsfSerial configuration");
  }

  crsf.begin(crsfSerial);
  crsf.setDeviceName("AlfredoCRSF");

  ledInit();

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

  pre_arm_safe();

  bootState = "Running";
}

void loop() {

  update_reboot();
  crsf_rx();
  crsf_tx();
  status();
  web();
  mixer(txchannel[1], txchannel[2]);
  drive_escs(drive1_pwm, drive2_pwm);
  updateRecords();  // max/min records, checked on every pass so short peaks aren't missed

  vTaskDelay(pdMS_TO_TICKS(1));
}
