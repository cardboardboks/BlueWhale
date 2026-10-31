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

// throttle, steering: 1000-2000 us (1500 = centre)
void mixer(float throttle, float steering) {

  float t = 1500;
  float s = 1500;

  // scale and constrain inputs
  // impliment logic for auto up and reverese

  // force only normal operation when imu is disabled
  // when auto up is off: all blinking purple/white (stabilisation not available)
  if (txchannel[4] == 2000 && txchannel[3] == 2000) {
    led(7, 1);
    modeString = "IMU Off";
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  } else {
    led(7, 0);
  }



  // 8 stabilisation off, drive forward, top: slow blink green
  if (txchannel[6] == 1000 && RP <= (70) && txchannel[3] == 2000 && txchannel[4] == 1500) {
    led(8, 1);
    modeString = "Man Up Fwd";
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  } else {
    led(8, 0);
  }

  // 9 stabilisation off, drive backwards, top: blinking green
  if (txchannel[6] == 2000 && RP <= (70) && txchannel[3] == 2000 && txchannel[4] == 1500) {
    led(9, 1);
    modeString = "Man Up Rev";
    t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
    s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN);
  } else {
    led(9, 0);
  }

  // 10 stabilisation off, drive forward, bottom: slow blink green
  if (txchannel[6] == 1000 && RP >= (70) && txchannel[3] == 2000 && txchannel[4] == 1500) {
    led(10, 1);
    modeString = "Man Dwn Fwd";
    t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  } else {
    led(10, 0);
  }

  // 11 stabilisation off, drive backwards, bottom: blinking green
  if (txchannel[6] == 2000 && RP >= (70) && txchannel[3] == 2000 && txchannel[4] == 1500) {
    led(11, 1);
    modeString = "Man Dwn Rev";
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN);
  } else {
    led(11, 0);
  }



  // 12 stabilisation on, drive forward, top: slow blink white
  if (txchannel[6] == 1000 && RP <= (70) && txchannel[3] == 2000 && txchannel[4] == 1000) {
    led(12, 1);
    modeString = "Stab Up Fwd";
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  } else {
    led(12, 0);
  }

  // 13 stabilisation on, drive backwards, top: blinking white
  if (txchannel[6] == 2000 && RP <= (70) && txchannel[3] == 2000 && txchannel[4] == 1000) {
    led(13, 1);
    modeString = "Stab Up Rev";
    t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
    s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN);
  } else {
    led(13, 0);
  }

  // 14 stabilisation on, drive forward, bottom: slow blink white
  if (txchannel[6] == 1000 && RP >= (70) && txchannel[3] == 2000 && txchannel[4] == 1000) {
    led(14, 1);
    modeString = "Stab Dwn Fwd";
    t = (deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f))) * -1;
    s = deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN;
  } else {
    led(14, 0);
  }

  // 15 stabilisation on, drive backwards, bottom: blinking white
  if (txchannel[6] == 2000 && RP >= (70) && txchannel[3] == 2000 && txchannel[4] == 1000) {
    led(15, 1);
    modeString = "Stab Dwn Rev";
    t = deadband(constrain((throttle - 1500.0f) / 500.0f, -1.0f, 1.0f));
    s = (deadband(constrain((steering - 1500.0f) / 500.0f, -1.0f, 1.0f)) * STEER_GAIN);
  } else {
    led(15, 0);
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