// Libs
#include <AlfredoDShot.h>
#include <BlockNot.h>
#include <math.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"
#include <WiFi.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include <AlfredoCRSF.h>
#include <HardwareSerial.h>

#define PIN_RX 20
#define PIN_TX 21
#define TELEM_INTERVAL_MS 100

//  these things
AlfredoDShot esc1;
AlfredoDShot esc2;
HardwareSerial crsfSerial(1);
AlfredoCRSF crsf;

//  varibles
//  esc varibles
const int PIN_ESC1 = 7;
const int PIN_ESC2 = 6;
const uint8_t MOTOR1_POLES = 14;
const uint8_t MOTOR2_POLES = 14;
const int esc_arm_duration = 5000;
int throttle1 = 0;
int throttle2 = 0;
int gearbox_ratio1 = 19;
int gearbox_ratio2 = 19;
int wheel_diamiter1 = 101;  // in mm
int wheel_diamiter2 = 101;  // in mm
BlockNot dshot_freq(1000, MICROSECONDS);

const bool SEND_EDT = true;  // run once true, once false

BlockNot serial_freq(500);

int ch[8] = { 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500 };

uint32_t lastTelemMs = 0;

static uint32_t next = micros(), lastPrint = 0, maxGap = 0, lastSend = micros();
static bool cmdSent = false;
uint32_t t0;

void setup() {

  // configure escs
  boot_esc();

  // set up serial
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  Serial.println("Whale booted");

  boot_crsf();
}

void loop() {

  drive_esc(ch[3], ch[3]);
  crsf_recive();
  serial_debug();
}