# CRSFReceiver (STM32 + Arduino)

Reads CRSF (ExpressLRS / TBS Crossfire) RC channels and link stats over a hardware UART.

## Wiring
Receiver TX -> MCU RX, Receiver RX -> MCU TX, GND, 5 V (or 3.3 V, per receiver). 420000 baud, 8N1, non-inverted.

## Usage
```cpp
HardwareSerial crsfSerial(PA10, PA9);   // RX, TX
CRSFReceiver crsf(crsfSerial);
void setup(){ crsf.begin(); }
void loop(){ if (crsf.update()) { uint16_t us = crsf.getChannelUs(0); } }
```

Single-wire (half duplex) receivers: `crsf.begin(420000, true)` and give the Serial object only the TX pin.

## Notes
- Call `update()` every loop; avoid blocking delays longer than ~5 ms (default RX buffer is small).
- Channel range: raw 172..1811, `getChannelUs()` gives ~988..2012 us.
- `isFailsafe()` is true if no RC frame in 500 ms (change with `setFailsafeTimeout()`).
