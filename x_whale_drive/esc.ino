void boot_esc() {

  // open drain so we only pull down, and via the IDF rather than pinMode() so
  // the Arduino peripheral manager does not claim the pad ahead of the RMT
  // driver in begin().

  // running this code pulled from the .cpp to avoid waiting 2.5s esc if it were to call the funtion twice
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << PIN_ESC1;
  cfg.mode = GPIO_MODE_OUTPUT_OD;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
  gpio_set_level((gpio_num_t)PIN_ESC1, 0);

  cfg.pin_bit_mask = 1ULL << PIN_ESC2;
  cfg.mode = GPIO_MODE_OUTPUT_OD;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
  gpio_set_level((gpio_num_t)PIN_ESC2, 0);

  // wait for boot and set leds to booting colour
  for (unsigned long start = millis(); millis() - start < esc_arm_duration / 2;) {
  }

  esc1.resetStats();
  esc2.resetStats();

  // create esc object, set up and bind to pin
  // esc#, pin bound to, dshot freq, bidirectinal, pol count
  esc1.begin(PIN_ESC1, DSHOT600, true, MOTOR1_POLES);
  esc2.begin(PIN_ESC2, DSHOT600, true, MOTOR2_POLES);

  // wait for ESC to boot before enabling EDT
  for (unsigned long start = millis(); millis() - start < esc_arm_duration;) {
    drive_esc(0, 0);
  }

  // enable EDT
  esc1.command(DSHOT_CMD_EDT_ENABLE);
  esc2.command(DSHOT_CMD_EDT_ENABLE);
}

void drive_esc(int thr1, int thr2) {

  static unsigned long aboveStart = 0;
  static bool timing = false;
  static bool triggered = false;

  if (ch[5] > 1800) {
    if (!timing) {
      timing = true;
      aboveStart = millis();  // start the timer
    } else if (!triggered && millis() - aboveStart >= 2000) {
      triggered = true;  // make sure it only fires once

      // make this longr flash
      // static_led(0, 12, 0xffffff);
      // showLeds();

      boot_esc();
    }
  } else {
    // dropped below threshold: reset everything
    timing = false;
    triggered = false;
  }

  if (esc1.lossPercent() > 10) {
    // 5 fast blinking orange/black left: esc1 failsafe
    led(5, 1);
  } else {
    led(5, 0);
  }
  if (esc2.lossPercent() > 10) {
    // 6 fast blinking orange/black right: esc2 failsafe
    led(6, 1);
  } else {
    led(6, 0);
  }

  // check for an active rf link right before sending esc command
  // if no rf connection, force esc to disarm and se leds
  if (crsf.isLinkUp() == 0) {
    thr1 = 0;
    thr2 = 0;
    throttle1 = 0;
    throttle2 = 1000;
    led(4, 1);
  } else {
    led(4, 0);
  }

  // if safe force esc to disarm and set leds
  if (ch[3] == 1000 && crsf.isLinkUp() == 1 && RP <= roll_flip) {
    thr1 = 0;
    thr2 = 0;
    throttle1 = 1500;
    throttle2 = 1500;
    if (safed == 1) {
      led(3, 1);
    }
  } else {
    led(3, 0);
  }

  if (ch[3] == 1000 && crsf.isLinkUp() == 1 && RP >= roll_flip) {
    thr1 = 0;
    thr2 = 0;
    throttle1 = 1500;
    throttle2 = 1500;
    if (safed == 1) {
      led(17, 1);
    }
  } else {
    led(17, 0);
  }

  if (ch[3] != 1000 && crsf.isLinkUp() == 1) {
    // copy the commanded drive values out for debugging purposes
    throttle1 = thr1;
    throttle2 = thr2;
  }

  //scale 1000-2000 range to dshot 3d mode lower and upper halves. if throttle centred send esc disarm command
  if (thr1 == 0) {
    thr1 = 0;
  } else if (thr1 == 1500) {
    thr1 = 0;
  } else {
    if (thr1 < 1500) {
      thr1 = map(thr1, 1000, 1499, 1047, 48);
    }
    if (thr1 > 1500) {
      thr1 = map(thr1, 1501, 2000, 1049, 2047);
    }
  }

  //scale 1000-2000 range to dshot 3d mode lower and upper halves. if throttle centred send esc disarm command
  if (thr2 == 0) {
    thr2 = 0;
  } else if (thr2 == 1500) {
    thr2 = 0;
  } else {
    if (thr2 < 1500) {
      thr2 = map(thr2, 1000, 1499, 1047, 48);
    }
    if (thr2 > 1500) {
      thr2 = map(thr2, 1501, 2000, 1049, 2047);
    }
  }

  // drive the esc's output every 1000microseconds
  if (dshot_freq.TRIGGERED) {

    // send packet to escs
    esc1.send(thr1);
    esc2.send(thr2);
  }
}
