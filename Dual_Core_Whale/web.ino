// ================================================================
//  Web dashboard: WiFi access point, web server, WebSocket, JSON
// ================================================================
// This tab is joined onto the end of the main sketch at compile time, so it can use every variable
// declared there (modeString, escs, txchannel, ws, server, ...). The page itself lives in web_page.h.

#include "web_page.h"

// ---- WiFi access point settings ----
IPAddress local_IP(192, 168, 10, 1);
IPAddress gateway(192, 168, 10, 1);
IPAddress subnet(255, 255, 255, 0);

const char *ssid = "Whale";
const char *password = "Whale#1234";

// Scale dBm to 0-100 %
int getSignalQuality(int dbm) {
  if (dbm == -127) return 0;
  if (dbm >= -50) return 100;
  if (dbm <= -100) return 0;
  return map(dbm, -100, -50, 0, 100);
}

static const crsfLinkStatistics_t emptyLink = {};  // all zeros, used if the library ever returns no stats

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

void addEsc(JsonArray arr, int i, bool ok) {
  JsonObject o = arr.add<JsonObject>();
  o["ok"] = ok;
  if (!ok) return;  // absent ESC -> page leaves its column blank
  float rpmOut = escs[i].rpm() / gearbox_ratio[i];
  o["thr"] = escThrPct(i);  // an unset output (0) works out to -300 %, shown as 0 %
  o["v"] = escs[i].voltage();
  o["a"] = escCurrent(i);
  o["t"] = escs[i].temperatureC();
  o["rpm"] = rpmOut;
  o["spd"] = escSpeed(i);
  o["link"] = (float)escs[i].lossPercent();  // adjust if you want a different "link" metric
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
      sumA += escCurrent(i);
      nEsc++;
    }
  }
  JsonObject avg = doc["avg"].to<JsonObject>();
  avg["n"] = nEsc;
  if (nEsc > 0) {
    avg["v"] = sumV / nEsc;
    avg["a"] = sumA;  // total current of the reporting ESCs (not averaged); the "a" key name is just historical
  }

  // Receiver
  JsonObject rx = doc["rx"].to<JsonObject>();
  const crsfLinkStatistics_t *link = crsf.getLinkStatistics();
  if (link == nullptr) link = &emptyLink;  // never dereference a null stats pointer
  int dbm = -(int)(link->active_antenna == 0 ? link->uplink_RSSI_1 : link->uplink_RSSI_2);
  rx["up"] = crsf.isLinkUp();
  rx["lq"] = link->uplink_Link_quality;
  rx["rssi"] = getSignalQuality(dbm);
  rx["dbm"] = dbm;
  JsonArray ch = rx["ch"].to<JsonArray>();
  for (int i = 1; i <= 8; i++) ch.add(txchannel[i]);

  doc["wifi"] = wifiRssi();
  doc["armed"] = armSwitchHigh();                             // the page greys out the tuning boxes while true
  doc["noreboot"] = rebootBlocked();                          // ...and the Reboot button while this is true (armed and link up)
  doc["dbg"] = dbgLine;                                       // built once a second in loop() (walking the heap every update would be wasteful)
  doc["up"] = (uint32_t)(esp_timer_get_time() / 1000000ULL);  // seconds since boot

  // Tuning values
  JsonArray tune = doc["tune"].to<JsonArray>();
  for (int i = 0; i < NUM_TUNE; i++) {
    if (i < NUM_TUNE_STR) {
      char txt[TUNE_STR_LEN];
      strlcpy(txt, tuneStr[i], TUNE_STR_LEN);  // snapshot, so the JSON gets a consistent copy
      tune.add(txt);                           // text variables go out as JSON strings
    } else {
      tune.add((float)tuneVal[i]);
    }
  }

  // Records
  JsonObject rec = doc["rec"].to<JsonObject>();
  rec["g"] = (float)recMaxG;
  rec["t"] = (float)recMaxTemp;
  rec["a"] = (float)recMaxA;
  rec["ta"] = (float)recMaxTotalA;
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
  imu["ud"] = (int)imuOrient;     // up/down arrow
  imu["lr"] = (int)imuDirection;  // left/right arrow

  // Bool array -> dots
  JsonArray arr = doc["arr"].to<JsonArray>();
  for (int i = 1; i < ARRAY_SIZE - 1; i++) arr.add(modeArray[i] ? 1 : 0);

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
        if (rebootBlocked()) {
          client->text("{\"blocked\":\"reboot\"}");  // armed: refuse, and tell the page
        } else {
          modeString = "Rebooting";
          rebootAtMs = millis() + 500;
          rebootPending = true;
        }
      }
      // armed: tuning values are locked, whatever the page sends
      else if (len > 4 && memcmp(data, "set,", 4) == 0 && armSwitchHigh()) {
        client->text("{\"blocked\":\"params\"}");
      }
      // tuning box edited on the page: "set,<index>,<value>"
      // numbers: "set,<index>,<number>"   text (the first NUM_TUNE_STR): "set,<index>,<any text, commas allowed>"
      else if (len > 4 && len < 64 && memcmp(data, "set,", 4) == 0) {
        char buf[64];
        memcpy(buf, data, len);
        buf[len] = 0;
        char *end;
        long idx = strtol(buf + 4, &end, 10);
        if (end != buf + 4 && *end == ',' && idx >= 0 && idx < NUM_TUNE) {
          const char *val = end + 1;
          bool changed = false;
          if (idx < NUM_TUNE_STR) {
            strlcpy(tuneStr[idx], val, TUNE_STR_LEN);
            changed = true;
          } else {
            char *numEnd;
            float f = strtof(val, &numEnd);
            if (numEnd != val) {
              tuneVal[idx] = f;
              changed = true;
            }
          }
          if (changed) {
            tuneChangedMs = millis();
            tuneDirty = true;  // loop() saves it to flash shortly after
          }
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

// Start the WiFi access point, the web server and the WebSocket. Called once from setup().
void webBegin() {
  Serial.println("\nConfiguring Access Point...");

  WiFi.mode(WIFI_AP);

  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Failed to configure AP IP address");
  }
  WiFi.softAP(ssid, password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.begin();

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Servers initialized smoothly.");
}
