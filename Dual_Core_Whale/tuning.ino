
// IMU "upright" direction (Level button). Nothing valid saved -> not levelled (up/down treated as no IMU).
void loadLevel() {
  imuRef[0] = imuRef[1] = imuRef[2] = 0;
  imuLevelled = false;
  float tmp[6];  // DMP up (3), then accel up (3). An older 3-value save doesn't match the size and is ignored.
  prefs.begin("whale", true);
  if (prefs.getBytesLength("lvl") == sizeof(tmp)) {
    prefs.getBytes("lvl", tmp, sizeof(tmp));
    float n = sqrtf(tmp[0] * tmp[0] + tmp[1] * tmp[1] + tmp[2] * tmp[2]);
    float m = sqrtf(tmp[3] * tmp[3] + tmp[4] * tmp[4] + tmp[5] * tmp[5]);
    if (n > 0.9f && n < 1.1f && m > 0.9f && m < 1.1f) {
      for (int i = 0; i < 3; i++) {
        imuRef[i] = tmp[i] / n;
        imuRefA[i] = tmp[3 + i] / m;
      }
      imuLevelled = true;
    }
  }
  prefs.end();
  if (!imuLevelled) Serial.println("IMU not levelled - press Level on the web page (up/down disabled until then)");
}

void saveLevel() {
  float tmp[6] = { imuRef[0], imuRef[1], imuRef[2], imuRefA[0], imuRefA[1], imuRefA[2] };
  prefs.begin("whale", false);
  prefs.putBytes("lvl", tmp, sizeof(tmp));
  prefs.end();
  Serial.printf("IMU levelled: dmp up = %.3f %.3f %.3f  accel up = %.3f %.3f %.3f\n", tmp[0], tmp[1], tmp[2], tmp[3], tmp[4], tmp[5]);
}

// IMU calibrations (Tuning page). Each one is only used if it was saved and looks sane.
void loadImuCal() {
  prefs.begin("whale", true);
  int16_t o[6];
  imuOffsValid = false;
  if (prefs.getBytesLength("imuoff") == sizeof(o)) {
    prefs.getBytes("imuoff", o, sizeof(o));
    for (int i = 0; i < 6; i++) imuOffs[i] = o[i];
    imuOffsValid = true;
  }
  float c[6];
  accCalValid = false;
  if (prefs.getBytesLength("acal") == sizeof(c)) {
    prefs.getBytes("acal", c, sizeof(c));
    bool ok = true;
    for (int i = 0; i < 3; i++)
      if (!isfinite(c[i]) || !(c[3 + i] > 0.25f * DMP_ACCEL_LSB_PER_G && c[3 + i] < 4.0f * DMP_ACCEL_LSB_PER_G)) ok = false;
    if (ok) {
      for (int i = 0; i < 3; i++) {
        accOff[i] = c[i];
        accScale[i] = c[3 + i];
      }
      accCalValid = true;
    }
  }
  prefs.end();
  Serial.printf("IMU offsets: %s  6-side accel: %s\n", imuOffsValid ? "saved" : "factory", accCalValid ? "saved" : "none");
}

// A calibration changed how the IMU reads, so the saved level no longer matches: remove it too.
void saveImuOffsets() {
  int16_t o[6];
  for (int i = 0; i < 6; i++) o[i] = imuOffs[i];
  prefs.begin("whale", false);
  if (imuOffsValid) prefs.putBytes("imuoff", o, sizeof(o));
  else prefs.remove("imuoff");  // factory offsets
  prefs.remove("lvl");
  prefs.end();
  if (imuOffsValid) Serial.printf("IMU offsets saved: accel %d %d %d  gyro %d %d %d\n", o[0], o[1], o[2], o[3], o[4], o[5]);
  else Serial.println("IMU offsets: back to factory");
}

void saveAccCal() {
  prefs.begin("whale", false);
  if (accCalValid) {
    float c[6] = { accOff[0], accOff[1], accOff[2], accScale[0], accScale[1], accScale[2] };
    prefs.putBytes("acal", c, sizeof(c));
    Serial.printf("6-side accel saved: off %.0f %.0f %.0f  scale %.0f %.0f %.0f\n", c[0], c[1], c[2], c[3], c[4], c[5]);
  } else {
    prefs.remove("acal");
  }
  if (!imuLevelled) prefs.remove("lvl");
  prefs.end();
}

void loadTune() {
  float tmp[NUM_TUNE];
  for (int i = 0; i < NUM_TUNE; i++) tuneVal[i] = tuneDefault[i];  // anything never saved keeps its default
  prefs.begin("whale", true);  // read-only
  size_t n = prefs.getBytesLength("tune");
  size_t k = n / sizeof(float);
  if (n % sizeof(float) == 0 && k >= 1 && k <= NUM_TUNE) {  // a save from a shorter list loads what it has
    prefs.getBytes("tune", tmp, n);
    for (size_t i = 0; i < k; i++)
      if (isfinite(tmp[i])) tuneVal[i] = tmp[i];
    if (k == 20) {  // the old 20-box layout: boxes 11-15 were unused placeholders, and 6-10 may be blank
      for (int i = 11; i <= 15; i++) tuneVal[i] = tuneDefault[i];
      for (int i = 6; i <= 10; i++)
        if (tmp[i] == 0) tuneVal[i] = tuneDefault[i];
    }
  }
  for (int i = 0; i < NUM_TUNE_STR; i++) {
    char key[8], txt[TUNE_STR_LEN] = "";
    snprintf(key, sizeof(key), "ts%d", i);
    prefs.getString(key, txt, sizeof(txt));  // stays empty if nothing was saved yet
    strlcpy(tuneStr[i], txt, TUNE_STR_LEN);
  }
  prefs.end();
}

void saveTune() {
  float tmp[NUM_TUNE];
  for (int i = 0; i < NUM_TUNE; i++) tmp[i] = tuneVal[i];
  prefs.begin("whale", false);  // read/write
  prefs.putBytes("tune", tmp, sizeof(tmp));
  for (int i = 0; i < NUM_TUNE_STR; i++) {
    char key[8], txt[TUNE_STR_LEN];
    snprintf(key, sizeof(key), "ts%d", i);
    strlcpy(txt, tuneStr[i], TUNE_STR_LEN);
    prefs.putString(key, txt);
  }
  prefs.end();
}