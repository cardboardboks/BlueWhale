// boards
//espressif - arduino-esp32
// make sure this is 3.3.10

// libs
#include <Arduino.h>
#include <SPI.h>
#include <BlockNot.h>
#include <AlfredoDShot.h>  // make sure this is 1.0
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"
#include <AlfredoCRSF.h>  // make sure this is 2.3
#include <HardwareSerial.h>

// defines
// crsf rx
#define PIN_RX 1
#define PIN_TX 2

#define SDA_PIN 6
#define SCL_PIN 7
#define INT_PIN 5

// leds
#define LED_PIN_MOSI 3  // mosi This becomes the SPI MOSI pin (any GPIO works via the GPIO matrix)
#define LED_PIN_SCK 4   // sck
#define NUM_LEDS 12
#define SPI_FREQ 3200000  // 3.2 MHz
SPIClass ledSPI(FSPI);
#define RESET_BYTES 64  // 64 bytes * 8 bits * 312.5 ns = ~160 us of low
static uint8_t spiBuf[NUM_LEDS * 12 + RESET_BYTES];
bool led_arry[26] = { 0 };

struct RGB {
  uint8_t r, g, b;
};
RGB leds[NUM_LEDS];

// Lookup: 2 data bits -> 1 SPI byte (two WS2812 bits = 8 SPI bits)
static const uint8_t lut[4] = {
  0b10001000,  // 00
  0b10001110,  // 01
  0b11101000,  // 10
  0b11101110   // 11
};

// imu data
// dmp orientation
int roll = 0;
int pitch = 0;
int yaw = 0;

// imu alive flag
int no_imu = 0;

// combined angle flip threshhold
int roll_flip = 70;

// smoothing for dmp orientation
const int RPnumReadings = 50;
int RPreadings[RPnumReadings];
int RPreadIndex = 0;
int RPtotal = 0;
int RP = 0;

// smoothing for yaw gyro
const int YnumReadings = 5;
int Yreadings[YnumReadings];
int YreadIndex = 0;
int Ytotal = 0;
int Y = 0;

// more imu stuff
MPU6050 mpu;

volatile bool mpuInterrupt = false;
void IRAM_ATTR dmpDataReady() {
  mpuInterrupt = true;
}

uint8_t fifoBuffer[64];
Quaternion q;
VectorFloat gravity;
VectorInt16 gy;  // [x, y, z]            gyro sensor measurements
float ypr[3];
bool dmpReady = false;

//  these esc and rx things
AlfredoDShot esc1;
AlfredoDShot esc2;
HardwareSerial crsfSerial(1);
AlfredoCRSF crsf;

//  esc varibles
const int PIN_ESC1 = 8;
const int PIN_ESC2 = 9;
const uint8_t MOTOR1_POLES = 14;
const uint8_t MOTOR2_POLES = 14;
const int esc_arm_duration = 5000;
int throttle1 = 0;
int throttle2 = 0;
int drive1_pwm = 0;
int drive2_pwm = 0;
int gearbox_ratio1 = 19;
int gearbox_ratio2 = 19;
int wheel_diamiter1 = 101;  // in mm
int wheel_diamiter2 = 101;  // in mm

// set up non blocking timers
BlockNot dshot_freq(1000, MICROSECONDS);
BlockNot crsf_telem_freq(100);
BlockNot led_freq(10);
BlockNot serial_freq(500);
BlockNot rainbow_freq(2);
BlockNot crsf_tx_freq(1000);

// rx channel array
int ch[10] = { 1500 };

// pre arm safe varibles
bool safe_switch = 0;
bool safe_sticks = 0;
bool safe_link = 0;
bool safed = 0;

// leds
// configure leds

void setup() {

  // configure leds, sneak this in beofre esc's so we can use them to show status
  // begin(sck, miso, mosi, ss) -> only MOSI is used
  ledSPI.begin(LED_PIN_SCK, -1, LED_PIN_MOSI, -1);
  led(1, 1);

  // configure escs, must be done very close to first. check esc lib documenation for details
  boot_esc();

  // configure serial
  Serial.begin(921600);
  // make sure usb cdc on boot is enabled or this will throw an error
  Serial.setTxTimeoutMs(0);
  Serial.println("Whale booted");

  // configure rx and imu
  boot_crsf();
  boot_imu();

  //start the pre arm safe checks
  pre_arm_safe();
}

void loop() {

  // read rx data
  crsf_rx();
  // read imu data
  imu();
  // invoke the mixer
  mixer(ch[1], ch[2]);
  // process stabilisation
  // pid();
  // send commands to the esc;s
  drive_esc(drive1_pwm, drive2_pwm);
  // send telem over rc link
  crsf_tx();
  // drive leds
  led(0, 0);
  // print out debug messages
  // serial_debug(2);
}
