// ================================================================
//  Autotune: traction calibration, FF & acceleration test, PID test
// ================================================================
// Both are started from the Whale Tuning page while DISARMED. The page then says to arm: the test runs once the robot
// is armed with both sticks centred (30 s to do it, or it gives up). While it runs, the robot spins itself on the spot
// from the test's own steering command - the yaw controller, traction control and heading hold are bypassed; battery,
// stall and temperature protection still apply.
//
// It STOPS INSTANTLY if: you disarm, move either stick, the radio link drops, an ESC or the IMU drops out - or the
// page's Stop button is pressed (works any time). Results are only shown; Apply (while disarmed) saves them.
//
// Traction calibration (~13 s): 6 gentle spins, alternating direction, at 15 / 25 / 35 % turning. On turns where the
//   tyres grip, compares the wheels' speed difference with the gyro: works out the effective Track mm (which also
//   soaks up the off-centre pivot, front drag and any small error in poles / gearing / wheel size), then the normal
//   slip left over, and suggests TC Slip % a little above it.
// FF & accel test (~15 s): 6 spins at 50 / 75 / 100 % turning, alternating, with a rest between. Measures the steady
//   turn rate at each power (spins at the gyro's ~2000 °/s limit are left out) and how fast the turn builds up, then
//   suggests FF for the current Max Rate and a Max Accel for this surface.
//
// PID test (~6-15 s): a "relay" test (Astrom-Hagglund), done around a steady spin so the tyres keep gripping.
//   First the robot spins steadily one way at half the Max Rate and the normal slip there is noted. Then the turning
//   power is nudged up / down around that spin: up until it's turning faster than the steady rate (plus a little),
//   then down until slower, and so on - a small, fast wobble in the spin rate. The wheels never reverse, so the tyres
//   stay gripping and the motors stay out of their direction-change jump. The wobble's rhythm and size tell where the
//   rate loop would start to oscillate on its own (the "ultimate gain" and period). The gyro is read and filtered just
//   as the PID sees it (one step per IMU sample, Gyro LPF), so its delays are included. The nudge starts tiny and is
//   only made bigger until the wobble is measurable - and a level where the wheels slip more than during the steady
//   spin is thrown out (on a slippery floor a sliding tyre makes the robot look slower than it is, and the gains come
//   out wrong). P, I and D are then set with the Tyreus-Luyben rule - a cautious one with plenty of margin, which
//   suits a loop that also has FF doing most of the work. Run the FF test first; P/I/D only fix what FF leaves.
//
// Needs: a clear area, wheels on the floor, Yaw Dir already correct (it stops with "check Yaw Dir" otherwise).

enum : uint8_t { AT_IDLE = 0, AT_WAIT_ARM, AT_RUN, AT_DONE };
#define AT_NONE 0
#define AT_TRACTION AT_TEST_TRACTION  // test types (main sketch)
#define AT_FF AT_TEST_FF
#define AT_PID AT_TEST_PID

#define AT_ARM_TIMEOUT_MS 30000
#define AT_STICK_CANCEL 0.10f     // any stick further than this from centre cancels
#define AT_GYRO_SAT_DPS 1900.0f   // steady spins faster than this are at the gyro's limit: left out

// Test plans: steering command, how long it's held (ms), and the rest after it (ms)
static const float atTcCmd[] = { 0.15f, -0.15f, 0.25f, -0.25f, 0.35f, -0.35f };
static const float atFfCmd[] = { 0.50f, -0.50f, 0.75f, -0.75f, 1.00f, -1.00f };
#define AT_TC_STEP_MS 1600
#define AT_TC_REST_MS 600
#define AT_FF_STEP_MS 1500
#define AT_FF_REST_MS 1200
// PID relay test (around a steady spin)
static const float atPidH[] = { 0.02f, 0.035f, 0.05f, 0.075f, 0.10f, 0.14f };  // nudge sizes, smallest first
#define AT_PID_LEVELS 6
#define AT_PID_BASE_FRAC 0.5f     // steady spin = this x Max Rate
#define AT_PID_BASE_HOLD_MS 700   // steady spin: once within 10 % of the rate for this long (normal slip noted meanwhile)...
#define AT_PID_BASE_MAX_MS 4000   // ...or give up after this long
#define AT_PID_HYST_DPS 8.0f      // switch when the rate is this far from the steady spin (keeps gyro noise from flipping it)
#define AT_PID_MIN_AMP_DPS 25.0f  // wobbles smaller than this: too close to noise, nudge harder
#define AT_PID_SLIP_EXTRA 0.05f   // a level is thrown out if its average slip is this much (5 %) above the steady spin's...
#define AT_PID_SLIP_PEAK 0.12f    // ...or it ever goes this much (12 %) above it
#define AT_PID_SETTLE_MS 600      // wobbling at a new size, before measuring starts
#define AT_PID_LEVEL_MS 4500      // longest at one size
#define AT_PID_SWITCHES 20        // switches measured (10 full wobbles, averaged)

volatile uint8_t atState = AT_IDLE;
volatile uint8_t atKind = AT_NONE;
volatile bool atStopReq = false;        // page's Stop button
const char *volatile atMsg = "";        // status for the page
static char atMsgBuf[160];
static char atResBuf[200];              // result summary for the page
const char *volatile atRes = "";
static float atSteer = 0.0f;            // the test's steering command while running

// pending results (applied by the page's Apply button)
static bool atHasTrack = false, atHasFf = false, atHasPid = false;
static float atNewTrackMm = 0, atNewSlipPct = 0, atNewFf = 0, atNewAccel = 0;
static float atNewP = 0, atNewI = 0, atNewD = 0;

static void atSetMsg(const char *m) { atMsg = m; }

// ---- data gathered during a run ----
static uint8_t atStep = 0;
static uint32_t atStepStartMs = 0, atWaitStartMs = 0;
static bool atResting = false;
// traction: sums for the track fit and the slip
static double atSdw = 0, atSww = 0;
static float atDsamp[600], atWsamp[600];  // samples kept for the slip after the fit
static uint16_t atNsamp = 0;
// FF: per-step steady rate and acceleration
static float atStepCmd[6], atStepRate[6], atStepAccel[6];
static double atRateSum = 0;
static uint32_t atRateN = 0;
static float atRate0 = 0;
static bool atAccelDone = false;
// PID relay test
static uint8_t atPidLevel = 0;
static uint32_t atPidLevelMs = 0;       // when this power started
static int8_t atRelay = 1;              // +1 / -1: which way it's steering
static float atPidF = 0;                // filtered rate, as the PID sees it
static uint32_t atPidSeq = 0, atPidUs = 0;
static bool atPidFresh = true;
static float atPidPeak = 0;             // biggest distance from the steady spin in the current half-wobble
static uint8_t atPidPhase = 0;          // 0 steady spin (finding the power for it, noting slip), 1 wobbling
static float atPidBaseDps = 0;          // the steady spin rate, °/s
static float atPidU0 = 0;               // turning power that holds it
static double atPidSlipSum = 0;         // slip: summed over the steady spin's second half, then each measured level
static uint32_t atPidSlipN = 0;
static float atPidSlipMax = 0;          // highest slip seen in this level
static float atPidSlip0 = 0;            // normal slip in the steady spin
static uint32_t atPidNearMs = 0;        // since when the steady spin has been within 10 % (0 = not)
static bool atPidHaveGood = false;      // a level that measured fine without slipping (kept if the next one slips)
static float atPidGoodH = 0, atPidGoodTu = 0, atPidGoodA = 0, atPidGoodSlip = 0;
static uint32_t atSwUs[AT_PID_SWITCHES + 1];  // switch times (IMU micros), measured part only
static float atSwPeak[AT_PID_SWITCHES + 1];   // the half-wobble's peak before each switch
static uint8_t atSwN = 0;
extern volatile float tcSlipNow;        // protect.ino: wheel slip, 0..1 (measured even with traction control off)

// Called from the web handler (page buttons)
void autotuneRequest(uint8_t kind) {
  if (atState == AT_RUN) return;
  atKind = kind;
  atWaitStartMs = 0;
  atState = AT_WAIT_ARM;
  atStopReq = false;
  atRes = "";
  atHasTrack = atHasFf = atHasPid = false;
  atSetMsg("Arm (ch3 high) with both sticks centred to start - disarm, move a stick or press Stop to cancel");
}
void autotuneStop() { atStopReq = true; }

// Apply the pending results to the tuning boxes (page's Apply button, disarmed only - checked by the caller)
void autotuneApply() {
  if (atState != AT_DONE) return;
  if (atHasTrack) {
    tuneVal[T_TRACK_MM] = atNewTrackMm;
    tuneVal[T_TC_SLIP] = atNewSlipPct;
  }
  if (atHasFf) {
    tuneVal[T_YAW_FF] = atNewFf;
    tuneVal[T_YAW_ACCEL] = atNewAccel;
  }
  if (atHasPid) {
    tuneVal[T_YAW_P] = atNewP;
    tuneVal[T_YAW_I] = atNewI;
    tuneVal[T_YAW_D] = atNewD;
  }
  tuneChangedMs = millis();
  tuneDirty = true;  // saved to flash shortly after, like an edit on the page
  atState = AT_IDLE;
  atRes = "";
  atSetMsg("Applied and saved");
}
void autotuneDiscard() {
  if (atState == AT_DONE) {
    atState = AT_IDLE;
    atRes = "";
    atSetMsg("Discarded");
  }
}

bool autotuneRunning() { return atState == AT_RUN; }

static void atAbort(const char *why) {
  atState = AT_IDLE;
  atSteer = 0;
  snprintf(atMsgBuf, sizeof(atMsgBuf), "Stopped: %s", why);
  atMsg = atMsgBuf;
}

static void atFinishTraction() {
  if (atSww < 1e-6 || atNsamp < 20) {
    atAbort("not enough gripping turns measured");
    return;
  }
  float track = (float)(atSdw / atSww);  // metres: (vL - vR) = track x omega
  if (track < 0) {
    atAbort("wheels and gyro disagree on direction - check Yaw Dir (and the ESC Roles / Dir)");
    return;
  }
  if (track < 0.08f || track > 1.5f) {
    atAbort("track came out unbelievable - check Motor Poles, Gear Ratio, Wheel Dia");
    return;
  }
  // normal slip left with that track: how much the wheels still run ahead of the gyro on these gripping turns
  double slipSum = 0;
  for (uint16_t k = 0; k < atNsamp; k++) {
    float implied = atDsamp[k] / track;
    float s = (fabsf(implied) - fabsf(atWsamp[k])) / fabsf(implied);
    slipSum += fabsf(s);
  }
  float slipBase = (float)(slipSum / atNsamp) * 100.0f;
  atNewTrackMm = roundf(track * 1000.0f);
  atNewSlipPct = constrain(roundf(slipBase + 12.0f), 15.0f, 50.0f);
  atHasTrack = true;
  snprintf(atResBuf, sizeof(atResBuf), "Track mm %.0f (now %.0f)   TC Slip %% %.0f (now %.0f) - normal slip %.0f%%",
           atNewTrackMm, (float)tuneVal[T_TRACK_MM], atNewSlipPct, (float)tuneVal[T_TC_SLIP], slipBase);
  atRes = atResBuf;
  atState = AT_DONE;
  atSetMsg("Done - check the results, then Apply (disarmed) or Discard");
}

static void atFinishFf() {
  double su = 0, suw = 0;
  int nRate = 0, nAcc = 0;
  float accs[6];
  for (int k = 0; k < 6; k++) {
    if (atStepRate[k] > 1.0f && atStepRate[k] < AT_GYRO_SAT_DPS) {
      su += atStepCmd[k] * atStepCmd[k];
      suw += atStepCmd[k] * atStepRate[k];
      nRate++;
    }
    if (atStepAccel[k] > 1.0f) accs[nAcc++] = atStepAccel[k];
  }
  if (nRate < 2 || su < 1e-6) {
    atAbort("not enough steady spins measured (all at the gyro limit?)");
    return;
  }
  float k = (float)(suw / su);  // °/s per unit of steering output
  float maxRate = tuneVal[T_YAW_RATE];
  if (!(maxRate > 0)) maxRate = 900.0f;
  atNewFf = constrain(roundf(maxRate / k * 100.0f) / 100.0f, 0.05f, 1.5f);
  // max accel: 80 % of the median build-up seen (sort the few values)
  for (int a = 0; a < nAcc; a++)
    for (int b = a + 1; b < nAcc; b++)
      if (accs[b] < accs[a]) { float t = accs[a]; accs[a] = accs[b]; accs[b] = t; }
  float med = nAcc ? accs[nAcc / 2] : 0;
  atNewAccel = (med > 0) ? roundf(med * 0.8f / 100.0f) * 100.0f : (float)tuneVal[T_YAW_ACCEL];
  atHasFf = true;
  snprintf(atResBuf, sizeof(atResBuf), "FF %.2f (now %.2f) - full turning = %.0f °/s   Max Accel %.0f (now %.0f)",
           atNewFf, (float)tuneVal[T_YAW_FF], k, atNewAccel, (float)tuneVal[T_YAW_ACCEL]);
  atRes = atResBuf;
  atState = AT_DONE;
  atSetMsg("Done - check the results, then Apply (disarmed) or Discard");
}

// PID: from the steady wobble with nudge h - period tu (s) and swing size a (°/s) - work out P, I, D
static void atFinishPid(float h, float tu, float a, float slip) {
  float maxRate = tuneVal[T_YAW_RATE];
  if (!(maxRate > 0)) maxRate = 900.0f;
  float eps = AT_PID_HYST_DPS;
  float aEff = sqrtf(max(a * a - eps * eps, 1.0f));  // the switching threshold makes the swings look bigger: undo that
  float ku = 4.0f * h / ((float)M_PI * aEff) * maxRate;  // ultimate gain, in the PID's units (error / max rate)
  // AT Aggr picks the rule, blending between three:
  //   0   soft:       Kp = 0.30 Ku, Ti = 2.5 Tu,  Td = Tu/8    (very cautious)
  //   0.5 medium:     Kp = 0.45 Ku, Ti = 2.2 Tu,  Td = Tu/6.3  (Tyreus-Luyben)
  //   1   aggressive: Kp = 0.60 Ku, Ti = 0.5 Tu,  Td = Tu/8    (classic Ziegler-Nichols)
  // This PID: I = Kp/Ti per second, D = Kp*Td seconds. Ti is blended as 1/Ti (how strong I is), so it moves evenly.
  float ag = tuneVal[T_AT_AGGR];
  if (!isfinite(ag)) ag = 0.5f;
  ag = constrain(ag, 0.0f, 1.0f);
  float kf, invTi, tdF;  // Kp / Ku, 1 / (Ti / Tu), Td / Tu
  if (ag <= 0.5f) {
    float f = ag / 0.5f;
    kf = 0.30f + f * (0.45f - 0.30f);
    invTi = 1.0f / 2.5f + f * (1.0f / 2.2f - 1.0f / 2.5f);
    tdF = 1.0f / 8.0f + f * (1.0f / 6.3f - 1.0f / 8.0f);
  } else {
    float f = (ag - 0.5f) / 0.5f;
    kf = 0.45f + f * (0.60f - 0.45f);
    invTi = 1.0f / 2.2f + f * (1.0f / 0.5f - 1.0f / 2.2f);
    tdF = 1.0f / 6.3f + f * (1.0f / 8.0f - 1.0f / 6.3f);
  }
  float kp = kf * ku;
  float ti = tu / invTi, td = tdF * tu;
  atNewP = constrain(roundf(kp * 100.0f) / 100.0f, 0.05f, 10.0f);
  atNewI = constrain(roundf(atNewP / ti * 100.0f) / 100.0f, 0.0f, 50.0f);
  atNewD = constrain(roundf(atNewP * td * 1000.0f) / 1000.0f, 0.0f, 0.3f);
  atHasPid = true;
  snprintf(atResBuf, sizeof(atResBuf), "P %.2f (now %.2f)  I %.2f (now %.2f)  D %.3f (now %.3f) - aggr %.2f, wobble %.1f Hz +-%.0f °/s, nudge %.1f%%, slip %.0f%% (steady %.0f%%)",
           atNewP, (float)tuneVal[T_YAW_P], atNewI, (float)tuneVal[T_YAW_I], atNewD, (float)tuneVal[T_YAW_D],
           ag, 1.0f / tu, a, h * 100.0f, slip * 100.0f, atPidSlip0 * 100.0f);
  atRes = atResBuf;
  atState = AT_DONE;
  atSetMsg("Done - check the results, then Apply (disarmed) or Discard");
}

static void atPidStartLevel(uint8_t lvl, uint32_t now) {
  atPidLevel = lvl;
  atPidLevelMs = now;
  atSwN = 0;
  atPidPeak = 0;
  atPidSlipSum = 0;
  atPidSlipN = 0;
  atPidSlipMax = 0;
}

// Out of levels (or this one slipped / never wobbled): use the last good level, or give up with why
static float atPidEnd(const char *why) {
  if (atPidHaveGood) atFinishPid(atPidGoodH, atPidGoodTu, atPidGoodA, atPidGoodSlip);
  else atAbort(why);
  return 0;
}

// One pass of the PID relay test. Returns the steering output.
static float atPidRun(uint32_t now) {
  float maxRate = tuneVal[T_YAW_RATE];
  if (!(maxRate > 0)) maxRate = 900.0f;
  float ffK = tuneVal[T_YAW_FF];  // FF = Max Rate / (°/s per unit steering): first guess at the steady-spin power
  float kEst = (ffK > 0.01f) ? maxRate / ffK : 2300.0f;
  bool newSample = false;
  uint32_t seq = yawImuSeqNow();
  if (atPidFresh || seq != atPidSeq) {  // new gyro sample: filter it like the PID does
    uint32_t us = yawImuUsNow();
    float r = yawRateStick();
    float dt = (us - atPidUs) * 1e-6f;
    if (atPidFresh || !(dt > 0.0005f && dt < 0.05f)) {
      atPidF = r;
      dt = 0.01f;
    } else {
      float fc = tuneVal[T_GYRO_LPF];
      float al = 1.0f;
      if (fc > 0) { float rc = 1.0f / (2.0f * (float)M_PI * fc); al = dt / (rc + dt); }
      atPidF += al * (r - atPidF);
    }
    if (atPidFresh) {
      atPidBaseDps = AT_PID_BASE_FRAC * maxRate;
      atPidU0 = constrain(atPidBaseDps / kEst, 0.05f, 0.6f);
      atPidPhase = 0;
      atPidSlip0 = 0;
      atPidHaveGood = false;
    }
    atPidFresh = false;
    atPidSeq = seq;
    atPidUs = us;
    newSample = true;

    if (atPidPhase == 0) {
      // steady spin: slowly trim the power until it holds the rate (a slow integrator - no wobble wanted here)
      atPidU0 = constrain(atPidU0 + 3.0f * (atPidBaseDps - atPidF) / kEst * dt, 0.03f, 0.7f);
    } else {
      float dev = atPidF - atPidBaseDps;
      if (fabsf(dev) > atPidPeak) atPidPeak = fabsf(dev);
      bool sw = (atRelay > 0 && dev > AT_PID_HYST_DPS) || (atRelay < 0 && dev < -AT_PID_HYST_DPS);
      if (sw) {
        atRelay = -atRelay;
        if (now - atPidLevelMs >= AT_PID_SETTLE_MS && atSwN <= AT_PID_SWITCHES) {
          atSwUs[atSwN] = us;
          atSwPeak[atSwN] = atPidPeak;  // peak of the half-wobble that just ended (it overshoots past the threshold)
          atSwN++;
        }
        atPidPeak = 0;
      }
    }
  }

  uint32_t el = now - atPidLevelMs;
  if (atPidPhase == 0) {
    bool near = fabsf(atPidF - atPidBaseDps) < 0.1f * atPidBaseDps;
    if (!near) {  // not there (yet, or any more): start the slip average again
      atPidNearMs = 0;
      atPidSlipSum = 0;
      atPidSlipN = 0;
    } else {
      if (atPidNearMs == 0) atPidNearMs = now;
      if (newSample) {
        atPidSlipSum += tcSlipNow;
        atPidSlipN++;
      }
    }
    if (newSample && tcSlipNow > atPidSlipMax) atPidSlipMax = tcSlipNow;
    if (el >= AT_PID_BASE_MAX_MS) {
      atAbort(atPidSlipMax > 0.3f ? "couldn't hold a steady spin - the tyres were slipping a lot: too slippery to measure P/I/D here"
                                  : "couldn't hold a steady spin - run the FF test first, check Yaw Dir");
      return 0;
    }
    if (atPidNearMs != 0 && now - atPidNearMs >= AT_PID_BASE_HOLD_MS) {
      atPidSlip0 = atPidSlipN ? (float)(atPidSlipSum / atPidSlipN) : 0.0f;
      atPidSlipMax = 0;
      atPidPhase = 1;
      atRelay = 1;
      atPidStartLevel(0, now);
    }
    return atPidU0;
  }

  // ---- wobbling at nudge h ----
  float h = min(atPidH[atPidLevel], 0.8f * atPidU0);  // never so big that a wheel reverses
  if (newSample && el >= AT_PID_SETTLE_MS) {
    atPidSlipSum += tcSlipNow;
    atPidSlipN++;
    if (tcSlipNow > atPidSlipMax) atPidSlipMax = tcSlipNow;
  }
  if (atSwN > AT_PID_SWITCHES) {  // enough wobbles measured at this size
    float tu = (atSwUs[AT_PID_SWITCHES] - atSwUs[0]) * 1e-6f / (AT_PID_SWITCHES / 2);
    float a = 0;
    for (uint8_t k = 1; k <= AT_PID_SWITCHES; k++) a += atSwPeak[k];  // first one is partial: skip
    a /= AT_PID_SWITCHES;
    float slip = atPidSlipN ? (float)(atPidSlipSum / atPidSlipN) : 0.0f;
    if (slip > atPidSlip0 + AT_PID_SLIP_EXTRA || atPidSlipMax > atPidSlip0 + AT_PID_SLIP_PEAK) return atPidEnd("the tyres slip even at the gentlest wobble - too slippery to measure P/I/D here");
    if (!(tu > 0.02f && tu < 3.0f)) return atPidEnd("wobble rhythm not steady - try again on a flat, even floor");
    atPidHaveGood = true;  // a clean measurement: keep it
    atPidGoodH = h; atPidGoodTu = tu; atPidGoodA = a; atPidGoodSlip = slip;
    if (a < AT_PID_MIN_AMP_DPS && atPidLevel + 1 < AT_PID_LEVELS && atPidH[atPidLevel] < 0.8f * atPidU0) {
      atPidStartLevel(atPidLevel + 1, now);  // swings too small to trust: nudge harder
    } else {
      atFinishPid(h, tu, a, slip);
      return 0;
    }
  } else if (el > AT_PID_LEVEL_MS) {  // never got a steady wobble at this size
    float slip = atPidSlipN ? (float)(atPidSlipSum / atPidSlipN) : 0.0f;
    if (slip > atPidSlip0 + AT_PID_SLIP_EXTRA || atPidSlipMax > atPidSlip0 + AT_PID_SLIP_PEAK) return atPidEnd("the tyres slip even at the gentlest wobble - too slippery to measure P/I/D here");
    if (atPidLevel + 1 < AT_PID_LEVELS && atPidH[atPidLevel] < 0.8f * atPidU0) atPidStartLevel(atPidLevel + 1, now);
    else return atPidEnd("no steady wobble - check Yaw Dir, ESCs and that the wheels are on the floor");
  }
  h = min(atPidH[atPidLevel], 0.8f * atPidU0);
  return atPidU0 + atRelay * h;
}

static void atStartRun() {
  atState = AT_RUN;
  atStep = 0;
  atResting = false;
  atStepStartMs = millis();
  atSdw = atSww = 0;
  atNsamp = 0;
  atRateSum = 0;
  atRateN = 0;
  atAccelDone = false;
  atRate0 = fabsf(yawRateStick());
  for (int k = 0; k < 6; k++) atStepCmd[k] = atStepRate[k] = atStepAccel[k] = 0;
  atRelay = 1;
  atPidFresh = true;
  atPidPhase = 0;
  atPidNearMs = 0;
  atPidStartLevel(0, atStepStartMs);
  atSetMsg(atKind == AT_TRACTION ? "Traction calibration running - move a stick or disarm to stop"
           : atKind == AT_FF     ? "FF & accel test running - move a stick or disarm to stop"
                                 : "PID test running (steady spin, then a wobble) - move a stick or disarm to stop");
}

// Called from mixer() every pass with the sticks (after the deadband, -1..1).
// Returns true while the test is driving: then use *steer as the steering output and 0 throttle.
bool autotuneUpdate(float stickThr, float stickSteer, float *steer) {
  bool armed = txchannel[ARM_CHANNEL] > ARM_HIGH_US;
  bool link = crsf.isLinkUp();
  bool centred = fabsf(stickThr) < AT_STICK_CANCEL && fabsf(stickSteer) < AT_STICK_CANCEL;
  bool escsOk = escForRole(ROLE_LEFT) >= 0 && escForRole(ROLE_RIGHT) >= 0 && isfinite(wheelDiffMs());
  uint32_t now = millis();

  if (atStopReq) {
    atStopReq = false;
    if (atState == AT_RUN || atState == AT_WAIT_ARM) atAbort("Stop pressed");
  }

  if (atState == AT_WAIT_ARM) {
    if (atWaitStartMs == 0) atWaitStartMs = now;
    if (now - atWaitStartMs > AT_ARM_TIMEOUT_MS) {
      atWaitStartMs = 0;
      atAbort("not armed within 30 s");
    } else if (armed && link && centred && imuAlive() && escsOk) {
      atWaitStartMs = 0;
      atStartRun();
    }
    return false;
  }
  if (atState != AT_RUN) {
    atWaitStartMs = 0;
    return false;
  }

  // ---- running: any of these stops it at once ----
  if (!armed) { atAbort("disarmed"); return false; }
  if (!link) { atAbort("radio link lost"); return false; }
  if (!centred) { atAbort("stick moved"); return false; }
  if (!imuAlive()) { atAbort("IMU lost"); return false; }
  if (!escsOk) { atAbort("drive ESC not reporting"); return false; }

  if (atKind == AT_PID) {
    float out = atPidRun(now);
    if (atState != AT_RUN) return false;  // finished or gave up
    atSteer = out;
    *steer = out;
    return true;
  }

  const float *plan = (atKind == AT_TRACTION) ? atTcCmd : atFfCmd;
  uint32_t stepMs = (atKind == AT_TRACTION) ? AT_TC_STEP_MS : AT_FF_STEP_MS;
  uint32_t restMs = (atKind == AT_TRACTION) ? AT_TC_REST_MS : AT_FF_REST_MS;
  uint32_t el = now - atStepStartMs;
  float rate = yawRateStick();  // °/s, positive = the way positive steering turns

  if (atResting) {
    atSteer = 0;
    if (el >= restMs) {  // next step
      atResting = false;
      atStep++;
      atStepStartMs = now;
      atAccelDone = false;
      atRateSum = 0;
      atRateN = 0;
      atRate0 = fabsf(rate);
      if (atStep >= 6) {
        atSteer = 0;
        if (atKind == AT_TRACTION) atFinishTraction();
        else atFinishFf();
        return false;
      }
    }
    *steer = atSteer;
    return true;
  }

  float cmd = plan[atStep];
  atSteer = cmd;

  if (atKind == AT_TRACTION) {
    // steady part of each gentle spin: gather (wheel speed difference, gyro) pairs
    float d = wheelDiffMs();
    float w = rate * (float)M_PI / 180.0f;  // rad/s
    if (el > stepMs / 2 && isfinite(d) && fabsf(rate) > 20.0f && atNsamp < 600) {
      atSdw += (double)d * w;
      atSww += (double)w * w;
      atDsamp[atNsamp] = d;
      atWsamp[atNsamp] = w;
      atNsamp++;
    }
  } else {
    // how fast the turn builds: average over the first 250 ms
    if (!atAccelDone && el >= 250) {
      atStepAccel[atStep] = (fabsf(rate) - atRate0) / 0.25f;
      atAccelDone = true;
    }
    // steady rate: the last third of the step
    if (el > stepMs * 2 / 3) {
      atRateSum += fabsf(rate);
      atRateN++;
    }
    atStepCmd[atStep] = fabsf(cmd);
    if (atRateN) atStepRate[atStep] = (float)(atRateSum / atRateN);
  }

  if (el >= stepMs) {  // into the rest after this step
    atResting = true;
    atStepStartMs = now;
    atSteer = 0;
  }
  *steer = atSteer;
  return true;
}

// For the page: state, message, results
void autotuneJson(JsonObject at) {
  at["st"] = (int)atState;
  at["kind"] = (int)atKind;
  at["msg"] = (const char *)atMsg;
  at["res"] = (const char *)atRes;
}
