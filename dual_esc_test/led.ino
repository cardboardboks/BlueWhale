// leds

// 1 all blinking yellow/black: booting
// 2 all blinking orange/white: waitng pre arm
// 3 all rainbow: vechile safe
// 4 all fast blinking red/black: rc failsafe

//  when auto up is active: upper leds show the status and that it is the top
//  when auto up is active: lower leds always red
// 5 stabilization off, drive forward: solid green
// 6 stabilization off, drive backwards: blinking green
// 7 stabilization on, drive forward: solid white
// 8 stabilization on, drive backwards: blinking white

// 9 when auto up is off: all purple (stabilization not avalible)

// defual to police if everything elese has failed

// leds[0] = CRGB::Yellow;
// leds[1] = CRGB::Yellow;

uint8_t ratio = 0;

void led(int led_type) {

  // 1 all blinking yellow: booting
  if (led_type == 1) {
    ratio = beatsin8(100, 0, 255);
    CRGB currentColor = blend(CRGB::Yellow, CRGB::Black, ratio);
    leds[0] = currentColor;
    leds[1] = currentColor;

    // 2 all blinking orange: waitng pre arm
  } else if (led_type == 2) {
    ratio = beatsin8(100, 0, 255);
    CRGB currentColor = blend(CRGB::Orange, CRGB::White, ratio);
    leds[0] = currentColor;
    leds[1] = currentColor;

    // 3 all rainbow: vechile safe
  } else if (led_type == 3) {
    static uint8_t hue = 0;
    fill_rainbow(leds, NUM_LEDS, hue++, 7);

    // 4 all fast blinking red: rc failsafe
  } else if (led_type == 4) {
    ratio = beatsin8(200, 0, 255);
    CRGB currentColor = blend(CRGB::Red, CRGB::Black, ratio);
    leds[0] = currentColor;
    leds[1] = currentColor;

  } else if (led_type == 5) {

  } else if (led_type == 6) {

  } else if (led_type == 7) {

  } else if (led_type == 8) {

  } else if (led_type == 9) {

  } else {
  }

  showSPI(255);
}