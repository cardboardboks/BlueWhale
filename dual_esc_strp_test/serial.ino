void serial_debug() {

  Serial.println(" Thr:  | Volt:  | Cur: | Tmp: | RPM: | Speed: ");

  auto &s1 = esc1.stats();

  if (((throttle1 - 1500) / 5 < 10) && ((throttle2 - 1500) / 5 > -10)) {
    Serial.print(" ");
  }

  if (((throttle1 - 1500) / 5 != 100) || ((throttle2 - 1500) / 5 != -010)) {
    Serial.print(" ");
  }

  if ((throttle1 - 1500) / 5 > 0) {
    Serial.print("+");
  }

  if ((throttle1 - 1500) / 5 == 0) {
    Serial.print(" ");
  }
  Serial.printf("%d %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h\n",
                (throttle1 - 1500) / 5,
                esc1.voltage(),
                esc1.current(),
                esc1.temperatureC(),
                (esc1.rpm() / gearbox_ratio1),
                (M_PI * (wheel_diamiter1 / 1000) * (esc1.rpm() / gearbox_ratio1) * .06));

  auto &s2 = esc2.stats();

  if (((throttle2 - 1500) / 5 < 10) && ((throttle2 - 1500) / 5 > -10)) {
    Serial.print(" ");
  }

  if (((throttle2 - 1500) / 5 != 100) || ((throttle2 - 1500) / 5 != -010)) {
    Serial.print(" ");
  }

  if ((throttle2 - 1500) / 5 > 0) {
    Serial.print("+");
  }

  if ((throttle2 - 1500) / 5 == 0) {
    Serial.print(" ");
  }

  Serial.printf("%d %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h\n",
                (throttle2 - 1500) / 5,
                esc2.voltage(),
                esc2.current(),
                esc2.temperatureC(),
                (esc2.rpm() / gearbox_ratio2),
                (M_PI * (wheel_diamiter1 / 1000) * (esc2.rpm() / gearbox_ratio2) * .06));
}