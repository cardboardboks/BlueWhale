#include <FastLED.h>
#include <SPI.h>

#define NUM_LEDS 12
#define PIN_SCK 6   // unused by the strip, but SPI needs one
#define PIN_MOSI 7  // connect to the strip's DIN (check your board's pinout)
#define SPI_HZ 2400000

CRGB leds[NUM_LEDS];
static uint8_t txbuf[NUM_LEDS * 9 + 120];  // 9 bytes per LED + reset padding

// Expand one color byte into 3 SPI bytes (8 bits * 3 = 24 bits)
static inline void encodeByte(uint8_t v, uint8_t *out) {
  uint32_t bits = 0;
  for (int i = 7; i >= 0; i--) {
    bits = (bits << 3) | ((v >> i) & 1 ? 0b110 : 0b100);
  }
  out[0] = bits >> 16;
  out[1] = bits >> 8;
  out[2] = bits;
}

void showSPI(uint8_t brightness = 255) {
  uint8_t *p = txbuf;
  for (int i = 0; i < NUM_LEDS; i++) {
    CRGB c = leds[i];
    c.nscale8(brightness);
    encodeByte(c.g, p);
    p += 3;  // WS2812 order is GRB
    encodeByte(c.r, p);
    p += 3;
    encodeByte(c.b, p);
    p += 3;
  }
  // trailing zeros hold the line low (> 280 us) as the reset latch
  memset(p, 0, 120);

  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  SPI.transferBytes(txbuf, nullptr, sizeof(txbuf));
  SPI.endTransaction();
}

void setup() {
  SPI.begin(PIN_SCK, -1, PIN_MOSI, -1);
}

void loop() {
  static uint8_t hue = 0;
  fill_rainbow(leds, NUM_LEDS, hue++, 7);
  showSPI(200);
 // delay(2);
}