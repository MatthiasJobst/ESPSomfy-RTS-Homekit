# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

ESPSomfy-RTS fork targeting **ESP32-S3 (8 MB flash)** with HomeKit integration. Controls Somfy RTS motorised blinds via a CC1101 RF transceiver. Built on **ESP-IDF v5.5** with **arduino-esp32 v3.x** embedded as a managed component — not the Arduino IDE. `app_main()` calls `initArduino()` explicitly so Arduino APIs work without the Arduino task scheduler.

## Firmware build commands

```bash
idf.py build
idf.py -p /dev/cu.usbmodem<PORT> flash monitor

# Static analysis (clang-tidy, run from repo root)
idf.py clang-check \
  --exclude-paths components \
  --run-clang-tidy-options '-header-filter=.*/SomfyController/main/.*'
```

## Unit tests

Tests live in `test/unit/` and run on the host (macOS/Linux) using GoogleTest + gmock. They compile only the `main/somfy/` files; everything else is stubbed.

```bash
cd test/unit

# Configure (first time or after CMakeLists changes)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -S .

# Build and run all tests
cmake --build build --parallel && ./build/shade_tests

# Run a single test suite
./build/shade_tests --gtest_filter='CommandTransmitterTest.*'

# Coverage report (--coverage is already in CMakeLists)
cmake --build build --parallel && ./build/shade_tests
gcovr --object-directory build --filter '.*main/somfy/'
```

Coverage as of last run: **88%** overall. `SomfyGroup.cpp` and `SomfyRoom.cpp` have 0% coverage — no tests exist for them yet.

## Architecture

### Task structure

`app_main()` is a thin trampoline that spawns `mainLoop` as a FreeRTOS task (8 KB stack). All initialisation and the poll loop run there. The task is registered with the **task watchdog at 7 s**. `esp_task_wdt_reset()` is called at the bottom of every loop iteration. Any blocking call on this task that takes >7 s triggers a panic — this is the most common cause of crashes.

**Critical:** `WebServer` (Arduino synchronous) runs handlers on the calling task (app_main). `WebSocketsServer::broadcastTXT()` calls `NetworkClient::write()` which uses `select()` in a retry loop — max blocking is `WIFI_CLIENT_MAX_WRITE_RETRY × WIFI_CLIENT_SELECT_TIMEOUT_US`. This value has been reduced from 1 s to 100 ms in `managed_components/espressif__arduino-esp32/libraries/Network/src/NetworkClient.cpp`.

### RF transmission (RMT)

Frame transmission uses the ESP-IDF v5 RMT TX peripheral (`SOMFY_TX_RMT` defined). Key behaviour:
- `rmt_transmit()` is called with `queue_nonblocking = 1` — if the TX queue is full it returns an error instead of blocking with `portMAX_DELAY`.
- `sendFrame()` and `repeatFrame()` skip silently if TX is busy, rather than spin-waiting.
- `endTransmit()` is deferred: a done-callback sets `s_rmtBusy = false`, and `Transceiver::loop()` calls `endTransmit()` (re-enables RX) when it sees the flag clear.

### Shade object model

`SomfyShadeController` (global `somfy`) owns arrays of `SomfyShade`, `SomfyRoom`, `SomfyGroup`. Each `SomfyShade` extends `SomfyRemote` (rolling-code address + send logic) and aggregates sub-objects set up via back-pointer in the constructor:

| Sub-object | Responsibility |
|---|---|
| `SomfyCommandTransmitter` | `linkRemote` / `unlinkRemote`, command dispatch |
| `SomfyCommandProcessor` | `processFrame()` — inbound frame → shade state |
| `SomfyMovementTracker` | Timed position tracking during movement |
| `SomfyTargetSequencer` | Multi-step move-to-target sequences |
| `SomfyMQTTPublisher` | MQTT state publishing |
| `SomfyJSONSerializer` | `toJSON()` / `fromJSON()` |
| `SomfyFlagManager` | Sun/wind/light sensor flags |
| `SomfyGPIOControl` | GPIO relay outputs |

### Persistence

All shade configuration is stored in **LittleFS** as `shades.cfg` — a versioned binary record file read/written via `ShadeConfigFile` (inherits `ConfigFile`). Rolling codes are stored in **NVS** under namespace `ShadeCodes` (key = remote address, value = `uint16_t`). Other settings (WiFi, MQTT, transceiver, IP) also use NVS namespaces (`WIFI`, `MQTT`, `CC1101`, etc.). The old per-shade NVS store has been fully removed.

### Web layer

HTTP on port 80 (`WebServer`), WebSocket on port 8080 (`WebSocketsServer`), REST API on port 8081. Handlers are split across `web/Web*.cpp` files by domain (shades, groups, rooms, settings, OTA). All state updates push JSON events over the WebSocket using `SocketEmitter` / `JsonSockEvent` / `WResp`.

### Unit test stubs

`test/unit/stubs/` provides host-compilable replacements for Arduino, ESP-IDF, and heavy project headers. `globals_stub.cpp` provides stub implementations of `SomfyShadeController` methods not under test, and the NVS in-memory store (`nvs_ns_stores`). When adding a new method to `SomfyShadeController`, add a stub implementation there too.

## Key files

| File | Purpose |
|---|---|
| `main/app_main.cpp` | Entry point, FreeRTOS task setup, WDT registration, main poll loop |
| `main/somfy/SomfyShadeController.cpp` | `begin()`, `loop()`, `processFrame()`, `sendFrame()`, shade/group/room lifecycle |
| `main/somfy/SomfyTransceiver.cpp` | CC1101 driver, RMT TX encoder, RX ISR, frequency scanning |
| `main/somfy/SomfyRemote.cpp` | Rolling code generation, `sendCommand()`, `repeatFrame()` |
| `main/ShadeConfigFile.cpp` | Binary config file read/write for all shade/room/group/settings records |
| `main/ConfigSettings.cpp` | NVS-backed settings (WiFi, MQTT, network, security) |
| `main/web/Web.cpp` | HTTP + WebSocket server loop, URL routing |

## CC1101 driver notes

The `components/CC1101/` copy has three fixes for arduino-esp32 v3.x:
1. `SPI.begin(..., -1)` — prevents hardware CS routing so `Reset()` can strobe it manually.
2. `SPI.beginTransaction(...)` added to `SpiStart()` — without it `SPI.transfer()` hangs on v3.x.
3. `SpiEnd()` called before `RegConfigSettings()` in `Init()` — prevents SPI mutex deadlock.
