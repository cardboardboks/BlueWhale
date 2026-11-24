// ================================================================
//  Drive protection: traction control, stall protection, ESC temperature protection
// ================================================================
// Uses the ESCs' RPM feedback. Which ESC is the left / right wheel comes from the ESC Role boxes (escForRole()).
// Wheel surface speed = motor RPM (Motor Poles box) / Gear Ratio box x pi x Wheel Dia mm box. The ESC's RPM has no
// direction, so each wheel's sign comes from what it was commanded.
//
// Traction control (turning): the turn rate the wheels should be producing, (left - right speed) / track, is
//   compared with the gyro. If the wheels are turning more than TC Slip % faster than the robot really is, the tyres
//   are spinning: only the turning part of the output is scaled back (throttle is left alone), quickly, then let back
//   gently once they grip (TC Drop /s, TC Rise /s). Never below TC Min %, so there's always some steering. Ignores
//   slow turns (< TC Min °/s) where the wheel speed is too coarse. While it's limiting, the yaw controller's I term is
//   held (yaw_pid.ino). Off in IMU Off mode, which stays plain 1:1 stick driving.
// Stall protection (per wheel): asked for more than STALL_MIN_OUT but moving slower than STALL_KMH for Stall Time ->
//   stalled: that wheel is held to Protect % until it's moving again (> UNSTALL_KMH) or the command drops below
//   STALL_MIN_OUT. With no current limit on the ESCs, a pinned robot otherwise cooks its motors and ESCs.
// Temperature protection (per wheel, part of the same limit): each wheel's maximum output is eased down from 100% at
//   ESC Max - TEMP_DERATE_C, to Protect % at ESC Max (and stays there above it), from that ESC's own temperature.
//   It never cuts completely, so you can still drive out of trouble.
// The per-wheel limit comes back up gently (PROT_RECOVER_S) once the stall / heat has gone.

// T_TRACK_MM (38) and T_TC_SLIP (40) are in the main sketch: autotune.ino uses them too
#define T_TC_ON 39         // Traction Ctrl: 1 on, 0 off
#define T_STALL_ON 41      // Stall Protect: 1 on, 0 off
#define T_STALL_S 42       // Stall Time s
#define T_PROT_LIM 43      // Protect %: per-wheel limit while stalled / at the temperature limit
#define T_ESC_MAX_C 44     // ESC Max °C: 0 = temperature protection off
#define T_TC_MIN_DPS 45    // TC Min °/s: traction control ignores turns slower than this
#define T_TC_MIN_PCT 46    // TC Min %: never cut the turning part below this
#define T_TC_DROP 47       // TC Drop /s: how fast it backs the turning off when the wheels spin
#define T_TC_RISE 48       // TC Rise /s: how fast it lets it back when they grip
#define STALL_MIN_OUT 0.25f    // a wheel must be asked for more than this to count as stalled
#define STALL_KMH 1.0f         // ...and be moving slower than this
#define UNSTALL_KMH 2.0f       // moving faster than this = free again
#define TEMP_DERATE_C 15.0f    // temperature limiting starts this far below ESC Max
#define PROT_RECOVER_S 1.0f    // the per-wheel limit climbs back from Protect % to 100% over about this long

volatile float tcGainNow = 1.0f;         // traction control: share of the turning output kept (1 = not limiting)
volatile float tcSlipNow = 0.0f;         // measured slip (smoothed), 0..1 - worked out even with TC off, for setting it up
volatile float tcImpliedDps = 0.0f;      // turn rate the wheels say, °/s (stick sign)
volatile float tcActualDps = 0.0f;       // turn rate the gyro says, °/s (stick sign)
#define TC_SLIP_TAU_S 0.05f              // slip smoothing, so one noisy reading can't trigger it
volatile float wheelCapNow[2] = { 1.0f, 1.0f };  // per-wheel limit, 0..1
volatile bool wheelStalled[2] = { false, false };
volatile bool wheelHot[2] = { false, false };

// Wheel surface speed in m/s, signed by the direction it was commanded (cmdSign = +1 / -1)
static float wheelSpeedMs(int i, float cmdSign) {  // i = ESC output
  if (i < 0 || i >= NUM_ESC || !escPresent(i)) return 0.0f;
  float wheelRpm = escRpm(i) / gearRatio();
  return cmdSign * wheelRpm * (float)M_PI * (wheelDiaMm() / 1000.0f) / 60.0f;
}

static float signL = 1.0f, signR = 1.0f;  // last commanded direction of each wheel (the ESC's RPM has no sign)

// Left minus right wheel surface speed, m/s, signed by their last commanded directions. NAN if a drive ESC is missing.
// (positive = the robot should turn the way a positive steering stick turns it). Used by autotune.ino.
float wheelDiffMs() {
  int iL = escForRole(ROLE_LEFT), iR = escForRole(ROLE_RIGHT);
  if (iL < 0 || iR < 0 || iL == iR || !escPresent(iL) || !escPresent(iR)) return NAN;
  return wheelSpeedMs(iL, signL) - wheelSpeedMs(iR, signR);
}

bool tcLimiting() {
  return tcGainNow < 0.98f;
}

// Traction control on the turning part of the drive. left / right: -1..1, changed in place.
// enabled = false (IMU Off mode): left alone, so that mode stays plain 1:1 stick driving.
void protectTraction(float &left, float &right, bool enabled) {
  static uint32_t lastUs = 0;
  static float gain = 1.0f;
  uint32_t nowUs = micros();
  float dt = (lastUs == 0) ? 0.001f : constrain((nowUs - lastUs) * 1e-6f, 0.0f, 0.05f);
  lastUs = nowUs;
  if (fabsf(left) > 0.02f) signL = (left > 0) ? 1.0f : -1.0f;
  if (fabsf(right) > 0.02f) signR = (right > 0) ? 1.0f : -1.0f;

  float trackM = tuneVal[T_TRACK_MM] / 1000.0f;
  int iL = escForRole(ROLE_LEFT), iR = escForRole(ROLE_RIGHT);
  bool measurable = trackM > 0.05f && iL >= 0 && iR >= 0 && iL != iR && escPresent(iL) && escPresent(iR) && imuAlive();
  if (!measurable) {
    gain = 1.0f;
    tcGainNow = 1.0f;
    tcSlipNow = 0.0f;
    tcImpliedDps = tcActualDps = 0.0f;
    return;
  }

  // Slip is worked out whenever it can be - even with traction control off or in IMU Off mode - so the Bulk Text
  // line shows your robot's normal slip, to set TC Slip % above it.
  // turn rate the wheels should give, in the stick's sign convention (positive = the way a positive stick turns)
  float implied = (wheelSpeedMs(iL, signL) - wheelSpeedMs(iR, signR)) / trackM * (180.0f / (float)M_PI);
  float actual = yawRateStick();  // the gyro, same sign convention
  float raw = 0.0f;
  float minDps = max((float)tuneVal[T_TC_MIN_DPS], 0.0f);
  // Only DRIVE slip counts: the wheels spinning faster than the robot turns, the same way. When the wheels go one
  // way and the robot is still sliding round the other way, that's braking out of a turn - not slip to cut (cutting
  // it took the brakes away and made the robot slide a long way past where it was let go). Wheels spinning with the
  // robot hardly turning at all still counts as full slip (spinning up from rest on a slick floor).
  if (fabsf(implied) > minDps && fabsf(implied) > 1.0f) {
    bool robotTurning = fabsf(actual) > 0.5f * minDps;
    if ((implied > 0) != (actual > 0)) raw = robotTurning ? 0.0f : 1.0f;
    else raw = constrain((fabsf(implied) - fabsf(actual)) / fabsf(implied), 0.0f, 1.0f);
  }
  static float slipF = 0.0f;
  slipF += (dt / (TC_SLIP_TAU_S + dt)) * (raw - slipF);
  float slip = slipF;
  tcSlipNow = slip;
  tcImpliedDps = implied;
  tcActualDps = actual;

  bool usable = enabled && tuneVal[T_TC_ON] >= 0.5f;
  if (!usable) {  // measuring only
    gain = 1.0f;
    tcGainNow = 1.0f;
    return;
  }

  float allowed = constrain((float)tuneVal[T_TC_SLIP] / 100.0f, 0.05f, 0.95f);
  float turn = (left - right) * 0.5f;
  float minGain = constrain((float)tuneVal[T_TC_MIN_PCT] / 100.0f, 0.0f, 1.0f);
  float dropRate = max((float)tuneVal[T_TC_DROP], 0.1f), riseRate = max((float)tuneVal[T_TC_RISE], 0.1f);
  // Braking: the turn asked for is the other way to how the robot is turning (or none) -> full output at once, so
  // stopping a spin is never held back. Traction control only limits building a turn up.
  bool braking = fabsf(actual) > 0.5f * minDps && (fabsf(turn) < 0.02f || (turn > 0) != (actual > 0));
  if (braking) gain = 1.0f;
  else if (slip > allowed && fabsf(turn) > 0.05f) gain = max(minGain, gain - dropRate * dt);
  else gain = min(1.0f, gain + riseRate * dt);
  tcGainNow = gain;

  if (gain < 1.0f) {  // scale only the turning part
    float fwd = (left + right) * 0.5f;
    turn *= gain;
    left = fwd + turn;
    right = fwd - turn;
  }
}

// Stall + temperature limits per wheel. left / right: -1..1, changed in place (each clamped to its own limit).
void protectWheels(float &left, float &right) {
  static uint32_t lastUs = 0;
  static uint32_t slowSinceMs[2] = { 0, 0 };
  static float cap[2] = { 1.0f, 1.0f };
  uint32_t nowUs = micros(), nowMs = millis();
  float dt = (lastUs == 0) ? 0.001f : constrain((nowUs - lastUs) * 1e-6f, 0.0f, 0.05f);
  lastUs = nowUs;

  float lim = constrain((float)tuneVal[T_PROT_LIM] / 100.0f, 0.05f, 1.0f);
  float stallS = tuneVal[T_STALL_S];
  float maxC = tuneVal[T_ESC_MAX_C];
  float *w[2] = { &left, &right };

  for (int i = 0; i < 2; i++) {  // i = wheel: 0 left, 1 right
    float cmd = *w[i];
    int e = escForRole(i == 0 ? ROLE_LEFT : ROLE_RIGHT);  // the ESC driving this wheel
    bool present = (e >= 0) && escPresent(e);

    // ---- stall ----
    bool stalled = wheelStalled[i];
    if (tuneVal[T_STALL_ON] >= 0.5f && present && stallS > 0) {
      float kmh = fabsf(wheelSpeedMs(e, 1.0f)) * 3.6f;
      if (fabsf(cmd) > STALL_MIN_OUT && kmh < STALL_KMH) {
        if (slowSinceMs[i] == 0) slowSinceMs[i] = nowMs;
        if (nowMs - slowSinceMs[i] >= (uint32_t)(stallS * 1000.0f)) stalled = true;
      } else {
        slowSinceMs[i] = 0;
      }
      if (stalled && (kmh > UNSTALL_KMH || fabsf(cmd) < STALL_MIN_OUT)) stalled = false;  // free, or eased off
    } else {
      stalled = false;
      slowSinceMs[i] = 0;
    }
    wheelStalled[i] = stalled;

    // ---- temperature ----
    float tempCap = 1.0f;
    bool hot = false;
    float tc = present ? escTempC(e) : NAN;
    if (maxC > 0 && isfinite(tc)) {
      float start = maxC - TEMP_DERATE_C;
      if (tc >= maxC) tempCap = lim;
      else if (tc > start) tempCap = 1.0f - (1.0f - lim) * (tc - start) / TEMP_DERATE_C;
      hot = tc > start;
    }
    wheelHot[i] = hot;

    // ---- this wheel's limit: drop at once, recover gently ----
    float target = min(stalled ? lim : 1.0f, tempCap);
    if (target < cap[i]) cap[i] = target;
    else cap[i] = min(target, cap[i] + dt * (1.0f - lim) / PROT_RECOVER_S);
    wheelCapNow[i] = cap[i];
    *w[i] = constrain(cmd, -cap[i], cap[i]);
  }

  // mode text (a battery cut message takes priority)
  if (!modeString.startsWith("Batt Cut")) {
    if (wheelStalled[0] || wheelStalled[1]) modeString = String("Stall ") + (wheelStalled[0] ? "L" : "") + (wheelStalled[1] ? "R" : "");
    else if (cap[0] < 0.98f || cap[1] < 0.98f) modeString = "ESC Hot";
  }
}

// Bulk Text line
String protectStatusLine() {
  // TC: how much turning it's keeping (or "off"), then the slip and the two turn rates it compares - shown even with
  // TC off, so you can see the robot's normal slip and set TC Slip % above it
  String s = "Prot TC ";
  s += (tuneVal[T_TC_ON] >= 0.5f) ? String((int)lroundf(tcGainNow * 100)) + "%" : String("off");
  s += " slip " + String((int)lroundf(tcSlipNow * 100)) + "% (wheels " + String((int)lroundf(tcImpliedDps)) + " gyro " +
       String((int)lroundf(tcActualDps)) + " °/s)\n    ";
  for (int i = 0; i < 2; i++) {  // wheel: 0 left, 1 right
    int e = escForRole(i == 0 ? ROLE_LEFT : ROLE_RIGHT);
    if (e < 0) continue;
    s += i ? "  R " : "  L ";
    s += String((int)lroundf(wheelCapNow[i] * 100)) + "%";
    if (wheelStalled[i]) s += " STALL";
    else if (wheelHot[i]) s += " HOT";
    float t = escTempC(e);
    if (isfinite(t)) s += " " + String((int)lroundf(t)) + "°C";
  }
  return s;
}
