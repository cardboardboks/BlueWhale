// saftey checks before allowing any control
// check crsf is connected
// check arming swith is is set to safe
// check that sticks are centered

void pre_arm_safe() {
  boot_esc();
  led(1, 0);

  // hold here until all conditions are met
  while (safe_switch == 0 || safe_sticks == 0 || safe_link == 0) {
    // drve the crital functions to be able to chceck health
    // force esc's to disarmed
    crsf_rx();
    drive_esc(0, 0);
    serial_debug(1);

    led(2, 1);

    // make sure arm switch is off
    if (ch[3] == 1000) {
      safe_switch = 1;
    } else {
      safe_switch = 0;
    }

    //make sure sticks are centered +/- some dead zone
    if ((ch[1] > 1480 && ch[1] < 1520) && (ch[2] > 1480 && ch[2] < 1520)) {
      safe_sticks = 1;
    } else {
      safe_sticks = 0;
    }

    //make sure RF link is up
    if (crsf.isLinkUp() == 1) {
      safe_link = 1;
    } else {
      safe_link = 0;
    }
  }
  safed = 1;
  led(2, 0);
}