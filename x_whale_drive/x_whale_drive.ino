// boards
//espressif - arduino-esp32
// make sure this is 3.3.12

// libs
#include <Arduino.h>
#include <SPI.h>
#include <BlockNot.h>
#include <AlfredoDShot.h>  // make sure this is 1.1.0
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"
#include <AlfredoCRSF.h>  // make sure this is 2.4
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"

// defines
// crsf rx
#define PIN_RX 1
#define PIN_TX 2

#define SDA_PIN 6
#define SCL_PIN 7
#define INT_PIN 5

// leds
#define LED_PIN_MOSI 3  // mosi This becomes the SPI MOSI pin (any GPIO works via the GPIO matrix)
#define LED_PIN_SCK 4   // sck
#define NUM_LEDS 12
#define SPI_FREQ 3200000  // 3.2 MHz
SPIClass ledSPI(FSPI);
#define RESET_BYTES 64  // 64 bytes * 8 bits * 312.5 ns = ~160 us of low
static uint8_t spiBuf[NUM_LEDS * 12 + RESET_BYTES];
bool led_arry[26] = { 0 };


// Add near the top with the other config
IPAddress local_IP(192, 168, 10, 1);  // The ESP32's address
IPAddress gateway(192, 168, 10, 1);   // Same as local_IP for an AP
IPAddress subnet(255, 255, 255, 0);

// 1. Assign Access Point Network Credentials
const char *ssid = "Whale";
const char *password = "Whale#1234";

// 2. Initialize Servers
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// 3. Variables to Monitor Live
int liveCounter = 0;
float temperature = 24.5;
float humidity = 55.0;
int rssi = -127;
int rssiPercentage = 0;  // New variable to store 0-100% scale
float batteryVoltage = 4.2;
String statusString = "Optimal";

unsigned long lastUpdate = 0;

// Helper function to scale dBm to a reliable 0-100% signal metric
int getSignalQuality(int dbm) {
  if (dbm == -127) return 0;   // Disconnected state
  if (dbm >= -50) return 100;  // Excellent signal is capped at 100%
  if (dbm <= -100) return 0;   // Unusable signal is capped at 0%

  // Linearly map the range [-100, -50] onto [0, 100]
  return map(dbm, -100, -50, 0, 100);
}

// iPhone-Optimized iOS UI Dashboard
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no, viewport-fit=cover">
    <title>Whale Stats</title>
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
        <h1>Whale Stats</h1>
        <div class="list-group">
            <div class="row-item"><div class="label">Live Counter</div><div class="value" id="v-counter">0</div></div>
            <div class="row-item"><div class="label">Temperature</div><div class="value" id="v-temp">0.0 °C</div></div>
            <div class="row-item"><div class="label">Humidity</div><div class="value" id="v-hum">0.0 %</div></div>
            <div class="row-item"><div class="label">Signal Strength</div><div class="value" id="v-rssi">-- %</div></div>
            <div class="row-item"><div class="label">Battery</div><div class="value" id="v-bat">0.00 V</div></div>
            <div class="row-item"><div class="label">System Status</div><div class="value" id="v-status">-</div></div>
        </div>
        <div class="status-footer" id="ws-status">Connecting to network...</div>
    </div>
    <script>
        const socket = new WebSocket('ws://' + window.location.hostname + ':81/');
        socket.onopen = () => {
            const footer = document.getElementById('ws-status');
            footer.innerText = 'CONNECTED TO WHALE';
            footer.style.color = '#30d158';
        };
        socket.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                document.getElementById('v-counter').innerText = data.counter;
                document.getElementById('v-temp').innerText = data.temp.toFixed(1) + ' °C';
                document.getElementById('v-hum').innerText = data.hum.toFixed(1) + ' %';
                
                // Read percentage directly from WebSocket bundle
                document.getElementById('v-rssi').innerText = (data.rssi_raw === -127) ? "Disconnected" : data.rssi_pct + '%';
                
                document.getElementById('v-bat').innerText = data.bat.toFixed(2) + ' V';
                const statusEl = document.getElementById('v-status');
                statusEl.innerText = data.status;
                statusEl.style.color = (data.status === "Optimal") ? '#30d158' : '#ff9f0a';
            } catch(e) { console.error("JSON parsing fault", e); }
        };
        socket.onclose = () => {
            const footer = document.getElementById('ws-status');
            footer.innerText = 'DISCONNECTED';
            footer.style.color = '#ff453a';
        };
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

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_CONNECTED) {
    String initialJson = getSerializedData();
    webSocket.sendTXT(num, initialJson);
  }
}

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

// imu data
// dmp orientation
int roll = 0;
int pitch = 0;
int yaw = 0;

// imu alive flag
int no_imu = 0;

// combined angle flip threshhold
int roll_flip = 70;

// smoothing for dmp orientation
const int RPnumReadings = 50;
int RPreadings[RPnumReadings];
int RPreadIndex = 0;
int RPtotal = 0;
int RP = 0;

// smoothing for yaw gyro
const int YnumReadings = 5;
int Yreadings[YnumReadings];
int YreadIndex = 0;
int Ytotal = 0;
int Y = 0;

// more imu stuff
MPU6050 mpu;

volatile bool mpuInterrupt = false;
void IRAM_ATTR dmpDataReady() {
  mpuInterrupt = true;
}

uint8_t fifoBuffer[64];
Quaternion q;
VectorFloat gravity;
VectorInt16 gy;  // [x, y, z]            gyro sensor measurements
float ypr[3];
bool dmpReady = false;

//  these esc and rx things
AlfredoDShot esc1;
AlfredoDShot esc2;
HardwareSerial crsfSerial(1);
AlfredoCRSF crsf;

//  esc varibles
const int PIN_ESC1 = 8;
const int PIN_ESC2 = 9;
const uint8_t MOTOR1_POLES = 14;
const uint8_t MOTOR2_POLES = 14;
const int esc_arm_duration = 5000;
int throttle1 = 0;
int throttle2 = 0;
int drive1_pwm = 0;
int drive2_pwm = 0;
int gearbox_ratio1 = 19;
int gearbox_ratio2 = 19;
int wheel_diamiter1 = 101;  // in mm
int wheel_diamiter2 = 101;  // in mm

// set up non blocking timers
BlockNot dshot_freq(1000, MICROSECONDS);
BlockNot crsf_telem_freq(100);
BlockNot led_freq(10);
BlockNot serial_freq(500);
BlockNot rainbow_freq(2);
BlockNot crsf_tx_freq(1000);

// rx channel array
int ch[10] = { 1500 };

// pre arm safe varibles
bool safe_switch = 0;
bool safe_sticks = 0;
bool safe_link = 0;
bool safed = 0;

// leds
// configure leds

void setup() {

  // configure leds, sneak this in beofre esc's so we can use them to show status
  // begin(sck, miso, mosi, ss) -> only MOSI is used
  ledSPI.begin(LED_PIN_SCK, -1, LED_PIN_MOSI, -1);
  led(1, 1);

  // configure escs, must be done very close to first. check esc lib documenation for details
  boot_esc();

  // configure serial
  Serial.begin(921600);
  // make sure usb cdc on boot is enabled or this will throw an error
  Serial.setTxTimeoutMs(0);
  Serial.println("Whale booted");

  // configure rx and imu
  boot_crsf();
  boot_imu();

  Serial.println("\nConfiguring Access Point...");

  WiFi.mode(WIFI_AP);

  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Failed to configure AP IP address");
  }
  WiFi.softAP(ssid, password);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("Servers initialized smoothly.");

  //start the pre arm safe checks
  pre_arm_safe();
}

void loop() {

  // read rx data
  crsf_rx();
  // read imu data
  imu();
  // invoke the mixer
  mixer(ch[1], ch[2]);
  // process stabilisation
  // pid();
  // send commands to the esc;s
  drive_esc(drive1_pwm, drive2_pwm);
  // send telem over rc link
  crsf_tx();
  // drive leds
  led(0, 0);
  // print out debug messages
  // serial_debug(2);


  server.handleClient();
  webSocket.loop();

  if (millis() - lastUpdate > 100) {
    lastUpdate = millis();

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
    humidity += random(-20, 21) / 100.0;
    batteryVoltage = 3.7 + (sin(millis() / 5000.0) * 0.5);

    if (liveCounter % 50 == 0) {
      statusString = (statusString == "Optimal") ? "Warning" : "Optimal";
    }

    String currentData = getSerializedData();
    webSocket.broadcastTXT(currentData);
  }
}
