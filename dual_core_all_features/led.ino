// moslty sorted - requires IMU implimentation and the full set of LEDS

// leds

// 1 all blinking yellow/white: booting
// 2 all blinking green/black: waiting pre arm
// 3 all rainbow: vehicle safe
// 4 all fast blinking red/black: rc8linking orange/black left: esc1 failsafe
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

  modeArray[array_num] = on_off;



  //   if (modeArray[4] == 1) {  // 4 all fast blinking red/black: rc failsafe
  //     fade_led(0, 12, 1, 0xFF0000, 0x000000);
  //   } else {
  //     if (modeArray[5] == 1 || modeArray[6] == 1) {

  //       if (modeArray[5] == 1) {  // 5 fast blinking orange/black left: esc1 failsafe
  //         fade_led(0, 6, 1, 0xff1e00, 0x000000);
  //       }

  //       if (modeArray[6] == 1) {  // 6 fast blinking orange/black right: esc2 failsafe
  //         fade_led(6, 6, 1, 0xff1e00, 0x000000);
  //       }
  //     } else {

  //       if (modeArray[1] == 1) {  // 1 all blinking yellow/white: booting
  //         fade_led(0, 12, 5, 0x404500, 0xedff00);
  //       }
  //       if (modeArray[2] == 1) {  // 2 all blinking green/black: waiting pre arm
  //         fade_led(0, 12, 5, 0x00ff00, 0x001c00);
  //       }
  //       if (modeArray[3] == 1) {  // 3 all rainbow: vehicle safe
  //         rainbow_led(0, 3, 2);
  //         static_led(3, 3, 0xFF0000);
  //         rainbow_led(6, 3, 2);
  //         static_led(9, 3, 0xFF0000);
  //       }
  //       if (modeArray[17] == 1) {  // 3 all rainbow: vehicle safe
  //         static_led(0, 3, 0xFF0000);
  //         rainbow_led(3, 3, 2);
  //         static_led(6, 3, 0xFF0000);
  //         rainbow_led(9, 3, 2);
  //       }
  //       if (modeArray[7] == 1) {  // 7 when auto up is off: all blinking purple/white (stabilisation not available)
  //         fade_led(0, 12, 1, 0xff00ff, 0x2e002e);
  //       }


  //       if (modeArray[8] == 1) {  // 8 stabilisation off, drive forward, top: slow blink teal
  //         fade_led(0, 3, 10, 0x0059ff, 0x000c24);
  //         static_led(3, 3, 0xFF0000);
  //         fade_led(6, 3, 10, 0x0059ff, 0x000c24);
  //         static_led(9, 3, 0x00ff2f);
  //       }
  //       if (modeArray[9] == 1) {  // 9 stabilisation off, drive backwards, top: blinking teal
  //         fade_led(0, 3, 1, 0x0059ff, 0x000c24);
  //         static_led(3, 3, 0xFF0000);
  //         fade_led(6, 3, 1, 0x0059ff, 0x000c24);
  //         static_led(9, 3, 0x00ff2f);
  //       }

  //       if (modeArray[10] == 1) {  // 10 stabilisation off, drive forward, bottom: slow blink teal
  //         static_led(0, 3, 0xFF0000);
  //         fade_led(3, 3, 10, 0x0059ff, 0x000c24);
  //         static_led(6, 3, 0x00ff2f);
  //         fade_led(9, 3, 10, 0x0059ff, 0x000c24);
  //       }
  //       if (modeArray[11] == 1) {  // 11 stabilisation off, drive backwards, bottom: blinking teal
  //         static_led(0, 3, 0xFF0000);
  //         fade_led(3, 3, 1, 0x0059ff, 0x000c24);
  //         static_led(6, 3, 0x00ff2f);
  //         fade_led(9, 3, 1, 0x0059ff, 0x000c24);
  //       }


  //       if (modeArray[12] == 1) {  // 12 stabilisation on, drive forward, top: slow blink white
  //         fade_led(0, 3, 10, 0xffffff, 0x2e2e2e);
  //         static_led(3, 3, 0xFF0000);
  //         fade_led(6, 3, 10, 0xffffff, 0x2e2e2e);
  //         static_led(9, 3, 0x00ff2f);
  //       }
  //       if (modeArray[13] == 1) {  // 13 stabilisation on, drive backwards, top: blinking white
  //         fade_led(0, 3, 1, 0xffffff, 0x000000);
  //         static_led(3, 3, 0xFF0000);
  //         fade_led(6, 3, 1, 0xffffff, 0x000000);
  //         static_led(9, 3, 0x00ff2f);
  //       }

  //       if (modeArray[14] == 1) {  // 14 stabilisation on, drive forward, bottom: slow blink white
  //         static_led(0, 3, 0xFF0000);
  //         fade_led(3, 3, 10, 0xffffff, 0x2e2e2e);
  //         static_led(6, 3, 0x00ff2f);
  //         fade_led(9, 3, 10, 0xffffff, 0x2e2e2e);
  //       }
  //       if (modeArray[15] == 1) {  // 15 stabilisation on, drive backwards, bottom: blinking white
  //         static_led(0, 3, 0xFF0000);
  //         fade_led(3, 3, 1, 0xffffff, 0x000000);
  //         static_led(6, 3, 0x00ff2f);
  //         fade_led(9, 3, 1, 0xffffff, 0x000000);
  //       }
  //     }
  //   }

  //   // drive the LEDs

  //   if (led_freq.TRIGGERED) {
  //     showLeds();
  //   }
}


// void rainbow_led(int start, int count, int freq) {

//   static uint8_t hue = 0;
//   static uint32_t lastfade = 0;

//   if (millis() - lastfade > freq) {
//     lastfade = millis();

//     for (int i = start; i < start + count; i++) {
//       uint8_t h = hue + i * (256 / NUM_LEDS);
//       // quick HSV(h,255,64) -> RGB
//       uint8_t region = h / 43, rem = (h - region * 43) * 6;
//       uint8_t v = 64, q = (v * (255 - rem)) >> 8, t = (v * rem) >> 8;
//       switch (region) {
//         case 0: leds[i] = { v, t, 0 }; break;
//         case 1: leds[i] = { q, v, 0 }; break;
//         case 2: leds[i] = { 0, v, t }; break;
//         case 3: leds[i] = { 0, q, v }; break;
//         case 4: leds[i] = { t, 0, v }; break;
//         default: leds[i] = { v, 0, q }; break;
//       }
//     }
//     hue++;
//   }
// }

// void static_led(int start, int count, uint32_t color) {
//   uint8_t r = (color >> 16) & 0xFF;
//   uint8_t g = (color >> 8) & 0xFF;
//   uint8_t b = color & 0xFF;

//   for (int i = start; i < start + count; i++) {
//     leds[i] = { r, g, b };
//   }
// }

// void fade_led(int start, int count, int freq, uint32_t colorA, uint32_t colorB) {

//   // Split each 0xRRGGBB value into its channels
//   const uint8_t COLOR_A[3] = { (colorA >> 16) & 0xFF, (colorA >> 8) & 0xFF, colorA & 0xFF };
//   const uint8_t COLOR_B[3] = { (colorB >> 16) & 0xFF, (colorB >> 8) & 0xFF, colorB & 0xFF };

//   static uint8_t phase = 0;
//   static uint32_t lastfade = 0;

//   if (millis() - lastfade > freq) {
//     lastfade = millis();

//     for (int i = start; i < start + count; i++) {

//       // triangle wave: 0..255..0 over a full 0..255 phase cycle
//       uint8_t mix = (phase < 128) ? phase * 2 : (255 - phase) * 2;

//       // linear blend A -> B
//       uint8_t r = (COLOR_A[0] * (255 - mix) + COLOR_B[0] * mix) / 255;
//       uint8_t g = (COLOR_A[1] * (255 - mix) + COLOR_B[1] * mix) / 255;
//       uint8_t b = (COLOR_A[2] * (255 - mix) + COLOR_B[2] * mix) / 255;

//       leds[i] = { r, g, b };
//     }
//     phase++;
//   }
// }