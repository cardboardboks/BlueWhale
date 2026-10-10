void boot_esc() {

  AlfredoDShot::releaseBootloader(PIN_ESC1);
  AlfredoDShot::releaseBootloader(PIN_ESC2);

  // make esc objects
  esc1.begin(PIN_ESC1, DSHOT600, true, MOTOR1_POLES);
  esc2.begin(PIN_ESC2, DSHOT600, true, MOTOR2_POLES);

  // wait for ESC to boot before enabling EDT
  for (unsigned long start = millis(); millis() - start < esc_arm_duration;) {
    // led waiting colour
  }
  // enable EDT
  esc1.command(DSHOT_CMD_EDT_ENABLE);
  esc2.command(DSHOT_CMD_EDT_ENABLE);

  // set ESC to 0 for 5s at end of startup to wait for esc to be arm and send telem data
  for (unsigned long start = millis(); millis() - start < esc_arm_duration;) {
    esc1.sendThrottle(0.0f);
    esc2.sendThrottle(0.0f);
  }
}

void drive_esc() {
  static uint32_t t0;
  static uint32_t next = micros(), lastPrint = 0, maxGap = 0, lastSend = micros();
  static bool cmdSent = false;
  const bool SEND_EDT = true;  // run once true, once false
  // map andscale throttle command

  // limit outputs

  // drive esc
  uint32_t runAt = SEND_EDT ? 9000 : 5000;
  uint32_t t = millis() - t0;  // time since begin()

  if (t > runAt) thr = min(0.54f, 0.50f + (t - runAt) * 0.00002f);  // ramp to 20%
  esc1.sendThrottle(thr);
  esc2.sendThrottle(thr);
}