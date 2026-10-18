// libs
#include <Arduino.h>
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
#define LED_PIN 6  // This becomes the SPI MOSI pin (any GPIO works via the GPIO matrix)
#define NUM_LEDS 12
#define SPI_FREQ 3200000  // 3.2 MHz
SPIClass ledSPI(FSPI);
#define RESET_BYTES 64  // 64 bytes * 8 bits * 312.5 ns = ~160 us of low
static uint8_t spiBuf[NUM_LEDS * 12 + RESET_BYTES];

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

void setup() {

  // configure leds, sneak this in beofre esc's so we can use them to show status
  // begin(sck, miso, mosi, ss) -> only MOSI is used
  ledSPI.begin(4, -1, LED_PIN, -1);
  // led(1);

  // configure escs, must be done very close to first. check esc lib documenation for details
  // open drain so we only pull down, and via the IDF rather than pinMode() so
  // the Arduino peripheral manager does not claim the pad ahead of the RMT
  // driver in begin().

  // running this code pulled from the .cpp to avoid waiting 2.5s esc if it were to call the funtion twice
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << PIN_ESC1;
  cfg.mode = GPIO_MODE_OUTPUT_OD;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
  gpio_set_level((gpio_num_t)PIN_ESC1, 0);

  cfg.pin_bit_mask = 1ULL << PIN_ESC2;
  cfg.mode = GPIO_MODE_OUTPUT_OD;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
  gpio_set_level((gpio_num_t)PIN_ESC2, 0);

  // wait for boot and set leds to booting colour
  for (unsigned long start = millis(); millis() - start < esc_arm_duration / 2;) {
    //   led(1);
  }

  // create esc object, set up and bind to pin
  // esc#, pin bound to, dshot freq, bidirectinal, pol count
  esc1.begin(PIN_ESC1, DSHOT600, true, MOTOR1_POLES);
  esc2.begin(PIN_ESC2, DSHOT600, true, MOTOR2_POLES);

  // wait for ESC to boot before enabling EDT
  for (unsigned long start = millis(); millis() - start < esc_arm_duration;) {
    //  led(1);
    drive_esc(0, 0);
  }

  // enable EDT
  esc1.command(DSHOT_CMD_EDT_ENABLE);
  esc2.command(DSHOT_CMD_EDT_ENABLE);

  // configure serial
  Serial.begin(115200);
  // make sure usb cdc on boot is enabled or this will throw an error
  Serial.setTxTimeoutMs(0);
  Serial.println("Whale booted");

  // configure rx and imu
  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);

  if (!crsfSerial)
    while (1) Serial.println("Invalid crsfSerial configuration");
  crsf.begin(crsfSerial);



  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  for (int RPthisReading = 0; RPthisReading < RPnumReadings; RPthisReading++) {
    RPreadings[RPthisReading] = 0;
  }
  for (int YthisReading = 0; YthisReading < YnumReadings; YthisReading++) {
    Yreadings[YthisReading] = 0;
  }

  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 not found - check wiring/address");
    //  while (1) delay(1000);
  }

  uint8_t devStatus = mpu.dmpInitialize();  // uploads DMP firmware (~1.9 KB)

  // supply your own gyro offsets here, scaled for min sensitivity
  mpu.setXGyroOffset(-234);
  mpu.setYGyroOffset(24);
  mpu.setZGyroOffset(-9);
  mpu.setXAccelOffset(-698);
  mpu.setYAccelOffset(476);
  mpu.setZAccelOffset(2538);

  if (devStatus == 0) {
    // mpu.CalibrateAccel(6);  // keep the sensor flat and still during this
    // mpu.CalibrateGyro(6);
    mpu.setDMPEnabled(true);
    pinMode(INT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), dmpDataReady, RISING);
    dmpReady = true;
    Serial.println("DMP ready");
  } else {
    // 1 = memory load failed, 2 = DMP config update failed
    Serial.printf("DMP init failed (code %d)\n", devStatus);
    // while (1) delay(1000);
  }

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
  // pid();
  // send commands to the esc;s
  drive_esc(drive1_pwm, drive2_pwm);
  // send telem over rc link
  crsf_tx();
  //print out debug messages
  serial_debug(2);
}
