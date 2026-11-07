bool imuReadFifoCount(uint16_t &count) {
  Wire.beginTransmission(IMU_I2C_ADDR);
  Wire.write(0x72);  // FIFO_COUNT_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)IMU_I2C_ADDR, (size_t)2, true) != 2) return false;
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  count = ((uint16_t)hi << 8) | lo;
  return true;
}

bool imuReadFifo(uint8_t *dst, uint8_t n) {
  Wire.beginTransmission(IMU_I2C_ADDR);
  Wire.write(0x74);  // FIFO_R_W
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)IMU_I2C_ADDR, (size_t)n, true) != n) return false;
  for (uint8_t i = 0; i < n; i++) dst[i] = Wire.read();
  return true;
}

// Start the sensor and load the DMP. Returns 0 on success (same as dmpInitialize()). 'found' says if it answered at all.
uint8_t imuBegin(bool &found) {
  mpu.initialize();
  found = mpu.testConnection();
  if (!found) return 1;
  uint8_t st = mpu.dmpInitialize();
  if (st == 0 && imuOffsValid) {  // calibrated offsets (Tuning page); otherwise the chip's factory values stay
    mpu.setXAccelOffset(imuOffs[0]);
    mpu.setYAccelOffset(imuOffs[1]);
    mpu.setZAccelOffset(imuOffs[2]);
    mpu.setXGyroOffset(imuOffs[3]);
    mpu.setYGyroOffset(imuOffs[4]);
    mpu.setZGyroOffset(imuOffs[5]);
  }
  return st;
}

// Called from the IMU task after repeated bus failures.
void imuRecover() {
  imuRecoveries++;
  Wire.end();
  // A device that was mid-transfer when the wire moved can hold SDA low forever. Clocking SCL up to 9 times lets it finish.
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, OUTPUT);
  for (int i = 0; i < 9; i++) {
    digitalWrite(SCL_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(SCL_PIN, HIGH);
    delayMicroseconds(5);
  }
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  Wire.setTimeOut(IMU_I2C_TIMEOUT_MS);
  bool found = false;
  if (imuBegin(found) == 0) {  // the sensor may have power-cycled and lost the DMP, so load it again
    mpu.setDMPEnabled(true);
    mpu.resetFIFO();
    imuWarm = 0;
  }
}

void imuTask(void *arg) {
  Med3 mGx, mGy, mGz, mAx, mAy, mAz;
  uint8_t imuFailStreak = 0;  // failed I2C transfers in a row
  if (otaInProgress == false) {
    for (;;) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

      // ---- calibration requests from the Tuning page (run here: this task owns the I2C bus) ----
      if (imuCalOffReq) {  // flat offset calibration: blocks this task for a few seconds
        imuCalOffReq = false;
        imuCalMsg = "Calibrating offsets - keep still";
        mpu.setDMPEnabled(false);
        mpu.CalibrateAccel(6);
        mpu.CalibrateGyro(6);
        imuOffs[0] = mpu.getXAccelOffset();
        imuOffs[1] = mpu.getYAccelOffset();
        imuOffs[2] = mpu.getZAccelOffset();
        imuOffs[3] = mpu.getXGyroOffset();
        imuOffs[4] = mpu.getYGyroOffset();
        imuOffs[5] = mpu.getZGyroOffset();
        mpu.setDMPEnabled(true);
        mpu.resetFIFO();
        imuWarm = 0;
        imuFailStreak = 0;
        imuOffsValid = true;
        imuOffSave = true;
        for (int k = 0; k < 3; k++) {  // the old 6-side data was taken with the old offsets: start that again
          accOff[k] = 0;
          accScale[k] = DMP_ACCEL_LSB_PER_G;
        }
        accCalValid = false;
        sideMask = 0;
        calN = 0;
        accCalSave = true;
        imuLevelled = false;  // and the level
        imuCalMsg = "Offsets saved - redo 6-side (optional), then Level";
        continue;
      }
      if (imuCalFactoryReq) {  // back to the chip's own offsets: restarting the IMU resets them
        imuCalFactoryReq = false;
        imuOffsValid = false;
        bool found = false;
        if (imuBegin(found) == 0) {
          mpu.setDMPEnabled(true);
          mpu.resetFIFO();
        }
        imuWarm = 0;
        imuFailStreak = 0;
        imuOffSave = true;      // removes the saved offsets
        imuCalClearReq = true;  // 6-side and level were taken with the old offsets
        imuLevelled = false;
        imuCalFactoryMsg = true;
        continue;
      }
      if (imuCalClearReq) {
        imuCalClearReq = false;
        for (int k = 0; k < 3; k++) {
          accOff[k] = 0;
          accScale[k] = DMP_ACCEL_LSB_PER_G;
        }
        if (accCalValid) imuLevelled = false;  // accel readings change, so the level does too
        accCalValid = false;
        sideMask = 0;
        calN = 0;
        accCalSave = true;  // removes the saved 6-side data
        imuCalMsg = imuCalFactoryMsg ? "Factory offsets - redo 6-side, then Level" : "6-side cleared";
        imuCalFactoryMsg = false;
      }

      uint32_t cn0 = micros();
      uint16_t fc = 0;
      bool countOk = imuReadFifoCount(fc);
      uint32_t cnUs = micros() - cn0;
      if (cnUs > imuCountMaxUs) imuCountMaxUs = cnUs;
      if (!countOk) {  // sensor didn't answer: count it, and after a run of failures reset the bus and restart the IMU
        imuI2cErrors++;
        if (++imuFailStreak >= IMU_FAIL_LIMIT) {
          imuRecover();
          imuFailStreak = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      if (fc > imuFifoMax) imuFifoMax = fc;
      if (fc < IMU_PKT) {  // no complete packet yet
        imuFailStreak = 0;
        continue;
      }
      if (fc > 200) {  // FIFO overflowed: its contents can't be trusted, clear it and wait for fresh packets
        imuOverflows++;
        mpu.resetFIFO();
        continue;
      }
      if (fc >= 2 * IMU_PKT) imuMultiPkt++;
      uint32_t rd0 = micros();
      bool gotPacket = true;
      while (gotPacket && fc >= 2 * IMU_PKT) {  // several packets waiting: read and discard the old ones, keep the newest
        gotPacket = imuReadFifo(fifoBuffer, IMU_PKT);
        fc -= IMU_PKT;
      }
      if (gotPacket) gotPacket = imuReadFifo(fifoBuffer, IMU_PKT);
      if (gotPacket) {
        imuFailStreak = 0;
      } else {
        imuI2cErrors++;
        if (++imuFailStreak >= IMU_FAIL_LIMIT) {
          imuRecover();
          imuFailStreak = 0;
        }
      }
      uint32_t rdUs = micros() - rd0;
      if (rdUs > imuReadMaxUs) imuReadMaxUs = rdUs;
      bool lateRead = rdUs > IMU_LATE_US;
      if (lateRead) imuLateReads++;
      if (!gotPacket) continue;

      mpu.dmpGetQuaternion(&q, fifoBuffer);

      // A good packet has a unit-length quaternion. A packet damaged by I2C noise (or a FIFO overflow)
      // doesn't, so drop it instead of letting it spike the tilt, yaw, G and the page values.
      float qn = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
      if (qn < 0.9f || qn > 1.1f) {
        imuBadPackets++;
        if (lateRead) imuBadOnLate++;
        imuLastBadQn = qn;
        mpu.resetFIFO();  // re-sync with the packet boundaries
        continue;
      }

      mpu.dmpGetGravity(&gravity, &q);
      mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

      pitch = (int)fabsf(ypr[1] * 180.0f / M_PI);
      roll = (int)fabsf(ypr[2] * 180.0f / M_PI);

      RPtotal -= RPreadings[RPreadIndex];
      RPreadings[RPreadIndex] = min(roll + pitch, 140);  // tilt shown in Bulk Text only; up/down comes from robotDown()
      RPtotal += RPreadings[RPreadIndex];
      RPreadIndex = (RPreadIndex + 1) % RPnumReadings;
      RP = RPtotal / RPnumReadings;

      mpu.dmpGetGyro(&gy, fifoBuffer);
      mpu.dmpGetAccel(&aa, fifoBuffer);

      // A half-read packet can have a valid quaternion but all-zero accelerometer bytes. Real readings are never
      // exactly 0,0,0 on every axis, and a zero here makes the gravity-removal below report a false 1.00 G.
      if (aa.x == 0 && aa.y == 0 && aa.z == 0) {
        imuBadPackets++;
        imuLastBadQn = qn;
        mpu.resetFIFO();
        continue;
      }

      imuLastGoodMs = millis();  // this packet passed every check

      // median-of-3 on every axis to remove single-sample spikes
      gy.x = mGx.add(gy.x);
      gy.y = mGy.add(gy.y);
      gy.z = mGz.add(gy.z);
      aa.x = mAx.add(aa.x);
      aa.y = mAy.add(aa.y);
      aa.z = mAz.add(aa.z);

      imuGx = gy.x;
      imuGy = gy.y;
      imuGz = gy.z;
      imuAx = aa.x;
      imuAy = aa.y;
      imuAz = aa.z;

      // 6-side calibration: average ~1 s of still readings per position, then fit offsets/scales to all positions
      {
        static uint16_t sideN = 0;
        static float sideSum[3], sideSq[3];
        if (imuCalSideReq) {
          if (sideN == 0) {
            for (int j = 0; j < 3; j++) sideSum[j] = sideSq[j] = 0;
            imuCalMsg = "Capturing - keep still";
          }
          float v[3] = { (float)aa.x / DMP_ACCEL_LSB_PER_G, (float)aa.y / DMP_ACCEL_LSB_PER_G, (float)aa.z / DMP_ACCEL_LSB_PER_G };
          for (int j = 0; j < 3; j++) {
            sideSum[j] += v[j];
            sideSq[j] += v[j] * v[j];
          }
          if (++sideN >= CAL_SIDE_SAMPLES) {
            calAddPoint(sideSum, sideSq, sideN);
            imuCalSideReq = false;
            sideN = 0;
          }
        } else {
          sideN = 0;
        }
      }

      // accel in g, corrected by the 6-side calibration (plain counts / 16384 until there is one)
      float ac[3] = { (aa.x - accOff[0]) / accScale[0], (aa.y - accOff[1]) / accScale[1], (aa.z - accOff[2]) / accScale[2] };

      // Up/down: measured along imuRef, the upright direction saved with the web page's Level button. Inside the
      // dead band (+/- imuOrientHyst) the previous state is kept, so it can't flicker while passing through sideways.
      // Not levelled yet: no up/down at all (arrow blank, robot treated as upright, same as a missing IMU).
      {
        // accelerometer gravity: low-pass the corrected accel (in g), then use its direction
        static float fa[3];
        static bool faPrimed = false;
        for (int k = 0; k < 3; k++) fa[k] = faPrimed ? fa[k] + IMU_ACC_LP * (ac[k] - fa[k]) : ac[k];
        faPrimed = true;
        float an = sqrtf(fa[0] * fa[0] + fa[1] * fa[1] + fa[2] * fa[2]);
        float gn = sqrtf(gravity.x * gravity.x + gravity.y * gravity.y + gravity.z * gravity.z);

        if (imuLevelReq) {  // Level pressed: the current gravity direction (both sources) becomes "upright"
          if (gn > 0.5f && an > 0.5f) {
            imuRef[0] = gravity.x / gn;
            imuRef[1] = gravity.y / gn;
            imuRef[2] = gravity.z / gn;
            for (int k = 0; k < 3; k++) imuRefA[k] = fa[k] / an;
            imuLevelled = true;
            imuLevelSave = true;
          }
          imuLevelReq = false;
        }
        if (imuLevelled && gn > 0.5f && an > 0.5f) {
          float upD = (gravity.x * imuRef[0] + gravity.y * imuRef[1] + gravity.z * imuRef[2]) / gn;  // cos(tilt), DMP
          float upA = (fa[0] * imuRefA[0] + fa[1] * imuRefA[1] + fa[2] * imuRefA[2]) / an;          // cos(tilt), accel
          imuTiltDmpDeg = acosf(constrain(upD, -1.0f, 1.0f)) * 180.0f / (float)M_PI;
          imuTiltAccDeg = acosf(constrain(upA, -1.0f, 1.0f)) * 180.0f / (float)M_PI;
          float up = IMU_UP_FROM_ACCEL ? upA : upD;
          imuTiltDeg = IMU_UP_FROM_ACCEL ? imuTiltAccDeg : imuTiltDmpDeg;
          if (up > imuOrientHyst) imuOrient = 1;
          else if (up < -imuOrientHyst) imuOrient = -1;
        } else if (!imuLevelled) {
          imuTiltDeg = imuTiltDmpDeg = imuTiltAccDeg = 0;
          imuOrient = 0;
        }
      }


      // Remove gravity using the DMP orientation. Done by hand (not dmpGetLinearAccel) because that
      // library function assumes 8192 counts/g, which doesn't match the 16384 this sensor is giving.
      // 'gravity' is a unit vector (computed above from the quaternion); ac is the corrected accel in g.
      float lx = ac[0] - gravity.x;
      float ly = ac[1] - gravity.y;
      float lz = ac[2] - gravity.z;
      float g = sqrtf(lx * lx + ly * ly + lz * lz);
      if (imuWarm < IMU_WARMUP) imuWarm++;  // let the DMP settle after (re)start before recording
      else if (g > recMaxG) recMaxG = g;

      yaw = abs(gy.z);

      Ytotal -= Yreadings[YreadIndex];
      Yreadings[YreadIndex] = yaw;
      Ytotal += Yreadings[YreadIndex];
      YreadIndex = (YreadIndex + 1) % YnumReadings;

      int y = Ytotal / YnumReadings;
      Y = (y > 1400) ? 1400 : y;
    }
  }
}
// ---------------- 6-side accel calibration (sphere fit) ----------------
// Each capture is the average accel (in g, offsets from the chip already applied) over ~1 s with the robot still.
// The fit finds a zero point and a scale per axis so that every capture comes out at exactly 1 g. It doesn't need
// the IMU to be square to the robot: any positions work, as long as each axis has been both up and down.

// Solve the 6x6 system A x = b in place (Gaussian elimination with partial pivoting). false = singular.
static bool solve6(float A[6][6], float b[6], float x[6]) {
  for (int c = 0; c < 6; c++) {
    int piv = c;
    for (int r = c + 1; r < 6; r++)
      if (fabsf(A[r][c]) > fabsf(A[piv][c])) piv = r;
    if (fabsf(A[piv][c]) < 1e-9f) return false;
    if (piv != c) {
      for (int k = 0; k < 6; k++) {
        float t = A[c][k];
        A[c][k] = A[piv][k];
        A[piv][k] = t;
      }
      float t = b[c];
      b[c] = b[piv];
      b[piv] = t;
    }
    for (int r = c + 1; r < 6; r++) {
      float f = A[r][c] / A[c][c];
      for (int k = c; k < 6; k++) A[r][k] -= f * A[c][k];
      b[r] -= f * b[c];
    }
  }
  for (int r = 5; r >= 0; r--) {
    float s = b[r];
    for (int k = r + 1; k < 6; k++) s -= A[r][k] * x[k];
    x[r] = s / A[r][r];
  }
  return true;
}

// Fit offset o[3] and scale s[3] (both in g units) to the captured points. Returns the rms error in g, or -1 on failure.
static float calFit(float o[3], float sc[3]) {
  // start from the midpoint and half-span of each axis
  for (int k = 0; k < 3; k++) {
    float mn = calPts[0][k], mx = calPts[0][k];
    for (int i = 1; i < calN; i++) {
      mn = min(mn, calPts[i][k]);
      mx = max(mx, calPts[i][k]);
    }
    o[k] = (mx + mn) * 0.5f;
    sc[k] = max((mx - mn) * 0.5f, 0.2f);
  }
  float rms = -1;
  for (int it = 0; it < 30; it++) {  // Gauss-Newton on r_i = |(a_i - o) / s|^2 - 1
    float JtJ[6][6] = { { 0 } }, Jtr[6] = { 0 }, dx[6];
    float sse = 0;
    for (int i = 0; i < calN; i++) {
      float J[6], r = -1;
      for (int k = 0; k < 3; k++) {
        float d = calPts[i][k] - o[k];
        r += (d * d) / (sc[k] * sc[k]);
        J[k] = -2.0f * d / (sc[k] * sc[k]);
        J[3 + k] = -2.0f * d * d / (sc[k] * sc[k] * sc[k]);
      }
      sse += r * r;
      for (int m = 0; m < 6; m++) {
        Jtr[m] += J[m] * r;
        for (int n = 0; n < 6; n++) JtJ[m][n] += J[m] * J[n];
      }
    }
    rms = sqrtf(sse / calN) * 0.5f;  // |v|^2 - 1 is about 2x the error in |v|
    for (int m = 0; m < 6; m++) JtJ[m][m] *= 1.001f;  // a touch of damping
    if (!solve6(JtJ, Jtr, dx)) return -1;
    float step = 0;
    for (int k = 0; k < 3; k++) {
      o[k] -= dx[k];
      sc[k] -= dx[3 + k];
      if (!(sc[k] > 0.1f)) return -1;
      step += dx[k] * dx[k] + dx[3 + k] * dx[3 + k];
    }
    if (step < 1e-10f) break;
  }
  return rms;
}

// One capture finished (sums over n packets, in g). Checks it, stores it, and fits once every axis has been up and down.
void calAddPoint(const float sum[3], const float sq[3], uint16_t n) {
  float avg[3], mag = 0, noise = 0;
  for (int j = 0; j < 3; j++) {
    avg[j] = sum[j] / n;
    noise = max(noise, sqrtf(max(sq[j] / n - avg[j] * avg[j], 0.0f)));
    mag += avg[j] * avg[j];
  }
  mag = sqrtf(mag);
  static char msg[64];
  if (noise > CAL_MAX_NOISE_G) {
    snprintf(msg, sizeof(msg), "Moved during capture (%.3f g) - try again", noise);
    imuCalMsg = msg;
    return;
  }
  // Only reject readings that are clearly broken. Offset errors can put a still reading anywhere from ~0 to ~2.5 g,
  // and the fit corrects that, so the size isn't checked against 1 g.
  if (mag < 0.15f || mag > 3.5f) {
    snprintf(msg, sizeof(msg), "Reading %.2f g - IMU fault? Try Factory offsets", mag);
    imuCalMsg = msg;
    return;
  }
  for (int i = 0; i < calN; i++) {  // too close to a position already captured adds nothing
    float d = 0;
    for (int j = 0; j < 3; j++) d += (avg[j] - calPts[i][j]) * (avg[j] - calPts[i][j]);
    if (sqrtf(d) < CAL_MIN_SPACING_G) {
      imuCalMsg = "Same position as before - turn it to another side";
      return;
    }
  }
  if (calN >= CAL_MAX_POINTS) {
    imuCalMsg = "Already have the maximum - Clear to start again";
    return;
  }
  for (int j = 0; j < 3; j++) calPts[calN][j] = avg[j];
  calN++;
  int k = 0;  // label the capture by the axis closest to vertical
  for (int j = 1; j < 3; j++)
    if (fabsf(avg[j]) > fabsf(avg[k])) k = j;
  sideMask |= 1 << (k * 2 + (avg[k] < 0 ? 1 : 0));

  if (sideMask != 0x3F || calN < 6) {
    snprintf(msg, sizeof(msg), "Captured %.2f g - next side", mag);
    imuCalMsg = msg;
    return;
  }
  float o[3], sc[3];
  float rms = calFit(o, sc);
  bool ok = rms >= 0 && rms < CAL_MAX_RMS_G;
  for (int j = 0; j < 3; j++)
    if (!(sc[j] > 0.25f && sc[j] < 4.0f) || !(fabsf(o[j]) < 2.5f)) ok = false;
  if (!ok) {
    imuCalMsg = (rms < 0) ? "Fit failed - Clear and start again" : "Fit poor - capture more sides or Clear";
    return;
  }
  for (int j = 0; j < 3; j++) {  // store in counts, the units the correction is applied in
    accOff[j] = o[j] * DMP_ACCEL_LSB_PER_G;
    accScale[j] = sc[j] * DMP_ACCEL_LSB_PER_G;
  }
  accCalRms = rms;
  accCalValid = true;
  accCalSave = true;
  imuLevelled = false;
  imuCalMsg = "6-side saved - now Level (more captures refine it)";
}
