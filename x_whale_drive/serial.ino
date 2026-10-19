

void serial_debug(int message_type) {
  static int safe_telem = 0;
  static int serial_telem = 0;

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


      Serial.println(" Thr:  | Volt:  | Cur: | Tmp:  | RPM: | Speed:    | Link:  ");

      if ((throttle1 - 1500) / 5 == 0) {
        Serial.print("   ");
      }
      if (((throttle1 - 1500) / 5 > 0) && ((throttle1 - 1500) / 5 < 10)) {
        Serial.print("   ");
      }
      if (((throttle1 - 1500) / 5 >= 10) && ((throttle1 - 1500) / 5 < 100)) {
        Serial.print("  ");
      }
      if ((throttle1 - 1500) / 5 == 100) {
        Serial.print(" ");
      }

      if (((throttle1 - 1500) / 5 < 0) && ((throttle1 - 1500) / 5 > -10)) {
        Serial.print("  ");
      }
      if (((throttle1 - 1500) / 5 <= -10) && ((throttle1 - 1500) / 5 > -100)) {
        Serial.print(" ");
      }

      Serial.printf(
        "%d %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h   |",
        (throttle1 - 1500) / 5,
        esc1.voltage(),
        esc1.current(),
        esc1.temperatureC(),
        (esc1.rpm() / gearbox_ratio1),
        (wheel_diamiter1 / 10) * (esc1.rpm() / gearbox_ratio1) * 0.001885);
      Serial.println(esc1.lossPercent());

      if ((throttle2 - 1500) / 5 == 0) {
        Serial.print("   ");
      }
      if (((throttle2 - 1500) / 5 > 0) && ((throttle2 - 1500) / 5 < 10)) {
        Serial.print("   ");
      }
      if (((throttle2 - 1500) / 5 >= 10) && ((throttle2 - 1500) / 5 < 100)) {
        Serial.print("  ");
      }
      if ((throttle2 - 1500) / 5 == 100) {
        Serial.print(" ");
      }

      if (((throttle2 - 1500) / 5 < 0) && ((throttle2 - 1500) / 5 > -10)) {
        Serial.print("  ");
      }
      if (((throttle2 - 1500) / 5 <= -10) && ((throttle2 - 1500) / 5 > -100)) {
        Serial.print(" ");
      }

      Serial.printf(
        "%d %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h   |",
        (throttle2 - 1500) / 5,
        esc2.voltage(),
        esc2.current(),
        esc2.temperatureC(),
        (esc2.rpm() / gearbox_ratio2),
        (wheel_diamiter2 / 10) * (esc2.rpm() / gearbox_ratio2) * 0.001885);
      Serial.println(esc2.lossPercent());

      // Serial.print(" link: ");
      // Serial.print(crsf.isLinkUp() ? "UP" : "DOWN");
      // const crsfLinkStatistics_t *link = crsf.getLinkStatistics();
      // Serial.print("  LQ: ");
      // Serial.print(link->uplink_Link_quality);
      // Serial.print("  RSSI: -");
      // Serial.print(link->active_antenna == 0 ? link->uplink_RSSI_1 : link->uplink_RSSI_2);
      // Serial.println("dBm ");

      // Serial.print(" CH 1-8:");
      // for (int i = 1; i <= 8; i++) {
      //   Serial.print(" | ");
      //   Serial.print(ch[i]);
      // }

      // Serial.println("");
      // Serial.println(" Mode: | Side:");

      // Serial.print(" ");
      // Serial.print(RP);
      // Serial.print("\t");
      // Serial.print(" ");
      // Serial.print(Y);
      // Serial.println("");

      // Serial.print(" ");
      // for (int i = 0; i < 15; i++) {
      //   Serial.print(led_arry[i]);
      //   Serial.print(" | ");
      // }
      // Serial.println("");

      Serial.println("");
      Serial.println("");
    }
  }
}