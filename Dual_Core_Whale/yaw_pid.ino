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

// ---- telemetry (shown in Bulk Text) ----
volatile bool yawActive = false;  // the controller is steering right now
volatile float yawSpDps = 0;      // requested rate after expo / accel limiting
volatile float yawMeasDps = 0;    // measured rate (filtered, same sign as the stick)
volatile float yawOut = 0;        // steering output, -1..1
volatile float yawITerm = 0;      // the I term's share of that

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
    return "Yaw °/s sp " + String((float)yawSpDps, 0) + " meas " + String((float)yawMeasDps, 0) + " out " + String((float)yawOut, 2) + " I " + String((float)yawITerm, 2);
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

  // ---- setpoint: expo, max rate, max rotational acceleration ----
  float st = stick * (1.0f - expo) + stick * stick * stick * expo;
  float target = st * maxRate;
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
  if (idle && band > 0) {  // idle band: ignore small errors at standstill (no twitching, no slow creep from gyro drift)
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

  float bleed = tuneVal[T_YAW_IBLEED];
  if (noTurn && bleed > 0) yawI *= expf(-bleed * dt);  // I bleed: slowly let go of a held heading (stops fighting a pin forever)

  float base = ff + pTerm + dTerm;
  float iNew = (ki > 0) ? constrain(yawI + ki * e * dt, -iLim, iLim) : 0.0f;
  float u = base + iNew;
  // anti-windup: if the output is already pinned and the error would push it further, don't let I grow
  if (fabsf(u) > oLim && ((u > 0) == (e > 0)) && fabsf(iNew) > fabsf(yawI)) iNew = (ki > 0) ? yawI : 0.0f;
  yawI = iNew;
  u = constrain(base + yawI, -oLim, oLim);

  yawU = u;
  yawRunning = true;
  yawActive = true;
  yawSpDps = yawSp;
  yawMeasDps = yawMeasF;
  yawOut = u;
  yawITerm = yawI;
  return u;
}
