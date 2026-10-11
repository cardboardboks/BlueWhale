// leds
// when auto up is off: all purple (stabilization not avalible)

// when auto up is active: upper leds show the status and that it is the top
// when auto up is active: lower leds always red
// solid colour: drive forwad
// blinking colour: drive backwards
// stabilixation off: upper green
// stabilixation on: upper white

// all fast flash yellow: booting
// all fast flash orage: waitng pre arm
// all fast flash red: rc failsafe
int r = 0;
int g = 0;
int b = 0;

void run_led() {
  if (led_freq.TRIGGERED) {
    leds[0] = CRGB(255, 255, 255);  // R, G, B
    leds[1] = CRGB(255, 255, 255);  // R, G, B
    showSPI(250);
  }
}

void set_led(int i) {

  if (i == 1) {
    r = 0;
    g = 0;
    b = 0;
  }
  if (i == 2) {
    r = 255;
    g = 165;
    b = 0;
  }
  if (i == 3) {
    r = 255;
    g = 255;
    b = 0;
  }
  if (i == 4) {
    r = 0;
    g = 0;
    b = 0;
  }
}
