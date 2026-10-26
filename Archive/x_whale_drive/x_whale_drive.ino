// boards
//espressif - arduino-esp32
// make sure this is 3.3.12

// libs
#include <BlockNot.h>
#include <AlfredoDShot.h>  // make sure this is 1.1.0
#include <AlfredoCRSF.h>  // make sure this is 2.4
#include <HardwareSerial.h>

// defines
// crsf rx
#define PIN_RX 9
#define PIN_TX 8

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

  // configure escs, must be done very close to first. check esc lib documenation for details
 // boot_esc();

  // configure serial
  Serial.begin(921600);
  // make sure usb cdc on boot is enabled or this will throw an error
  Serial.setTxTimeoutMs(0);
  Serial.println("Whale booted");

  // configure rx and imu
  boot_crsf();
 // boot_imu();

  //start the pre arm safe checks
  pre_arm_safe();
}

void loop() {

  // read rx data
  crsf_rx();
  // read imu data
  //imu();
  // invoke the mixer
  //mixer(ch[1], ch[2]);
  // process stabilisation
  // pid();
  // send commands to the esc;s
 // drive_esc(drive1_pwm, drive2_pwm);
  // send telem over rc link
  crsf_tx();
  // drive leds
  //led(0, 0);
  // print out debug messages
  // serial_debug(2);
}
