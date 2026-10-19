void boot_crsf() {

  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);

  if (!crsfSerial)
    while (1) Serial.println("Invalid crsfSerial configuration");
  crsf.begin(crsfSerial);

  unsigned long start = millis();
  while (millis() - start < 2000) {
    led(1, 1);
    drive_esc(0, 0);
  }
}

void crsf_rx() {

  crsf.update();

  for (int i = 1; i <= 8; i++) {
    ch[i] = crsf.getChannel(i);
    if (ch[i] < 1000) {
      ch[i] = 1000;
    }
    if (ch[i] > 2000) {
      ch[i] = 2000;
    }
  }
}

void crsf_tx() {

  if (crsf_tx_freq.TRIGGERED) {

    esc1.stats();
    esc2.stats();

    crsf_sensor_battery_t crsfBatt = { 0 };

    if (crsf_telem_freq.TRIGGERED) {
      crsfBatt.voltage = htobe16((uint16_t)(esc1.voltage() * 10.0));  //Volts
      crsfBatt.current = htobe16((uint16_t)(esc2.voltage() * 10.0));  //Amps
      crsfBatt.capacity = htobe24((uint32_t)(esc1.temperatureC()));   //mAh (24 bit field, max 16777215mAh)
      crsfBatt.remaining = (uint8_t)(esc2.temperatureC());            //percent
      crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_BATTERY_SENSOR, &crsfBatt, sizeof(crsfBatt));
    }
  }
}
