void serial_debug() {
/*
  auto &s1 = esc1.stats();
  auto &s2 = esc2.stats();

  Serial.print(" Thr   | Voltage | Current | RPM | Temp \n");
  Serial.printf("%.2f %% | %3.1f V | %2.0f A | %2.0f | %3.0f C \n", throttle1, esc1.voltage(), esc1.current(), (esc1.rpm() / gearbox_ratio1), esc1.temperatureC());
  Serial.printf("%.2f %% | %3.1f V | %2.0f A | %2.0f | %3.0f C \n", throttle2, esc2.voltage(), esc2.current(), (esc2.rpm() / gearbox_ratio1), esc2.temperatureC());
*/
    auto &s1 = esc1.stats();
    Serial.printf("Thr1: %.2f %% | Voltage1: %3.1f V | Current1: %2.0f A | RPM1 %2.0f | Temp1: %3.0f C \n",
                  thr,
                  esc1.voltage(),
                  esc1.current(),
                  (esc1.rpm() / 19),
                  esc1.temperatureC());

    auto &s2 = esc2.stats();
    Serial.printf("Thr2: %.2f %% | Voltage2: %3.1f V | Current2: %2.0f A | RPM2 %2.0f | Temp2: %3.0f C \n",
                  thr,
                  esc2.voltage(),
                  esc2.current(),
                  (esc2.rpm() / 19),
                  esc2.temperatureC());
}