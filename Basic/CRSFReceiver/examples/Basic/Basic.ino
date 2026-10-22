#include <CRSFReceiver.h>

// STM32 "Black Pill" F411: USART1  RX = PA10, TX = PA9
// Connect receiver TX -> PA10, receiver RX -> PA9 (3.3 V logic, common GND)
HardwareSerial crsfSerial(PA10, PA9);
CRSFReceiver crsf(crsfSerial);

uint32_t lastPrint = 0;
uint32_t lastTelem = 0;

void setup() {
  Serial.begin(115200);          // USB CDC or another UART for debug output
  crsf.begin();                  // 420000 baud
}

void loop() {
  if (crsf.update()) {
    // New RC frame arrived: use channel data here
    // e.g. servo / ESC / motor control
  }

  if (millis() - lastTelem >= 1000) {
    lastTelem = millis();
    crsf.sendBattery(123, 45, 1200, 80);   // 12.3 V, 4.5 A, 1200 mAh, 80 %
    crsf.sendFlightMode("ACRO");
  }

  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    if (crsf.isFailsafe()) {
      Serial.println("FAILSAFE");
      return;
    }
    for (uint8_t i = 0; i < 8; i++) {
      Serial.print(crsf.getChannelUs(i));
      Serial.print(' ');
    }
    if (crsf.hasLinkStats()) {
      Serial.print(" | LQ ");   Serial.print(crsf.linkStats().uplinkLQ);
      Serial.print(" RSSI -");  Serial.print(crsf.linkStats().uplinkRssi1);
    }
    Serial.println();
  }
}
