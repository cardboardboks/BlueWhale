#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiAP.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"

// Add near the top with the other config
IPAddress local_IP(192, 168, 10, 1);  // The ESP32's address
IPAddress gateway(192, 168, 10, 1);   // Same as local_IP for an AP
IPAddress subnet(255, 255, 255, 0);

// 1. Assign Access Point Network Credentials
const char *ssid = "Whale";
const char *password = "Whale#1234";
uint32_t last_ota_time = 0;

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
    <title>Whale Stats 🐋</title>
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
        <h1>🐳 Whale Stats 🐋</h1>
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

void setup() {
  Serial.begin(115200);
  delay(2000);
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

  ArduinoOTA
    .onStart([]() {
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
}

void loop() {
  server.handleClient();
  webSocket.loop();

  ArduinoOTA.handle();

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