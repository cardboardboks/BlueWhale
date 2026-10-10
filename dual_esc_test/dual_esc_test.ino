// Libs
#include <AlfredoDShot.h>

AlfredoDShot esc1;
AlfredoDShot esc2;

//  Varibles
//ESC
const int PIN_ESC1 = 7;
const int PIN_ESC2 = 6;
const bool SEND_EDT = true;  // run once true, once false


uint32_t t0;

void setup() {
  Serial.begin(115200);
  delay(3000);  // time to attach the monitor
  Serial.setTxTimeoutMs(0);
  Serial.println("BOOT");
  AlfredoDShot::releaseBootloader(PIN_ESC1);
  AlfredoDShot::releaseBootloader(PIN_ESC2);
  esc1.begin(PIN_ESC1, DSHOT600, true, 14);
  esc2.begin(PIN_ESC2, DSHOT600, true, 14);
  t0 = millis();
}

void loop() {
  static uint32_t next = micros(), lastPrint = 0, maxGap = 0, lastSend = micros();
  static bool cmdSent = false;

  if ((int32_t)(micros() - next) < 0) return;
  next += 1000;

  uint32_t gap = micros() - lastSend;
  lastSend = micros();
  if (gap > maxGap) maxGap = gap;

  uint32_t t = millis() - t0;  // time since begin()
  if (SEND_EDT && !cmdSent && t > 5000) {
    esc1.command(DSHOT_CMD_EDT_ENABLE);
    esc2.command(DSHOT_CMD_EDT_ENABLE);
    cmdSent = true;
  }
  uint32_t runAt = SEND_EDT ? 9000 : 5000;
  float thr = 0.0f;
  if (t > runAt) thr = min(0.54f, 0.50f + (t - runAt) * 0.00002f);  // ramp to 20%
  esc1.sendThrottle(thr);
  esc2.sendThrottle(thr);
  //esc.send(200);

  if (t - lastPrint >= 500) {
    lastPrint = t;
    auto &s1 = esc1.stats();
    Serial.printf("Thr: %.2f %% | Voltage: %3.1f V | Current: %2.0f A | RPM %2.0f | Temp: %3.0f C \n",
                  thr,
                  esc1.voltage(),
                  esc1.current(),
                  (esc1.rpm() / 19),
                  esc1.temperatureC());

    auto &s2 = esc2.stats();
    Serial.printf("Thr: %.2f %% | Voltage: %3.1f V | Current: %2.0f A | RPM %2.0f | Temp: %3.0f C \n",
                  thr,
                  esc2.voltage(),
                  esc2.current(),
                  (esc2.rpm() / 19),
                  esc2.temperatureC());
  }
}