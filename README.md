# SensorHub

This repository is a part of [co2 display esp32](https://github.com/CelliesProjects/co2-display-esp32) and is useless on its own.

![SAM_3247](https://github.com/user-attachments/assets/331d3980-0abc-4c27-8e4d-9d70900f45ac)

The hardware part consist of a esp32 with a [sht31 temperature/humidity sensor](https://www.adafruit.com/product/2857) plus a [SenseAir S8 co2 sensor](https://senseair.com/product/s8/).
You can use any esp32 to run this app. Configured compile targets are esp32-s2 and esp32-c3. 

This app will also run on a ancient plain esp32 with the psram issue as it only uses normal ram.

The reason the sensors are on a separate board are the power requirements of the co2 sensor which uses 300mA for some milliseconds every measurement. This causes the display to flicker. This issue was solved by putting the sensors on a separate board.


