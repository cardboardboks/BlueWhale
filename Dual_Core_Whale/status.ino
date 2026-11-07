void status() {

  if (tuneDirty && millis() - tuneChangedMs > TUNE_SAVE_DELAY_MS) {
    tuneDirty = false;
    saveTune();
  }

  // ---- Bulk text ----
  bulkText = String(safe_switch) + String(safe_sticks) + String(safe_link) + "\n" + String(escs[0].lossPercent()) + "  " + String(escs[1].lossPercent()) + "\nTilt: " + String(RP) + "  Yaw: " + String(Y);

  // longest gap between loop() passes, reported once a second on the page's diagnostics line
  static uint32_t lastLoopUs = micros();
  uint32_t nowUs = micros();
  if (nowUs - lastLoopUs > maxGapUs) maxGapUs = nowUs - lastLoopUs;
  lastLoopUs = nowUs;

  // CPU load with peak-hold: jumps up instantly, then decays back toward the live reading.
  // Runs once per CPU monitor sample (every 10 ms, same as cpucfg.sampleIntervalMs) so the decay
  // speed doesn't depend on how fast loop() spins. With alpha 0.01 the fall-off takes about 1 second.
  static uint32_t lastCpuMs = 0;
  if (millis() - lastCpuMs >= 10) {
    lastCpuMs = millis();
    CpuUsageSample sample;
    if (cpuMonitorOk && cpuMonitor.getLastSample(sample)) {
      core0Peak = peakHold(sample.perCore[0], core0Peak);
      core1Peak = peakHold(sample.perCore[1], core1Peak);
      coreAveragePeak = peakHold(sample.average, coreAveragePeak);

      // what the web page shows
      core0 = core0Peak;
      core1 = core1Peak;
      coreA = coreAveragePeak;
    }
  }
}

// Called on every pass of loop() (about every 1 ms): updates max temperature, max current, min voltage, max average speed
void updateRecords() {
  float sumSpd = 0;
  float sumA = 0;
  int n = 0;
  for (int i = 0; i < NUM_ESC; i++) {
    if (!escPresent(i)) continue;
    n++;
    float v = escs[i].voltage();
    float a = escCurrent(i);
    float t = escs[i].temperatureC();
    if (isfinite(t) && t > recMaxTemp) recMaxTemp = t;
    if (a > recMaxA) recMaxA = a;
    sumA += a;
    if (v > 1.0f && (recMinV == 0 || v < recMinV)) recMinV = v;
    sumSpd += fabsf(escSpeed(i));
  }
  if (n > 0) {
    if (sumA > recMaxTotalA) recMaxTotalA = sumA;
    float avgSpd = sumSpd / n;
    if (avgSpd > recMaxSpd) recMaxSpd = avgSpd;
  }
}

// Peak hold: a new high is taken immediately, otherwise ease down toward the raw reading
float peakHold(float raw, float peak) {
  if (raw > peak) return raw;
  return raw * PEAK_DECAY_ALPHA + peak * (1.0f - PEAK_DECAY_ALPHA);
}

// Why the board last restarted - shown on the page and printed at boot. This is the quickest way to tell
// a brownout (power dip) from a crash (PANIC), a watchdog (IWDT / TWDT) or a normal restart (SW / PWR).
const char *resetReasonShort() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "PWR";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "IWDT";
    case ESP_RST_TASK_WDT: return "TWDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BROWN";
    case ESP_RST_DEEPSLEEP: return "SLEEP";
    default:

      {  // any other reason (power glitch, USB, CPU lock-up, ...): show the raw number so it can be looked up
        static char other[8];
        snprintf(other, sizeof(other), "R%d", (int)esp_reset_reason());
        return other;
      }
  }
}