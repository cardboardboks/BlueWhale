#include "CRSFReceiver.h"

static constexpr uint8_t ADDR_FC       = 0xC8;  // flight controller (sync byte on RX frames)
static constexpr uint8_t ADDR_RADIO    = 0xEA;
static constexpr uint8_t ADDR_RX       = 0xEC;
static constexpr uint8_t ADDR_TX_MOD   = 0xEE;
static constexpr uint32_t INTERBYTE_GAP_US = 2000;  // resync if gap longer than this

CRSFReceiver::CRSFReceiver(HardwareSerial &serial) : _serial(&serial) {
  for (auto &c : _ch) c = CRSF_MID;
}

void CRSFReceiver::begin(uint32_t baud, bool halfDuplex) {
  if (halfDuplex) _serial->setHalfDuplex();
  _serial->begin(baud);
  _lastRcMs = millis();
  _idx = 0;
}

uint8_t CRSFReceiver::crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
  }
  return crc;
}

bool CRSFReceiver::update() {
  bool newRc = false;

  while (_serial->available() > 0) {
    uint32_t now = micros();
    uint8_t b = (uint8_t)_serial->read();

    // Resync after a gap in the middle of a frame
    if (_idx > 0 && (now - _lastByteUs) > INTERBYTE_GAP_US) _idx = 0;
    _lastByteUs = now;

    if (_idx == 0) {
      if (b != ADDR_FC && b != ADDR_TX_MOD && b != ADDR_RADIO && b != ADDR_RX) continue;
      _buf[_idx++] = b;
      continue;
    }

    if (_idx == 1) {
      if (b < 2 || b > MAX_FRAME - 2) { _idx = 0; continue; }  // invalid length
      _buf[_idx++] = b;
      continue;
    }

    _buf[_idx++] = b;
    uint8_t total = _buf[1] + 2;

    if (_idx >= total) {
      // _buf: [addr][len][type][payload...][crc]
      uint8_t crc = crc8(&_buf[2], _buf[1] - 1);
      if (crc == _buf[total - 1]) {
        _framesOk++;
        if (_buf[2] == TYPE_RC_CHANNELS) newRc = true;
        handleFrame(_buf);
      } else {
        _crcErrors++;
      }
      _idx = 0;
    }
  }
  return newRc;
}

void CRSFReceiver::handleFrame(const uint8_t *f) {
  uint8_t type = f[2];
  const uint8_t *payload = &f[3];
  uint8_t plen = f[1] - 2;   // len minus type and crc

  switch (type) {
    case TYPE_RC_CHANNELS:
      if (plen >= 22) {
        unpackChannels(payload);
        _lastRcMs = millis();
      }
      break;

    case TYPE_LINK_STATS:
      if (plen >= 10) {
        _stats.uplinkRssi1  = payload[0];
        _stats.uplinkRssi2  = payload[1];
        _stats.uplinkLQ     = payload[2];
        _stats.uplinkSnr    = (int8_t)payload[3];
        _stats.activeAntenna= payload[4];
        _stats.rfMode       = payload[5];
        _stats.txPower      = payload[6];
        _stats.downlinkRssi = payload[7];
        _stats.downlinkLQ   = payload[8];
        _stats.downlinkSnr  = (int8_t)payload[9];
        _gotStats = true;
      }
      break;

    default:
      if (_cb) _cb(type, payload, plen);
      break;
  }
}

// 16 channels x 11 bits, packed LSB first
void CRSFReceiver::unpackChannels(const uint8_t *p) {
  uint32_t bits = 0;
  uint8_t nbits = 0, n = 0;
  for (uint8_t i = 0; i < 22 && n < NUM_CHANNELS; i++) {
    bits |= (uint32_t)p[i] << nbits;
    nbits += 8;
    while (nbits >= 11 && n < NUM_CHANNELS) {
      _ch[n++] = bits & 0x7FF;
      bits >>= 11;
      nbits -= 11;
    }
  }
}

uint16_t CRSFReceiver::getChannelRaw(uint8_t ch) const {
  return ch < NUM_CHANNELS ? _ch[ch] : CRSF_MID;
}

uint16_t CRSFReceiver::getChannelUs(uint8_t ch) const {
  // 172 -> 987.5, 992 -> 1500, 1811 -> 2012
  return (uint16_t)((5UL * getChannelRaw(ch) + 4) / 8 + 880);
}

int16_t CRSFReceiver::getChannelPercent(uint8_t ch) const {
  int32_t v = (int32_t)getChannelRaw(ch) - CRSF_MID;
  int32_t range = (v >= 0) ? (CRSF_MAX - CRSF_MID) : (CRSF_MID - CRSF_MIN);
  return (int16_t)constrain((v * 100) / range, -100, 100);
}

bool CRSFReceiver::isFailsafe() const {
  return (millis() - _lastRcMs) > _fsTimeoutMs;
}

// ---- Telemetry ----

void CRSFReceiver::sendFrame(uint8_t type, const uint8_t *payload, uint8_t len) {
  if (len > MAX_FRAME - 4) return;
  uint8_t out[MAX_FRAME];
  out[0] = ADDR_RADIO;           // destination: radio / handset
  out[1] = len + 2;              // type + payload + crc
  out[2] = type;
  memcpy(&out[3], payload, len);
  out[3 + len] = crc8(&out[2], len + 1);
  _serial->write(out, len + 4);
}

void CRSFReceiver::sendBattery(uint16_t v, uint16_t a, uint32_t mah, uint8_t pct) {
  uint8_t p[8];
  p[0] = v >> 8;  p[1] = v & 0xFF;
  p[2] = a >> 8;  p[3] = a & 0xFF;
  p[4] = (mah >> 16) & 0xFF;
  p[5] = (mah >> 8) & 0xFF;
  p[6] = mah & 0xFF;
  p[7] = pct;
  sendFrame(TYPE_BATTERY, p, 8);
}

void CRSFReceiver::sendFlightMode(const char *mode) {
  uint8_t p[16];
  uint8_t n = 0;
  while (mode[n] && n < 15) { p[n] = mode[n]; n++; }
  p[n++] = 0;                    // null terminator
  sendFrame(TYPE_FLIGHT_MODE, p, n);
}
