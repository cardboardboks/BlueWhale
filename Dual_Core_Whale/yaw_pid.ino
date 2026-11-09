// ================================================================
//  Yaw stabilisation: closed-loop yaw RATE control for the drive
// ================================================================
// The steering stick asks for a yaw rate; the gyro measures the real one; this works out the steering needed to make
// them match. Hits that knock the robot round get fought, and with some I it turns back to where it was pointing.
//
// Built the way Brett Beauregard's Arduino PID library is ("improving the beginner's PID"): fixed sample time,
// derivative on measurement (no kick when the stick moves), integrator clamped against windup, bumpless switching
// and tuning changes while running. Plus what the library doesn't have:
//   - setpoint shaping: expo, a maximum rate and a maximum rotational acceleration
//   - feedforward (rate and acceleration), so the stick feels direct and PID only fixes what's left
//   - low-pass filters on the measured rate and on the D term
//   - the integrator stops growing while the output is pinned at its limit, and resets on flips, mode changes,
//     IMU loss and ESC dropouts
// Every setting is a box on the Whale Tuning page (T_YAW_... below; names, help and defaults are in the main sketch).
// Gains work on the error as a fraction of the max rate, so 1.0 = "a full stick's worth of error".
//
// Runs once per new IMU sample (~100 Hz); the output is held between samples.
// Everything for it is in this file. The rest of the sketch only calls in:
//   imu.ino    -> yawImuSample()    each IMU packet
//   mixer.ino  -> yawStab() / yawStabReset()   steering in / out of the Stab modes
//   status.ino -> yawStatusLine()   the Bulk Text line
//   imu.ino    -> yawGyroCalSample() while the Calibrate gyro scale button is running
//   web.ino    -> yawKeepOnDefaults() so Load defaults doesn't undo that calibration
// Active in the "Stab" modes (ch4 low). If the IMU is lost, the max rate is 0, or an ESC is out, it hands back
// plain stick steering.

// ---- which tuning box is which (names, help text and defaults: tuneName / tuneHelp / tuneDefault in the main sketch) ----
#define T_YAW_P 6        // proportional gain: steering per unit of rate error (error / max rate)
#define T_YAW_I 7        // integral gain, per second. Also pulls the heading back after a hit
#define T_YAW_D 8        // derivative gain, seconds (on the measured rate, so stick moves don't kick it)
#define T_YAW_RATE 9     // full stick = this yaw rate, deg/s. 0 = stabilisation off (plain stick steering)
#define T_YAW_ACCEL 10   // most the requested rate may change per second, deg/s/s. 0 = no limit
#define T_YAW_FF 11      // feedforward: steering straight from the requested rate. 1 = same as plain stick
#define T_YAW_FFA 12     // acceleration feedforward, seconds: extra kick while the requested rate is changing
#define T_YAW_EXPO 13    // stick expo, 0 (linear) to 1 (soft centre)
#define T_GYRO_LPF 14    // low-pass on the measured yaw rate, Hz. 0 = off
#define T_YAW_DLPF 15    // low-pass on the D term, Hz. 0 = off
#define T_YAW_ILIM 20    // most the I term may contribute, 0 to 1 of full steering
#define T_YAW_OLIM 21    // most the whole controller may steer, 0 to 1
#define T_YAW_DIR 22     // 1 or -1: flip if the robot spins away instead of being held
#define T_GYRO_SCALE 23  // gyro counts per deg/s (16.4 at the DMP's 2000 deg/s range)
#define T_YAW_AIR 24     // airmode: 1 = keep correcting with both sticks centred, 0 = hands off when idle
#define T_YAW_IBAND 25   // idle band, deg/s: with both sticks centred, rate errors smaller than this are ignored
#define T_YAW_IBLEED 26  // I bleed, per second: while no turn is asked for, the I term fades at this rate
#define T_HOLD_ON 27     // heading hold: 1 = steer back to the heading held when the stick was centred, 0 = off
#define T_HOLD_K 28      // hold strength, per second: return turn rate (°/s) per degree of heading error
#define T_HOLD_MAX 29    // fastest return turn, °/s
#define T_HOLD_BAND 30   // at standstill, rest when within this many degrees of the held heading
#define T_HOLD_SETTLE 36 // after the stick is released, the heading is locked once the robot turns slower than this, °/s
#define HOLD_SETTLE_MS 80       // ...for this long
#define HOLD_SETTLE_MAX_MS 600  // ...or after this long anyway (a hit straight after letting go can't stop it locking)
#define HDG_NOISE_DPS 1.5f  // measured rates below this are ignored when adding up the heading (no drift creep)

// ---- telemetry (shown in Bulk Text) ----
volatile bool yawActive = false;  // the controller is steering right now
volatile float yawSpDps = 0;      // requested rate after expo / accel limiting
volatile float yawMeasDps = 0;    // measured rate (filtered, same sign as the stick)
volatile float yawOut = 0;        // steering output, -1..1
volatile float yawITerm = 0;      // the I term's share of that
volatile bool yawHolding = false; // heading hold is holding a heading right now
volatile float yawHdgErr = 0;     // held heading minus the current one, degrees

// ---- from the IMU task (core 0): yaw rate about the world vertical, deg/s ----
volatile float imuYawRate = 0;
volatile uint32_t imuYawUs = 0;   // micros() of that sample
volatile uint32_t imuYawSeq = 0;  // counts up with every new sample

// Called by the IMU task for every good packet: gyro (sensor axes, raw counts) and the DMP's gravity direction.
// The yaw rate is the gyro along gravity, i.e. about the world vertical - right whichever way up the robot is and
// however the IMU is mounted.
void yawImuSample(float gx, float gy, float gz, float grx, float gry, float grz) {
  float gn = sqrtf(grx * grx + gry * gry + grz * grz);
  if (gn < 0.5f) return;
  float gs = tuneVal[T_GYRO_SCALE];
  if (!(gs > 0)) gs = 16.4f;
  imuYawRate = (gx * grx + gy * gry + gz * grz) / gn / gs;
  imuYawUs = micros();
  imuYawSeq++;
}

// ---- Gyro scale calibration (Calibrate gyro scale button) ----
// The gyro readings come through the DMP and can be scaled differently from the datasheet's 16.4 counts per °/s.
// The DMP's orientation (the quaternion) is right, though, so while the robot is twisted by hand, compare how fast the
// quaternion turns with how big the gyro reading is: the ratio is the gyro scale. Uses the size of the rotation
// only, so it doesn't matter which way it's twisted or how the IMU is mounted.
void yawGyroCalSample(float gx, float gy, float gz, float qw, float qx, float qy, float qz, uint32_t us) {
  static bool on = false;
  static float pw, px, py, pz;
  static uint32_t pus, startMs, n;
  static double sg, sq, total;
  if (!on) {  // first packet: just remember where it started
    on = true;
    pw = qw; px = qx; py = qy; pz = qz;
    pus = us;
    startMs = millis();
    sg = sq = total = 0;
    n = 0;
    imuCalMsg = "Twist it back and forth, fast, for a few seconds";
    return;
  }
  // rotation since the last packet: conj(previous) * current
  float rw = pw * qw + px * qx + py * qy + pz * qz;
  float rx = pw * qx - px * qw - py * qz + pz * qy;
  float ry = pw * qy + px * qz - py * qw - pz * qx;
  float rz = pw * qz - px * qy + py * qx - pz * qw;
  float dt = (us - pus) * 1e-6f;
  pw = qw; px = qx; py = qy; pz = qz;
  pus = us;
  if (dt > 0.002f && dt < 0.05f) {
    float ang = 2.0f * atan2f(sqrtf(rx * rx + ry * ry + rz * rz), fabsf(rw)) * 180.0f / (float)M_PI;  // degrees
    float wq = ang / dt;                                  // °/s from the quaternion
    float gmag = sqrtf(gx * gx + gy * gy + gz * gz);      // gyro counts
    total += ang;
    if (wq > 60.0f && gmag < 32000.0f) {  // only fast-enough samples, and none where the gyro is at its limit
      sg += (double)gmag * wq;
      sq += (double)wq * wq;
      n++;
    }
  }
  if (total >= 720.0 && n >= 30) {  // two turns' worth of twisting: done
    float sc = (float)(sg / sq);
    static char msg[64];
    if (sc > 0.1f && sc < 200.0f) {
      tuneVal[T_GYRO_SCALE] = sc;
      tuneChangedMs = millis();
      tuneDirty = true;  // saved to flash shortly after, like an edit on the page
      snprintf(msg, sizeof(msg), "Gyro scale %.2f counts per °/s - saved", sc);
      Serial.printf("Gyro scale calibrated: %.3f\n", sc);
    } else {
      snprintf(msg, sizeof(msg), "Result %.2f looks wrong - try again", sc);
    }
    imuCalMsg = msg;
    imuCalGyroReq = false;
    on = false;
  } else if (millis() - startMs > 10000) {
    imuCalMsg = "Not enough fast twisting - press again and twist harder";
    imuCalGyroReq = false;
    on = false;
  }
}

// Load defaults skips these boxes: they're measured calibrations, not tuning choices
bool yawKeepOnDefaults(int i) {
  return i == T_GYRO_SCALE;
}

// Bulk Text line. Measured rate is in the stick's sign: steer right -> it should go positive (see Yaw Dir).
String yawStatusLine() {
  if (yawActive)
    return "Yaw °/s sp " + String((float)yawSpDps, 0) + " meas " + String((float)yawMeasDps, 0) + " out " + String((float)yawOut, 2) + " I " + String((float)yawITerm, 2) +
           (yawHolding ? " hdg " + String((float)yawHdgErr, 1) + "°" : String(""));
  float now = -(float)imuYawRate * ((tuneVal[T_YAW_DIR] < 0) ? -1.0f : 1.0f);
  return "Yaw " + String(now, 0) + " °/s (stab off)";
}

// ---------------- the controller ----------------
static float yawSp = 0;        // shaped setpoint, deg/s
static float yawSpPrev = 0;    // for the acceleration feedforward
static float yawMeasF = 0;     // filtered measured rate, deg/s
static float yawMeasPrev = 0;  // for the D term
static float yawDF = 0;        // filtered derivative (normalised, per second)
static float yawI = 0;         // integrator, in output units
static float yawU = 0;         // last output
static uint32_t yawLastSeq = 0;
static uint32_t yawLastUs = 0;
static bool yawRunning = false;  // false = next sample starts fresh (no D kick, no dt guess)
static bool yawLastDown = false;
static float yawHeading = 0;     // heading from adding up the measured rate, degrees (only differences matter)
static float yawHdgTarget = 0;   // heading being held
static bool yawHold = false;     // holding a heading
static bool yawSettled = true;        // false from releasing the stick until the robot has come to rest
static uint32_t yawSettleMs = 0;      // when the stick was released (0 = not settling)
static uint32_t yawSlowSinceMs = 0;   // when the robot dropped below the settle rate (0 = not yet)

// first-order low-pass blend factor for cut-off fc (Hz) at time step dt (s); fc <= 0 = no filtering
static float lpfAlpha(float fc, float dt) {
  if (!(fc > 0)) return 1.0f;
  float rc = 1.0f / (2.0f * (float)M_PI * fc);
  return dt / (rc + dt);
}

// Forget the controller's history: next time it runs it starts bumplessly from the current stick and gyro
void yawStabReset() {
  yawRunning = false;
  yawI = 0;
  yawDF = 0;
  yawU = 0;
  yawActive = false;
  yawITerm = 0;
  yawHold = false;
  yawSettled = true;
  yawSettleMs = 0;
  yawSlowSinceMs = 0;
  yawHolding = false;
  yawHdgErr = 0;
}

// stick: steering stick after the deadband, -1..1. throttle: the throttle after the deadband, -1..1 (only used to
// tell when both sticks are centred). Returns the steering to use, -1..1.
float yawStab(float stick, float throttle) {
  float maxRate = tuneVal[T_YAW_RATE];
  bool escsOk = true;
  for (int i = 0; i < NUM_ESC; i++)
    if (escMonitorOn && escState[i] != ESC_OK) escsOk = false;
  bool down = robotDown();
  if (!imuAlive() || !(maxRate > 0) || !escsOk || down != yawLastDown) {
    // can't (or shouldn't) control right now: plain stick steering, and start clean when it's possible again
    yawLastDown = down;
    yawStabReset();
    return stick;
  }

  // "Idle" = both sticks centred. The deadband in the mixer makes a centred stick exactly 0.
  bool noTurn = (stick == 0.0f);
  bool idle = noTurn && (throttle == 0.0f);
  if (idle && tuneVal[T_YAW_AIR] < 0.5f) {  // airmode off: no correcting at all until a stick moves
    yawStabReset();
    return 0.0f;
  }

  uint32_t seq = imuYawSeq;
  if (yawRunning && seq == yawLastSeq) return yawU;  // no new gyro sample yet: hold the last output
  uint32_t us = imuYawUs;
  float raw = imuYawRate;

  float dt = (us - yawLastUs) * 1e-6f;
  bool fresh = !yawRunning || !(dt > 0.0005f && dt < 0.05f);  // first sample, or a gap: don't trust dt or d/dt
  if (fresh) dt = 0.01f;
  yawLastSeq = seq;
  yawLastUs = us;

  // ---- settings (read every time, so edits on the page apply live) ----
  float kp = tuneVal[T_YAW_P], ki = tuneVal[T_YAW_I], kd = tuneVal[T_YAW_D];
  float kff = tuneVal[T_YAW_FF], kffa = tuneVal[T_YAW_FFA];
  float maxAcc = tuneVal[T_YAW_ACCEL];
  float expo = constrain((float)tuneVal[T_YAW_EXPO], 0.0f, 1.0f);
  float iLim = constrain((float)tuneVal[T_YAW_ILIM], 0.0f, 1.0f);
  float oLim = constrain((float)tuneVal[T_YAW_OLIM], 0.0f, 1.0f);
  float dir = (tuneVal[T_YAW_DIR] < 0) ? -1.0f : 1.0f;

  // ---- measured rate, in the stick's sign convention (positive = the way a positive stick turns) ----
  float meas = -raw * dir;
  if (fresh) yawMeasF = meas;
  else yawMeasF += lpfAlpha(tuneVal[T_GYRO_LPF], dt) * (meas - yawMeasF);

  // ---- heading: add up the measured rate (tiny rates are gyro noise/drift: ignored, so the heading can't creep) ----
  if (fresh) yawHeading = 0;
  else if (fabsf(yawMeasF) > HDG_NOISE_DPS) yawHeading += yawMeasF * dt;

  // ---- setpoint: expo, max rate, max rotational acceleration ----
  float st = stick * (1.0f - expo) + stick * stick * stick * expo;
  float target = st * maxRate;

  // ---- heading hold: with the stick centred, once the turn has finished, hold that heading ----
  float bleed = tuneVal[T_YAW_IBLEED];
  float hdgErr = 0;
  // ---- settling after a manual turn ----
  // When the stick is released, the robot is still braking and overshoots a little. Two things used to pull it back
  // 10-15° afterwards: the I term (it had built up "where the robot should have stopped") and heading hold locking
  // the heading as soon as the requested rate reached 0. So from release until the robot is turning slower than
  // Hold Settle for HOLD_SETTLE_MS (or HOLD_SETTLE_MAX_MS at most): I is cleared and held at 0, and the heading is
  // only locked once it has come to rest - wherever that is. After that, knocks with no stick input are fought by
  // I and heading hold as normal.
  if (noTurn) {
    if (!yawSettled && !fresh) {
      uint32_t now = millis();
      if (yawSettleMs == 0) yawSettleMs = now;
      float settle = tuneVal[T_HOLD_SETTLE];
      bool slow = !(settle > 0) || fabsf(yawMeasF) < settle;
      if (fabsf(yawSp) < 1.0f && slow) {
        if (yawSlowSinceMs == 0) yawSlowSinceMs = now;
      } else {
        yawSlowSinceMs = 0;
      }
      if ((yawSlowSinceMs != 0 && now - yawSlowSinceMs >= HOLD_SETTLE_MS) || now - yawSettleMs >= HOLD_SETTLE_MAX_MS)
        yawSettled = true;
    }
  } else {
    yawSettled = false;
    yawSettleMs = 0;
    yawSlowSinceMs = 0;
  }
  bool settling = noTurn && !yawSettled;

  if (tuneVal[T_HOLD_ON] >= 0.5f && noTurn) {
    if (!yawHold && yawSettled) {  // come to rest: hold this heading
      yawHold = true;
      yawHdgTarget = yawHeading;
    }
    if (yawHold) {
      hdgErr = yawHdgTarget - yawHeading;
      if (bleed > 0) {  // I Bleed also lets the held heading drift to where the robot is (so a pin isn't fought forever)
        yawHdgTarget -= hdgErr * (1.0f - expf(-bleed * dt));
        hdgErr = yawHdgTarget - yawHeading;
      }
      float hb = max((float)tuneVal[T_HOLD_BAND], 0.0f);
      float he = (idle && fabsf(hdgErr) <= hb) ? 0.0f : hdgErr;  // at standstill, close enough = rest
      float hmax = max((float)tuneVal[T_HOLD_MAX], 0.0f);
      target = constrain(tuneVal[T_HOLD_K] * he, -hmax, hmax);  // turn back toward the held heading
    }
  } else {
    yawHold = false;  // stick moved (or hold off): it takes a new heading when it comes to rest again
  }

  if (fresh) {
    yawSp = yawMeasF;  // bumpless: start the ramp from what the robot is actually doing
    yawSpPrev = yawSp;
    yawMeasPrev = yawMeasF;
    yawDF = 0;
  }
  if (maxAcc > 0) {
    float step = maxAcc * dt;
    yawSp += constrain(target - yawSp, -step, step);
  } else {
    yawSp = target;
  }
  float spDot = (yawSp - yawSpPrev) / dt;
  yawSpPrev = yawSp;

  // ---- PID on the normalised error ----
  float errDps = yawSp - yawMeasF;
  float band = tuneVal[T_YAW_IBAND];
  if (idle && band > 0 && !yawHold) {  // idle band: ignore small errors at standstill (heading hold has its own band)
    float m = fabsf(errDps) - band;
    errDps = (m > 0) ? copysignf(m, errDps) : 0.0f;  // subtract rather than cut, so there's no step at the edge
  }
  float e = errDps / maxRate;
  float pTerm = kp * e;

  float dRaw = -(yawMeasF - yawMeasPrev) / dt / maxRate;  // derivative on measurement
  yawMeasPrev = yawMeasF;
  yawDF += lpfAlpha(tuneVal[T_YAW_DLPF], dt) * (dRaw - yawDF);
  float dTerm = kd * yawDF;

  float ff = kff * (yawSp / maxRate) + kffa * (spDot / maxRate);

  if (noTurn && bleed > 0) {  // I bleed: slowly let go of a held heading (stops fighting a pin forever)
    yawI *= expf(-bleed * dt);
    if (fabsf(yawI) < 0.002f) yawI = 0;  // fully let go: no crumb left to nudge the output
  }

  if (settling) yawI = 0.0f;  // just let go of the stick: no I until it has come to rest (see settling above)

  float base = ff + pTerm + dTerm;
  float iNew = (ki > 0 && !settling) ? constrain(yawI + ki * e * dt, -iLim, iLim) : 0.0f;
  float u = base + iNew;
  // anti-windup: if the output is already pinned and the error would push it further, don't let I grow
  if (fabsf(u) > oLim && ((u > 0) == (e > 0)) && fabsf(iNew) > fabsf(yawI)) iNew = (ki > 0) ? yawI : 0.0f;
  yawI = iNew;
  u = constrain(base + yawI, -oLim, oLim);

  // Idle quiet zone: both sticks centred and the robot turning slower than the idle band -> output exactly 0.
  // In 3D DShot any output off 1500, even by one count, jumps to the ESC's lowest forward/reverse step, so the
  // crumbs from gyro noise through D (and a bleeding I) made the motors twitch. I is kept (still bleeding), so a
  // knock bigger than the band gets the full correction straight away.
  // With heading hold, it also has to be within the Hold Band of its heading.
  if (idle && band > 0 && fabsf(yawMeasF) < band && (!yawHold || fabsf(hdgErr) <= max((float)tuneVal[T_HOLD_BAND], 0.0f))) u = 0.0f;

  yawU = u;
  yawRunning = true;
  yawActive = true;
  yawSpDps = yawSp;
  yawMeasDps = yawMeasF;
  yawOut = u;
  yawITerm = yawI;
  yawHolding = yawHold;
  yawHdgErr = hdgErr;
  return u;
}
