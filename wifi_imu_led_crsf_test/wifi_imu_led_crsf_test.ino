#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"
#include <WiFi.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"

#include <AlfredoCRSF.h>
#include <HardwareSerial.h>

#define PIN_RX 20
#define PIN_TX 21

HardwareSerial crsfSerial(1);
AlfredoCRSF crsf;


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
            <div class="row-item"><div class="label">System Orientation</div><div class="value" id="v-status">-</div></div>
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
                statusEl.style.color = (data.status === "Right Side Up") ? '#30d158' : '#ff9f0a';
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


#define SDA_PIN 8
#define SCL_PIN 9
#define INT_PIN 10

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>  // Required for 16 MHz Adafruit Trinket
#endif

Adafruit_NeoPixel pixels(2, 4, NEO_GRB + NEO_KHZ800);

int roll = 0;
int pitch = 0;
int yaw = 0;

int roll_flip = 70;

const int RPnumReadings = 50;
int RPreadings[RPnumReadings];  // the readings from the analog input
int RPreadIndex = 0;            // the index of the current reading
int RPtotal = 0;                // the running total
int RP = 0;                     // the average

const int YnumReadings = 5;
int Yreadings[YnumReadings];  // the readings from the analog input
int YreadIndex = 0;           // the index of the current reading
int Ytotal = 0;               // the running total
int Y = 0;                    // the average

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

void setup() {
 // Serial.begin(921600);

  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);
  if (!crsfSerial) while (1) Serial.println("Invalid crsfSerial configuration");

  crsf.begin(crsfSerial); // defaults to CRSF_ADDRESS_FLIGHT_CONTROLLER

  delay(2000);
  Serial.println("\nConfiguring Access Point...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  server.on("/", handleRoot);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("Servers initialized smoothly.");


  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);



  for (int RPthisReading = 0; RPthisReading < RPnumReadings; RPthisReading++) {
    RPreadings[RPthisReading] = 0;
  }
  for (int YthisReading = 0; YthisReading < YnumReadings; YthisReading++) {
    Yreadings[YthisReading] = 0;
  }


  pixels.begin();  // INITIALIZE NeoPixel strip object (REQUIRED)
  pixels.clear();  // Set all pixel colors to 'off'


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
    pinMode(INT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), dmpDataReady, RISING);
    dmpReady = true;
    Serial.println("DMP ready");
  } else {
    // 1 = memory load failed, 2 = DMP config update failed
    Serial.printf("DMP init failed (code %d)\n", devStatus);
    while (1) delay(1000);
  }
}

void loop() {
  if (!dmpReady || !mpuInterrupt) return;
  mpuInterrupt = false;

  if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    //Serial.printf("ypr: %.1f  %.1f  %.1f\n",
    //              ypr[0] * 180 / M_PI, ypr[1] * 180 / M_PI, ypr[2] * 180 / M_PI);

    if (ypr[1] * 180 / M_PI < 0) {
      pitch = ypr[1] * -180 / M_PI;
    } else {
      pitch = ypr[1] * 180 / M_PI;
    }

    if (ypr[2] * 180 / M_PI < 0) {
      roll = ypr[2] * -180 / M_PI;
    } else {
      roll = ypr[2] * 180 / M_PI;
    }



    RPtotal = RPtotal - RPreadings[RPreadIndex];
    if ((roll + pitch) > (roll_flip * 2)) {
      RPreadings[RPreadIndex] = roll_flip * 2;
    } else {
      RPreadings[RPreadIndex] = roll + pitch;
    }
    RPtotal = RPtotal + RPreadings[RPreadIndex];
    RPreadIndex = RPreadIndex + 1;

    if (RPreadIndex >= RPnumReadings) {
      RPreadIndex = 0;
    }
    RP = RPtotal / RPnumReadings;

    Serial.print(RP);

    Serial.print("\t");
    mpu.dmpGetGyro(&gy, fifoBuffer);
    if (gy.z < 0) {
      yaw = gy.z * -1;
    } else {
      yaw = gy.z * 1;
    }

    Ytotal = Ytotal - Yreadings[YreadIndex];
    Yreadings[YreadIndex] = yaw;
    Ytotal = Ytotal + Yreadings[YreadIndex];
    YreadIndex = YreadIndex + 1;

    if (YreadIndex >= YnumReadings) {
      YreadIndex = 0;
    }
    Y = Ytotal / YnumReadings;

    Serial.println(Y);
  }

  if (Y > 1400) {
    Y = 1400;
  }

  if (RP > roll_flip) {
    pixels.setPixelColor(0, pixels.Color(Y * .175 + 5, 0, 0));
    pixels.setPixelColor(1, pixels.Color(Y * .175 + 5, 0, 0));
    pixels.setPixelColor(2, pixels.Color(Y * .175 + 5, 0, 0));
    statusString = "Up Side Down";
  } else {
    pixels.setPixelColor(0, pixels.Color(0, Y * .175 + 5, 0));
    pixels.setPixelColor(1, pixels.Color(0, Y * .175 + 5, 0));
    pixels.setPixelColor(2, pixels.Color(0, Y * .175 + 5, 0));
    statusString = "Right Side Up";
  }
  pixels.show();
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
    temperature = crsf.getChannel(3);
    humidity += random(-20, 21) / 100.0;
    batteryVoltage = 3.7 + (sin(millis() / 5000.0) * 0.5);
    String currentData = getSerializedData();
    webSocket.broadcastTXT(currentData);
  }

crsf.update();

  Serial.print("link: ");
  Serial.print(crsf.isLinkUp() ? "UP" : "DOWN");
  const crsfLinkStatistics_t *link = crsf.getLinkStatistics();
  Serial.print("  LQ: ");
  Serial.print(link->uplink_Link_quality);
  Serial.print("  RSSI: -");
  Serial.print(link->active_antenna == 0 ? link->uplink_RSSI_1 : link->uplink_RSSI_2);
  Serial.print("dBm  armed: ");
  Serial.print(crsf.isArmed() ? "yes" : "no");

  Serial.print("channels 1-8:");
  for (int i = 1; i <= 8; i++)
  {
    Serial.print(" ");
    Serial.print(crsf.getChannel(i));
  }
  Serial.println("");


}