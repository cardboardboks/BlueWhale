#include <HardwareSerial.h>

// Assigning Pins
#define ESC_PIN 7       // Pin 7 connected to ESC signal line
#define BAUD_RATE 19200 // Default AM32 Bootloader connection speed

void setup() {
  // Initialize native USB CDC serial link to the computer
  Serial.begin(BAUD_RATE); 
  
  // Initialize Hardware UART1 on Pin 7 in half-duplex mode
  // This automatically forces GPIO 7 to handle both RX and TX tasks internally
  Serial1.begin(BAUD_RATE, SERIAL_8N1, ESC_PIN, ESC_PIN, false);
}

void loop() {
  // Pass data from the Computer USB to the ESC
  if (Serial.available()) {
    char data = Serial.read();
    Serial1.write(data);
  }

  // Pass data from the ESC back to the Computer USB
  if (Serial1.available()) {
    char data = Serial1.read();
    Serial.write(data);
  }
}