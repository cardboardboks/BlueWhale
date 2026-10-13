// ch1 throttle - analog
// ch2 steering - analog
// ch3 arming - disared/armed
// ch4 auto up - on/off/off
// ch5 stabilization - on/off/off
// ch6 reverse - forward/backwards
// ch7 n/a
// ch8 n/a

const float DEADBAND = 0.05f;    // ignore stick noise near centre
const float STEER_GAIN = 1.0f;   // lower = gentler turning
const bool INVERT_LEFT = false;  // flip if a motor spins the wrong way
const bool INVERT_RIGHT = false;

// throttle, steering: 1000-2000 us (1500 = centre)
void mixer(float throttle, float steering) {

  // 1000-2000 -> -1.0 ... +1.0
  float t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
  float s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;

  // Differential mix
  float left = t + s;
  float right = t - s;

  // Scale both down together if either exceeds full range (keeps turn ratio)
  float peak = max(fabs(left), fabs(right));
  if (peak > 1.0f) {
    left /= peak;
    right /= peak;
  }

  if (INVERT_LEFT) left = -left;
  if (INVERT_RIGHT) right = -right;

  // -1.0 ... +1.0 -> 1000-2000 us
  drive_esc(1500 + (int)lroundf(left * 500.0f), 1500 + (int)lroundf(right * 500.0f));
}



// Rescale so output still reaches full range after the deadband
static float deadband(float v) {
  if (fabs(v) < DEADBAND) return 0.0f;
  float sign = (v > 0) ? 1.0f : -1.0f;
  return sign * (fabs(v) - DEADBAND) / (1.0f - DEADBAND);
}