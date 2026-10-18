void drive_esc(int thr1, int thr2) {

  // check for an active rf link right before sending esc command
  // if no rf connection, force esc to disarm and se leds
  if (crsf.isLinkUp() == 0) {
    thr1 = 0;
    thr2 = 0;
    throttle1 = 1500;
    throttle2 = 1500;
    led(4);

    // if safe force esc to disarm and set leds
  } else if (ch[3] == 1000) {
    thr1 = 0;
    thr2 = 0;
    throttle1 = 1500;
    throttle2 = 1500;
    led(3);
  } else {
    // copy the commanded drive values out for debugging purposes
    throttle1 = thr1;
    throttle2 = thr2;
  }

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

  // drive the esc's output every 1000microseconds
  if (dshot_freq.TRIGGERED) {

    // send packet to escs
    esc1.send(thr1);
    esc2.send(thr2);
  }
}
