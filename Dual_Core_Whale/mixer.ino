// ch1 throttle - analogled(7)
// ch2 steering - analog
// ch3 arming - disared/armed
// ch4 auto up - on/off/off
// ch5 stabilisation - on/off/off
// ch6 reverse - forward/led(12, 1);
// ch7 n/a
// ch8 n/a

const float DEADBAND = 0.05f;   // ignore stick noise near centre
const float STEER_GAIN = 1.0f;  // lower = gentler turning
// When the drive direction flips (robot turned over, or the ch6 reverse switch), the outputs blend from what they
// were sending to the new command over this long, instead of jumping. 0 = instant flip like before.
const uint32_t FLIP_RAMP_MS = 200;

// throttle, steering: 1000-2000 us (1500 = centre)
void mixer(float throttle, float steering) {

  // stopped unless one of the modes below is active (these are -1..1, so 0 = centre)
  float t = 0;
  float s = 0;
  int dir = 1;  // -1 in the modes that invert the throttle (used to spot a direction flip)
  bool stab = false;  // true in the "Stab" modes: steering goes through the yaw rate controller (pid.ino)

  // scale and constrain inputs
  // impliment logic for auto up and reverese

  // force only normal operation when imu is disabled
  // when auto up is off: all blinking purple/white (stabilisation not available)
  bool imuOffMode = false;  // IMU Off mode keeps a little drive even when the battery has cut it
  if (txchannel[4] == 2000 && txchannel[3] == 2000) {
    imuOffMode = true;
    led(7, 1);
    modeString = "IMU Off";
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  } else {
    led(7, 0);
  }



  // 8 stabilisation off, drive forward, top: slow blink green
  if (txchannel[6] == 1000 && !robotDown() && txchannel[3] == 2000 && txchannel[4] == 1500) {
    led(8, 1);
    modeString = "Man Up Fwd";
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  } else {
    led(8, 0);
  }

  // 9 stabilisation off, drive backwards, top: blinking green
  if (txchannel[6] == 2000 && !robotDown() && txchannel[3] == 2000 && txchannel[4] == 1500) {
    led(9, 1);
    modeString = "Man Up Rev";
    dir = -1;
    t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
    s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN);
  } else {
    led(9, 0);
  }

  // 10 stabilisation off, drive forward, bottom: slow blink green
  if (txchannel[6] == 1000 && robotDown() && txchannel[3] == 2000 && txchannel[4] == 1500) {
    led(10, 1);
    modeString = "Man Dwn Fwd";
    dir = -1;
    t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  } else {
    led(10, 0);
  }

  // 11 stabilisation off, drive backwards, bottom: blinking green
  if (txchannel[6] == 2000 && robotDown() && txchannel[3] == 2000 && txchannel[4] == 1500) {
    led(11, 1);
    modeString = "Man Dwn Rev";
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN);
  } else {
    led(11, 0);
  }



  // 12 stabilisation on, drive forward, top: slow blink white
  if (txchannel[6] == 1000 && !robotDown() && txchannel[3] == 2000 && txchannel[4] == 1000) {
    led(12, 1);
    modeString = "Stab Up Fwd";
    stab = true;
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  } else {
    led(12, 0);
  }

  // 13 stabilisation on, drive backwards, top: blinking white
  if (txchannel[6] == 2000 && !robotDown() && txchannel[3] == 2000 && txchannel[4] == 1000) {
    led(13, 1);
    modeString = "Stab Up Rev";
    stab = true;
    dir = -1;
    t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
    s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN);
  } else {
    led(13, 0);
  }

  // 14 stabilisation on, drive forward, bottom: slow blink white
  if (txchannel[6] == 1000 && robotDown() && txchannel[3] == 2000 && txchannel[4] == 1000) {
    led(14, 1);
    modeString = "Stab Dwn Fwd";
    stab = true;
    dir = -1;
    t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  } else {
    led(14, 0);
  }

  // 15 stabilisation on, drive backwards, bottom: blinking white
  if (txchannel[6] == 2000 && robotDown() && txchannel[3] == 2000 && txchannel[4] == 1000) {
    led(15, 1);
    modeString = "Stab Dwn Rev";
    stab = true;
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN);
  } else {
    led(15, 0);
  }


  // Yaw stabilisation: the steering stick becomes a yaw rate request and the controller does the steering.
  // Outside the Stab modes it's reset, so switching in is bumpless.
  if (stab) s = yawStab(deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)),
                       deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f)));
  else yawStabReset();

  // Differential mix
  float left = t + s;
  float right = t - s;

  // Scale both down together if either exceeds full range (keeps turn ratio)
  float peak = max(fabs(left), fabs(right));
  if (peak > 1.0f) {
    left /= peak;
    right /= peak;
  }

  // Direction flip ramp: on a flip, remember what was being sent and blend from it to the live command.
  // The target keeps following the sticks during the ramp. A second flip mid-ramp starts again from wherever it got to.
  static int lastDir = 1;
  static float outL = 0, outR = 0;    // what was sent last pass
  static float fromL = 0, fromR = 0;  // what was being sent when the flip happened
  static uint32_t flipMs = 0;
  static bool ramping = false;
  if (dir != lastDir) {
    lastDir = dir;
    fromL = outL;
    fromR = outR;
    flipMs = millis();
    ramping = true;
  }
  if (ramping) {
    uint32_t el = millis() - flipMs;
    if (el >= FLIP_RAMP_MS) {
      ramping = false;
    } else {
      float k = (float)el / (float)FLIP_RAMP_MS;
      left = fromL + (left - fromL) * k;
      right = fromR + (right - fromR) * k;
    }
  }
  outL = left;
  outR = right;

  // Low battery (battery.ino): scale both wheels by the cap, so throttle, steering and the yaw controller all come down
  // together and turns keep their shape. IMU Off mode never goes below the "Batt Limp %" box, so it can always
  // show movement.
  float cap = battCap();
  float battLimp = constrain((float)tuneVal[T_BAT_LIMP] / 100.0f, 0.0f, 1.0f);
  if (imuOffMode && cap < battLimp) cap = battLimp;
  left *= cap;
  right *= cap;
  if (battIsCut()) {
    if (imuOffMode && battLimp > 0) modeString = "Batt Cut " + String((int)lroundf(battLimp * 100)) + "%";
    else modeString = "Batt Cut";
  }
  else if (cap < 0.995f) modeString = "Low Batt " + String((int)lroundf(cap * 100)) + "%";

  // Min Output %: any non-zero wheel output starts at least here; the rest of the range is spread over what's left
  float mo = constrain((float)tuneVal[T_MIN_OUT] / 100.0f, 0.0f, 0.9f);
  left = minOutput(left, mo);
  right = minOutput(right, mo);

  // scale back to standard rc range 1000-2000
  drive1_pwm = 1500 + (int)lroundf(left * 500.0f);
  drive2_pwm = 1500 + (int)lroundf(right * 500.0f);
}

// Min Output: 0 stays 0 (anything under one output step counts as 0, so noise doesn't kick it), otherwise
// sign * (min + (1 - min) * |v|), so it starts at min and still reaches full at full
static float minOutput(float v, float mo) {
  if (!(mo > 0) || fabsf(v) < 0.001f) return (fabsf(v) < 0.001f) ? 0.0f : v;
  return copysignf(mo + (1.0f - mo) * fabsf(v), v);
}

// Rescale so output still reaches full range after the deadband
static float deadband(float v) {
  if (fabs(v) < DEADBAND) return 0.0f;
  float sign = (v > 0) ? 1.0f : -1.0f;
  return sign * (fabs(v) - DEADBAND) / (1.0f - DEADBAND);
}