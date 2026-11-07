void drive_escs(int thr1, int thr2) {

  static unsigned long aboveStart = 0;
  static bool timing = false;
  static bool triggered = false;

  outChannel[0] = throttle1;
  outChannel[1] = throttle2;

  // channel 5 held high for ESC_REBOOT_HOLD_MS: reboot every ESC (once per hold - let go to do it again)
  if (txchannel[ESC_REBOOT_CH] > ESC_REBOOT_US) {
    if (!timing) {
      timing = true;
      aboveStart = millis();  // start the timer
    } else if (!triggered && millis() - aboveStart >= ESC_REBOOT_HOLD_MS) {
      triggered = true;  // make sure it only fires once
      escRebootAll();
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

  // ESC reconnect: watch each ESC's telemetry, re-initialise any that dropped out, and ramp the
  // command (1000-2000, before scaling) of one that has just come back
  thr1 = escMonitorUpdate(0, thr1);
  if (NUM_ESC > 1) thr2 = escMonitorUpdate(1, thr2);
  for (int i = 2; i < NUM_ESC; i++) escMonitorUpdate(i, 0);

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

  // an ESC that is lost / re-arming only ever gets zero (disarmed); while ramping it gets the ramped command
  bool escMsg = false;
  for (int i = 0; i < NUM_ESC; i++) {
    bool ok = escAllowed(i);
    if (!ok) throttleCmd[i] = 0;
    if (i < 2) led(5 + i, !ok);  // dots 5 / 6: ESC1 / ESC2 not running
    if (!ok && !escMsg && crsf.isLinkUp()) {  // RC failsafe message wins over this one
      escMsg = true;
      modeString = String("ESC") + (i + 1) + (escState[i] == ESC_LOST ? " Lost" : escState[i] == ESC_CUT ? " Reboot" : " Rearm");
    }
  }

  // drive the esc's output every 1000microseconds
  unsigned long currentMicros = micros();

  if (currentMicros - previousMicros >= INTERVAL_US) {
    previousMicros += INTERVAL_US;
    uint32_t t0 = micros();
    for (int i = 0; i < NUM_ESC; i++) {
      if (escState[i] == ESC_CUT) continue;  // manual reboot: no signal at all, so the ESC drops out
      uint32_t s0 = micros();
      // link stats: did the previous frame get a telemetry reply? (counted only while the ESC is running normally)
      if (escLastSendUs[i] != 0 && (!escMonitorOn || escState[i] == ESC_OK)) {
        escFramesSent[i]++;
        if ((uint32_t)escs[i].ageUs() < s0 - escLastSendUs[i]) escFramesGood[i]++;
      }
      escs[i].send(throttleCmd[i]);
      escLastSendUs[i] = s0;
    }
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

// ---------------- ESC reconnect ----------------
// Per ESC:  OK --(no telemetry for ESC_LOST_US)--> LOST --(telemetry back)--> REARM (zero throttle, then EDT enable
// sent ESC_EDT_TRIES times, then stats reset) --> RAMP (stopped -> live command over ESC_RAMP_MS) --> OK.
// If it goes quiet again during REARM / RAMP it drops straight back to LOST.
// Manual reboot (channel 5): any state --> CUT (no signal for ESC_CUT_MS) --> LOST --> the normal path above.

// Link stats restart from zero (called on every successful reconnect)
void escResetStats(int i) {
  escFramesSent[i] = 0;
  escFramesGood[i] = 0;
}

// Percentage of DShot frames since the last (re)connect that got no telemetry reply. Replaces the library's
// lossPercent(), which is a running total since boot and can't be reset.
float escLossPct(int i) {
  uint32_t s = escFramesSent[i], g = escFramesGood[i];
  return (s == 0) ? 0.0f : 100.0f * (float)(s - g) / (float)s;
}

// true when the ESC may be given throttle (fully running, or ramping back up)
bool escAllowed(int i) {
  return !escMonitorOn || escState[i] == ESC_OK || escState[i] == ESC_RAMP;
}

// Called once in setup(), after the ESCs have booted and EDT was first enabled.
// An ESC that isn't answering yet starts as LOST, so one plugged in later gets set up when it appears.
void escMonitorStart() {
  for (int i = 0; i < NUM_ESC; i++) {
    escState[i] = ((uint32_t)escs[i].ageUs() < ESC_LOST_US) ? ESC_OK : ESC_LOST;
    escStateMs[i] = millis();
    escEdtTries[i] = 0;
    escResetStats(i);
  }
  escMonitorOn = true;
}

static void escSetState(int i, uint8_t st) {
  escState[i] = st;
  escStateMs[i] = millis();
}

// Reboot every ESC, whether or not it looks healthy
void escRebootAll() {
  if (!escMonitorOn) return;  // still booting: setup() is already doing this
  for (int i = 0; i < NUM_ESC; i++) escSetState(i, ESC_CUT);
  Serial.println("ESC reboot (channel 5)");
}

// true while the ESC's LEDs should blink orange: lost, rebooting or re-arming (not while ramping back up)
bool escLedAlert(int i) {
  return escMonitorOn && (escState[i] == ESC_LOST || escState[i] == ESC_REARM || escState[i] == ESC_CUT);
}

// Called from drive_escs() on every pass. rawThr = the 1000-2000 command for this ESC (0 or 1500 = stopped).
// Returns the command to actually use: rawThr normally, a scaled-down version of it while ramping.
int escMonitorUpdate(int i, int rawThr) {
  if (!escMonitorOn) return rawThr;
  bool hearing = (uint32_t)escs[i].ageUs() < ESC_LOST_US;
  uint32_t now = millis();

  switch (escState[i]) {
    case ESC_CUT:
      if (now - escStateMs[i] >= ESC_CUT_MS) escSetState(i, ESC_LOST);  // signal comes back (zero); waits to hear it
      return 0;

    case ESC_OK:
      if (!hearing) {
        escDrops[i]++;
        escSetState(i, ESC_LOST);
        Serial.printf("ESC%d lost\n", i + 1);
      }
      break;

    case ESC_LOST:
      if (hearing) {  // it's answering again (it may have rebooted): give it zero throttle so it arms
        escEdtTries[i] = 0;
        escSetState(i, ESC_REARM);
        Serial.printf("ESC%d back, re-arming\n", i + 1);
      }
      break;

    case ESC_REARM:
      if (!hearing) {
        escSetState(i, ESC_LOST);
        break;
      }
      if (now - escStateMs[i] >= (uint32_t)ESC_REARM_MS + escEdtTries[i] * (uint32_t)ESC_EDT_GAP_MS) {
        escs[i].command(DSHOT_CMD_EDT_ENABLE);  // a rebooted ESC has forgotten EDT, so ask for it again
        if (++escEdtTries[i] >= ESC_EDT_TRIES) {
          escResetStats(i);
          escReinits[i]++;
          escSetState(i, ESC_RAMP);
          Serial.printf("ESC%d re-initialised, ramping\n", i + 1);
        }
      }
      break;

    case ESC_RAMP:
      if (!hearing) {
        escSetState(i, ESC_LOST);
        break;
      }
      {
        uint32_t el = now - escStateMs[i];
        if (el >= ESC_RAMP_MS) {
          escSetState(i, ESC_OK);  // ramp finished: full command from here on
          break;
        }
        if (rawThr == 0) return 0;  // disarmed stays disarmed
        // scale the distance from centre (1500 = stopped) by how far through the ramp we are,
        // so it follows the live stick the whole time, just smaller at first
        float k = (float)el / (float)ESC_RAMP_MS;
        return 1500 + (int)lroundf((rawThr - 1500) * k);
      }
  }
  return rawThr;
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