// ch1 throttle - analog
// ch2 steering - analog
// ch3 arming - disared/armed
// ch4 auto up - on/off/off
// ch5 stabilization - on/off/off
// ch6 reverse - forward/backwards
// ch7 n/a
// ch8 n/a

const float DEADBAND = 0.05f;   // ignore stick noise near centre
const float STEER_GAIN = 1.0f;  // lower = gentler turning

// throttle, steering: 1000-2000 us (1500 = centre)
void mixer(float throttle, float steering) {

  float t;
  float s;

  // 1000-2000 -> -1.0 ... +1.0
  // upright - forwards
  if (ch[6] == 1000 && RP < roll_flip) {
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  }

  // upright - backwards
  if (ch[6] == 2000 && RP < roll_flip) {
    t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
    s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN) * -1;
  }

  // upsidedown - forwards
  if (ch[6] == 1000 && RP > roll_flip) {
    t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  }

  // upsidedown - backwards
  if (ch[6] == 2000 && RP > roll_flip) {
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN) * -1;
  }

  // Differential mix
  float left = t + s;
  float right = t - s;

  // Scale both down together if either exceeds full range (keeps turn ratio)
  float peak = max(fabs(left), fabs(right));
  if (peak > 1.0f) {
    left /= peak;
    right /= peak;
  }

  // -1.0 ... +1.0 -> 1000-2000 us
  drive_esc(1500 + (int)lroundf(left * 500.0f), 1500 + (int)lroundf(right * 500.0f));
}



// Rescale so output still reaches full range after the deadband
static float deadband(float v) {
  if (fabs(v) < DEADBAND) return 0.0f;
  float sign = (v > 0) ? 1.0f : -1.0f;
  return sign * (fabs(v) - DEADBAND) / (1.0f - DEADBAND);
}