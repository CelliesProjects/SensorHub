# SensorHub

This [PlatformIO](https://platformio.org/) repository contains the code for the sensor board that belongs with [CO<sub>2</sub> display esp32](https://github.com/CelliesProjects/co2-display-esp32) and is useless on its own.

![SAM_3247](https://github.com/user-attachments/assets/331d3980-0abc-4c27-8e4d-9d70900f45ac)

The hardware part consist of a esp32 with a [sht31 temperature/humidity sensor](https://www.adafruit.com/product/2857) plus a [SenseAir S8 CO<sub>2</sub> sensor](https://senseair.com/product/s8/).<br>
You can use any esp32 to run this app. Configured compile targets are esp32-s2 and esp32-c3.

## Installation

1.  Clone this repository.
```bash
git clone https://github.com/CelliesProjects/SensorHub.git
```
 2.  Open the install folder in PlatformIO and add a `wifiSecrets.h` file to the `src` folder with the following content:<br>
 ```c++
#ifndef _WIFI_SECRET_
#define _WIFI_SECRET_

#define SSID "wifi network"
#define PSK "wifi password"

#endif
```
3.  In `platformio.ini` adjust `NTP_POOL` to [your countries iso code](https://www.iban.com/country-codes) and `TIMEZONE` to [your local time zone](https://remotemonitoringsystems.ca/time-zone-abbreviations.php).
4.  Build and upload the firmware to the esp32.

## Done!

The sensorHub will now be found automagically by the CO<sub>2</sub> display.

## About

This app will also run on a ancient plain esp32 with the psram issue as it only uses normal ram.

All sensor reading, averaging and history keeping are done on this board and pushed to the client over websocket.

The reason the sensors are on a separate board are the power requirements of the CO<sub>2</sub> sensor which uses 300mA for some milliseconds every measurement. This caused the tft display to flicker. This issue was solved by putting the sensors on a separate board.

### License

MIT License

Copyright (c) 2024 Cellie

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

