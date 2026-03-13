# ESPSomfy-RTS with Homekit

## Note for this fork
This code has been created to merge the [ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS) controller with Homekit. This should allow the device to connect directly with homekit and expose shades there.

NOTE: THIS HAS BEEN CREATED WITH THE HELP OF AI.

For all technical details like how to build the appropriate device, please refer to [ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS). This project has followed the same approach.

## Hardware

Tested on a **Seeed Studio XIAO ESP32-S3** with a **CC1101 433 MHz radio module**.

### CC1101 wiring (ESP32-S3)

| CC1101 pin | ESP32-S3 GPIO |
|------------|---------------|
| GDO0       | GPIO 3        |
| GDO2       | GPIO 4        |
| CSN        | GPIO 6        |
| SCK        | GPIO 7        |
| MISO       | GPIO 8        |
| MOSI       | GPIO 9        |
| VCC        | 3.3 V         |
| GND        | GND           |

## Build environment

This project targets **ESP-IDF v5.5** with **arduino-esp32 v3.x** embedded as a component (not the Arduino IDE).

```
idf.py build
idf.py -p /dev/cu.usbmodem<PORT> flash monitor
```

### CC1101 driver notes (arduino-esp32 v3.x)

The upstream [SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib) was written for arduino-esp32 v2.x. The copy in `components/CC1101/` contains three fixes required for v3.x:

1. `SPI.begin(..., -1)` — pass `-1` for the SS argument so the pin is not routed through the hardware CS peripheral, allowing `Reset()` to strobe it manually via `digitalWrite`.
2. `SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0))` added to `SpiStart()` — on v3.x `SPI.transfer()` without a prior `beginTransaction()` does not configure the hardware clock and hangs indefinitely.
3. `SpiEnd()` is called immediately after `Reset()` in `Init()`, before `RegConfigSettings()` — this prevents a mutex deadlock caused by `RegConfigSettings()` → `SpiWriteReg()` → `SpiStart()` → `beginTransaction()` trying to re-lock the non-reentrant SPI mutex while the outer `Init()` still holds it.