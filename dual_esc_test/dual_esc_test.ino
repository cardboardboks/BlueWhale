// libs
#include <FastLED.h>
#include <SPI.h>
#include <BlockNot.h>
#include <AlfredoDShot.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"
#include <AlfredoCRSF.h>
#include <HardwareSerial.h>

// defines
// crsf rx
#define PIN_RX 20
#define PIN_TX 21

// imu
#define SDA_PIN 8
#define SCL_PIN 9
#define INT_PIN 10

// leds
#define NUM_LEDS 12
#define PIN_SCK 6   // unused by the strip, but SPI needs one
#define PIN_MOSI 7  // connect to the strip's DIN (check your board's pinout)
#define SPI_HZ 2400000

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
const int PIN_ESC1 = 0;
const int PIN_ESC2 = 1;
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
BlockNot dshot_telem_freq(1000);
BlockNot crsf_telem_freq(100);
BlockNot led_freq(1000);
BlockNot serial_freq(500);

// rx channel array
int ch[8] = { 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500 };

// pre arm safe varibles
bool safe_switch = 0;
bool safe_sticks = 0;
bool safe_link = 0;

// leds
// configure leds
CRGB leds[NUM_LEDS];

static uint8_t txbuf[NUM_LEDS * 9 + 120];  // 9 bytes per LED + reset padding
// Expand one color byte into 3 SPI bytes (8 bits * 3 = 24 bits)
static inline void encodeByte(uint8_t v, uint8_t *out) {
  uint32_t bits = 0;
  for (int i = 7; i >= 0; i--) {
    bits = (bits << 3) | ((v >> i) & 1 ? 0b110 : 0b100);
  }
  out[0] = bits >> 16;
  out[1] = bits >> 8;
  out[2] = bits;
}

void showSPI(uint8_t brightness = 255) {
  uint8_t *p = txbuf;
  for (int i = 0; i < NUM_LEDS; i++) {
    CRGB c = leds[i];
    c.nscale8(brightness);
    encodeByte(c.g, p);
    p += 3;  // WS2812 order is GRB
    encodeByte(c.r, p);
    p += 3;
    encodeByte(c.b, p);
    p += 3;
  }
  // trailing zeros hold the line low (> 280 us) as the reset latch
  memset(p, 0, 120);

  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  SPI.transferBytes(txbuf, nullptr, sizeof(txbuf));
  SPI.endTransaction();
}

void setup() {

  // configure leds, sneak this in beofre esc's so we can use them to show status
  SPI.begin(PIN_SCK, -1, PIN_MOSI, -1);
  led(1);

  // configure escs, must be done very close to first. check esc lib documenation for details
  boot_esc();

  // configure serial
  Serial.begin(115200);
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
  // process stabilization
  pid();
  // send commands to the esc;s
  drive_esc(drive1_pwm, drive2_pwm);
  // send telem over rc link
  crsf_tx();
  //print out debug messages
  serial_debug(2);
}
