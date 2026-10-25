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
#include <WebServer.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include "driver/spi_master.h"
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>  // Required for ESP32

spi_device_handle_t ledDev;

SET_TIME_BEFORE_STARTING_SKETCH_MS(5000);
//ArduinoOTA.setRebootOnSuccess(true);

#define configTICK_RATE_HZ 2000

// Add near the top with the other config
IPAddress local_IP(192, 168, 10, 1);  // The ESP32's address
IPAddress gateway(192, 168, 10, 1);   // Same as local_IP for an AP
IPAddress subnet(255, 255, 255, 0);

// 1. Assign Access Point Network Credentials
const char *ssid = "Whale";
const char *password = "Whale#1234";
uint32_t last_ota_time = 0;

// 2. Initialize Servers
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// 3. Variables to Monitor Live
int liveCounter = 0;
float temperature = 24.5;
float humidity = 55.0;
int rssi = -127;
int rssiPercentage = 0;  // New variable to store 0-100% scale
float batteryVoltage = 4.2;
String statusString = "Optimal";

volatile bool otaInProgress = false;
unsigned long lastUpdate = 0;

// Helper function to scale dBm to a reliable 0-100% signal metric
int getSignalQuality(int dbm) {
  if (dbm == -127) return 0;   // Disconnected state
  if (dbm >= -50) return 100;  // Excellent signal is capped at 100%
  if (dbm <= -100) return 0;   // Unusable signal is capped at 0%

  // Linearly map the range [-100, -50] onto [0, 100]
  return map(dbm, -100, -50, 0, 100);
}

// Handle WebSocket events (connect, disconnect, data)
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("Client connected");
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.println("Client disconnected");
  }
}

// iPhone-Optimized iOS UI Dashboard
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
        }
        * { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; }
        body { 
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; 
            background-color: var(--bg-color); 
            color: var(--text-main);
            padding: 30px 20px env(safe-area-inset-bottom);
            display: flex;
            flex-direction: column;
            align-items: center;
        }
        .phone-wrapper { width: 100%; max-width: 375px; }
        h1 { font-size: 28px; font-weight: 700; letter-spacing: -0.5px; margin-bottom: 25px; padding-left: 5px; }
        .list-group { background: var(--card-bg); backdrop-filter: blur(20px); -webkit-backdrop-filter: blur(20px); border: 1px solid var(--border-color); border-radius: 14px; overflow: hidden; margin-bottom: 20px; }
        .row-item { display: flex; justify-content: space-between; align-items: center; padding: 16px 20px; border-bottom: 1px solid var(--border-color); }
        .row-item:last-child { border-bottom: none; }
        .label { font-size: 16px; font-weight: 500; }
        .value { font-size: 16px; font-weight: 400; color: var(--text-muted); }
        #v-counter { color: var(--accent); font-weight: 600; }
        #v-status { font-weight: 600; }
        .status-footer { text-align: center; font-size: 12px; color: var(--text-muted); margin-top: 15px; }
    </style>
</head>
<body>
    <div class="phone-wrapper">
        <h1>🐋🐋 Whale Stats 🐋🐋</h1>
        <div class="list-group">
            <div class="row-item"><div class="label">Live Counter</div><div class="value" id="v-counter">0</div></div>
            <div class="row-item"><div class="label">Temperature</div><div class="value" id="v-temp">0.0 °C</div></div>
            <div class="row-item"><div class="label">Humidity</div><div class="value" id="v-hum">0.0 %</div></div>
            <div class="row-item"><div class="label">Signal Strength</div><div class="value" id="v-rssi">-- %</div></div>
            <div class="row-item"><div class="label">Battery</div><div class="value" id="v-bat">0.00 V</div></div>
            <div class="row-item"><div class="label">System Status</div><div class="value" id="v-status">-</div></div>
        </div>
    <script>
       // Open connection automatically using the current window host
    const gateway = `ws://${window.location.host}/ws`;
    let websocket;

    function initWebSocket() {
        console.log('Trying to open a WebSocket connection...');
        websocket = new WebSocket(gateway);
        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onmessage = onMessage;
    }

    function onOpen(event) {
        console.log('Connection opened');
    }

    function onClose(event) {
        console.log('Connection closed');
        setTimeout(initWebSocket, 2000); // Auto-reconnect if lost
    }

    function onMessage(event) {
        // Parse the incoming JSON stream
        const data = JSON.parse(event.data);
        
        // Update DOM elements using the exact IDs defined in your HTML layout
        if (data.counter !== undefined) document.getElementById('v-counter').innerText = data.counter;
        if (data.temp    !== undefined) document.getElementById('v-temp').innerText    = data.temp.toFixed(1) + " °C";
        if (data.hum     !== undefined) document.getElementById('v-hum').innerText     = data.hum.toFixed(1) + " %";
        if (data.rssi_pct !== undefined) document.getElementById('v-rssi').innerText    = data.rssi_pct + " %";
        if (data.bat     !== undefined) document.getElementById('v-bat').innerText     = data.bat.toFixed(2) + " V";
        if (data.status  !== undefined) document.getElementById('v-status').innerText  = data.status;
    }


    window.addEventListener('load', initWebSocket);
    </script>
</body>
</html>
)rawliteral";

String getSerializedData() {
  JsonDocument doc;
  doc["counter"] = liveCounter;
  doc["temp"] = temperature;
  doc["hum"] = humidity;
  doc["rssi_raw"] = rssi;            // Keeping raw data as fallback
  doc["rssi_pct"] = rssiPercentage;  // Clean 0-100% scaling
  doc["bat"] = batteryVoltage;
  doc["status"] = statusString;

  String output;
  serializeJson(doc, output);
  return output;
}

const int PIN_ESC1 = 1;
const int PIN_ESC2 = 2;
const uint8_t MOTOR_POLES = 14;

AlfredoDShot esc1;
AlfredoDShot esc2;

ESPCpuMonitor cpuMonitor;  // you create the instance; only one can be active at a time

#define PIN_RX 5
#define PIN_TX 4

#define SCL_PIN 6
#define SDA_PIN 7
#define INT_PIN 8


#define LED_PIN 11  // This becomes the SPI MOSI pin (any GPIO works via the GPIO matrix)
#define NUM_LEDS 12
#define SPI_FREQ 3200000  // 3.2 MHz

#define RESET_BYTES 64  // 64 bytes * 8 bits * 312.5 ns = ~160 us of low
static uint8_t spiBuf[NUM_LEDS * 12 + RESET_BYTES];

struct RGB {
  uint8_t r, g, b;
};
RGB leds[NUM_LEDS];

// Lookup: 2 data bits -> 1 SPI byte (two WS2812 bits = 8 SPI bits)
static const uint8_t lut[4] = {
  0b10001000,  // 00
  0b10001110,  // 01
  0b11101000,  // 10
  0b11101110   // 11
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
    // WS2812 expects GRB order
    encodeByte(leds[i].g, p);
    p += 4;
    encodeByte(leds[i].r, p);
    p += 4;
    encodeByte(leds[i].b, p);
    p += 4;
  }
  memset(p, 0, RESET_BYTES);  // latch

  spi_transaction_t t = {};
  t.length = sizeof(spiBuf) * 8;  // in bits
  t.tx_buffer = spiBuf;
  spi_device_transmit(ledDev, &t);
}

// ---- LED task (runs on core 0) ----
void ledTask(void *pvParameters) {
  // Init SPI inside the task so it's set up on core 0
  // begin(sck, miso, mosi, ss) -> only MOSI is used
  ledInit();

  uint8_t hue = 0;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    if (otaInProgress == false) {
      // Simple rainbow chase
      for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t h = hue + i * (256 / NUM_LEDS);
        // quick HSV(h,255,64) -> RGB
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

// Shared between the IMU task (core 0) and loop() (core 1).
// volatile because they are written on one core and read on the other.
volatile int RP = 0;  // smoothed roll+pitch
volatile int Y = 0;   // smoothed yaw rate

volatile float core0 = 0;
volatile float core1 = 0;
volatile float coreA = 0;

int throttle1 = 1500;
int throttle2 = 1500;
int drive1_pwm = 0;
int drive2_pwm = 0;
int gearbox_ratio1 = 19;
int gearbox_ratio2 = 19;
int wheel_diamiter1 = 101;  // in mm
int wheel_diamiter2 = 101;  // in mm


const unsigned long INTERVAL_US = 1000;  // 1000 µs = 1 ms
unsigned long previousMicros = 0;

MPU6050 mpu;

TaskHandle_t imuTaskHandle = nullptr;

// MPU interrupt: wake the IMU task. No polling, no delay needed.
void IRAM_ATTR dmpDataReady() {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(imuTaskHandle, &woken);
  if (woken) portYIELD_FROM_ISR();
}

uint8_t fifoBuffer[64];
Quaternion q;
VectorFloat gravity;
VectorInt16 gy;  // [x, y, z] gyro sensor measurements
float ypr[3];
bool dmpReady = false;

// All IMU work lives here, pinned to core 0.
void imuTask(void *arg) {
  if (otaInProgress == false) {
    for (;;) {
      // Sleep until the MPU interrupt fires. The 100 ms timeout recovers from a
      // missed interrupt, and the idle task always gets to run so the watchdog is happy.
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

void setup() {
  // running this code pulled from the .cpp to avoid waiting 2.5s esc if it were to call the funtion twice
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

  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);
  if (!crsfSerial)
    while (1) Serial.println("Invalid crsfSerial configuration");

  crsf.begin(crsfSerial);             // defaults to CRSF_ADDRESS_FLIGHT_CONTROLLER
  crsf.setDeviceName("AlfredoCRSF");  // answer device discovery pings

  for (int i = 0; i < RPnumReadings; i++) RPreadings[i] = 0;
  for (int i = 0; i < YnumReadings; i++) Yreadings[i] = 0;

  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 not found - check wiring/address");
    while (1) delay(1000);
  }

  uint8_t devStatus = mpu.dmpInitialize();  // uploads DMP firmware (~1.9 KB)

  // supply your own gyro offsets here, scaled for min sensitivity
  mpu.setXGyroOffset(-234);
  mpu.setYGyroOffset(24);
  mpu.setZGyroOffset(-9);
  mpu.setXAccelOffset(-698);
  mpu.setYAccelOffset(476);
  mpu.setZAccelOffset(2538);

  if (devStatus == 0) {
    // mpu.CalibrateAccel(6);  // keep the sensor flat and still during this
    // mpu.CalibrateGyro(6);
    mpu.setDMPEnabled(true);

    // Create the task BEFORE attaching the interrupt: the ISR needs a valid handle.
    xTaskCreatePinnedToCore(
      imuTask,         // task function
      "imu_0",         // name
      4096,            // stack size (bytes)
      nullptr,         // parameter
      2,               // priority
      &imuTaskHandle,  // task handle (not needed)
      0                // core 0
    );

    xTaskCreatePinnedToCore(
      ledTask,    // task function
      "ledTask",  // name
      4096,       // stack size (bytes)
      NULL,       // parameter
      1,          // priority
      NULL,       // task handle (not needed)
      0           // core 0
    );


    pinMode(INT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), dmpDataReady, RISING);

    dmpReady = true;
    Serial.println("DMP ready");
  } else {
    // 1 = memory load failed, 2 = DMP config update failed
    Serial.printf("DMP init failed (code %d)\n", devStatus);
    while (1) delay(1000);
  }

  CpuMonitorConfig cpucfg;
  cpucfg.sampleIntervalMs = 10;
  cpucfg.calibrationSamples = 5;     // first 5 windows = "100% idle" baseline
  cpucfg.enablePerCore = true;       // separate core0/core1 values
  cpucfg.enableTemperature = false;  // not needed here

  if (!cpuMonitor.init(cpucfg)) {
    Serial.println("CPU monitor init failed");
    return;
  }

  cpuMonitor.onSample([](const CpuUsageSample &s) {
    core0 = s.perCore[0];
    core1 = s.perCore[1];
    coreA = s.average;
  });





  Serial.println("\nConfiguring Access Point...");

  WiFi.mode(WIFI_AP);

  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Failed to configure AP IP address");
  }
  WiFi.softAP(ssid, password);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // Initialize WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Serve static HTML page
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
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      otaInProgress = false;
      Serial.println("\nEnd");
      // ESP.restart();
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


  // wait for boot and set leds to booting colour
  for (unsigned long start = millis(); millis() - start < 2000;) {
  }

  Serial.begin(115200);
  esc2.begin(PIN_ESC1, DSHOT600, true, MOTOR_POLES);
  esc1.begin(PIN_ESC2, DSHOT600, true, MOTOR_POLES);

  // wait for ESC to boot before enabling EDT
  for (unsigned long start = millis(); millis() - start < 5000;) {
    unsigned long currentMicros = micros();

    if (currentMicros - previousMicros >= INTERVAL_US) {
      previousMicros += INTERVAL_US;  // drift-free
      esc1.sendThrottle(0.f);         // 0 %
      esc2.sendThrottle(0.f);         // 0 %
    }
  }

  // enable EDT
  esc1.command(DSHOT_CMD_EDT_ENABLE);
  esc2.command(DSHOT_CMD_EDT_ENABLE);
}

void loop() {

  ArduinoOTA.handle();

  if (otaInProgress) {
    return;  // skip all your other code while updating
  }

  unsigned long now = millis();
  if (now - lastUpdate > 100) {
    lastUpdate = now;

    // Use your existing, comprehensive JSON serializer
    String jsonString = getSerializedData();

    // Send to all connected clients instantly over WebSockets
    ws.textAll(jsonString);
  }

  crsf.update();
  static uint32_t next = micros();
  static uint32_t lastPrint = 0;

  unsigned long currentMicros = micros();

  if (currentMicros - previousMicros >= INTERVAL_US) {
    previousMicros += INTERVAL_US;  // drift-free
    esc1.sendThrottle(0.08f);       // 8 %
    esc2.sendThrottle(0.08f);       // 8 %
  }

  // server.handleClient();

  // Announce ourselves to the CRSF router once per second
  if (millis() - lastHeartbeatMs > 1000) {
    lastHeartbeatMs = millis();
    crsf.sendHeartbeat();

    esc1.stats();
    esc2.stats();

    crsf_sensor_battery_t crsfBatt = { 0 };

    crsfBatt.voltage = htobe16((uint16_t)(esc1.voltage() * 10.0));  //Volts
    crsfBatt.current = htobe16((uint16_t)(esc2.voltage() * 10.0));  //Amps
    crsfBatt.capacity = htobe24((uint32_t)(esc1.temperatureC()));   //mAh (24 bit field, max 16777215mAh)
    crsfBatt.remaining = (uint8_t)(esc2.temperatureC());            //percent
    crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_BATTERY_SENSOR, &crsfBatt, sizeof(crsfBatt));
  }
  // Native Station AP RSSI Logic
  wifi_sta_list_t wifi_sta_list;
  esp_wifi_ap_get_sta_list(&wifi_sta_list);

  if (wifi_sta_list.num > 0) {
    rssi = wifi_sta_list.sta[0].rssi;  // Note: added missing array index '[0]' to target first device
  } else {
    rssi = -127;
  }

  // Scale the signal
  rssiPercentage = getSignalQuality(rssi);

  // Core Variable Simulations
  liveCounter++;
  temperature += random(-10, 11) / 100.0;
  humidity = RP;
  batteryVoltage = esc1.voltage();

  if (liveCounter % 50 == 0) {
    statusString = (statusString == "Optimal") ? "Warning" : "Optimal";
  }

  String currentData = getSerializedData();

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
    "%d %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h   |",
    (throttle1 - 1500) / 5,
    esc1.voltage(),
    esc1.current(),
    esc1.temperatureC(),
    (esc1.rpm() / gearbox_ratio1),
    (wheel_diamiter1 / 10) * (esc1.rpm() / gearbox_ratio1) * 0.001885);
  Serial.println(esc1.lossPercent());

  Serial.printf(
    "%d %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h   |",
    (throttle2 - 1500) / 5,
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