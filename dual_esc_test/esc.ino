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

  for (unsigned long start = millis(); millis() - start < esc_arm_duration / 2;) {
    led(1);
  }

  // create esc object, set up and bind to pin
  // esc#, pin bound to, dshot freq, bidirectinal, pol count
  esc1.begin(PIN_ESC1, DSHOT600, true, MOTOR1_POLES);
  esc2.begin(PIN_ESC2, DSHOT600, true, MOTOR2_POLES);

  // wait for ESC to boot before enabling EDT
  for (unsigned long start = millis(); millis() - start < esc_arm_duration;) {
    led(1);
    drive_esc(0, 0);
  }

  // enable EDT
  esc1.command(DSHOT_CMD_EDT_ENABLE);
  esc2.command(DSHOT_CMD_EDT_ENABLE);
}

void drive_esc(int thr1, int thr2) {

  if (thr1 == 0) {
    thr1 = 0;
  } else {
    if (thr1 < 1500) {
      thr1 = map(thr1, 1000, 1499, 1047, 48);
    }
    if (thr1 == 1500) {
      thr1 = 0;
    }
    if (thr1 > 1500) {
      thr1 = map(thr1, 1501, 2000, 1049, 2047);
    }
  }

  if (thr2 == 0) {
    thr2 = 0;
  } else {
    if (thr2 < 1500) {
      thr2 = map(thr2, 1000, 1499, 1047, 48);
    }
    if (thr2 == 1500) {
      thr2 = 0;
    }
    if (thr2 > 1500) {
      thr2 = map(thr2, 1501, 2000, 1049, 2047);
    }
  }

  // run esc's every 1000microseconds
  if (dshot_freq.TRIGGERED) {

    //sort this out

    uint32_t gap = micros() - lastSend;
    lastSend = micros();
    if (gap > maxGap) maxGap = gap;

    uint32_t t = millis() - t0;  // time since begin()
    if (SEND_EDT && !cmdSent && t > 5000) {

      cmdSent = true;
    }
    uint32_t runAt = SEND_EDT ? 9000 : 5000;

    if (crsf.isLinkUp() == 0 || ch[3] == 1000) {
      thr1 = 0;
      thr2 = 0;
    }

    throttle1 = thr1;
    throttle2 = thr2;

    esc1.send(thr1);
    esc2.send(thr2);
  }
}
