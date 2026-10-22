#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "Wire.h"
#include <AlfredoCRSF.h>
#include <HardwareSerial.h>
#include <Arduino.h>
#include <ESPCpuMonitor.h>
#include <SPI.h>
#include <AlfredoDShot.h>

const int PIN_ESC1 = 1;
const int PIN_ESC2 = 2;
const uint8_t MOTOR_POLES = 14;

AlfredoDShot esc1;
AlfredoDShot esc2;

ESPCpuMonitor cpuMonitor;  // you create the instance; only one can be active at a time

#define PIN_RX 5
#define PIN_TX 4

#define SCL_PIN 6
#define SDA_PIN 7
#define INT_PIN 8


#define LED_PIN 11  // This becomes the SPI MOSI pin (any GPIO works via the GPIO matrix)
#define NUM_LEDS 12
#define SPI_FREQ 3200000  // 3.2 MHz

// Classic ESP32 uses VSPI; ESP32-S2/S3/C3 use FSPI
#if defined(CONFIG_IDF_TARGET_ESP32)
SPIClass ledSPI(VSPI);
#else
SPIClass ledSPI(FSPI);
#endif

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

void encodeByte(uint8_t v, uint8_t *out) {
  out[0] = lut[(v >> 6) & 3];
  out[1] = lut[(v >> 4) & 3];
  out[2] = lut[(v >> 2) & 3];
  out[3] = lut[v & 3];
}

void showLeds() {
  uint8_t *p = spiBuf;
  for (int i = 0; i < NUM_LEDS; i++) {
    // WS2812 expects GRB order
    encodeByte(leds[i].g, p);
    p += 4;
    encodeByte(leds[i].r, p);
    p += 4;
    encodeByte(leds[i].b, p);
    p += 4;
  }
  memset(p, 0, RESET_BYTES);  // latch

  ledSPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
  ledSPI.writeBytes(spiBuf, sizeof(spiBuf));
  ledSPI.endTransaction();
}

// ---- LED task (runs on core 0) ----
void ledTask(void *pvParameters) {
  // Init SPI inside the task so it's set up on core 0
  // begin(sck, miso, mosi, ss) -> only MOSI is used
  ledSPI.begin(12, -1, LED_PIN, -1);

  uint8_t hue = 0;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    // Simple rainbow chase
    for (int i = 0; i < NUM_LEDS; i++) {
      uint8_t h = hue + i * (256 / NUM_LEDS);
      // quick HSV(h,255,64) -> RGB
      uint8_t region = h / 43, rem = (h - region * 43) * 6;
      uint8_t v = 64, q = (v * (255 - rem)) >> 8, t = (v * rem) >> 8;
      switch (region) {
        case 0: leds[i] = { v, t, 0 }; break;
        case 1: leds[i] = { q, v, 0 }; break;
        case 2: leds[i] = { 0, v, t }; break;
        case 3: leds[i] = { 0, q, v }; break;
        case 4: leds[i] = { t, 0, v }; break;
        default: leds[i] = { v, 0, q }; break;
      }
    }
    hue++;
    showLeds();

    // Fixed 10 ms period (more consistent than delay() after work)
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(5));
  }
}

HardwareSerial crsfSerial(1);
AlfredoCRSF crsf;

uint32_t lastHeartbeatMs = 0;
uint32_t lastPrintMs = 0;

int roll = 0;
int pitch = 0;
int yaw = 0;

int roll_flip = 70;

const int RPnumReadings = 50;
int RPreadings[RPnumReadings];
int RPreadIndex = 0;
int RPtotal = 0;

const int YnumReadings = 5;
int Yreadings[YnumReadings];
int YreadIndex = 0;
int Ytotal = 0;

// Shared between the IMU task (core 0) and loop() (core 1).
// volatile because they are written on one core and read on the other.
volatile int RP = 0;  // smoothed roll+pitch
volatile int Y = 0;   // smoothed yaw rate

volatile float core0 = 0;
volatile float core1 = 0;
volatile float coreA = 0;

int throttle1 = 0;
int throttle2 = 0;
int drive1_pwm = 0;
int drive2_pwm = 0;
int gearbox_ratio1 = 19;
int gearbox_ratio2 = 19;
int wheel_diamiter1 = 101;  // in mm
int wheel_diamiter2 = 101;  // in mm


const unsigned long INTERVAL_US = 1000;  // 1000 µs = 1 ms
unsigned long previousMicros = 0;

MPU6050 mpu;

TaskHandle_t imuTaskHandle = nullptr;

// MPU interrupt: wake the IMU task. No polling, no delay needed.
void IRAM_ATTR dmpDataReady() {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(imuTaskHandle, &woken);
  if (woken) portYIELD_FROM_ISR();
}

uint8_t fifoBuffer[64];
Quaternion q;
VectorFloat gravity;
VectorInt16 gy;  // [x, y, z] gyro sensor measurements
float ypr[3];
bool dmpReady = false;

// All IMU work lives here, pinned to core 0.
void imuTask(void *arg) {
  for (;;) {
    // Sleep until the MPU interrupt fires. The 100 ms timeout recovers from a
    // missed interrupt, and the idle task always gets to run so the watchdog is happy.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

    if (!mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) continue;

    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

    pitch = (int)fabsf(ypr[1] * 180.0f / M_PI);
    roll = (int)fabsf(ypr[2] * 180.0f / M_PI);

    RPtotal -= RPreadings[RPreadIndex];
    RPreadings[RPreadIndex] = min(roll + pitch, roll_flip * 2);
    RPtotal += RPreadings[RPreadIndex];
    RPreadIndex = (RPreadIndex + 1) % RPnumReadings;
    RP = RPtotal / RPnumReadings;

    mpu.dmpGetGyro(&gy, fifoBuffer);
    yaw = abs(gy.z);

    Ytotal -= Yreadings[YreadIndex];
    Yreadings[YreadIndex] = yaw;
    Ytotal += Yreadings[YreadIndex];
    YreadIndex = (YreadIndex + 1) % YnumReadings;

    int y = Ytotal / YnumReadings;
    Y = (y > 1400) ? 1400 : y;
  }
}

void setup() {
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


  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  Serial.println("ELRS 4.x receiver test");

  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);
  if (!crsfSerial)
    while (1) Serial.println("Invalid crsfSerial configuration");

  crsf.begin(crsfSerial);             // defaults to CRSF_ADDRESS_FLIGHT_CONTROLLER
  crsf.setDeviceName("AlfredoCRSF");  // answer device discovery pings

  for (int i = 0; i < RPnumReadings; i++) RPreadings[i] = 0;
  for (int i = 0; i < YnumReadings; i++) Yreadings[i] = 0;

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

    // Create the task BEFORE attaching the interrupt: the ISR needs a valid handle.
    xTaskCreatePinnedToCore(
      imuTask,         // task function
      "imu_0",         // name
      4096,            // stack size (bytes)
      nullptr,         // parameter
      10,              // priority
      &imuTaskHandle,  // task handle (not needed)
      0                // core 0
    );

    xTaskCreatePinnedToCore(
      ledTask,    // task function
      "ledTask",  // name
      4096,       // stack size (bytes)
      NULL,       // parameter
      15,         // priority
      NULL,       // task handle (not needed)
      0           // core 0
    );


    pinMode(INT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), dmpDataReady, RISING);
    dmpReady = true;
    Serial.println("DMP ready");
  } else {
    // 1 = memory load failed, 2 = DMP config update failed
    Serial.printf("DMP init failed (code %d)\n", devStatus);
    while (1) delay(1000);
  }

  CpuMonitorConfig cpucfg;
  cpucfg.sampleIntervalMs = 100;
  cpucfg.calibrationSamples = 5;     // first 5 windows = "100% idle" baseline
  cpucfg.enablePerCore = true;       // separate core0/core1 values
  cpucfg.enableTemperature = false;  // not needed here

  if (!cpuMonitor.init(cpucfg)) {
    Serial.println("CPU monitor init failed");
    return;
  }

  cpuMonitor.onSample([](const CpuUsageSample &s) {
    core0 = s.perCore[0];
    core1 = s.perCore[1];
    coreA = s.average;
  });


  // wait for boot and set leds to booting colour
  for (unsigned long start = millis(); millis() - start < 2000;) {
  }

  Serial.begin(115200);
  esc2.begin(PIN_ESC1, DSHOT600, true, MOTOR_POLES);
  esc1.begin(PIN_ESC2, DSHOT600, true, MOTOR_POLES);

  // wait for ESC to boot before enabling EDT
  for (unsigned long start = millis(); millis() - start < 5000;) {
    unsigned long currentMicros = micros();

    if (currentMicros - previousMicros >= INTERVAL_US) {
      previousMicros += INTERVAL_US;  // drift-free
      esc1.sendThrottle(0.f);         // 0 %
      esc2.sendThrottle(0.f);         // 0 %
    }
  }

  // enable EDT
  esc1.command(DSHOT_CMD_EDT_ENABLE);
  esc2.command(DSHOT_CMD_EDT_ENABLE);
}

void loop() {
  crsf.update();
  static uint32_t next = micros();
  static uint32_t lastPrint = 0;

  unsigned long currentMicros = micros();

  if (currentMicros - previousMicros >= INTERVAL_US) {
    previousMicros += INTERVAL_US;  // drift-free
    esc1.sendThrottle(0.08f);       // 8 %
    esc2.sendThrottle(0.08f);       // 8 %
  }

  // Announce ourselves to the CRSF router once per second
  if (millis() - lastHeartbeatMs > 1000) {
    lastHeartbeatMs = millis();
    crsf.sendHeartbeat();

    esc1.stats();
    esc2.stats();

    crsf_sensor_battery_t crsfBatt = { 0 };

    crsfBatt.voltage = htobe16((uint16_t)(esc1.voltage() * 10.0));  //Volts
    crsfBatt.current = htobe16((uint16_t)(esc2.voltage() * 10.0));  //Amps
    crsfBatt.capacity = htobe24((uint32_t)(esc1.temperatureC()));   //mAh (24 bit field, max 16777215mAh)
    crsfBatt.remaining = (uint8_t)(esc2.temperatureC());            //percent
    crsf.queuePacket(CRSF_SYNC_BYTE, CRSF_FRAMETYPE_BATTERY_SENSOR, &crsfBatt, sizeof(crsfBatt));
  }

  if (millis() - lastPrintMs > 100) {
    lastPrintMs = millis();
    printEverything();
  }

  vTaskDelay(pdMS_TO_TICKS(1));
}

void printEverything() {
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
  Serial.printf(
    "%d %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h   |",
    (throttle1 - 1500) / 5,
    esc1.voltage(),
    esc1.current(),
    esc1.temperatureC(),
    (esc1.rpm() / gearbox_ratio1),
    (wheel_diamiter1 / 10) * (esc1.rpm() / gearbox_ratio1) * 0.001885);
  Serial.println(esc1.lossPercent());

  Serial.printf(
    "%d %% | %3.1f V | %2.0f A | %2.0f C | %4.0f | %3.0f Km/h   |",
    (throttle2 - 1500) / 5,
    esc2.voltage(),
    esc2.current(),
    esc2.temperatureC(),
    (esc2.rpm() / gearbox_ratio2),
    (wheel_diamiter2 / 10) * (esc2.rpm() / gearbox_ratio2) * 0.001885);
  Serial.println(esc2.lossPercent());

  Serial.printf("core0=%.1f%%  core1=%.1f%%  avg=%.1f%%\n",
                core0, core1, coreA);

  Serial.print("Tilt: ");
  Serial.print(RP);
  Serial.print("\t");
  Serial.print("Yaw: ");
  Serial.println(Y);

  Serial.println("");
  Serial.println("");
}
