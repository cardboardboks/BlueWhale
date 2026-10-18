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

  float t = 1500;
  float s = 1500;

  // scale and constrain inputs
  // impliment logic for auto up and reverese

  // force only normal operation when imu is disabled
  if (ch[4] != 1000) {
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  } else {

    // upright - forwards
    if (ch[6] == 1000 && RP < roll_flip) {
      led(7);
      t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
      s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
    }

    // upright - backwards
    if (ch[6] == 2000 && RP < roll_flip) {
      led(8);
      t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
      s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN);
    }

    // upsidedown - forwards
    led(7);
    if (ch[6] == 1000 && RP > roll_flip) {
      t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
      s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
    }

    // upsidedown - backwards
    led(8);
    if (ch[6] == 2000 && RP > roll_flip) {
      t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
      s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN);
    }
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

  // scale back to standard rc range 1000-2000
  drive1_pwm = 1500 + (int)lroundf(left * 500.0f);
  drive2_pwm = 1500 + (int)lroundf(right * 500.0f);
}

// Rescale so output still reaches full range after the deadband
static float deadband(float v) {
  if (fabs(v) < DEADBAND) return 0.0f;
  float sign = (v > 0) ? 1.0f : -1.0f;
  return sign * (fabs(v) - DEADBAND) / (1.0f - DEADBAND);
}