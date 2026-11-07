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
  if (st == 0) {
    mpu.setXGyroOffset(-234);
    mpu.setYGyroOffset(24);
    mpu.setZGyroOffset(-9);
    mpu.setXAccelOffset(-698);
    mpu.setYAccelOffset(476);
    mpu.setZAccelOffset(2538);
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

      // Up/down: the DMP gravity vector is +1 on whichever sensor axis currently points up. Inside the dead band
      // (+/- imuOrientHyst) the previous state is kept, so it can't flicker while the robot passes through sideways.
      {
        float up = (imuUpAxis == 0) ? gravity.x : (imuUpAxis == 1) ? gravity.y
                                                                   : gravity.z;
        if (imuUpInverted) up = -up;
        if (up > imuOrientHyst) imuOrient = 1;
        else if (up < -imuOrientHyst) imuOrient = -1;
      }


      // Remove gravity using the DMP orientation. Done by hand (not dmpGetLinearAccel) because that
      // library function assumes 8192 counts/g, which doesn't match the 16384 this sensor is giving.
      // 'gravity' is a unit vector (computed above from the quaternion), so scale it to counts first.
      float lx = aa.x - gravity.x * DMP_ACCEL_LSB_PER_G;
      float ly = aa.y - gravity.y * DMP_ACCEL_LSB_PER_G;
      float lz = aa.z - gravity.z * DMP_ACCEL_LSB_PER_G;
      float g = sqrtf(lx * lx + ly * ly + lz * lz) / DMP_ACCEL_LSB_PER_G;
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