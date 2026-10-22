#pragma once
#include <Arduino.h>

/*
 * Minimal CRSF receiver library (ExpressLRS / TBS Crossfire).
 * - 420000 baud, 8N1, non-inverted
 * - Polling based: call update() often from loop()
 */
class CRSFReceiver {
public:
  static constexpr uint8_t  NUM_CHANNELS   = 16;
  static constexpr uint32_t CRSF_BAUD      = 420000;
  static constexpr uint16_t CRSF_MIN       = 172;   // ~988 us
  static constexpr uint16_t CRSF_MID       = 992;   // 1500 us
  static constexpr uint16_t CRSF_MAX       = 1811;  // ~2012 us

  // Frame types
  static constexpr uint8_t TYPE_GPS           = 0x02;
  static constexpr uint8_t TYPE_BATTERY       = 0x08;
  static constexpr uint8_t TYPE_LINK_STATS    = 0x14;
  static constexpr uint8_t TYPE_RC_CHANNELS   = 0x16;
  static constexpr uint8_t TYPE_ATTITUDE      = 0x1E;
  static constexpr uint8_t TYPE_FLIGHT_MODE   = 0x21;

  struct LinkStats {
    uint8_t uplinkRssi1;   // -dBm (e.g. 40 = -40 dBm)
    uint8_t uplinkRssi2;   // -dBm
    uint8_t uplinkLQ;      // 0..100 %
    int8_t  uplinkSnr;     // dB
    uint8_t activeAntenna;
    uint8_t rfMode;
    uint8_t txPower;       // enum, see CRSF spec
    uint8_t downlinkRssi;  // -dBm
    uint8_t downlinkLQ;    // %
    int8_t  downlinkSnr;   // dB
  };

  explicit CRSFReceiver(HardwareSerial &serial);

  // Call after construction. Serial must be created with the right RX/TX pins,
  // e.g.  HardwareSerial crsfSerial(PA10, PA9);
  void begin(uint32_t baud = CRSF_BAUD, bool halfDuplex = false);

  // Poll UART. Returns true if a new RC channel frame was decoded in this call.
  bool update();

  // Channels (index 0..15)
  uint16_t getChannelRaw(uint8_t ch) const;              // 172..1811
  uint16_t getChannelUs(uint8_t ch) const;               // ~988..2012
  int16_t  getChannelPercent(uint8_t ch) const;          // -100..100

  // Link / health
  const LinkStats &linkStats() const { return _stats; }
  bool     hasLinkStats() const { return _gotStats; }
  bool     isFailsafe() const;                           // no RC frame for timeout
  uint32_t lastRcAgeMs() const { return millis() - _lastRcMs; }
  void     setFailsafeTimeout(uint32_t ms) { _fsTimeoutMs = ms; }

  // Counters
  uint32_t framesOk()   const { return _framesOk; }
  uint32_t crcErrors()  const { return _crcErrors; }

  // Telemetry back to the receiver (shows up on the TX / OSD)
  // voltage in 0.1 V, current in 0.1 A, capacity in mAh, remaining in %
  void sendBattery(uint16_t voltage_dV, uint16_t current_dA,
                   uint32_t capacity_mAh, uint8_t remaining_pct);
  void sendFlightMode(const char *mode);
  void sendFrame(uint8_t type, const uint8_t *payload, uint8_t len);

  // Optional callback for frames this library does not decode itself
  typedef void (*FrameCallback)(uint8_t type, const uint8_t *payload, uint8_t len);
  void onFrame(FrameCallback cb) { _cb = cb; }

  static uint8_t crc8(const uint8_t *data, uint8_t len);

private:
  static constexpr uint8_t MAX_FRAME = 64;

  void handleFrame(const uint8_t *frame);
  void unpackChannels(const uint8_t *payload);

  HardwareSerial *_serial;
  uint8_t  _buf[MAX_FRAME];
  uint8_t  _idx = 0;
  uint32_t _lastByteUs = 0;

  uint16_t _ch[NUM_CHANNELS];
  LinkStats _stats{};
  bool     _gotStats = false;

  uint32_t _lastRcMs = 0;
  uint32_t _fsTimeoutMs = 500;
  uint32_t _framesOk = 0;
  uint32_t _crcErrors = 0;
  FrameCallback _cb = nullptr;
};
