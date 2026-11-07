// saftey checks before allowing any control
// check crsf is connected
// check arming swith is is set to safe
// check that sticks are centered

void pre_arm_safe() {
  led(2, 1);
  bootState = "SafeCheck";

  // hold here until all conditions are met
  while (safe_switch == 0 || safe_sticks == 0 || safe_link == 0) {

    update_reboot();

      if (!otaInProgress) {
      // drve the crital functions to be able to chceck health
      // force esc's to disarmed
      crsf_rx();
      crsf_tx();  // keep telemetry going while waiting in the safe check
      drive_escs(0, 0);
      status();
      web();

      // make sure arm switch is off
      if (txchannel[3] == 1000) {
        safe_switch = 1;
      } else {
        safe_switch = 0;
      }

      //make sure sticks are centered +/- some dead zone
      if ((txchannel[1] > 1480 && txchannel[1] < 1520) && (txchannel[2] > 1480 && txchannel[2] < 1520)) {
        safe_sticks = 1;
      } else {
        safe_sticks = 0;
      }

      //make sure RF link is up
      if (crsf.isLinkUp() == 1) {
        safe_link = 1;
        modeString = "Disarmed";
      } else {
        safe_link = 0;
      }
    }
  }
  safed = 1;
  led(2, 0);
}