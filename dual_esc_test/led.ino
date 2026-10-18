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

// reminder code
// leds[0] = CRGB::Yellow;
// leds[1] = CRGB::Yellow;

void led(int led_type) {

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
  // showSPI(255);
}