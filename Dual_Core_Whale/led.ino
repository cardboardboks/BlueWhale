// ================================================================
//  LEDs: 12 addressable LEDs in four groups of three
// ================================================================
// The effects below are written for POSITIONS - top left, top right, bottom left, bottom right - and for "front to
// back" within each group. Which three LEDs along the chain sit where, and which way round they are, comes from the
// LED boxes on the Whale Tuning page (LEDs 1-3 Pos / Dir ... LEDs 10-12 Pos / Dir), so the strip can be wired in any
// order. "Show LED layout" on that page lights each position in its own colour, front LED white, to check them.
//
// What they show:
// 1 all blinking yellow/white: booting
// 2 all blinking green/black: waiting pre arm
// 3 rainbow top: vehicle safe top
// 17 rainbow bottom: vehicle safe bottom
// 4 all fast blinking red/black: rc failsafe
// orange blink on the left / right LEDs (top and bottom): the ESC driving that side is lost / rebooting
// 7 when auto up is off: all blinking purple/black (stabilisation not available)
// 16 when imu is not online: all blinking purple/red (stabilisation not available)
//  the LEDs on the face that's up show the status; the face that's down is all red. Without a working, levelled
//  IMU it can't tell which face is up: both show the status (safe: rainbow all round)
// 8 / 10 stabilisation off, drive forward, top / bottom: slow blink teal
// 9 / 11 stabilisation off, drive backwards, top / bottom: blinking teal
// 12 / 14 stabilisation on, drive forward, top / bottom: slow blink white
// 13 / 15 stabilisation on, drive backwards, top / bottom: blinking white
// low battery: a quick red flash on every LED, every 5 s at Low Volt down to every 0.5 s at Crit

#define LED_GROUPS 4
#define LED_PER_GROUP 3
// positions, as bits so an effect can cover several at once
#define P_TL 1
#define P_TR 2
#define P_BL 4
#define P_BR 8
#define P_TOP (P_TL | P_TR)
#define P_BOT (P_BL | P_BR)
#define P_LEFT (P_TL | P_BL)
#define P_RIGHT (P_TR | P_BR)
#define P_ALL 15
#define LED_TEST_MS 60000  // Show LED layout runs this long at most

struct RGB {
  uint8_t r, g, b;
};
RGB leds[NUM_LEDS];                        // as sent down the chain
static RGB ledPos[LED_GROUPS][LED_PER_GROUP];  // by position (0 TL, 1 TR, 2 BL, 3 BR), front LED first

volatile uint32_t ledTestUntilMs = 0;  // Show LED layout: running until this millis() (0 = off)

void ledTestSet(bool on) { ledTestUntilMs = on ? (millis() + LED_TEST_MS) | 1 : 0; }  // |1: never exactly 0 when on
bool ledTestOn() { uint32_t u = ledTestUntilMs; return u != 0 && (int32_t)(u - millis()) > 0; }

void led(int array_num, bool on_off) {
  // move led commands into state array
  modeArray[array_num] = on_off;
}

// (plain r, g, b - not RGB: the Arduino IDE puts its own copies of function headers above where RGB is declared)
static void setPos(int mask, int k, uint8_t r, uint8_t g, uint8_t b) {
  for (int p = 0; p < LED_GROUPS; p++)
    if (mask & (1 << p)) ledPos[p][k] = { r, g, b };
}

void static_led(int mask, uint32_t color) {
  for (int k = 0; k < LED_PER_GROUP; k++) setPos(mask, k, color >> 16, color >> 8, color);
}

void fade_led(int mask, int freq, uint32_t colorA, uint32_t colorB) {
  uint8_t phase = (millis() / freq) & 0xFF;
  uint8_t mix = (phase < 128) ? phase * 2 : (255 - phase) * 2;
  uint8_t aR = colorA >> 16, aG = colorA >> 8, aB = colorA;
  uint8_t bR = colorB >> 16, bG = colorB >> 8, bB = colorB;
  uint8_t r = (aR * (255 - mix) + bR * mix) / 255, g = (aG * (255 - mix) + bG * mix) / 255,
          b = (aB * (255 - mix) + bB * mix) / 255;
  for (int k = 0; k < LED_PER_GROUP; k++) setPos(mask, k, r, g, b);
}

void rainbow_led(int mask, int freq) {
  // Hue derived from time, so every call (and every group) stays in sync; it steps along front to back
  uint8_t hue = millis() / freq;
  for (int p = 0; p < LED_GROUPS; p++) {
    if (!(mask & (1 << p))) continue;
    for (int k = 0; k < LED_PER_GROUP; k++) {
      uint8_t h = hue + (p * LED_PER_GROUP + k) * (256 / NUM_LEDS);
      // quick HSV(h,255,64) -> RGB
      uint8_t region = h / 43, rem = (h - region * 43) * 6;
      uint8_t v = 64, q = (v * (255 - rem)) >> 8, t = (v * rem) >> 8;
      RGB c;
      switch (region) {
        case 0: c = { v, t, 0 }; break;
        case 1: c = { q, v, 0 }; break;
        case 2: c = { 0, v, t }; break;
        case 3: c = { 0, q, v }; break;
        case 4: c = { t, 0, v }; break;
        default: c = { v, 0, q }; break;
      }
      ledPos[p][k] = c;
    }
  }
}

// Show LED layout: each position its own colour, front LED white, middle bright, back dim - so both where each group
// is and which way round it is can be seen at a glance
static void ledLayoutTest() {
  static const uint32_t col[LED_GROUPS] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFC000 };  // TL red, TR green, BL blue, BR yellow
  for (int p = 0; p < LED_GROUPS; p++) {
    uint8_t r = col[p] >> 16, g = col[p] >> 8, b = col[p];
    ledPos[p][0] = { 255, 255, 255 };
    ledPos[p][1] = { r, g, b };
    ledPos[p][2] = { (uint8_t)(r / 10), (uint8_t)(g / 10), (uint8_t)(b / 10) };
  }
}

// Positions -> the chain, using the LED Pos / Dir boxes. A group set to a position that another group also has shows
// the same thing; a group with no valid position stays dark.
static void mapLeds() {
  for (int grp = 0; grp < LED_GROUPS; grp++) {
    int pos = (int)lroundf(tuneVal[T_LED_POS + 2 * grp]) - 1;  // 1..4 -> 0..3
    bool backFirst = tuneVal[T_LED_POS + 2 * grp + 1] >= 0.5f;
    for (int k = 0; k < LED_PER_GROUP; k++) {
      int phys = grp * LED_PER_GROUP + (backFirst ? LED_PER_GROUP - 1 - k : k);
      leds[phys] = (pos >= 0 && pos < LED_GROUPS) ? ledPos[pos][k] : RGB{ 0, 0, 0 };
    }
  }
}

void showLeds();

void ledTask(void *pvParameters) {

  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    if (ledTestOn() && armSwitchHigh()) ledTestSet(false);  // arming ends the layout check
    // Which way up is only known with a working, levelled IMU. Without it, both faces show the same thing (no red
    // "down" face - it can't tell which one that is).
    bool upKnown = IMU_safe && imuLevelled && imuAlive();

    // ---- Priority 0: updating (overrides everything) ----
    if (otaInProgress) {
      fade_led(P_ALL, 2, 0xFFFFFF, 0x000000);

      // ---- layout check from the Tuning page (disarmed only) ----
    } else if (ledTestOn()) {
      ledLayoutTest();

      // ---- Priority 1: RC failsafe (overrides everything but an update) ----
    } else if (modeArray[4] == 1) {
      fade_led(P_ALL, 1, 0xFF0000, 0x000000);

      // ---- Priority 2: booting / pre-arm / safe ----
    } else if (modeArray[1] == 1 || modeArray[2] == 1 || modeArray[3] == 1 || modeArray[17] == 1) {
      if (modeArray[1] == 1) {  // booting
        fade_led(P_ALL, 5, 0x404500, 0xedff00);
      } else if (modeArray[2] == 1) {  // waiting pre arm
        fade_led(P_ALL, 5, 0x00ff00, 0x001c00);
      } else if (!upKnown) {  // vehicle safe, don't know which way up: rainbow all round
        rainbow_led(P_ALL, 2);
      } else if (modeArray[3] == 1) {  // vehicle safe, top up
        rainbow_led(P_TOP, 2);
        static_led(P_BOT, 0xFF0000);
      } else {  // vehicle safe, bottom up
        static_led(P_TOP, 0xFF0000);
        rainbow_led(P_BOT, 2);
      }

      // ---- Priority 3: IMU not booted ----
    } else if (modeArray[16] == 1) {
      fade_led(P_ALL, 1, 0xff00ff, 0xFF0000);

      // ---- Priority 4: auto up off ----
    } else if (modeArray[7] == 1) {
      fade_led(P_ALL, 5, 0xff00ff, 0x2e002e);

      // ---- Priority 5: driving modes: the face that's up shows the mode, the one that's down is all red ----
    } else {
      int up = -1, slow = 10;  // up: 0 top face up, 1 bottom face up
      uint32_t a = 0, b = 0;
      if (modeArray[8] == 1) { up = 0; slow = 10; a = 0x0059ff; b = 0x000c24; }         // stab off, fwd, top
      else if (modeArray[9] == 1) { up = 0; slow = 5; a = 0x0059ff; b = 0x000c24; }      // stab off, back, top
      else if (modeArray[10] == 1) { up = 1; slow = 10; a = 0x0059ff; b = 0x000c24; }    // stab off, fwd, bottom
      else if (modeArray[11] == 1) { up = 1; slow = 5; a = 0x0059ff; b = 0x000c24; }     // stab off, back, bottom
      else if (modeArray[12] == 1) { up = 0; slow = 10; a = 0xffffff; b = 0x2e2e2e; }    // stab on, fwd, top
      else if (modeArray[13] == 1) { up = 0; slow = 5; a = 0xffffff; b = 0x000000; }     // stab on, back, top
      else if (modeArray[14] == 1) { up = 1; slow = 10; a = 0xffffff; b = 0x2e2e2e; }    // stab on, fwd, bottom
      else if (modeArray[15] == 1) { up = 1; slow = 5; a = 0xffffff; b = 0x000000; }     // stab on, back, bottom
      if (up >= 0 && !upKnown) {  // IMU dropped out mid-run: the mode on both faces
        fade_led(P_ALL, slow, a, b);
      } else if (up == 0) {
        fade_led(P_TOP, slow, a, b);
        static_led(P_BOT, 0xFF0000);
      } else if (up == 1) {
        fade_led(P_BOT, slow, a, b);
        static_led(P_TOP, 0xFF0000);
      }
    }

    // ---- ESC lost / rebooting: blink orange on that ESC's side, over whatever is showing (not during an update
    //      or the layout check) ----
    if (!otaInProgress && !ledTestOn()) {
      bool on = (millis() / ESC_LED_BLINK_MS) & 1;
      for (int i = 0; i < NUM_ESC; i++) {  // the side follows what the ESC drives (ESC Role boxes)
        if (!escLedAlert(i)) continue;
        int role = escRole(i);
        if (role == ROLE_LEFT) static_led(P_LEFT, on ? ESC_LED_COLOR : 0x000000);
        else if (role == ROLE_RIGHT) static_led(P_RIGHT, on ? ESC_LED_COLOR : 0x000000);
      }
      // ---- low battery: a quick red flash on every LED, every 5 s at Low Volt down to every 0.5 s at Crit ----
      if (battLedFlash()) static_led(P_ALL, 0xFF0000);
    }

    mapLeds();
    showLeds();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(25));
  }
}

static const uint8_t lut[4] = {
  0b10001000,
  0b10001110,
  0b11101000,
  0b11101110
};





void showLeds() {
  uint8_t *p = spiBuf;
  for (int i = 0; i < NUM_LEDS; i++) {
    encodeByte(leds[i].g, p);
    p += 4;
    encodeByte(leds[i].r, p);
    p += 4;
    encodeByte(leds[i].b, p);
    p += 4;
  }
  memset(p, 0, RESET_BYTES);

  spi_transaction_t t = {};
  t.length = sizeof(spiBuf) * 8;
  t.tx_buffer = spiBuf;
  spi_device_transmit(ledDev, &t);
}

void encodeByte(uint8_t v, uint8_t *out) {
  out[0] = lut[(v >> 6) & 3];
  out[1] = lut[(v >> 4) & 3];
  out[2] = lut[(v >> 2) & 3];
  out[3] = lut[v & 3];
}

void ledInit() {
  spi_bus_config_t bus = {};
  bus.mosi_io_num = LED_PIN;
  bus.miso_io_num = -1;
  bus.sclk_io_num = 12;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = sizeof(spiBuf);
  spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = SPI_FREQ;
  dev.mode = 0;
  dev.spics_io_num = -1;
  dev.queue_size = 1;
  spi_bus_add_device(SPI3_HOST, &dev, &ledDev);
}