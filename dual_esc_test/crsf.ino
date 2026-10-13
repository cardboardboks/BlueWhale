void boot_crsf() {

  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);

  if (!crsfSerial)
    while (1) Serial.println("Invalid crsfSerial configuration");
  crsf.begin(crsfSerial);  // defaults to CRSF_ADDRESS_FLIGHT_CONTROLLER
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

void crsf_tx() {

  float voltage;
  float current;
  float capacity;
  float remaining;
  crsf_sensor_battery_t crsfBatt = { 0 };

  if (millis() - lastTelemMs >= TELEM_INTERVAL_MS) {
    lastTelemMs = millis();

    crsfBatt.voltage = htobe16((uint16_t)(esc1.voltage() * 10.0));  //Volts
    crsfBatt.current = htobe16((uint16_t)(esc2.voltage() * 10.0));  //Amps
    crsfBatt.capacity = htobe24((uint32_t)(esc1.temperatureC()));   //mAh (24 bit field, max 16777215mAh)
    crsfBatt.remaining = (uint8_t)(esc2.temperatureC());            //percent
    crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_BATTERY_SENSOR, &crsfBatt, sizeof(crsfBatt));
  }
}
