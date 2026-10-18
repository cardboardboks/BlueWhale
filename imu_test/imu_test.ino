#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"
#include <AlfredoCRSF.h>
#include <HardwareSerial.h>

#define PIN_RX 20
#define PIN_TX 21

#define SDA_PIN 8
#define SCL_PIN 9
#define INT_PIN 10

HardwareSerial crsfSerial(1);
AlfredoCRSF crsf;

uint32_t lastHeartbeatMs = 0;
uint32_t lastPrintMs = 0;

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>  // Required for 16 MHz Adafruit Trinket
#endif

Adafruit_NeoPixel pixels(2, 4, NEO_GRB + NEO_KHZ800);

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

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  Serial.println("ELRS 4.x receiver test");

  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);
  if (!crsfSerial)
    while (1) Serial.println("Invalid crsfSerial configuration");

  crsf.begin(crsfSerial);             // defaults to CRSF_ADDRESS_FLIGHT_CONTROLLER
  crsf.setDeviceName("AlfredoCRSF");  // answer device discovery pings

  for (int RPthisReading = 0; RPthisReading < RPnumReadings; RPthisReading++) {
    RPreadings[RPthisReading] = 0;
  }
  for (int YthisReading = 0; YthisReading < YnumReadings; YthisReading++) {
    Yreadings[YthisReading] = 0;
  }


  pixels.begin();  // INITIALIZE NeoPixel strip object (REQUIRED)
  pixels.clear();  // Set all pixel colors to 'off'


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
}

void loop() {


  crsf.update();

  // Announce ourselves to the CRSF router once per second
  if (millis() - lastHeartbeatMs > 1000) {
    lastHeartbeatMs = millis();
    crsf.sendHeartbeat();
  }

  if (millis() - lastPrintMs > 1000) {
    lastPrintMs = millis();
    printEverything();
  }


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


void printEverything() {
  Serial.print(RP);
  Serial.print("\t");
  Serial.println(Y);
  Serial.println("----------------------------------------");
  Serial.print("link: ");
  Serial.print(crsf.isLinkUp() ? "UP" : "DOWN");
  const crsfLinkStatistics_t *link = crsf.getLinkStatistics();
  Serial.print("  LQ: ");
  Serial.print(link->uplink_Link_quality);
  Serial.print("  RSSI: -");
  Serial.print(link->active_antenna == 0 ? link->uplink_RSSI_1 : link->uplink_RSSI_2);
  Serial.print("dBm  armed: ");
  Serial.println(crsf.isArmed() ? "yes" : "no");

  Serial.print("channels 1-8:");
  for (int i = 1; i <= 8; i++) {
    Serial.print(" ");
    Serial.print(crsf.getChannel(i));
  }
  Serial.println("");

  // ELRS 4.0: VBAT receivers send millivolt-precision voltage as CELLS
  const crsf_sensor_cells_t *cells = crsf.getCellsSensor();
  if (cells->cell_count > 0) {
    Serial.print("cells (source ");
    Serial.print(cells->source_id);
    Serial.print("):");
    for (int i = 0; i < cells->cell_count; i++) {
      Serial.print(" ");
      Serial.print(cells->cell[i]);
      Serial.print("mV");
    }
    Serial.println("");
  }

  const crsf_sensor_temp_t *temp = crsf.getTempSensor();
  if (temp->temp_count > 0) {
    Serial.print("temps (deci-C):");
    for (int i = 0; i < temp->temp_count; i++) {
      Serial.print(" ");
      Serial.print(temp->temperature[i]);
    }
    Serial.println("");
  }

  const crsf_sensor_rpm_t *rpm = crsf.getRpmSensor();
  if (rpm->rpm_count > 0) {
    Serial.print("rpm:");
    for (int i = 0; i < rpm->rpm_count; i++) {
      Serial.print(" ");
      Serial.print(rpm->rpm[i]);
    }
    Serial.println("");
  }

  if (crsf.getAirspeedSensor()->speed != 0) {
    Serial.print("airspeed: ");
    Serial.print(crsf.getAirspeedSensor()->speed / 10.0);
    Serial.println(" km/h");
  }

  const crsf_sensor_gps_time_t *gpsTime = crsf.getGpsTimeSensor();
  if (gpsTime->year != 0) {
    Serial.print("gps time: ");
    Serial.print(gpsTime->year);
    Serial.print("-");
    Serial.print(gpsTime->month);
    Serial.print("-");
    Serial.print(gpsTime->day);
    Serial.print(" ");
    Serial.print(gpsTime->hour);
    Serial.print(":");
    Serial.print(gpsTime->minute);
    Serial.print(":");
    Serial.println(gpsTime->second);
  }
}
