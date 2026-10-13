void drive_logic() {

  if (crsf.isLinkUp() == 0) {
    led(4);
  } else if (ch[3] == 1000) {
    led(3);
  } else {
    if (ch[4] != 1000) {
      led(9);
    } else {
      if (ch[5] != 1000 && ch[6] == 1000) {
        led(5);
      }
      if (ch[5] != 1000 && ch[6] == 2000) {
        led(6);
      }
      if (ch[5] == 1000 && ch[6] == 1000) {
        led(7);
      }
      if (ch[5] == 1000 && ch[6] == 2000) {
        led(8);
      }
    }
  }
}