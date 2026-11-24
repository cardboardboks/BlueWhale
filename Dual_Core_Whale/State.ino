void state() {

  if (txchannel[6] < 1500 && crsf.isLinkUp() == 1) {
    imuDirection = 1;
  } else if (txchannel[6] > 1500 && crsf.isLinkUp() == 1) {
    imuDirection = -1;
  } else {
    imuDirection = 0;
  }

  // purple/red "no IMU" LEDs while there's no usable up/down: IMU missing / failed, or not levelled yet
  led(16, !IMU_safe || !imuLevelled);

  if (IMU_safe == 0) {
    imuOrient = 0;  // (no led(17) here: that's "safe, bottom up", which drive.ino sets / clears every loop - setting
                    // it here only made the LEDs flicker. The purple/red "no IMU" LEDs come from led(16) above.)
    modeString = "No UMI";
  }
}