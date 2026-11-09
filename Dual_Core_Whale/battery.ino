// ================================================================
//  Low battery protection: two stages, Low Volt and Crit Volt
// ================================================================
// Voltage: per the Batt Source box - the battery divider if selected and reading (vbatValid()), otherwise the average
// of the ESCs that are reporting. Smoothed over BATT_TAU_S so a brief sag on a hard punch doesn't snap the limit on.
//
// Ramp mode (default): below Low Volt the drive output is capped in proportion - 100% at Low down to 0 at Crit. Pushing
//   harder sags the pack, which lowers the cap, so it settles where the pack holds just above Crit.
// Hard cut mode: full power right down to Crit.
// Both: below Crit for BATT_CUT_DELAY_MS -> drive is cut, and stays cut until you disarm (ch3 low) with the voltage
//   back above Crit (or, with ESC voltage, at rest - see below).
// The cap drops at once but recovers slowly (BATT_RECOVER_S from 0 to full), so it doesn't pump on and off.
// IMU Off mode always keeps at least the "Batt Limp %" box (mixer.ino), even when cut.
//
// ESC voltage only updates while the motors turn (AM32 stops sending it at rest), so the reading freezes when you stop.
// While no motor is turning, the frozen value isn't trusted: the cap is allowed to recover and the cut can clear on
// disarm; as soon as the motors turn, fresh readings take over again. (With the divider this doesn't apply.)
//
// LEDs (led.ino): below Low Volt all LEDs flash red briefly, every 5 s at Low getting quicker to every 0.5 s at Crit
// (and 0.5 s while cut), over the normal pattern.
//
// Settings: Low Volt V, Crit Volt V, Batt Limit (Ramp / Hard cut) on the tuning page. Low Volt 0 = all of this off.

#define T_BAT_LOW 32         // Low Volt V
#define T_BAT_CRIT 33        // Crit Volt V
#define T_BAT_MODE 34        // 1 = Ramp, 0 = Hard cut
#define BATT_TAU_S 0.5f      // voltage smoothing time constant
#define BATT_CUT_DELAY_MS 1000
#define BATT_RECOVER_S 2.0f  // the cap climbs back from 0 to full over this long
#define BATT_FLASH_MS 80     // length of each red LED flash
#define BATT_FLASH_SLOW_MS 5000
#define BATT_FLASH_FAST_MS 500

volatile float battVolt = NAN;     // smoothed pack voltage (NaN = no reading)
volatile uint8_t battSrc = 0;      // 0 none, 1 divider, 2 ESCs
volatile float battCapNow = 1.0f;  // drive output cap, 0..1
volatile bool battCut = false;     // cut below Crit (latched until disarmed and recovered)
volatile float battWarn = -1.0f;   // LED warning: -1 none, 0 at Low Volt .. 1 at Crit / cut

// Called from loop() every pass; does its work every 10 ms
void batteryUpdate() {
  static uint32_t lastMs = 0, belowSinceMs = 0;
  static float vf = NAN, cap = 1.0f;
  uint32_t now = millis();
  if (now - lastMs < 10) return;
  float dt = (lastMs == 0) ? 0.01f : (now - lastMs) / 1000.0f;
  lastMs = now;

  // ---- voltage, and whether it's live ----
  float v = NAN;
  bool fresh = false;
  uint8_t src = 0;
  if (vbatValid()) {
    v = vbat;
    fresh = true;
    src = 1;
  } else {
    float sum = 0;
    int n = 0;
    bool turning = false;
    for (int i = 0; i < NUM_ESC; i++) {
      if (!escPresent(i)) continue;
      float ev = escVolts(i);
      if (!isfinite(ev) || ev < 1.0f) continue;
      sum += ev;
      n++;
      if (escRpm(i) > 0) turning = true;
    }
    if (n > 0) {
      v = sum / n;
      fresh = turning;  // AM32 only sends voltage while turning: at rest it's a frozen value
      src = 2;
    }
  }
  battSrc = src;

  float low = tuneVal[T_BAT_LOW], crit = tuneVal[T_BAT_CRIT];
  bool on = (low > 0 && crit > 0 && low > crit);
  if (!on || !isfinite(v)) {  // off, or nothing to measure: no limit
    vf = v;
    cap = 1.0f;
    battCut = false;
    battCapNow = 1.0f;
    battWarn = -1.0f;
    battVolt = v;
    belowSinceMs = 0;
    return;
  }

  // ---- smooth the voltage (only from live readings) ----
  // The first ESC reading can arrive mid-punch (AM32 only reports while turning), already sagged: start the smoothing
  // no lower than Low Volt, so it pulls down at the normal smoothed rate instead of capping hard straight away.
  if (!isfinite(vf)) vf = max(v, low);
  else if (fresh) vf += (dt / (BATT_TAU_S + dt)) * (v - vf);
  battVolt = vf;

  // ---- cut: below Crit for a while (only judged on live readings) ----
  if (fresh && vf < crit) {
    if (belowSinceMs == 0) belowSinceMs = now;
    if (now - belowSinceMs >= BATT_CUT_DELAY_MS) battCut = true;
  } else if (fresh) {
    belowSinceMs = 0;
  }
  bool disarmed = txchannel[ARM_CHANNEL] < 1100;
  if (battCut && disarmed && (vf > crit || !fresh)) {  // disarm to clear it (at rest the ESC reading can't be trusted)
    battCut = false;
    belowSinceMs = 0;
  }

  // ---- cap ----
  float target;
  if (battCut) target = 0.0f;
  else if (!fresh) target = 1.0f;  // at rest with a frozen ESC reading: let it recover; live readings re-limit at once
  else if (tuneVal[T_BAT_MODE] >= 0.5f) target = constrain((vf - crit) / (low - crit), 0.0f, 1.0f);  // ramp
  else target = 1.0f;  // hard cut: full power until the cut
  if (target < cap) cap = target;                               // drop at once
  else cap = min(target, cap + dt / BATT_RECOVER_S);            // recover slowly
  battCapNow = cap;

  // ---- LED warning level ----
  if (battCut) battWarn = 1.0f;
  else if (vf < low) battWarn = constrain((low - vf) / (low - crit), 0.0f, 1.0f);
  else battWarn = -1.0f;
}

float battCap() { return battCapNow; }
bool battIsCut() { return battCut; }

// true while the red battery flash should be showing (led.ino)
bool battLedFlash() {
  float w = battWarn;
  if (w < 0) return false;
  uint32_t period = (uint32_t)(BATT_FLASH_SLOW_MS - w * (BATT_FLASH_SLOW_MS - BATT_FLASH_FAST_MS));
  return (millis() % period) < BATT_FLASH_MS;
}

// Bulk Text line
String battStatusLine() {
  float v = battVolt;
  if (!(tuneVal[T_BAT_LOW] > 0)) return "Batt: protection off";
  if (!isfinite(v)) return "Batt: no voltage reading";
  return "Batt " + String(v, 2) + "V " + (battSrc == 1 ? "div" : "esc") + "  cap " + String((int)lroundf(battCapNow * 100)) + "%" +
         (battCut ? "  CUT" : (battWarn >= 0 ? "  LOW" : ""));
}
