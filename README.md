# BlueWhale
Whale, now dual core and wifi4 enabled!

Takes CRSF Rx input / transmits CRSF telem back to Tx.

-only basic four telem currently, repurposing data slots for voltage and temprature for two esc's.

Bidirectional Dshot with extended telem, current voltage and RPM being the key ones.

-tested with two esc's, but enough RMT's to support four in theory.

Yee old faithful MPU6050 IMU with DMP running.

-DMP active for off-board motion processing to get orientation, also pulling gyro data for future active stabilization.

Addressable one wire LED control.

-currently driving 12 RGBW pixels for status feedback

Wifi!

-hosting a simple web gui, end goal is full telemetry and interface for tuning and param updates
