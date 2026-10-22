// Bench test for an ELRS 4.x receiver connected as a flight controller would
// be. Prints channels, link stats, and all the newer telemetry sensors the
// receiver can produce, and joins the CRSF network as a discoverable device:
// with a 4.0 TX/RX pair this sketch should show up in the ExpressLRS Lua
// under Other Devices, and a VBAT-sensing receiver should produce a CELLS
// frame (source id 128) with millivolt precision.

#include <AlfredoCRSF.h>
#include <HardwareSerial.h>
#include <Arduino.h>
#include <SPI.h>
#include <AlfredoDShot.h>

const int PIN_ESC1 = 10;
const int PIN_ESC2 = 11;
const uint8_t MOTOR_POLES = 14;

AlfredoDShot esc1;
AlfredoDShot esc2;

#define PIN_RX 9
#define PIN_TX 8


HardwareSerial crsfSerial(1);
AlfredoCRSF crsf;

uint32_t lastHeartbeatMs = 0;
uint32_t lastPrintMs = 0;


#define LED_PIN 12  // This becomes the SPI MOSI pin (any GPIO works via the GPIO matrix)
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

uint8_t hue = 0;
unsigned long previousMicros = 0;
const unsigned long interval = 1000;  // 1000 microseconds (1 ms)
unsigned long previousMillis = 0;
const unsigned long interval1 = 10;  // 10 ms

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

  // wait for boot and set leds to booting colour
  for (unsigned long start = millis(); millis() - start < 2000;) {
  }

  Serial.begin(115200);

  esc1.begin(PIN_ESC1, DSHOT600, true, MOTOR_POLES);
  esc2.begin(PIN_ESC2, DSHOT600, true, MOTOR_POLES);

  Serial.println("ELRS 4.x receiver test");

  crsfSerial.begin(CRSF_BAUDRATE, SERIAL_8N1, PIN_RX, PIN_TX);
  if (!crsfSerial)
    while (1) Serial.println("Invalid crsfSerial configuration");

  crsf.begin(crsfSerial);             // defaults to CRSF_ADDRESS_FLIGHT_CONTROLLER
  crsf.setDeviceName("AlfredoCRSF");  // answer device discovery pings
  ledSPI.begin(4, -1, LED_PIN, -1);
}

void loop() {
  crsf.update();

  static uint32_t next = micros();
  static uint32_t lastPrint = 0;

  unsigned long currentMicros = micros();

  if (currentMicros - previousMicros >= interval) {
    previousMicros += interval;  // keeps timing steady, no drift
    esc1.sendThrottle(0.08f);    // 8 %
    esc2.sendThrottle(0.08f);    // 8 %
  }


  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval1) {
    previousMillis = currentMillis;

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
  }

  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    Serial.printf("rpm %8.0f   erpm %6lu   loss %.2f%%\n", esc1.rpm(),
                  (unsigned long)esc1.erpm(), esc1.lossPercent());

    Serial.printf("rpm %8.0f   erpm %6lu   loss %.2f%%\n", esc2.rpm(),
                  (unsigned long)esc2.erpm(), esc2.lossPercent());

    Serial.println("");
  }




  // Announce ourselves to the CRSF router once per second
  if (millis() - lastHeartbeatMs > 1000) {
    lastHeartbeatMs = millis();
    crsf.sendHeartbeat();
  }

  if (millis() - lastPrintMs > 100) {
    lastPrintMs = millis();
    printEverything();
  }
}

void printEverything() {
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
