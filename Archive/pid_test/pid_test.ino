//WHALE

//PID controller stuff
//https://github.com/br3ttb/Arduino-PID-Library/

//Board https://espressif.github.io/arduino-esp32/package_esp32_index.json
//arduino-esp32 by espressif
//Flash as ESP32C3 Dev Module

#include <Wire.h>
#include "src/libraries/Arduino-PID-Library/PID_v1.h"
#include "src/libraries/ESP32Servo/ESP32Servo.h"

#define PIN_INPUT 0
#define PIN_OUTPUT 3

Servo left;
Servo right;

//Define Variables we'll be connecting to
double Setpoint, Input, Output;

//Specify the links and initial tuning parameters
double Kp = 2, Ki = 5, Kd = 1;
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);

void setup() {
  //initialize the variables we're linked to
  Input = analogRead(PIN_INPUT);
  Setpoint = 100;

  //turn the PID on
  myPID.SetMode(AUTOMATIC);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  left.setPeriodHertz(50);
  right.setPeriodHertz(50);       // standard 50 hz servo
  left.attach(34, 1000, 2000);  // attaches the servo object
  right.attach(35, 1000, 2000);
}

void loop() {
  Input = analogRead(PIN_INPUT);
  myPID.Compute();
  analogWrite(PIN_OUTPUT, Output);
}