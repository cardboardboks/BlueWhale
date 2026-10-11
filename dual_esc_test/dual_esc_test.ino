// Libs
#include <FastLED.h>
#include <SPI.h>
#include <BlockNot.h>
#include <AlfredoDShot.h>
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

#define SDA_PIN 8
#define SCL_PIN 9
#define INT_PIN 10

#define NUM_LEDS 30
#define PIN_SCK 6   // unused by the strip, but SPI needs one
#define PIN_MOSI 7  // connect to the strip's DIN (check your board's pinout)
#define SPI_HZ 2400000

int roll = 0;
int pitch = 0;
int yaw = 0;

int roll_flip = 70;

const int RPnumReadings = 50;
int RPreadings[RPnumReadings];  // the readings from the analog input
int RPreadIndex = 0;            // the index of the current reading
int RPtotal = 0;                // the running total
int RP = 0;                     // the average

const int YnumReadings = 5;
int Yreadings[YnumReadings];  // the readings from the analog input
int YreadIndex = 0;           // the index of the current reading
int Ytotal = 0;               // the running total
int Y = 0;                    // the average

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

CRGB leds[NUM_LEDS];
static uint8_t txbuf[NUM_LEDS * 9 + 120];  // 9 bytes per LED + reset padding


//  these things
AlfredoDShot esc1;
AlfredoDShot esc2;
HardwareSerial crsfSerial(1);
AlfredoCRSF crsf;

//  varibles
//  esc varibles
const int PIN_ESC1 = 0;
const int PIN_ESC2 = 1;
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
BlockNot led_freq(1000);

int ch[8] = { 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500 };
bool safe_switch = 0;
bool safe_sticks = 0;
bool safe_link = 0;

uint32_t lastTelemMs = 0;

static uint32_t next = micros(), lastPrint = 0, maxGap = 0, lastSend = micros();
static bool cmdSent = false;
uint32_t t0;

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

  // configure leds
  SPI.begin(PIN_SCK, -1, PIN_MOSI, -1);
  leds[0] = CRGB(255, 255, 0);  // R, G, B
  leds[1] = CRGB(255, 255, 0);  // R, G, B
  showSPI(250);

  // configure escs
  boot_esc();

  // set up serial
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  Serial.println("Whale booted");

  boot_crsf();

  SPI.begin(PIN_SCK, -1, PIN_MOSI, -1);
  leds[0] = CRGB(255, 165, 0);  // R, G, B
  leds[1] = CRGB(255, 165, 0);  // R, G, B
  showSPI(250);

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
    while (1) delay(1000);
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
    while (1) delay(1000);
  }

  pre_arm_safe();
}

void loop() {
  crsf_rx();
  mixer(ch[1], ch[2]);
  crsf_tx();
  run_led();
  serial_debug(2);



  if (!dmpReady || !mpuInterrupt) return;
  mpuInterrupt = false;

  if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    //Serial.printf("ypr: %.1f  %.1f  %.1f\n",
    //              ypr[0] * 180 / M_PI, ypr[1] * 180 / M_PI, ypr[2] * 180 / M_PI);

    if (ypr[1] * 180 / M_PI < 0) {
      pitch = ypr[1] * -180 / M_PI;
    } else {
      pitch = ypr[1] * 180 / M_PI;
    }

    if (ypr[2] * 180 / M_PI < 0) {
      roll = ypr[2] * -180 / M_PI;
    } else {
      roll = ypr[2] * 180 / M_PI;
    }



    RPtotal = RPtotal - RPreadings[RPreadIndex];
    if ((roll + pitch) > (roll_flip * 2)) {
      RPreadings[RPreadIndex] = roll_flip * 2;
    } else {
      RPreadings[RPreadIndex] = roll + pitch;
    }
    RPtotal = RPtotal + RPreadings[RPreadIndex];
    RPreadIndex = RPreadIndex + 1;

    if (RPreadIndex >= RPnumReadings) {
      RPreadIndex = 0;
    }
    RP = RPtotal / RPnumReadings;

    Serial.print(RP);

    Serial.print("\t");
    mpu.dmpGetGyro(&gy, fifoBuffer);
    if (gy.z < 0) {
      yaw = gy.z * -1;
    } else {
      yaw = gy.z * 1;
    }

    Ytotal = Ytotal - Yreadings[YreadIndex];
    Yreadings[YreadIndex] = yaw;
    Ytotal = Ytotal + Yreadings[YreadIndex];
    YreadIndex = YreadIndex + 1;

    if (YreadIndex >= YnumReadings) {
      YreadIndex = 0;
    }
    Y = Ytotal / YnumReadings;

    Serial.println(Y);
  }

  if (Y > 1400) {
    Y = 1400;
  }

  if (RP > roll_flip) {
    // pixels.setPixelColor(0, pixels.Color(Y * .175 + 5, 0, 0));
    // pixels.setPixelColor(1, pixels.Color(Y * .175 + 5, 0, 0));
    // pixels.setPixelColor(2, pixels.Color(Y * .175 + 5, 0, 0));
  } else {
    // pixels.setPixelColor(0, pixels.Color(0, Y * .175 + 5, 0));
    // pixels.setPixelColor(1, pixels.Color(0, Y * .175 + 5, 0));
    // pixels.setPixelColor(2, pixels.Color(0, Y * .175 + 5, 0));
  }
  // pixels.show();
}