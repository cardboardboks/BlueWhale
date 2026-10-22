// Drive WS2812/WS2812B/SK6812 (clockless, single-wire) LEDs from the ESP32 SPI MOSI pin.
//
// Trick: run SPI at 3.2 MHz (1 SPI bit = 312.5 ns). Each WS2812 bit is sent as 4 SPI bits:
//   WS2812 "0"  ->  1000   (~312 ns high, ~937 ns low)
//   WS2812 "1"  ->  1110   (~937 ns high, ~312 ns low)
// So every colour byte becomes 4 SPI bytes, and every LED (3 bytes) becomes 12 SPI bytes.
// Reset/latch = >50 us of low line, sent as a run of zero bytes.

#include <Arduino.h>
#include <SPI.h>

#define LED_PIN 2  // This becomes the SPI MOSI pin (any GPIO works via the GPIO matrix)
#define NUM_LEDS 12
#define SPI_FREQ 3200000  // 3.2 MHz

// Classic ESP32 uses VSPI; ESP32-S2/S3/C3 use FSPI
#if defined(CONFIG_IDF_TARGET_ESP32)
SPIClass ledSPI(VSPI);
#else
SPIClass ledSPI(FSPI);
#endif

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

  ledSPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
  ledSPI.writeBytes(spiBuf, sizeof(spiBuf));
  ledSPI.endTransaction();
}

uint8_t hue = 0;

void setup() {
  // begin(sck, miso, mosi, ss) -> only MOSI is used
ledSPI.begin(4, -1, LED_PIN, -1);
}

void loop() {
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
  delay(10);
  showLeds();
}