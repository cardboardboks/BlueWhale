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

void web() {
  // once a second, drop dead/stalled web clients so their queued messages don't pile up in RAM
  static uint32_t lastCleanupMs = 0;
  if (millis() - lastCleanupMs >= 1000) {
    lastCleanupMs = millis();
    ws.cleanupClients();
    applyWifiPower();  // WiFi Power box changed?
    dbgMaxLoopGapUs = maxGapUs;
    uint32_t pMin = escPeriodMinUs, pMax = escPeriodMaxUs;
    escShowTickMs = escMaxUs / 1000.0f;  // last second's DShot timing, shown on the Bulk Text ESC line (status.ino)
    escShowMinMs = (pMin == 0xFFFFFFFFUL) ? 0.0f : pMin / 1000.0f;
    escShowMaxMs = pMax / 1000.0f;
    {  // each ESC's reply results over the last second (from the library's counters)
      static uint32_t pOk[MAX_ESC], pNr[MAX_ESC], pFr[MAX_ESC], pGcr[MAX_ESC], pCrc[MAX_ESC];
      for (int i = 0; i < NUM_ESC; i++) {
        uint32_t ok = escStOk[i], nr = escStNoRep[i], fr = escStFrame[i], gc = escStGcr[i], cr = escStCrc[i];
        escSecOk[i] = ok - pOk[i];
        escSecNr[i] = nr - pNr[i];
        escSecFr[i] = fr - pFr[i];
        escSecGcr[i] = gc - pGcr[i];
        escSecCrc[i] = cr - pCrc[i];
        pOk[i] = ok; pNr[i] = nr; pFr[i] = fr; pGcr[i] = gc; pCrc[i] = cr;
      }
    }
    uint32_t skipped = wsSkipped;
    wsSkipped = 0;
    snprintf(dbgLine, sizeof(dbgLine), "heap %uk min %uk blk %uk \ngap %.1fms json %.1fms %ub skip %u | ws %u\nimu read %.1fms late %u  cnt %.1fms\nfifo %u multi %u  bad %u (on late reads %u) last qn %.2f\nI2C err %u rec %u ovf %u",
             (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024),
             maxGapUs / 1000.0f, jsonMaxUs / 1000.0f, (unsigned)wsLastBytes, (unsigned)skipped, (unsigned)ws.count(),
             imuReadMaxUs / 1000.0f, (unsigned)imuLateReads, imuCountMaxUs / 1000.0f, (unsigned)imuFifoMax, (unsigned)imuMultiPkt, (unsigned)imuBadPackets, (unsigned)imuBadOnLate, (float)imuLastBadQn, (unsigned)imuI2cErrors, (unsigned)imuRecoveries, (unsigned)imuOverflows);
    imuReadMaxUs = 0;
    imuCountMaxUs = 0;
    imuFifoMax = 0;
    maxGapUs = 0;
    jsonMaxUs = 0;
    escMaxUs = 0;
    escPeriodMinUs = 0xFFFFFFFFUL;
    escPeriodMaxUs = 0;
  }

  unsigned long now = millis();
  if (now - lastUpdate >= WEB_UPDATE_MS) {
    lastUpdate = now;

    state();
    if (!imuAlive()) imuOrient = 0;  // IMU dropout: blank the up/down arrow instead of showing a stale value

    if (ws.count() > 0) {  // nobody watching -> don't build the JSON at all
      // If the last update hasn't gone out yet (slow phone / WiFi), skip this one rather than queue it: a full
      // queue makes the web server close the connection, which the page shows as STALE until it reconnects.
      if (!ws.availableForWriteAll()) {
        wsSkipped++;
      } else {
        static uint8_t n = 0;
        bool full = (++n >= 10);  // tuning values + diagnostics in every 10th update only (2 a second)
        if (full) n = 0;
        uint32_t t0 = micros();
        String msg = getSerializedData(full);
        wsLastBytes = msg.length();
        ws.textAll(msg);
        uint32_t d = micros() - t0;
        if (d > jsonMaxUs) jsonMaxUs = d;
      }
    }
  }
}

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
  float rpmOut = escRpm(i) / gearbox_ratio[i];
  o["thr"] = escThrPct(i);  // an unset output (0) works out to -300 %, shown as 0 %
  o["v"] = escVolts(i);
  o["a"] = escCurrent(i);
  o["t"] = escTempC(i);
  o["rpm"] = rpmOut;
  o["spd"] = escSpeed(i);
  o["link"] = escLossPct(i);  // % of frames with no telemetry reply since boot / the last reconnect
}

// full = include the tuning values and the diagnostics line. They change rarely, so the regular updates leave them
// out (the page keeps the last ones it got) - that keeps each update small. Connecting always gets a full one.
String getSerializedData(bool full) {
  JsonDocument doc;

  doc["mode"] = modeString;
  doc["boot"] = bootState;
  doc["bulk"] = buildBulkText();  // built here, only when the page is actually sent (status.ino)

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
      sumV += escVolts(i);
      sumA += escCurrent(i);
      nEsc++;
    }
  }
  JsonObject avg = doc["avg"].to<JsonObject>();
  avg["n"] = nEsc;
  if (vbatValid()) avg["v"] = (float)vbat;    // battery divider: valid with the motors stopped
  else if (nEsc > 0) avg["v"] = sumV / nEsc;  // no divider fitted: fall back to the ESC average
  if (nEsc > 0) avg["a"] = sumA;              // total current of the reporting ESCs (not averaged); the "a" key name is just historical

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
  if (full) doc["dbg"] = dbgLine;                             // built once a second in loop() (walking the heap every update would be wasteful)
  doc["up"] = (uint32_t)(esp_timer_get_time() / 1000000ULL);  // seconds since boot

  // Tuning values (full updates only)
  if (full) {
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
  if (imuLevelled) imu["tilt"] = (float)imuTiltDeg;  // degrees from the levelled upright (left out until levelled)
  imu["lvl"] = (bool)imuLevelled;
  JsonObject cal = imu["cal"].to<JsonObject>();  // calibration status for the Tuning page
  cal["offs"] = (bool)imuOffsValid;
  cal["acal"] = (bool)accCalValid;
  cal["sides"] = (int)sideMask;
  cal["n"] = (int)calN;
  if (accCalValid) cal["rms"] = (float)accCalRms;
  cal["busy"] = imuCalOffReq || imuCalSideReq || imuCalFactoryReq || imuCalGyroReq;
  cal["msg"] = (const char *)imuCalMsg;                     // false = never levelled: the page shows NOT LEVELLED
  imu["ud"] = (int)imuOrient;     // up/down arrow
  imu["lr"] = (int)imuDirection;  // left/right arrow
  imu["ok"] = imuAlive();         // false = IMU dropout (no good packet lately); the page shows LOST and greys the numbers

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
  JsonArray help = doc["help"].to<JsonArray>();  // text for the ? next to each box
  for (int i = 0; i < NUM_TUNE; i++) help.add(tuneHelp[i]);
  doc["groups"] = tuneGroups;  // how the boxes are split into sections
  JsonArray choice = doc["choice"].to<JsonArray>();  // "" = number box, else the dropdown's options
  for (int i = 0; i < NUM_TUNE; i++) choice.add(tuneChoice[i] ? tuneChoice[i] : "");
  String output;
  serializeJson(doc, output);
  return output;
}

void onWsEvent(AsyncWebSocket *s, AsyncWebSocketClient *client, AwsEventType type,
               void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    client->text(getSerializedData(true));  // send current values immediately (full: tuning + diagnostics too)
    client->text(getTuneNames());
  } else if (type == WS_EVT_DATA) {
    // the page's "Reset" button sends the text "reset"
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      if (len == 5 && memcmp(data, "reset", 5) == 0) resetRecords();
      // IMU calibration buttons (Tuning page): locked while armed, and need a working IMU
      else if (len >= 7 && memcmp(data, "cal_", 4) == 0) {
        if (armSwitchHigh()) client->text("{\"blocked\":\"cal\"}");
        else if (len == 7 && memcmp(data, "cal_clr", 7) == 0) imuCalClearReq = true;
        else if (!imuAlive()) client->text("{\"blocked\":\"imu\"}");
        else if (len == 7 && memcmp(data, "cal_off", 7) == 0) imuCalOffReq = true;
        else if (len == 7 && memcmp(data, "cal_fac", 7) == 0) imuCalFactoryReq = true;
        else if (len == 8 && memcmp(data, "cal_side", 8) == 0) imuCalSideReq = true;
        else if (len == 8 && memcmp(data, "cal_gyro", 8) == 0) imuCalGyroReq = true;
      }
      // "Level" button: robot sitting flat and right way up -> that becomes the IMU's upright direction
      else if (len == 5 && memcmp(data, "level", 5) == 0) {
        if (armSwitchHigh()) client->text("{\"blocked\":\"level\"}");
        else if (!imuAlive()) client->text("{\"blocked\":\"imu\"}");
        else imuLevelReq = true;
      }
      else if (len == 6 && memcmp(data, "reboot", 6) == 0) {
        if (rebootBlocked()) {
          client->text("{\"blocked\":\"reboot\"}");  // armed: refuse, and tell the page
        } else {
          modeString = "Rebooting";
          rebootAtMs = millis() + 500;
          rebootPending = true;
        }
      }
      // "Load defaults" button: every number box back to tuneDefault[] (text boxes - Name, WiFi - are kept)
      else if (len == 8 && memcmp(data, "defaults", 8) == 0) {
        if (armSwitchHigh()) {
          client->text("{\"blocked\":\"params\"}");
        } else {
          for (int i = NUM_TUNE_STR; i < NUM_TUNE; i++)
            if (!yawKeepOnDefaults(i)) tuneVal[i] = tuneDefault[i];  // calibrated values (Gyro Scale) are kept
          tuneChangedMs = millis();
          tuneDirty = true;  // saved to flash shortly after, like any edit
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
  ws.textAll(getSerializedData(true));
  ws.cleanupClients();
}

// WiFi transmit power from the WiFi Power box; only touches the radio when the setting changes. Called from web().
void applyWifiPower() {
  static int last = -1;
  int v = (int)lroundf(tuneVal[T_WIFI_POWER]);
  if (v != 1 && v != 2) v = 3;
  if (v == last) return;
  last = v;
  WiFi.setTxPower(v == 1 ? WIFI_POWER_8_5dBm : v == 2 ? WIFI_POWER_15dBm : WIFI_POWER_19_5dBm);
}

// true if channel 5 is held high at power-up (transmitter on): start with the default WiFi network
bool wifiDefaultsHeld() {
  for (uint32_t t0 = millis(); millis() - t0 < 600;) {  // the CRSF task is already reading the receiver
    if (crsf.isLinkUp() && txchannel[ESC_REBOOT_CH] > ESC_REBOOT_US) return true;
    delay(5);
  }
  return false;
}

// Start the WiFi access point, the web server and the WebSocket. Called once from setup().
void webBegin() {
  Serial.println("\nConfiguring Access Point...");

  WiFi.mode(WIFI_AP);

  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Failed to configure AP IP address");
  }
  // network name / password from the SSID and Password boxes (empty or invalid = the defaults above)
  const char *apSsid = tuneStr[2][0] ? tuneStr[2] : ssid;
  size_t pl = strlen(tuneStr[3]);
  const char *apPass = (pl >= 8 && pl <= 63) ? tuneStr[3] : password;
  if (pl > 0 && pl < 8) Serial.println("WiFi password box is under 8 characters - using the default");
  if (wifiDefaultsHeld()) {  // way back in if the saved network is forgotten
    apSsid = ssid;
    apPass = password;
    Serial.println("Channel 5 held at power-up: default WiFi network this boot");
  }
  WiFi.softAP(apSsid, apPass);
  Serial.printf("WiFi network: %s\n", apSsid);
  applyWifiPower();

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
