// ================================================================
//  Biggest Hit record (just for fun)
// ================================================================
// Max G is the single highest acceleration reading. Two things keep it small on this robot: the accelerometer runs
// at +/-2 g (the range the DMP is set up with), so anything harder just reads "about 2-3 g"; and it is only read
// 100 times a second through the chip's filter, so a 2 ms spike is smeared out or missed. Real hits peak at tens or
// hundreds of g for a few milliseconds.
//
// This record measures the whole hit instead of its peak: how much the hit changed the robot's motion.
//   - Speed change, m/s: the acceleration (gravity removed, turned into fixed world directions with the DMP's
//     orientation so the robot spinning mid-hit doesn't scramble it) added up over the hit. Filtering doesn't change
//     the total much, which is why this is more trustworthy than a peak - but while the accelerometer is pinned at
//     its 2 g limit, the part over the limit is lost, so it can read low ("≥" on the page when that happened).
//   - Spin change, °/s: turn rate after the hit minus before, from the gyro (good to its 2000 °/s limit).
//   - Energy, J: what those changes would take from a standing start - 1/2 m v² for the push plus 1/2 I w² for the
//     spin. I (how hard the robot is to spin) comes from Mass kg and the Length / Width boxes, treating the robot as
//     an even slab: I = m (L² + W²) / 12. Real robots with heavy parts near the middle are a bit easier to spin.
// The biggest hit (by energy) is kept until Reset.
//
// A hit starts when the acceleration (gravity removed) passes HIT_START_G, or the turn rate jumps by more than
// HIT_START_DPS between two readings (far more than the drive can do). It ends after HIT_QUIET_N quiet readings in a
// row, or HIT_MAX_MS at most. Driving, spinning up and bumps over floor joints stay well under these.

#define HIT_START_G 1.5f       // start: acceleration over this (g, gravity removed)...
#define HIT_START_DPS 150.0f   // ...or the turn rate jumps this much between readings (~10 ms) - 15000 °/s²
#define HIT_QUIET_G 0.5f       // quiet: under this...
#define HIT_QUIET_DPS 40.0f    // ...and turn-rate change under this per reading
#define HIT_QUIET_N 3          // this many quiet readings in a row ends the hit
#define HIT_MAX_MS 250         // longest a hit is measured
#define HIT_ACC_CLIP 32000     // raw accel counts at (near) the +/-2 g limit
#define HIT_GYRO_CLIP_DPS 1950.0f

// Called by the IMU task for every good packet.
// ax..az: accel in g with gravity removed, sensor axes (from the readings BEFORE the median filter, so a one-reading
// spike isn't thrown away). qw..qz: the DMP orientation. rateDps: turn rate about the vertical. rawMaxAcc: the
// largest raw accel count on any axis (to spot the 2 g limit). us: when this reading was taken.
void hitSample(float ax, float ay, float az, float qw, float qx, float qy, float qz, float rateDps, int rawMaxAcc,
               uint32_t us) {
  static bool active = false;
  static uint32_t startUs = 0, lastUs = 0;
  static float lastRate = 0, rate0 = 0;
  static double vx = 0, vy = 0, vz = 0;  // speed change so far, world axes, m/s
  static uint8_t quietN = 0;
  static bool clipped = false;
  static bool primed = false;

  float dt = (us - lastUs) * 1e-6f;
  lastUs = us;
  if (!primed || !(dt > 0.0005f && dt < 0.05f)) {  // first reading or a gap: just remember where we are
    primed = true;
    active = false;
    lastRate = rateDps;
    return;
  }
  float g = sqrtf(ax * ax + ay * ay + az * az);
  float dRate = fabsf(rateDps - lastRate);

  if (!active && (g > HIT_START_G || dRate > HIT_START_DPS)) {
    active = true;
    startUs = us;
    rate0 = lastRate;  // turn rate just before the hit
    vx = vy = vz = 0;
    quietN = 0;
    clipped = false;
  }
  if (active) {
    // sensor axes -> world axes with the orientation quaternion: v' = q v q*
    float tx = 2.0f * (qy * az - qz * ay), ty = 2.0f * (qz * ax - qx * az), tz = 2.0f * (qx * ay - qy * ax);
    float wx = ax + qw * tx + (qy * tz - qz * ty);
    float wy = ay + qw * ty + (qz * tx - qx * tz);
    float wz = az + qw * tz + (qx * ty - qy * tx);
    vx += wx * 9.81 * dt;
    vy += wy * 9.81 * dt;
    vz += wz * 9.81 * dt;
    if (rawMaxAcc >= HIT_ACC_CLIP || fabsf(rateDps) >= HIT_GYRO_CLIP_DPS) clipped = true;
    if (g < HIT_QUIET_G && dRate < HIT_QUIET_DPS) quietN++;
    else quietN = 0;

    if (quietN >= HIT_QUIET_N || us - startUs > HIT_MAX_MS * 1000UL) {  // hit over: size it up
      active = false;
      float dv = (float)sqrt(vx * vx + vy * vy + vz * vz);  // m/s
      float dw = fabsf(rateDps - rate0);                      // °/s
      float m = tuneVal[T_MASS_KG];
      if (!(m > 0)) m = 0;
      float L = tuneVal[T_LEN_MM], W = tuneVal[T_WID_MM];
      float guess = (tuneVal[T_TRACK_MM] + tuneVal[T_WHEEL_MM]) / 1000.0f;  // Length / Width 0: rough size from the wheels
      L = (L > 0) ? L / 1000.0f : guess;
      W = (W > 0) ? W / 1000.0f : guess;
      float inertia = m * (L * L + W * W) / 12.0f;  // kg m²
      float w = dw * (float)M_PI / 180.0f;
      float e = 0.5f * m * dv * dv + 0.5f * inertia * w * w;  // J
      // a hit with no mass entered is still recorded by its size (energy 0, so any later hit with mass replaces it)
      bool bigger = (m > 0) ? (e > recHitJ) : (recHitJ == 0 && dv > recHitDv);
      if (bigger) {
        portENTER_CRITICAL(&recHitMux);
        recHitJ = e;
        recHitDv = dv;
        recHitDw = dw;
        recHitClip = clipped;
        portEXIT_CRITICAL(&recHitMux);
      }
    }
  }
  lastRate = rateDps;
}
