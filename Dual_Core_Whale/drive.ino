void drive_escs(int thr1, int thr2) {

  static unsigned long aboveStart = 0;
  static bool timing = false;
  static bool triggered = false;

  outChannel[0] = throttle1;
  outChannel[1] = throttle2;

  if (txchannel[5] > 1800) {
    if (!timing) {
      timing = true;
      aboveStart = millis();  // start the timer
    } else if (!triggered && millis() - aboveStart >= 1000) {
      triggered = true;  // make sure it only fires once

      // make this longr flash
      // static_led(0, 12, 0xffffff);
      // showLeds();
    }
  } else {
    // dropped below threshold: reset everything
    timing = false;
    triggered = false;
  }

  // if (escs[0].lossPercent() > 25) {
  //   // 5 fast blinking orange/black left: esc1 failsafe
  //   led(5, 1);
  //   modeString = "Lft ESC Off";
  // } else {
  //   led(5, 0);
  // }
  // if (escs[1].lossPercent() > 25) {
  //   // 6 fast blinking orange/black right: esc2 failsafe
  //   led(6, 1);
  //   modeString = "Rght ESC Off";
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
    modeString = "RC FailSafe";
  } else {
    led(4, 0);
  }

  // if safe force esc to disarm and set leds
  if (txchannel[3] < 1100 && crsf.isLinkUp() == 1 && RP <= (70)) {
    thr1 = 0;
    thr2 = 0;
    throttle1 = 0;
    throttle2 = 0;
    if (safed == 1) {
      led(3, 1);
      modeString = "Disarmed";
    }
  } else {
    led(3, 0);
  }

  if (txchannel[3] < 1100 && crsf.isLinkUp() == 1 && RP >= (70)) {
    thr1 = 0;
    thr2 = 0;
    throttle1 = 0;
    throttle2 = 0;
    if (safed == 1) {
      modeString = "Disarmed";
      led(17, 1);
    }
  } else {
    led(17, 0);
  }

  if (txchannel[3] != 1000 && crsf.isLinkUp() == 1) {
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

// ---------------- ESC telemetry helpers ----------------
// An ESC counts as present while it keeps sending valid telemetry. ageUs() is the time since the last
// good frame (huge if there never was one), so an ESC that goes quiet or is unplugged drops out after
// ESC_STALE_US. voltage() is NaN until the first extended-telemetry frame, so that must have arrived too.
// (Not lossPercent(): that is a running total since boot and almost never reaches 100.)
bool escPresent(int i) {
  return escs[i].ageUs() < ESC_STALE_US && isfinite(escs[i].voltage());
}

// Throttle shown on the page, in %: (output - 1500) / 5. An unset output (0) works out to -300, shown as 0.
int escThrPct(int i) {
  int thr = (outChannel[i] - 1500) / 5;
  return (thr <= -300) ? 0 : thr;
}

// ESC current, forced to 0 while the throttle reads 0. The ESC sends no new readings at zero throttle,
// so its last current would otherwise stay stuck at a non-zero value.
float escCurrent(int i) {
  return (escThrPct(i) == 0) ? 0.0f : escs[i].current();
}

float escSpeed(int i) {
  float rpmOut = escs[i].rpm() / gearbox_ratio[i];
  float spd = (wheel_diamiter[i] / 10) * rpmOut * 0.001885;
  // The ESC's eRPM telemetry has no direction in it, so the sign comes from the command:
  // an output below 1500 means the motor was told to run backwards, so the speed shows negative.
  return (outChannel[i] < 1500) ? -spd : spd;
}