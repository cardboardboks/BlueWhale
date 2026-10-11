// saftey checks before allowing any control
// check crsf is connected
// check arming swith is is set to safe
// check that sticks are centered

void pre_arm_safe() {

  while (safe_switch == 0 || safe_sticks == 0 || safe_link == 0) {
    crsf_rx();
    drive_esc(0, 0);

    if (ch[3] == 2000) {
      safe_switch = 1;
    } else {
      safe_switch = 0;
    }

    if (ch[1] == 1500 && ch[2] == 1500) {
      safe_sticks = 1;
    } else {
      safe_sticks = 0;
    }

    if (crsf.isLinkUp() == 1) {
      safe_link = 1;
    } else {
      safe_link = 0;
    }

    serial_debug(1);
  }
}