// start crsf
void boot_crsf() {
}

// receive crsf data
void crsf_rx() {
  crsf.update();
  for (int i = 1; i <= 8; i++) {
    txchannel[i] = crsf.getChannel(i);
    if (txchannel[i] < 1000) {
      txchannel[i] = 1000;
    }
    if (txchannel[i] > 2000) {
      txchannel[i] = 2000;
    }
  }
}

// send crsf telem
void crsf_tx() {
  if (millis() - lastHeartbeatMs > 1000) {
    lastHeartbeatMs = millis();
    crsf.sendHeartbeat();

    for (int i = 0; i < NUM_ESC; i++) escs[i].stats();

    crsf_sensor_battery_t crsfBatt = { 0 };

    crsfBatt.voltage = htobe16((uint16_t)(escs[0].voltage() * 10.0));
    crsfBatt.current = htobe16((uint16_t)(escs[1].voltage() * 10.0));
    crsfBatt.capacity = htobe24((uint32_t)(escs[0].temperatureC()));
    crsfBatt.remaining = (uint8_t)(escs[1].temperatureC());
    crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_BATTERY_SENSOR, &crsfBatt, sizeof(crsfBatt));
  }
}
