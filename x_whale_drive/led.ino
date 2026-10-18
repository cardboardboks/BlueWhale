// moslty sorted - requires IMU implimentation and the full set of LEDS

// leds

// 1 all blinking yellow/white: booting
// 2 all blinking turquoise/black: waitng pre arm
// 3 all rainbow: vechile safe
// 4 all fast blinking red/black: rc failsafe

//  when auto up is active: upper leds show the status and that it is the top
//  when auto up is active: lower leds always red
// 5 stabilization off, drive forward: solid green
// 6 stabilization off, drive backwards: blinking green
// 7 stabilization on, drive forward: solid white
// 8 stabilization on, drive backwards: blinking white

// 9 when auto up is off: all blinking purple/white (stabilization not avalible)

// defual to police if everything elese has failed

void led(int led_type) {

  if (led_type == 3) {

    rainbow_led(0, 3, 2);
    static_led(3, 3, 255, 0, 0);

    rainbow_led(6, 3, 2);
    static_led(9, 3, 255, 0, 0);
  }

  // Serial.println(led_type);

  // uint8_t ratio = 0;

  // // 1 all blinking yellow/white: booting
  // if (led_type == 1) {
  //   ratio = beatsin8(100, 0, 255);
  //   CRGB currentColor = blend(0xFF8C00, 0xFFB963, ratio);
  //   for (int i = 0; i < NUM_LEDS; i++) {
  //     leds[i] = currentColor;
  //   }

  //   // 2 all blinking turquoise/black: waitng pre arm
  // } else if (led_type == 2) {
  //   ratio = beatsin8(50, 0, 255);
  //   CRGB currentColor = blend(0x00F1FF, 0x1C0000, ratio);
  //   for (int i = 0; i < NUM_LEDS; i++) {
  //     leds[i] = currentColor;
  //   }

  //   // 3 all rainbow: vechile safe
  // } else if (led_type == 3) {
  //   static uint8_t hue = 0;
  //   fill_rainbow(&(leds[0]), 3, hue++, 100);

  //   leds[3] = CRGB::Red;
  //   leds[4] = CRGB::Red;
  //   leds[5] = CRGB::Red;

  //   fill_rainbow(&(leds[6]), 3, hue++, 100);

  //   leds[9] = CRGB::Red;
  //   leds[10] = CRGB::Red;
  //   leds[11] = CRGB::Red;

  //   // 4 all fast blinking red/black: rc failsafe
  // } else if (led_type == 4) {
  //   ratio = beatsin8(200, 0, 255);
  //   CRGB currentColor = blend(0xFF0000, 0x1C0000, ratio);
  //   for (int i = 0; i < NUM_LEDS; i++) {
  //     leds[i] = currentColor;
  //   }

  //   // 5 stabilization off, drive forward: solid green
  // } else if (led_type == 5) {
  //   ratio = beatsin8(100, 0, 255);
  //   CRGB currentColor = blend(0x07FF00, 0x07FF00, ratio);
  //   leds[0] = currentColor;
  //   leds[1] = currentColor;
  //   leds[2] = currentColor;

  //   leds[3] = CRGB::Red;
  //   leds[4] = CRGB::Red;
  //   leds[5] = CRGB::Red;

  //   leds[6] = currentColor;
  //   leds[7] = currentColor;
  //   leds[8] = currentColor;

  //   leds[9] = CRGB::Red;
  //   leds[10] = CRGB::Red;
  //   leds[11] = CRGB::Red;

  //   // 6 stabilization off, drive backwards: blinking green
  // } else if (led_type == 6) {
  //   ratio = beatsin8(100, 0, 255);
  //   CRGB currentColor = blend(0x07FF00, 0x094500, ratio);

  //   leds[0] = currentColor;
  //   leds[1] = currentColor;
  //   leds[2] = currentColor;

  //   leds[3] = CRGB::Red;
  //   leds[4] = CRGB::Red;
  //   leds[5] = CRGB::Red;

  //   leds[6] = currentColor;
  //   leds[7] = currentColor;
  //   leds[8] = currentColor;

  //   leds[9] = CRGB::Red;
  //   leds[10] = CRGB::Red;
  //   leds[11] = CRGB::Red;

  //   // 7 stabilization on, drive forward: solid white
  // } else if (led_type == 7) {
  //   ratio = beatsin8(100, 0, 255);
  //   CRGB currentColor = blend(0xFFFFFF, 0xFFFFFF, ratio);
  //   leds[0] = currentColor;
  //   leds[1] = currentColor;
  //   leds[2] = currentColor;

  //   leds[3] = CRGB::Red;
  //   leds[4] = CRGB::Red;
  //   leds[5] = CRGB::Red;

  //   leds[6] = currentColor;
  //   leds[7] = currentColor;
  //   leds[8] = currentColor;

  //   leds[9] = CRGB::Red;
  //   leds[10] = CRGB::Red;
  //   leds[11] = CRGB::Red;

  //   // 8 stabilization on, drive backwards: blinking white
  // } else if (led_type == 8) {
  //   ratio = beatsin8(100, 0, 255);
  //   CRGB currentColor = blend(0xFFFFFF, 0x292929, ratio);
  //   leds[0] = currentColor;
  //   leds[1] = currentColor;
  //   leds[2] = currentColor;

  //   leds[3] = CRGB::Red;
  //   leds[4] = CRGB::Red;
  //   leds[5] = CRGB::Red;

  //   leds[6] = currentColor;
  //   leds[7] = currentColor;
  //   leds[8] = currentColor;

  //   leds[9] = CRGB::Red;
  //   leds[10] = CRGB::Red;
  //   leds[11] = CRGB::Red;

  //   // 9 when auto up is off: all purple (stabilization not avalible)
  // } else if (led_type == 9) {
  //   ratio = beatsin8(50, 0, 255);
  //   CRGB currentColor = blend(0xFF05F3, 0x990B91, ratio);
  //   for (int i = 0; i < NUM_LEDS; i++) {
  //     leds[i] = currentColor;
  //   }

  //   // defual to police if everything elese has failed
  // } else {
  //   ratio = beatsin8(100, 0, 255);
  //   CRGB currentColor = blend(CRGB::Blue, CRGB::Red, ratio);
  //   for (int i = 0; i < NUM_LEDS; i++) {
  //     leds[i] = currentColor;
  //   }
  // }

  // // drive the LEDs
  showLeds();
}

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

void rainbow_led(int start, int count, int freq) {

  static uint8_t hue = 0;
  static uint32_t lastfade = 0;

  if (millis() - lastfade > freq) {
    lastfade = millis();

    for (int i = start; i < start + count; i++) {
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
  }
}

void static_led(int start, int count, uint8_t  r, uint8_t  g, uint8_t  b) {

  for (int i = start; i < start + count; i++) {
    leds[i] = { r, g, b };
  }
}