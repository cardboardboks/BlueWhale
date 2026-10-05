// I2Cdev and MPU6050 must be installed as libraries, or else the .cpp/.h files
// for both classes must be in the include path of your project
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps612.h"
#include "Wire.h"

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>  // Required for 16 MHz Adafruit Trinket
#endif

// class default I2C address is 0x68
MPU6050 mpu;

Adafruit_NeoPixel pixels(3, 2, NEO_GRB + NEO_KHZ800);

#define INTERRUPT_PIN 3  // use pin 2 on Arduino Uno & most boards
#define LED_PIN 13       // (Arduino is 13, Teensy is 11, Teensy++ is 6)
bool blinkState = false;

// MPU control/status vars
bool dmpReady = false;   // set true if DMP init was successful
uint8_t mpuIntStatus;    // holds actual interrupt status byte from MPU
uint8_t devStatus;       // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;     // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;      // count of all bytes currently in FIFO
uint8_t fifoBuffer[64];  // FIFO storage buffer

// orientation/motion vars
Quaternion q;         // [w, x, y, z]         quaternion container
VectorInt16 aa;       // [x, y, z]            accel sensor measurements
VectorInt16 gy;       // [x, y, z]            gyro sensor measurements
VectorInt16 aaReal;   // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaWorld;  // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity;  // [x, y, z]            gravity vector
float euler[3];       // [psi, theta, phi]    Euler angle container
float ypr[3];         // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector

// packet structure for InvenSense teapot demo
uint8_t teapotPacket[14] = { '$', 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, '\r', '\n' };

int roll = 0;
int pitch = 0;
int yaw = 0;

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

// ================================================================
// ===               INTERRUPT DETECTION ROUTINE                ===
// ================================================================

volatile bool mpuInterrupt = false;  // indicates whether MPU interrupt pin has gone high
void dmpDataReady() {
  mpuInterrupt = true;
}


// ================================================================
// ===                      INITIAL SETUP                       ===
// ================================================================

void setup() {
  Wire.begin();
  Wire.setClock(400000);  // 400kHz I2C clock. Comment this line if having compilation difficulties

  // initialize serial communication
  // Serial.begin(115200);

  // initialize device
  Serial.println(F("Initializing I2C devices..."));
  mpu.initialize();
  pinMode(INTERRUPT_PIN, INPUT);

  // verify connection
  Serial.println(F("Testing device connections..."));
  Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));

  // load and configure the DMP
  Serial.println(F("Initializing DMP..."));
  devStatus = mpu.dmpInitialize();

  // supply your own gyro offsets here, scaled for min sensitivity
  mpu.setXGyroOffset(-234);
  mpu.setYGyroOffset(24);
  mpu.setZGyroOffset(-9);
  mpu.setXAccelOffset(-698);
  mpu.setYAccelOffset(476);
  mpu.setZAccelOffset(2538);
  // make sure it worked (returns 0 if so)
  if (devStatus == 0) {
    // Calibration Time: generate offsets and calibrate our MPU6050
    //mpu.CalibrateAccel(50);
    //mpu.CalibrateGyro(50);
    Serial.println();
    mpu.PrintActiveOffsets();
    // turn on the DMP, now that it's ready
    Serial.println(F("Enabling DMP..."));
    mpu.setDMPEnabled(true);

    // enable Arduino interrupt detection
    Serial.print(F("Enabling interrupt detection (Arduino external interrupt "));
    Serial.print(digitalPinToInterrupt(INTERRUPT_PIN));
    Serial.println(F(")..."));
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
    mpuIntStatus = mpu.getIntStatus();

    // set our DMP Ready flag so the main loop() function knows it's okay to use it
    Serial.println(F("DMP ready! Waiting for first interrupt..."));
    dmpReady = true;

    // get expected DMP packet size for later comparison
    packetSize = mpu.dmpGetFIFOPacketSize();
  } else {
    // ERROR!
    // 1 = initial memory load failed
    // 2 = DMP configuration updates failed
    // (if it's going to break, usually the code will be 1)
    Serial.print(F("DMP Initialization failed (code "));
    Serial.print(devStatus);
    Serial.println(F(")"));
  }

  // configure LED for output
  pinMode(LED_PIN, OUTPUT);


  for (int RPthisReading = 0; RPthisReading < RPnumReadings; RPthisReading++) {
    RPreadings[RPthisReading] = 0;
  }
  for (int YthisReading = 0; YthisReading < YnumReadings; YthisReading++) {
    Yreadings[YthisReading] = 0;
  }


  pixels.begin();                   // INITIALIZE NeoPixel strip object (REQUIRED)
  pixels.clear();                   // Set all pixel colors to 'off'
  Wire.setWireTimeout(3000, true);  // Timeout after 3ms and reset the bus
}



// ================================================================
// ===                    MAIN PROGRAM LOOP                     ===
// ================================================================

void loop() {
  // if programming failed, don't try to do anything
  if (!dmpReady) return;
  // read a packet from FIFO
  if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {  // Get the Latest packet

    // display Euler angles in degrees
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
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
    RPreadings[RPreadIndex] = roll + pitch;
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

    // blink LED to indicate activity
    blinkState = !blinkState;
    digitalWrite(LED_PIN, blinkState);
  }

  if (Y > 24000) {
    Y = 24000;
  }

  if (RP > 70) {
    pixels.setPixelColor(0, pixels.Color(Y * .01 + 10, 0, 0));
    pixels.setPixelColor(1, pixels.Color(Y * .01 + 10, 0, 0));
    pixels.setPixelColor(2, pixels.Color(Y * .01 + 10, 0, 0));
  } else {
    pixels.setPixelColor(0, pixels.Color(0, Y * .01 + 10, 0));
    pixels.setPixelColor(1, pixels.Color(0, Y * .01 + 10, 0));
    pixels.setPixelColor(2, pixels.Color(0, Y * .01 + 10, 0));
  }
  pixels.show();
}
