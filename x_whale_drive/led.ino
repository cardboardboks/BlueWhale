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

  // Serial.println(led_type);

  // 1 all blinking yellow/white: booting
  if (led_type == 1) {
    fade_led(0, 12, 5, 0x404500, 0xedff00);

    // 2 all blinking turquoise/black: waitng pre arm
  } else if (led_type == 2) {
    fade_led(0, 12, 5, 0x00ccff, 0x00080a);

    // 3 all rainbow: vechile safe
  } else if (led_type == 3) {
    rainbow_led(0, 3, 2);
    static_led(3, 3, 0xFF0000);
    rainbow_led(6, 9, 2);
    static_led(9, 3, 0xFF0000);

    // 4 all fast blinking red/black: rc failsafe
  } else if (led_type == 4) {
    fade_led(0, 12, 1, 0xFF0000, 0x000000);

    // 5 stabilization off, drive forward: solid green
  } else if (led_type == 5) {
    static_led(0, 3, 0x00ff2f);
    static_led(3, 3, 0xFF0000);
    static_led(6, 3, 0x00ff2f);
    static_led(9, 3, 0xFF0000);

    // 6 stabilization off, drive backwards: blinking green
  } else if (led_type == 6) {
    fade_led(0, 3, 1, 0x00ff2f, 0x000000);
    static_led(3, 3, 0xFF0000);
    fade_led(6, 3, 1, 0x00ff2f, 0x000000);
    static_led(9, 3, 0x00ff2f);

    // 7 stabilization on, drive forward: solid white
  } else if (led_type == 7) {
    static_led(0, 3, 0xffffff);
    static_led(3, 3, 0xFF0000);
    static_led(6, 3, 0xffffff);
    static_led(9, 3, 0xFF0000);

    // 8 stabilization on, drive backwards: blinking white
  } else if (led_type == 8) {
    fade_led(0, 3, 1, 0xffffff, 0x000000);
    static_led(3, 3, 0xFF0000);
    fade_led(6, 3, 1, 0xffffff, 0x000000);
    static_led(9, 3, 0x00ff2f);

    // 9 when auto up is off: all purple (stabilization not avalible)
  } else if (led_type == 9) {
    fade_led(0, 12, 5, 0xcc00ff, 0x000000);

    // defual to police if everything elese has failed
  } else {
    fade_led(0, 12, 5, 0xff0000, 0xff0000);
  }

  // drive the LEDs
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

void static_led(int start, int count, uint32_t color) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;

  for (int i = start; i < start + count; i++) {
    leds[i] = { r, g, b };
  }
}

void fade_led(int start, int count, int freq, uint32_t colorA, uint32_t colorB) {

  // Split each 0xRRGGBB value into its channels
  const uint8_t COLOR_A[3] = { (colorA >> 16) & 0xFF, (colorA >> 8) & 0xFF, colorA & 0xFF };
  const uint8_t COLOR_B[3] = { (colorB >> 16) & 0xFF, (colorB >> 8) & 0xFF, colorB & 0xFF };

  static uint8_t phase = 0;
  static uint32_t lastfade = 0;

  if (millis() - lastfade > freq) {
    lastfade = millis();

    for (int i = start; i < start + count; i++) {

      // triangle wave: 0..255..0 over a full 0..255 phase cycle
      uint8_t mix = (phase < 128) ? phase * 2 : (255 - phase) * 2;

      // linear blend A -> B
      uint8_t r = (COLOR_A[0] * (255 - mix) + COLOR_B[0] * mix) / 255;
      uint8_t g = (COLOR_A[1] * (255 - mix) + COLOR_B[1] * mix) / 255;
      uint8_t b = (COLOR_A[2] * (255 - mix) + COLOR_B[2] * mix) / 255;

      leds[i] = { r, g, b };
    }
    phase++;
  }
}