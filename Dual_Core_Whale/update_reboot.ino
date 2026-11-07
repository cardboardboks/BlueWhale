void update_reboot() {

update_bypass:

  ArduinoOTA.handle();

  if (rebootPending && rebootBlocked()) {  // armed during the 0.5 s countdown: cancel the reboot
    rebootPending = false;
    modeString = "Running";
  }
  if (rebootPending && millis() > rebootAtMs) {
    if (tuneDirty) saveTune();  // don't lose a tuning edit made in the last 2 seconds
    Serial.println("Rebooting...");
    ESP.restart();
  }

  if (otaInProgress) {
    led(0, 0);
    goto update_bypass;
  }
}