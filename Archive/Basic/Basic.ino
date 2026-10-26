#include <CRSFReceiver.h>

// Maple Mini (STM32F103CB):
//   Serial1 = USART1  RX = PA10, TX = PA9
//   Serial2 = USART2  RX = PA3,  TX = PA2
//   Serial3 = USART3  RX = PB11, TX = PB10
// Wire: receiver TX -> MCU RX, receiver RX -> MCU TX, common GND.
// Tools > USB support: "CDC (generic 'Serial' supersede U(S)ART)" so Serial = USB.
CRSFReceiver crsf(Serial1);

uint32_t lastPrint = 0;

void setup() {
  Serial.begin(115200);                     // USB serial for debug
  Serial1.begin(CRSFReceiver::CRSF_BAUD);   // 420000 baud
  crsf.begin();
}

void loop() {
  crsf.update();

  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    if (crsf.isFailsafe()) {
      Serial.println("FAILSAFE");
      return;
    }
    for (uint8_t i = 0; i < 8; i++) { Serial.print(crsf.getChannelUs(i)); Serial.print(' '); }
    if (crsf.hasLinkStats()) {
      Serial.print("| LQ "); Serial.print(crsf.linkStats().uplinkLQ);
    }
    Serial.println();
  }
}
