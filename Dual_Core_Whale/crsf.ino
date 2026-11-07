/*
 * AlfredoCRSF - custom CRSF telemetry, protocol v3 (4 ESC slots)
 *
 * Two custom frame types:
 *
 * 0x42 DATA  (35-byte payload, all big-endian)
 *   [0]      uint8   version (3)
 *   [1..2]   uint16  average voltage   mV   (battery divider if fitted, else average over ESCs that are reporting)
 *   [3..6]   int32   total current     cA (0.01 A)
 *   [7..10]  uint32  uptime            seconds
 *   then for ESC1..ESC4, 6 bytes each, starting at [11]:
 *     int16  temp      0.1 degC
 *     int16  current   cA
 *     uint16 voltage   mV
 *   ESCs that aren't fitted or aren't reporting are sent as 0.
 *
 * 0x43 STATUS (sent when a string changes, and every 1 s anyway)
 *   [0]      uint8   version (3)
 *   then 3 null-terminated strings: mode, boot, orientation (max 15 chars each)
 *
 * Decoded on the radio by /SCRIPTS/WHALE/whale_decode.lua
 * VI_PROTO_VERSION (main sketch) must be 3 to match the decoder.
 */

#if VI_PROTO_VERSION != 3
#error "VI_PROTO_VERSION must be 3 to match whale_decode.lua"
#endif

#define CRSF_TX_INTERVAL_MS 100    // one telemetry frame per 100 ms
#define CRSF_STATUS_EVERY_MS 500  // resend the strings at least this often

// ------------------------------------------------------------------
// Telemetry state - filled from the live values in readSensors()
// ------------------------------------------------------------------
struct EscTelem {
  float temp = 0;     // degC
  float current = 0;  // A
  float voltage = 0;  // V
};

struct Telemetry {
  char mode[VI_MAX_STR + 1] = "IDLE";
  char boot[VI_MAX_STR + 1] = "OK";
  char orientation[VI_MAX_STR + 1] = "--";
  float avgVoltage = 0;   // V
  float totalAmps = 0;    // A
  EscTelem esc[MAX_ESC];  // esc[0] = ESC1 ... esc[3] = ESC4
} telem;

// Copies a string into a telemetry field; marks the status frame for sending if it changed
bool statusDirty = true;
void setStatusString(char *dst, const char *src) {
  if (strncmp(dst, src, VI_MAX_STR) != 0) {
    strncpy(dst, src, VI_MAX_STR);
    dst[VI_MAX_STR] = '\0';
    statusDirty = true;
  }
}

static float finiteOr0(float x) {
  return isfinite(x) ? x : 0.0f;
}

// Live values -> telem. Same rules as the web page: only ESCs that are reporting count.
void readSensors() {
  float vSum = 0, iSum = 0;
  int n = 0;
  for (int k = 0; k < MAX_ESC; k++) {
    EscTelem &e = telem.esc[k];
    if (k < NUM_ESC && escPresent(k)) {
      e.voltage = finiteOr0(escs[k].voltage());
      e.current = finiteOr0(escCurrent(k));
      e.temp = finiteOr0(escs[k].temperatureC());
      vSum += e.voltage;
      iSum += e.current;
      n++;
    } else {
      e.voltage = e.current = e.temp = 0;  // not fitted / not reporting
    }
  }
  // battery divider when fitted (live with the motors stopped), otherwise the average of the reporting ESCs
  telem.avgVoltage = vbatValid() ? vbat : ((n > 0) ? vSum / n : 0);
  telem.totalAmps = iSum;

  setStatusString(telem.mode, modeString.c_str());
  setStatusString(telem.boot, bootState.c_str());
  const char *o = !imuAlive() ? "IMU Lost" : (imuOrient > 0) ? "Up" : (imuOrient < 0) ? "Down" : "--";
  setStatusString(telem.orientation, o);
}

// ---------- big-endian packing helpers ----------
static inline void put16(uint8_t *p, uint16_t v) {
  p[0] = v >> 8;
  p[1] = v;
}
static inline void put32(uint8_t *p, uint32_t v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}
static inline uint16_t u16(float x) {
  return (uint16_t)constrain(lroundf(x), 0L, 65535L);
}
static inline int16_t s16(float x) {
  return (int16_t)constrain(lroundf(x), -32768L, 32767L);
}
static inline int32_t s32(float x) {
  return (int32_t)lroundf(x);
}

struct __attribute__((packed)) EscPayload {
  uint8_t t_dC[2];
  uint8_t i_cA[2];
  uint8_t v_mV[2];
};

struct __attribute__((packed)) ViDataPayload {
  uint8_t version;
  uint8_t avgV_mV[2];
  uint8_t totalA_cA[4];
  uint8_t uptime_s[4];
  EscPayload esc[MAX_ESC];
};
static_assert(sizeof(ViDataPayload) == 35, "payload layout changed");

void sendData() {
  ViDataPayload p;
  p.version = VI_PROTO_VERSION;
  put16(p.avgV_mV, u16(telem.avgVoltage * 1000.0f));
  put32(p.totalA_cA, (uint32_t)s32(telem.totalAmps * 100.0f));
  put32(p.uptime_s, millis() / 1000UL);
  for (int k = 0; k < MAX_ESC; k++) {
    put16(p.esc[k].t_dC, (uint16_t)s16(telem.esc[k].temp * 10.0f));
    put16(p.esc[k].i_cA, (uint16_t)s16(telem.esc[k].current * 100.0f));
    put16(p.esc[k].v_mV, u16(telem.esc[k].voltage * 1000.0f));
  }
  crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_TYPE_VI_DATA, &p, sizeof(p));
}

static uint8_t appendString(uint8_t *buf, uint8_t idx, const char *s) {
  uint8_t n = 0;
  while (s[n] && n < VI_MAX_STR) buf[idx++] = (uint8_t)s[n++];
  buf[idx++] = 0;
  return idx;
}

void sendStatus() {
  uint8_t buf[1 + 3 * (VI_MAX_STR + 1)];  // max 49 bytes, fits in one CRSF frame
  uint8_t len = 0;
  buf[len++] = VI_PROTO_VERSION;
  len = appendString(buf, len, telem.mode);
  len = appendString(buf, len, telem.boot);
  len = appendString(buf, len, telem.orientation);
  crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_TYPE_VI_STATUS, buf, len);
  statusDirty = false;
}

// start crsf
void boot_crsf() {
}

// receive crsf data
void crsf_rx() {
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
}

// send crsf telem - call from loop() AND from any wait loop (e.g. pre_arm_safe)
void crsf_tx() {
  static uint32_t lastTxMs = 0;
  static uint32_t lastStatusMs = 0;

  if (millis() - lastTxMs < CRSF_TX_INTERVAL_MS) return;
  lastTxMs = millis();

  crsf.sendHeartbeat();
  for (int i = 0; i < NUM_ESC; i++) escs[i].stats();

  readSensors();
  if (!crsf.isLinkUp()) return;  // nothing to send it to

  // Strings take a slot when they change or once a second; data otherwise
  if (statusDirty || millis() - lastStatusMs >= CRSF_STATUS_EVERY_MS) {
    sendStatus();
    lastStatusMs = millis();
  } else {
    sendData();
  }
}
