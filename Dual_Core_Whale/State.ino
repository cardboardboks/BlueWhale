void state() {

  if (txchannel[6] < 1500 && crsf.isLinkUp() == 1) {
    imuDirection = 1;
  } else if (txchannel[6] > 1500 && crsf.isLinkUp() == 1) {
    imuDirection = -1;
  } else {
    imuDirection = 0;
  }

  if (IMU_safe == 0) {
    imuOrient = 0;
    led(17, 1);
    modeString = "No UMI";
  }
}