void drive_escs(int thr1, int thr2) {

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

      // boot_esc();
    }
  } else {
    // dropped below threshold: reset everything
    timing = false;
    triggered = false;
  }

  // if (escs[1].lossPercent() > 10) {
  //   // 5 fast blinking orange/black left: esc1 failsafe
  //   led(5, 1);
  // } else {
  //   led(5, 0);
  // }
  // if (escs[2].lossPercent() > 10) {
  //   // 6 fast blinking orange/black right: esc2 failsafe
  //   led(6, 1);
  // } else {
  //   led(6, 0);
  // }

  // check for an active rf link right before sending esc command
  // if no rf connection, force esc to disarm and FS leds
  if (crsf.isLinkUp() == 0) {
    thr1 = 0;
    thr2 = 0;
    throttle1 = 0;
    throttle2 = 0;
    led(4, 1);
  } else {
    led(4, 0);
  }

  // if safe force esc to disarm and set leds
  if (ch[3] == 1000 && crsf.isLinkUp() == 1 && RP <= roll_flip) {
    thr1 = 0;
    thr2 = 0;
    throttle1 = 0;
    throttle2 = 0;
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

  throttleCmd[0] = thr1;
  throttleCmd[1] = thr2;

  // drive the esc's output every 1000microseconds
  unsigned long currentMicros = micros();

  if (currentMicros - previousMicros >= INTERVAL_US) {
    previousMicros += INTERVAL_US;
    uint32_t t0 = micros();
    for (int i = 0; i < NUM_ESC; i++) escs[i].send(throttleCmd[i]);
    uint32_t d = micros() - t0;
    if (d > escMaxUs) escMaxUs = d;
  }
}
