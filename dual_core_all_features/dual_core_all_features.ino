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
#include "esp_wifi.h"
#include "driver/spi_master.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>  // Required for ESP32

spi_device_handle_t ledDev;

SET_TIME_BEFORE_STARTING_SKETCH_MS(5000);

#define configTICK_RATE_HZ 2000

IPAddress local_IP(192, 168, 10, 1);
IPAddress gateway(192, 168, 10, 1);
IPAddress subnet(255, 255, 255, 0);

const char *ssid = "Whale";
const char *password = "Whale#1234";
uint32_t last_ota_time = 0;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ---------------- Web dashboard variables ----------------
String modeString = "Idle";       // <- set this from anywhere in your code
String bootState = "Starting";    // <- updated during setup()
String bulkText = "";             // <- free text box, put anything in here

#define ARRAY_SIZE 20
volatile bool flagArray[ARRAY_SIZE];  // <- set any slot true to light its green dot

volatile int16_t imuSpare1 = 0;  // two spare IMU fields
volatile int16_t imuSpare2 = 0;

float throttleCmd1 = 0.08f;  // 0.0 - 1.0, what you send to the ESC
float throttleCmd2 = 0.08f;

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
            --bg-color: #0b0f19;
            --card-bg: rgba(255, 255, 255, 0.05);
            --border-color: rgba(255, 255, 255, 0.1);
            --text-main: #ffffff;
            --text-muted: #8e8e93;
            --accent: #0a84ff;
            --success: #30d158;
            --bad: #ff453a;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-main);
            padding: 30px 20px env(safe-area-inset-bottom);
            display: flex; flex-direction: column; align-items: center;
        }
        .phone-wrapper { width: 100%; max-width: 420px; }
        h1 { font-size: 28px; font-weight: 700; letter-spacing: -0.5px; margin-bottom: 25px; padding-left: 5px; }
        h2 { font-size: 13px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.6px; color: var(--text-muted); margin: 0 0 8px 8px; }
        .list-group { background: var(--card-bg); backdrop-filter: blur(20px); -webkit-backdrop-filter: blur(20px); border: 1px solid var(--border-color); border-radius: 14px; overflow: hidden; margin-bottom: 20px; }
        .row-item { display: flex; justify-content: space-between; align-items: center; padding: 12px 20px; border-bottom: 1px solid var(--border-color); gap: 12px; }
        .row-item:last-child { border-bottom: none; }
        .label { font-size: 16px; font-weight: 500; }
        .value { font-size: 16px; font-weight: 400; color: var(--text-muted); text-align: right; font-variant-numeric: tabular-nums; }
        .bulk { padding: 14px 20px; font-family: ui-monospace, Menlo, monospace; font-size: 13px; color: var(--text-muted); white-space: pre-wrap; word-break: break-word; min-height: 60px; max-height: 200px; overflow-y: auto; }
        .dots { display: grid; grid-template-columns: repeat(10, 1fr); gap: 10px; padding: 16px 20px; justify-items: center; }
        .dot { width: 16px; height: 16px; border-radius: 50%; background: rgba(255,255,255,0.12); }
        .dot.on { background: var(--success); box-shadow: 0 0 8px var(--success); }
        .good { color: var(--success) !important; }
        .bad { color: var(--bad) !important; }
    </style>
</head>
<body>
    <div class="phone-wrapper">
        <h1>🐋🐋 Whale Stats 🐋🐋</h1>
        <div id="root"></div>
    </div>
<script>
// Each section: [title, [[label, id], ...]]
const SECTIONS = [
  ["Status", [["Page Data","v-live"],["Mode","v-mode"],["Boot State","v-boot"]]],
  ["CPU Load", [["Core 0","v-c0"],["Core 1","v-c1"],["Average","v-ca"]]],
];
for (let n = 1; n <= 2; n++) {
  SECTIONS.push(["ESC " + n, [
    ["Throttle","e"+n+"-thr"],["Voltage","e"+n+"-v"],["Current","e"+n+"-a"],
    ["Temperature","e"+n+"-t"],["RPM","e"+n+"-rpm"],["Speed","e"+n+"-spd"],["Link","e"+n+"-link"]
  ]]);
}
const rx = [["Link","rx-up"],["Link Quality","rx-lq"],["RSSI","rx-rssi"],["dBm","rx-dbm"]];
for (let c = 1; c <= 8; c++) rx.push(["Channel " + c, "rx-ch" + c]);
SECTIONS.push(["Receiver", rx]);
SECTIONS.push(["IMU", [
  ["Gyro X","i-gx"],["Gyro Y","i-gy"],["Gyro Z","i-gz"],
  ["Accel X","i-ax"],["Accel Y","i-ay"],["Accel Z","i-az"],
  ["Spare 1","i-s1"],["Spare 2","i-s2"]
]]);

const root = document.getElementById('root');
function build() {
  let html = "";
  for (const [title, rows] of SECTIONS) {
    html += `<h2>${title}</h2><div class="list-group">`;
    for (const [label, id] of rows)
      html += `<div class="row-item"><div class="label">${label}</div><div class="value" id="${id}">--</div></div>`;
    html += `</div>`;
    if (title === "Status") {
      html += `<h2>Bulk Text</h2><div class="list-group"><div class="bulk" id="v-bulk"></div></div>`;
    }
  }
  html += `<h2>Array</h2><div class="list-group"><div class="dots">`;
  for (let i = 0; i < 20; i++) html += `<div class="dot" id="d${i}"></div>`;
  html += `</div></div>`;
  root.innerHTML = html;
}
build();

const $ = id => document.getElementById(id);
const set = (id, v) => { const e = $(id); if (e) e.textContent = v; };
const f = (v, d) => (v === undefined || v === null) ? "--" : Number(v).toFixed(d);

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

  if (d.esc) d.esc.forEach((e, i) => {
    const n = i + 1;
    set(`e${n}-thr`, f(e.thr,0)+" %");
    set(`e${n}-v`,   f(e.v,1)+" V");
    set(`e${n}-a`,   f(e.a,1)+" A");
    set(`e${n}-t`,   f(e.t,0)+" °C");
    set(`e${n}-rpm`, f(e.rpm,0));
    set(`e${n}-spd`, f(e.spd,1)+" km/h");
    set(`e${n}-link`,f(e.link,0)+" %");
  });

  if (d.rx) {
    const up = $('rx-up');
    up.textContent = d.rx.up ? "UP" : "DOWN";
    up.className = "value " + (d.rx.up ? "good" : "bad");
    set('rx-lq', d.rx.lq + " %");
    set('rx-rssi', d.rx.rssi + " %");
    set('rx-dbm', d.rx.dbm + " dBm");
    (d.rx.ch || []).forEach((v, i) => set('rx-ch' + (i + 1), v));
  }

  if (d.imu) {
    const m = d.imu;
    set('i-gx', m.gx); set('i-gy', m.gy); set('i-gz', m.gz);
    set('i-ax', m.ax); set('i-ay', m.ay); set('i-az', m.az);
    set('i-s1', m.s1); set('i-s2', m.s2);
  }

  if (d.arr) d.arr.forEach((v, i) => $('d' + i).classList.toggle('on', !!v));
}

// "Page Data" shows LIVE only while messages keep arriving
setInterval(() => {
  const live = (Date.now() - lastMsg) < 1000;
  const e = $('v-live');
  e.textContent = live ? "LIVE" : "STALE";
  e.className = "value " + (live ? "good" : "bad");
}, 300);

window.addEventListener('load', initWebSocket);
</script>
</body>
</html>
)rawliteral";

// ---------------- Hardware ----------------
const int PIN_ESC1 = 1;
const int PIN_ESC2 = 2;
const uint8_t MOTOR_POLES = 14;

AlfredoDShot esc1;
AlfredoDShot esc2;

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

volatile float core0 = 0;
volatile float core1 = 0;
volatile float coreA = 0;

int gearbox_ratio1 = 19;
int gearbox_ratio2 = 19;
int wheel_diamiter1 = 101;  // in mm
int wheel_diamiter2 = 101;  // in mm

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
VectorInt16 aa;  // accel
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
void addEsc(JsonArray arr, AlfredoDShot &e, float thrCmd, int gear, int wheel) {
  JsonObject o = arr.add<JsonObject>();
  float rpmOut = e.rpm() / gear;
  o["thr"] = thrCmd * 100.0f;
  o["v"] = e.voltage();
  o["a"] = e.current();
  o["t"] = e.temperatureC();
  o["rpm"] = rpmOut;
  o["spd"] = (wheel / 10) * rpmOut * 0.001885;
  o["link"] = (float)e.lossPercent();  // adjust if you want a different "link" metric
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

  // ESCs
  JsonArray escs = doc["esc"].to<JsonArray>();
  addEsc(escs, esc1, throttleCmd1, gearbox_ratio1, wheel_diamiter1);
  addEsc(escs, esc2, throttleCmd2, gearbox_ratio2, wheel_diamiter2);

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

void onWsEvent(AsyncWebSocket *s, AsyncWebSocketClient *client, AwsEventType type,
               void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    client->text(getSerializedData());  // send current values immediately
  }
}

// Push a web update right now (used during boot, when loop() isn't running yet)
void pushWeb() {
  ws.textAll(getSerializedData());
  ws.cleanupClients();
}

void setup() {
  bootState = "GPIO init";

  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << PIN_ESC1;
  cfg.mode = GPIO_MODE_OUTPUT_OD;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
  gpio_set_level((gpio_num_t)PIN_ESC1, 0);

  cfg.pin_bit_mask = 1ULL << PIN_ESC2;
  cfg.mode = GPIO_MODE_OUTPUT_OD;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
  gpio_set_level((gpio_num_t)PIN_ESC2, 0);

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

  cpuMonitor.onSample([](const CpuUsageSample &s) {
    core0 = s.perCore[0];
    core1 = s.perCore[1];
    coreA = s.average;
  });

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

  Serial.begin(115200);
  esc2.begin(PIN_ESC1, DSHOT600, true, MOTOR_POLES);
  esc1.begin(PIN_ESC2, DSHOT600, true, MOTOR_POLES);

  bootState = "Waiting for ESC boot";
  unsigned long lastBootPush = 0;
  for (unsigned long start = millis(); millis() - start < 5000;) {
    unsigned long currentMicros = micros();

    if (currentMicros - previousMicros >= INTERVAL_US) {
      previousMicros += INTERVAL_US;
      esc1.sendThrottle(0.f);
      esc2.sendThrottle(0.f);
    }

    // keep the web page updating during the ESC wait (slow, so it won't upset DShot timing much)
    if (millis() - lastBootPush > 250) {
      lastBootPush = millis();
      pushWeb();
    }
  }

  bootState = "Enabling EDT";
  esc1.command(DSHOT_CMD_EDT_ENABLE);
  esc2.command(DSHOT_CMD_EDT_ENABLE);

  bootState = "Ready";
  modeString = "Running";
}

void loop() {

  ArduinoOTA.handle();

  if (otaInProgress) {
    return;
  }

  unsigned long now = millis();
  if (now - lastUpdate > 100) {
    lastUpdate = now;
    ws.textAll(getSerializedData());
  }

  crsf.update();

  unsigned long currentMicros = micros();

  if (currentMicros - previousMicros >= INTERVAL_US) {
    previousMicros += INTERVAL_US;
    esc1.sendThrottle(throttleCmd1);
    esc2.sendThrottle(throttleCmd2);
  }

  if (millis() - lastHeartbeatMs > 1000) {
    lastHeartbeatMs = millis();
    crsf.sendHeartbeat();

    esc1.stats();
    esc2.stats();

    crsf_sensor_battery_t crsfBatt = { 0 };

    crsfBatt.voltage = htobe16((uint16_t)(esc1.voltage() * 10.0));
    crsfBatt.current = htobe16((uint16_t)(esc2.voltage() * 10.0));
    crsfBatt.capacity = htobe24((uint32_t)(esc1.temperatureC()));
    crsfBatt.remaining = (uint8_t)(esc2.temperatureC());
    crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_BATTERY_SENSOR, &crsfBatt, sizeof(crsfBatt));
  }

  // ---- DEMO: remove these once you drive the array / text yourself ----
  static int demoTick = 0;
  if (++demoTick % 100 == 0) {
    int pos = (demoTick / 100) % ARRAY_SIZE;
    for (int i = 0; i < ARRAY_SIZE; i++) flagArray[i] = (i == pos);
    bulkText = "Demo text, tick " + String(demoTick) + "\nTilt: " + String(RP) + "  Yaw: " + String(Y);
  }
  // ----------------------------------------------------------------------

  if (millis() - lastPrintMs > 100) {
    lastPrintMs = millis();
    printEverything();
  }

  vTaskDelay(pdMS_TO_TICKS(1));
}

void printEverything() {
  Serial.print("link: ");
  Serial.print(crsf.isLinkUp() ? "UP" : "DOWN");
  const crsfLinkStatistics_t *link = crsf.getLinkStatistics();
  Serial.print("  LQ: ");
  Serial.print(link->uplink_Link_quality);
  Serial.print("  RSSI: -");
  Serial.print(link->active_antenna == 0 ? link->uplink_RSSI_1 : link->uplink_RSSI_2);
  Serial.print("dBm  armed: ");
  Serial.println(crsf.isArmed() ? "yes" : "no");

  Serial.print("channels 1-8:");
  for (int i = 1; i <= 8; i++) {
    Serial.print(" ");
    Serial.print(crsf.getChannel(i));
  }

  Serial.println("");
  Serial.printf(
    "%.0f %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h   |",
    throttleCmd1 * 100.0f,
    esc1.voltage(),
    esc1.current(),
    esc1.temperatureC(),
    (esc1.rpm() / gearbox_ratio1),
    (wheel_diamiter1 / 10) * (esc1.rpm() / gearbox_ratio1) * 0.001885);
  Serial.println(esc1.lossPercent());

  Serial.printf(
    "%.0f %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h   |",
    throttleCmd2 * 100.0f,
    esc2.voltage(),
    esc2.current(),
    esc2.temperatureC(),
    (esc2.rpm() / gearbox_ratio2),
    (wheel_diamiter2 / 10) * (esc2.rpm() / gearbox_ratio2) * 0.001885);
  Serial.println(esc2.lossPercent());

  Serial.printf("core0=%.1f%%  core1=%.1f%%  avg=%.1f%%\n",
                core0, core1, coreA);

  Serial.print("Tilt: ");
  Serial.print(RP);
  Serial.print("\t");
  Serial.print("Yaw: ");
  Serial.println(Y);

  Serial.println("");
  Serial.println("");
}
