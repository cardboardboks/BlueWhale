int safe_telem = 0;
int serial_telem = 0;

void serial_debug(int message_type) {

  if (message_type == 1) {
    safe_telem = 1;
  }
  if (message_type == 2) {
    serial_telem = 1;
  }

  // print a serial frame every .5s
  if (serial_freq.TRIGGERED) {

    if (safe_telem == 1) {
      safe_telem = 0;

      Serial.println(" Pre Arm Safe Check");

      Serial.print(" safe_switch ");
      Serial.println(safe_switch);

      Serial.print(" safe_sticks ");
      Serial.println(safe_sticks);

      Serial.print(" safe_link ");
      Serial.println(safe_link);

      for (int i = 1; i <= 8; i++) {
        Serial.print(" ");
        Serial.print(ch[i]);
      }
      Serial.println("");
      Serial.println("");
    }

    if (serial_telem == 1) {
      serial_telem = 0;

      Serial.println(" Thr:  | Volt:  | Cur: | Tmp: | RPM: | Speed: ");

      auto &s1 = esc1.stats();

      if (((throttle1 - 1042) / 10 < 10) && ((throttle2 - 1042) / 10 > -10)) {
        Serial.print(" ");
      }

      if (((throttle1 - 1042) / 10 != 100) || ((throttle2 - 1042) / 10 != -10)) {
        Serial.print(" ");
      }

      if ((throttle1 - 1042) / 10 > 0) {
        Serial.print("+");
      }

      if ((throttle1 - 1042) / 10 == 0) {
        Serial.print(" ");
      }

      if ((throttle1 - 1042) / 10 == -104) {
        throttle1 = 0;
      } else if ((throttle1 - 1042) / 10 < 0) {
        throttle1 = (((throttle1 - 1042) / 10) + 100) * -1;
      } else {
        throttle1 = (throttle1 - 1042) / 10;
      }

      Serial.printf(
        "%d %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h\n",
        throttle1,
        esc1.voltage(),
        esc1.current(),
        esc1.temperatureC(),
        (esc1.rpm() / gearbox_ratio1),
        (M_PI * (wheel_diamiter1 / 1000) * (esc1.rpm() / gearbox_ratio1) * .06));

      auto &s2 = esc2.stats();

      if (((throttle2 - 1042) / 10 < 10) && ((throttle2 - 1042) / 10 > -10)) {
        Serial.print(" ");
      }

      if (((throttle2 - 1042) / 10 != 100) || ((throttle2 - 1042) / 10 != -10)) {
        Serial.print(" ");
      }

      if ((throttle2 - 1042) / 10 > 0) {
        Serial.print("+");
      }

      if ((throttle2 - 1042) / 10 == 0) {
        Serial.print(" ");
      }

      if ((throttle2 - 1042) / 10 == -104) {
        throttle2 = 0;
      } else if ((throttle2 - 1042) / 10 < 0) {
        throttle2 = (((throttle2 - 1042) / 10) + 100) * -1;
      } else {
        throttle2 = (throttle2 - 1042) / 10;
      }

      Serial.printf(
        "%d %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h\n",
        throttle2,
        esc2.voltage(),
        esc2.current(),
        esc2.temperatureC(),
        (esc2.rpm() / gearbox_ratio2),
        (M_PI * (wheel_diamiter1 / 1000) * (esc2.rpm() / gearbox_ratio2) * .06));

      Serial.print(" link: ");
      Serial.print(crsf.isLinkUp() ? "UP" : "DOWN");
      const crsfLinkStatistics_t *link = crsf.getLinkStatistics();
      Serial.print("  LQ: ");
      Serial.print(link->uplink_Link_quality);
      Serial.print("  RSSI: -");
      Serial.print(link->active_antenna == 0 ? link->uplink_RSSI_1 : link->uplink_RSSI_2);
      Serial.println("dBm ");

      for (int i = 1; i <= 8; i++) {
        Serial.print(" ");
        Serial.print(ch[i]);
      }
      Serial.println("");
      Serial.println("");
    }
  }
}