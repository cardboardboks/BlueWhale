void status() {

  if (tuneDirty && millis() - tuneChangedMs > TUNE_SAVE_DELAY_MS) {
    tuneDirty = false;
    saveTune();
  }
  if (imuOffSave) {  // IMU calibrations finished in the IMU task: store them here (flash writes stay off that task)
    imuOffSave = false;
    saveImuOffsets();
  }
  if (accCalSave) {
    accCalSave = false;
    saveAccCal();
  }
  if (imuLevelSave) {  // Level pressed and the IMU took the reading: store it (flash writes stay off the IMU task)
    imuLevelSave = false;
    saveLevel();
  }

  // longest gap between loop() passes, reported once a second on the page's diagnostics line
  static uint32_t lastLoopUs = micros();
  uint32_t nowUs = micros();
  if (nowUs - lastLoopUs > maxGapUs) maxGapUs = nowUs - lastLoopUs;
  lastLoopUs = nowUs;

  // CPU load, smoothed: a rolling average, the same speed up and down (CPU_SMOOTH_ALPHA, about 1 s).
  // Runs once per CPU monitor sample (every 10 ms, same as cpucfg.sampleIntervalMs) so the smoothing
  // speed doesn't depend on how fast loop() spins.
  static uint32_t lastCpuMs = 0;
  if (millis() - lastCpuMs >= 10) {
    lastCpuMs = millis();
    CpuUsageSample sample;
    if (cpuMonitorOk && cpuMonitor.getLastSample(sample)) {
      core0Avg = smoothCpu(sample.perCore[0], core0Avg);
      core1Avg = smoothCpu(sample.perCore[1], core1Avg);
      coreAllAvg = smoothCpu(sample.average, coreAllAvg);

      // what the web page shows
      core0 = core0Avg;
      core1 = core1Avg;
      coreA = coreAllAvg;
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
    float v = escVolts(i);
    float a = escCurrent(i);
    float t = escTempC(i);
    if (isfinite(t) && t > recMaxTemp) recMaxTemp = t;
    if (a > recMaxA) recMaxA = a;
    sumA += a;
    if (!vbatValid() && v > 1.0f && (recMinV == 0 || v < recMinV)) recMinV = v;  // ESC voltage only when there's no divider
    sumSpd += fabsf(escSpeed(i));
  }
  if (vbatValid()) {  // battery divider, recorded even with no ESC reporting
    float vb = vbat;
    if (recMinV == 0 || vb < recMinV) recMinV = vb;
  }
  if (n > 0) {
    if (sumA > recMaxTotalA) recMaxTotalA = sumA;
    float avgSpd = sumSpd / n;
    if (avgSpd > recMaxSpd) recMaxSpd = avgSpd;
  }
}

// Rolling average: ease toward the raw reading at the same speed whether it's going up or down
float smoothCpu(float raw, float avg) {
  return avg + CPU_SMOOTH_ALPHA * (raw - avg);
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

// Bulk Text for the web page. Built only when the page data is put together (getSerializedData(), ~20 times a
// second) instead of on every loop() pass (~1000 a second) - same text, a fraction of the CPU and heap churn.
// Returns a fresh String each time, so it's safe from the web server's task too (the page's first update on connect).
String buildBulkText() {
  // line 2, per ESC: loss % since (re)connect, then drops/reinits since boot
  String escLine = "";
  for (int i = 0; i < NUM_ESC; i++) {
    if (i) escLine += "  ";
    escLine += String(escLossPct(i), 1) + "% d" + String(escDrops[i]) + "/r" + String(escReinits[i]) + "/b" + String(escBootAssists[i]);
  }
  // DShot timing over the last second: longest tick, and the shortest-longest gap between ticks
  escLine += "  esc " + String(escShowTickMs, 2) + "ms dshot " + String(escShowMinMs, 2) + "-" + String(escShowMaxMs, 2) + "ms";
  // per ESC, replies in the last second: ok / none / garbled (framing, GCR, CRC), and the wiring echo (31 = good)
  for (int i = 0; i < NUM_ESC; i++)
    escLine += "\nE" + String(i + 1) + " ok " + String(escSecOk[i]) + " none " + String(escSecNr[i]) + " fr " + String(escSecFr[i]) +
               " gcr " + String(escSecGcr[i]) + " crc " + String(escSecCrc[i]) + " echo " + String(escEcho[i]);
  String out = String(safe_switch) + String(safe_sticks) + String(safe_link) + "\n" + escLine + "\nTilt: " + (imuLevelled ? String((float)imuTiltDeg, 0) + "° (dmp " + String((float)imuTiltDmpDeg, 0) + " acc " + String((float)imuTiltAccDeg, 0) + ")" : String("not levelled")) + "\n" + yawStatusLine() +  // yaw controller line (yaw_pid.ino)
               "\n" + battStatusLine();  // battery protection (battery.ino)
  if (bulkText.length()) {  // anything you've put in bulkText yourself
    out += "\n";
    out += bulkText;
  }
  return out;
}
