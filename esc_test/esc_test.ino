
// libs
#include <AlfredoDShot.h>
#include <BlockNot.h>

//  these things
AlfredoDShot esc1;
AlfredoDShot esc2;

//  varibles
//  esc varibles
const int PIN_ESC1 = 10;
const int PIN_ESC2 = 11;
const uint8_t MOTOR1_POLES = 14;
const uint8_t MOTOR2_POLES = 14;
float ESC1_thr = 0.0f;
float ESC2_thr = 0.0f;
const int esc_arm_duration = 5000;
int throttle1 = 0;
int throttle2 = 0;
int gearbox_ratio1 = 19;
int gearbox_ratio2 = 19;
int wheel_diamiter1 = 14;
int wheel_diamiter2 = 14;
BlockNot dshot_freq(1000, MICROSECONDS);

float thr = 0.0f;

BlockNot serial_freq(500);

void setup() {

  //configure and arm esc
  boot_esc();

  // set up serial
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // make sure usb cdc is enabled
  Serial.println("Whale Booted");
}

void loop() {

  schedular();
}

void schedular() {

  // run esc's every 1000microseconds
  if (dshot_freq.TRIGGERED) {
    drive_esc();
  }

  // print a seril frame every .5s
  if (serial_freq.TRIGGERED) {
    serial_debug();
  }

  // add a rolling schedule here that loops through a bunch off different functions
}