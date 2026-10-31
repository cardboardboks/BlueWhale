void ledTask(void *pvParameters) {
  ledInit();
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {

    if (otaInProgress) {
      static_led(0, 12, 0xFF0000);

      // ---- Priority 1: RC failsafe (overrides everything) ----
    } else if (modeArray[4] == 1) {
      fade_led(0, 12, 1, 0xFF0000, 0x000000);

      // ---- Priority 2: booting / pre-arm / safe ----
    } else if (modeArray[1] == 1 || modeArray[2] == 1 || modeArray[3] == 1 || modeArray[17] == 1) {
      if (modeArray[1] == 1) {  // booting
        fade_led(0, 12, 5, 0x404500, 0xedff00);
      } else if (modeArray[2] == 1) {  // waiting pre arm
        fade_led(0, 12, 5, 0x00ff00, 0x001c00);
      } else if (modeArray[3] == 1) {  // vehicle safe
        rainbow_led(0, 3, 2);
        static_led(3, 3, 0xFF0000);
        rainbow_led(6, 3, 2);
        static_led(9, 3, 0xFF0000);
      } else {
        static_led(0, 3, 0xFF0000);
        rainbow_led(3, 3, 2);
        static_led(6, 3, 0xFF0000);
        rainbow_led(9, 3, 2);
      }

      // ---- Priority 3: IMU not booted ----
    } else if (modeArray[16] == 1) {
      fade_led(0, 12, 1, 0xff00ff, 0xFF0000);

      // ---- Priority 4: auto up off ----
    } else if (modeArray[7] == 1) {
      fade_led(0, 12, 5, 0xff00ff, 0x2e002e);

      // ---- Priority 5: driving modes ----
    } else {
      if (modeArray[8] == 1) {  // stab off, fwd, top
        fade_led(0, 3, 10, 0x0059ff, 0x000c24);
        static_led(3, 3, 0xFF0000);
        fade_led(6, 3, 10, 0x0059ff, 0x000c24);
        static_led(9, 3, 0x00ff2f);
      } else if (modeArray[9] == 1) {  // stab off, back, top
        fade_led(0, 3, 5, 0x0059ff, 0x000c24);
        static_led(3, 3, 0xFF0000);
        fade_led(6, 3, 5, 0x0059ff, 0x000c24);
        static_led(9, 3, 0x00ff2f);
      } else if (modeArray[10] == 1) {  // stab off, fwd, bottom
        static_led(0, 3, 0xFF0000);
        fade_led(3, 3, 10, 0x0059ff, 0x000c24);
        static_led(6, 3, 0x00ff2f);
        fade_led(9, 3, 10, 0x0059ff, 0x000c24);
      } else if (modeArray[11] == 1) {  // stab off, back, bottom
        static_led(0, 3, 0xFF0000);
        fade_led(3, 3, 5, 0x0059ff, 0x000c24);
        static_led(6, 3, 0x00ff2f);
        fade_led(9, 3, 5, 0x0059ff, 0x000c24);
      } else if (modeArray[12] == 1) {  // stab on, fwd, top
        fade_led(0, 3, 10, 0xffffff, 0x2e2e2e);
        static_led(3, 3, 0xFF0000);
        fade_led(6, 3, 10, 0xffffff, 0x2e2e2e);
        static_led(9, 3, 0x00ff2f);
      } else if (modeArray[13] == 1) {  // stab on, back, top
        fade_led(0, 3, 5, 0xffffff, 0x000000);
        static_led(3, 3, 0xFF0000);
        fade_led(6, 3, 5, 0xffffff, 0x000000);
        static_led(9, 3, 0x00ff2f);
      } else if (modeArray[14] == 1) {  // stab on, fwd, bottom
        static_led(0, 3, 0xFF0000);
        fade_led(3, 3, 10, 0xffffff, 0x2e2e2e);
        static_led(6, 3, 0x00ff2f);
        fade_led(9, 3, 10, 0xffffff, 0x2e2e2e);
      } else if (modeArray[15] == 1) {  // stab on, back, bottom
        static_led(0, 3, 0xFF0000);
        fade_led(3, 3, 5, 0xffffff, 0x000000);
        static_led(6, 3, 0x00ff2f);
        fade_led(9, 3, 5, 0xffffff, 0x000000);
      }
    }

    showLeds();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(25));
  }
}

// leds

// 1 all blinking yellow/white: booting
// 2 all blinking green/black: waiting pre arm
// 3 rainbow top: vehicle safe top
// 17 rainbow bottom: vehicle safe bottom
// 4 all fast blinking red/black: rc failsafe
// 5 fast blinking orange/black left: esc1 failsafe
// 6 fast blinking orange/black right: esc2 failsafe
// 7 when auto up is off: all blinking purple/black (stabilisation not available)
// 16 when imu is not online: all blinking purple/red (stabilisation not available)

//  when auto up is active: upper leds show the status and that it is the top
//  when auto up is active: lower leds always red
// 8 stabilisation off, drive forward, top: slow blink teal
// 9 stabilisation off, drive backwards, top: blinking teal
// 10 stabilisation off, drive forward, bottom: slow blink teal
// 11 stabilisation off, drive backwards, bottom: blinking teal

// 12 stabilisation on, drive forward, top: slow blink white
// 13 stabilisation on, drive backwards, top: blinking white
// 14 stabilisation on, drive forward, bottom: slow blink white
// 15 stabilisation on, drive backwards, bottom: blinking white

void led(int array_num, bool on_off) {
// move led commands into state array
  modeArray[array_num] = on_off;
}


void rainbow_led(int start, int count, int freq) {

  // Hue derived from time, so every call (and every segment) stays in sync
  uint8_t hue = millis() / freq;

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
  uint8_t phase = (millis() / freq) & 0xFF;
  uint8_t mix = (phase < 128) ? phase * 2 : (255 - phase) * 2;
  uint8_t aR = colorA >> 16, aG = colorA >> 8, aB = colorA;
  uint8_t bR = colorB >> 16, bG = colorB >> 8, bB = colorB;
  uint8_t r = (aR * (255 - mix) + bR * mix) / 255;
  uint8_t g = (aG * (255 - mix) + bG * mix) / 255;
  uint8_t b = (aB * (255 - mix) + bB * mix) / 255;
  for (int i = start; i < start + count; i++) leds[i] = { r, g, b };
}