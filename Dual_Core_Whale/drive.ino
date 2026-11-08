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
  if (txchannel[3] < 1100 && crsf.isLinkUp() == 1 && !robotDown()) {
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

  if (txchannel[3] < 1100 && crsf.isLinkUp() == 1 && robotDown()) {
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

  // ESC Dir boxes: reverse a motor in software (1000..2000 mirrored about 1500; 0 = disarm stays 0)
  if (NUM_ESC > 0 && tuneVal[T_ESC_DIR + 0] >= 0.5f && thr1 != 0) thr1 = 3000 - thr1;
  if (NUM_ESC > 1 && tuneVal[T_ESC_DIR + 1] >= 0.5f && thr2 != 0) thr2 = 3000 - thr2;

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

  // Work out each ESC's final command here, then write throttleCmd once. The DShot task can send at any moment
  // (it runs above loop()), so throttleCmd must never hold a value that isn't meant to go out - writing the stick
  // value first and zeroing it afterwards let a lost / re-arming ESC catch the odd frame of real throttle.
  float cmd[MAX_ESC] = { (float)thr1, (float)thr2, 0.0f, 0.0f };

  // an ESC that is lost / re-arming only ever gets zero (disarmed); while ramping it gets the ramped command
  bool escMsg = false;
  for (int i = 0; i < NUM_ESC; i++) {
    bool ok = escAllowed(i);
    if (!ok) cmd[i] = 0;
    if (i < 2) led(5 + i, !ok);  // dots 5 / 6: ESC1 / ESC2 not running
    if (!ok && !escMsg && crsf.isLinkUp()) {  // RC failsafe message wins over this one
      escMsg = true;
      modeString = String("ESC") + (i + 1) + (escState[i] == ESC_LOST ? " Lost" : escState[i] == ESC_CUT ? " Reboot" : " Rearm");
    }
  }
  for (int i = 0; i < MAX_ESC; i++) throttleCmd[i] = cmd[i];  // one write per ESC, already final

  // The DShot task (below) sends throttleCmd every 1 ms. Stamp the time so it knows these are fresh.
  escCmdStampMs = millis();
}

// ---------------- DShot task ----------------
// Runs on core 1 above loop(), woken every ESC_TICK_US (1000 µs) by an esp_timer - precise, not tied to the FreeRTOS
// tick - so frames go out on time whatever loop() is doing. Each tick sends every ESC its frame (which also collects
// the reply to the previous one), runs any queued EDT enable, and copies the telemetry and the library's reply
// counters out for the rest of the sketch. The task also calls the ESCs' begin(): it is the only code that ever
// touches the ESC library.
// Safety: if loop() stops refreshing the throttles for ESC_CMD_TIMEOUT_MS (OTA update, a hang, a crash), or an OTA
// update is running, it sends stop (0) instead of repeating the last command.
// Bulk Text's ESC line shows "esc" (longest tick) and "dshot min-max" (shortest-longest gap between ticks), each second.
#define ESC_TASK_PRIO (configMAX_PRIORITIES - 2)  // above loop() (1) and the web/TCP tasks on core 1
#define ESC_CMD_TIMEOUT_MS 100
volatile bool escEdtPending[MAX_ESC] = { false, false, false, false };  // send "enable EDT" on the next tick
TaskHandle_t escTaskHandle = nullptr;
static esp_timer_handle_t escTimer = nullptr;

// Ask for "enable extended telemetry" to be sent to ESC i on the next tick
void escRequestEdt(int i) {
  if (i >= 0 && i < NUM_ESC) escEdtPending[i] = true;
}

// Boot assist for ESC i (see ESC_BOOT_... in the main sketch). Returns true while it has the ESC's line, so the tick
// skips its normal frame. Never blocks: the hold is timed across ticks, so the other ESC keeps getting its frames.
static bool escBootAssist(int i) {
  static bool holding[MAX_ESC] = { false };
  static uint32_t holdUntil[MAX_ESC] = { 0 }, retryAt[MAX_ESC] = { 0 };
  uint32_t now = millis();
  if (holding[i]) {
    if ((int32_t)(now - holdUntil[i]) < 0) return true;  // keep holding the line low
    holding[i] = false;
    escs[i].begin(ESC_PINS[i], DSHOT600, true, MOTOR_POLES);  // back to DShot; it sends zeros ~1.2 s so AM32 arms
    retryAt[i] = now + ESC_BOOT_RETRY_MS;
    return true;  // begin() already sent this tick's frame
  }
  if (escMonitorOn && escState[i] == ESC_LOST && now - escStateMs[i] >= ESC_BOOT_AFTER_MS &&
      (int32_t)(now - retryAt[i]) >= 0) {
    escs[i].end();  // release the RMT channels, then hold the pin low (open drain, like setup() does at power-up)
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << ESC_PINS[i];
    cfg.mode = GPIO_MODE_OUTPUT_OD;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    gpio_set_level((gpio_num_t)ESC_PINS[i], 0);
    holding[i] = true;
    holdUntil[i] = now + ESC_BOOT_HOLD_MS;
    escBootAssists[i]++;
    return true;
  }
  return false;
}

void escTick() {
  static uint32_t lastTickUs = 0;
  uint32_t t0 = micros();
  if (lastTickUs != 0) {  // tick spacing, for the diagnostics line
    uint32_t p = t0 - lastTickUs;
    if (p < escPeriodMinUs) escPeriodMinUs = p;
    if (p > escPeriodMaxUs) escPeriodMaxUs = p;
  }
  lastTickUs = t0;

  bool stale = otaInProgress || (millis() - escCmdStampMs > ESC_CMD_TIMEOUT_MS);
  for (int i = 0; i < NUM_ESC; i++) {
    if (escBootAssist(i)) continue;  // lost ESC: its line is being held low / DShot restarting (see escBootAssist)
    if (escEdtPending[i]) {  // a queued EDT enable goes out in place of this tick's throttle frame
      escEdtPending[i] = false;
      escs[i].command(DSHOT_CMD_EDT_ENABLE);
      continue;
    }
    if (escState[i] == ESC_CUT) continue;  // manual reboot: no signal at all, so the ESC drops out
    escs[i].send(stale ? 0 : throttleCmd[i]);  // also collects the reply to the previous frame
  }

  // copy the telemetry out for everyone else (each value is a single 32-bit store, so it's never read half-updated)
  uint32_t nowUs = micros();
  for (int i = 0; i < NUM_ESC; i++) {
    uint64_t age = (uint64_t)escs[i].ageUs();
    if (age < 0x7FFFFFFFULL) {  // "huge" = never had a reply
      escRxUs[i] = nowUs - (uint32_t)age;
      escRxEver[i] = true;
    }
    escSnapV[i] = escs[i].voltage();
    escSnapA[i] = escs[i].current();
    escSnapT[i] = escs[i].temperatureC();
    escSnapRpm[i] = escs[i].rpm();
    const AlfredoDShot::Stats &st = escs[i].stats();  // the library's reply counters
    escStOk[i] = st.ok;
    escStNoRep[i] = st.noReply;
    escStFrame[i] = st.framing;
    escStGcr[i] = st.badGcr;
    escStCrc[i] = st.badCrc;
    escEcho[i] = escs[i].echoPulses();
  }
  uint32_t d = micros() - t0;  // whole tick, stats() included (it can be the slow part)
  if (d > escMaxUs) escMaxUs = d;
}

// esp_timer callback (runs in the esp_timer task): wake the DShot task
static void escTimerCb(void *arg) {
  if (escTaskHandle) xTaskNotifyGive(escTaskHandle);
}

volatile bool escTaskReady = false;

void escTask(void *arg) {
  // The task starts the ESCs itself, so the same task owns the library from begin() on. The library may tie its
  // telemetry receiving to whichever task called begin() - with begin() in setup() and sending here, the ESCs dropped.
  for (int i = 0; i < NUM_ESC; i++) escs[i].begin(ESC_PINS[i], DSHOT600, true, MOTOR_POLES);
  escTaskReady = true;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));  // woken every 1000 µs by the timer (5 ms = safety net)
    escTick();
  }
}

// Called once in setup(): starts the DShot task (which starts the ESCs), waits for it, then starts the 1 ms timer.
void escTaskStart() {
  escCmdStampMs = millis();
  xTaskCreatePinnedToCore(escTask, "dshot", 4096, nullptr, ESC_TASK_PRIO, &escTaskHandle, 1);
  for (uint32_t t0 = millis(); !escTaskReady && millis() - t0 < 1000;) delay(1);  // wait for it to start the ESCs
  if (!escTaskReady) Serial.println("DShot task didn't start the ESCs within 1 s");
  esp_timer_create_args_t args = {};
  args.callback = escTimerCb;
  args.name = "dshot";
  if (esp_timer_create(&args, &escTimer) == ESP_OK) esp_timer_start_periodic(escTimer, ESC_TICK_US);
}

// ---- telemetry for the rest of the sketch (the DShot task's copies; safe to call from any task) ----
uint32_t escAgeUs(int i) {  // time since the last good reply, µs (0xFFFFFFFF = never)
  if (!escRxEver[i]) return 0xFFFFFFFFUL;
  // Read the reply time BEFORE the clock. The DShot task can interrupt this function (it runs above loop() on the
  // same core) and store a newer reply time; read the other way round, that reply time could be later than "now",
  // the subtraction wrapped to ~4000 s, and a perfectly healthy ESC was declared lost. Negative also clamps to 0.
  uint32_t rx = escRxUs[i];
  __asm__ __volatile__("" ::: "memory");  // keep the compiler from swapping the two reads
  int32_t age = (int32_t)(micros() - rx);
  return (age < 0) ? 0 : (uint32_t)age;
}
float escVolts(int i) { return escSnapV[i]; }  // NaN until the first extended-telemetry frame
float escAmps(int i) { return escSnapA[i]; }
float escTempC(int i) { return escSnapT[i]; }
float escRpm(int i) { return escSnapRpm[i]; }


// ---------------- ESC telemetry helpers ----------------
// An ESC counts as present while it keeps sending valid telemetry. ageUs() is the time since the last
// good frame (huge if there never was one), so an ESC that goes quiet or is unplugged drops out after
// ESC_STALE_US. voltage() is NaN until the first extended-telemetry frame, so that must have arrived too.
// (Not lossPercent(): that is a running total since boot and almost never reaches 100.)
bool escPresent(int i) {
  return escAgeUs(i) < ESC_STALE_US && isfinite(escVolts(i));
}

// ---------------- ESC reconnect ----------------
// Per ESC:  OK --(no telemetry for ESC_LOST_US)--> LOST --(telemetry back)--> REARM (zero throttle, then EDT enable
// sent ESC_EDT_TRIES times, then stats reset) --> RAMP (stopped -> live command over ESC_RAMP_MS) --> OK.
// If it goes quiet again during REARM / RAMP it drops straight back to LOST.
// Manual reboot (channel 5), blind: any state --> CUT (no signal for ESC_CUT_MS) --> REARM --> RAMP --> OK, on
// timers alone. Telemetry is ignored the whole way, so it always hands control back.
// In OK, an ESC only counts as lost if it has been heard from since entering OK - so one whose telemetry
// didn't come back after a blind reboot keeps driving instead of locking out again.

static uint32_t escTried(int i) {  // replies the library has tried to decode, since boot
  return escStOk[i] + escStNoRep[i] + escStFrame[i] + escStGcr[i] + escStCrc[i];
}

// Link stats restart from zero (called on every successful reconnect): remember where the library's totals are now
void escResetStats(int i) {
  escBaseOk[i] = escStOk[i];
  escBaseTried[i] = escTried(i);
}

// Percentage of frames since the last (re)connect whose reply was missing or garbled - from the library's own
// counters, so EDT replies (voltage / current / temperature) count as good, which the old measure got wrong.
float escLossPct(int i) {
  // Read "ok" before "tried": the DShot task can update the counters between the reads (it runs above loop(), and the
  // page's first update on connect is built from the web server's task). Read the other way round, ok could come out
  // bigger than tried and the subtraction wrapped to a huge number. Clamped as well, to be sure.
  uint32_t ok = escStOk[i] - escBaseOk[i];
  __asm__ __volatile__("" ::: "memory");
  uint32_t tried = escTried(i) - escBaseTried[i];
  if (tried == 0 || ok >= tried) return 0.0f;
  return 100.0f * (float)(tried - ok) / (float)tried;
}

// true when the ESC may be given throttle (fully running, or ramping back up)
bool escAllowed(int i) {
  return !escMonitorOn || escState[i] == ESC_OK || escState[i] == ESC_RAMP;
}

// Called once in setup(), after the ESCs have booted and EDT was first enabled.
// An ESC that isn't answering yet starts as LOST, so one plugged in later gets set up when it appears.
void escMonitorStart() {
  for (int i = 0; i < NUM_ESC; i++) {
    escState[i] = (escAgeUs(i) < ESC_LOST_US) ? ESC_OK : ESC_LOST;
    escStateMs[i] = millis();
    escEdtTries[i] = 0;
    escResetStats(i);
  }
  escMonitorOn = true;
}

static void escSetState(int i, uint8_t st) {
  escStateMs[i] = millis();  // time first, then the state: the DShot task never sees a new state with an old time
  escState[i] = st;
  if (st == ESC_OK) {
    escHeard[i] = false;  // must hear it again before it can be declared lost
    escBlind[i] = false;
  }
}

// Reboot every ESC blind, whether or not it looks healthy
void escRebootAll() {
  if (!escMonitorOn) return;  // still booting: setup() is already doing this
  for (int i = 0; i < NUM_ESC; i++) {
    escBlind[i] = true;
    escEdtTries[i] = 0;
    escSetState(i, ESC_CUT);
  }
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
  bool hearing = escAgeUs(i) < ESC_LOST_US;
  uint32_t now = millis();

  switch (escState[i]) {
    case ESC_CUT:
      if (now - escStateMs[i] >= ESC_CUT_MS) {  // signal back on (zero throttle) and straight into re-arm
        escEdtTries[i] = 0;
        escSetState(i, ESC_REARM);
      }
      return 0;

    case ESC_OK:
      if (hearing && !escHeard[i]) {
        escHeard[i] = true;
      } else if (!hearing && escHeard[i]) {
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
      if (!hearing && !escBlind[i]) {
        escSetState(i, ESC_LOST);
        break;
      }
      if (now - escStateMs[i] >= (uint32_t)ESC_REARM_MS + escEdtTries[i] * (uint32_t)ESC_EDT_GAP_MS) {
        escRequestEdt(i);  // a rebooted ESC has forgotten EDT, so ask for it again (sent by the DShot task)
        if (++escEdtTries[i] >= ESC_EDT_TRIES) {
          escResetStats(i);
          escReinits[i]++;
          escSetState(i, ESC_RAMP);
          Serial.printf("ESC%d re-initialised, ramping\n", i + 1);
        }
      }
      break;

    case ESC_RAMP:
      if (!hearing && !escBlind[i]) {
        escSetState(i, ESC_LOST);
        break;
      }
      {
        uint32_t el = now - escStateMs[i];
        if (el >= ESC_RAMP_MS) {
          if (escBlind[i]) Serial.printf("ESC%d blind reboot done, telemetry %s\n", i + 1, hearing ? "OK" : "NOT back");
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
  return (escThrPct(i) == 0) ? 0.0f : escAmps(i);
}

float escSpeed(int i) {
  float rpmOut = escRpm(i) / gearbox_ratio[i];
  float spd = (wheel_diamiter[i] / 10) * rpmOut * 0.001885;
  // The ESC's eRPM telemetry has no direction in it, so the sign comes from the command:
  // an output below 1500 means the motor was told to run backwards, so the speed shows negative.
  return (outChannel[i] < 1500) ? -spd : spd;
}