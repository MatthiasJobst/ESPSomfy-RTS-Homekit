# ESPSomfy-RTS with Homekit

[![CI](https://github.com/MatthiasJobst/ESPSomfy-RTS-Homekit/actions/workflows/ci.yaml/badge.svg)](https://github.com/MatthiasJobst/ESPSomfy-RTS-Homekit/actions/workflows/ci.yaml) [![Release](https://github.com/MatthiasJobst/ESPSomfy-RTS-Homekit/actions/workflows/release.yaml/badge.svg)](https://github.com/MatthiasJobst/ESPSomfy-RTS-Homekit/actions/workflows/release.yaml)

Current version: **v0.7.0**

## Note for this fork
This code has been created to merge the [ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS) controller with Homekit. This should allow the device to connect directly with homekit and expose shades there.

NOTE: THIS HAS BEEN CREATED WITH THE HELP OF AI.

For all technical details like how to build the appropriate hardware device, please refer to [ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS). This project has followed the same approach.

## Contributors and upstream projects

- **[rstrouse](https://github.com/rstrouse/ESPSomfy-RTS)** — original ESPSomfy-RTS firmware; this fork is based entirely on that work.
- **[Tsury](https://github.com/Tsury/ESPSomfy-RTS)** — RF noise detection, ported in v0.2.0
- **[Cjkas](https://github.com/cjkas/ESPSomfy-RTS)** - command execution queue, ported in v0.2.0.

## Hardware requirements

### Supported targets

| Target | Flash | Firmware binary | LittleFS image |
|--------|-------|-----------------|----------------|
| ESP32-S3 | 8 MB | `SomfyController.esp32s3.bin` | `SomfyController.littlefs.bin` |
| ESP32 | 4 MB | `SomfyController.esp32.bin` | `SomfyController.littlefs.esp32.bin` |

The ESP32-S3 build uses 3 MB OTA slots. The ESP32 build uses the release configuration (`sdkconfig.release`, optimization `-Os`) with 1.5 MB OTA slots to fit within 4 MB flash — the debug build is too large for a 4 MB device.

## Why I built this

### Merging [ESPSomfyRTS](https://github.com/rstrouse/ESPSomfy-RTS) with [ESP HomeKit SDK](https://github.com/espressif/esp-homekit-sdk)

We live in an apartment building with centralised control of the window blinds — four motors driven by Animeo IB+ controllers. Integrating those into a smart home system had resisted earlier attempts with Shelly and similar components.

The SOMFY RTS protocol turned out to be the key. A Tahoma bridge brought the shades into HomeKit, but it operated all four motors as a group. What I needed was per-shade control, and that is exactly what ESPSomfy-RTS provides: it emulates individual remotes (each motor controller accepts up to 20), so every shade can be addressed separately. RTS is a one-way protocol, so there is no position feedback, but for my use case that was an acceptable trade-off.

ESPSomfy-RTS alone would have allowed shade control through its web interface, but I wanted a proper HomeKit integration — no homebridge, no intermediate service. That is when I asked GitHub Copilot to come up with a plan to merge the two codebases directly.

#### Moving ESPSomfyRTS to ESP-IDF Toolchain

The latest version of the ESPSomfyRTS relies on features of the [Arduino 1.8](https://www.arduino.cc/en/software/) environment. It took me some time to set it up, mostly because I have not used it extensively before. Nevertheless I was inclined to switch to [Espressif Framework](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/get-started/index.html) because that is newer and still maintained. I settled on 5.5.3 and later 5.5.4, as the current version 6.0 was not released when I started.

My development takes place on MacOS, therefore those instructions [Installation of ESP-IDF and Tools on macOS](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/macos-setup.html#installation-of-esp-idf-and-tools-on-macos) were relevant. (I used Homebrew and ran the CLI install using EIM.)
Now I was able to [Build from source](https://github.com/MatthiasJobst/ESPSomfy-RTS-Homekit/wiki#building-from-source).

At this point I used an ESP32-S3+ that has 16MB of flash. It was not clear whether the code would fit into the smaller flash of the ESP32 that was the initial hardware setup. The 16Mb was too much, the 8MB were nevertheless necessary. (The 4 MB variant came later.)

I tested the code with ESP-IDF tools and had some things that still needed fixing after the build passed.

#### Setting up HomeKit

The HomeKit SDK does not only come with code it also requires certain flash areas. I created a dedicated HomeKit module for a clean separation of concerns. The start order of the network was not made for this, so as a first step homekit was started with the network startup. (That was corrected later, but I wanted to see whether this very straightforward approach worked.)

Well, it worked. I started the device, and the device showed up as a bridge in my Home App. What I had expected to take at least a week was achieved in a couple of days. 

Now some clean-up was necessary, but basically it was about removing some remains of the Arduino environment, creating my own version number.

The user interface in the web browser needed some extension to handle the HomeKit things, like QR-code. And bug fixing continued, as with continued use and testing, issues surfaced. 

##### Reducing complexity

I was quite happy and impressed having come so far. But since I used AI for many changes, the code, which wasn't mine to begin with, felt complex and overwhelming.

The first move was straightforward: reorganise the file structure and split `Somfy.cpp` (over 5 kloc) into five modules. That helped, but did not meaningfully reduce complexity.

The real insight came from [Lizard](https://pypi.org/project/lizard/): `Web::begin` had a [cyclomatic complexity](https://en.wikipedia.org/wiki/Cyclomatic_complexity) of 325, caused by a switch statement that contained most handler logic inline. Refactoring it into separate handler functions — a task well-suited to an AI agent — brought that number down dramatically and turned `web.cpp` into a set of focused components.

A further pass applied the [DNRY](https://en.wikipedia.org/wiki/Don't_repeat_yourself) principle and removed unused code paths such as [CORS](https://de.wikipedia.org/wiki/Cross-Origin_Resource_Sharing) handling. On the front-end side, the JavaScript was split into smaller files while keeping the single-load principle: the device serves everything upfront, which suits embedded hardware where client round-trips are expensive.

Switching to ESP-IDF logging was the last step. Arduino's `Serial.printf` output is invisible in the IDF terminal monitor, so moving to `ESP_LOGI`/`ESP_LOGE` made debugging significantly more practical.

At this point it felt ready for further development and that there was no need to roll back and the Homekit-Merge was ready.

### Adding unit tests and refactoring SomfyShade

At this point SomfyShade was still a very large file with more than 2 kloc. The necessity to make this more manageable seemed clear. Clear was as well that this was critical functionality which requires a certain care to refactoring. 
I decided to use [GoogleTest](https://google.github.io/googletest/) for a test suite that would prevent the refactoring from degrading. GoogleTest was chosen because the code base was in C++ and it provides extensive mock and stub capabilities. 
ESP-IDF comes with the [Unity](https://github.com/ThrowTheSwitch/Unity) framework, but that is focusing on C code with embedded toolchains.

The unit tests reside in `test/unit/` with extensive stubs in `test/unit/stubs`. 

With this setup SomfyShade was moved to its own subdirectory `main/somfy`. The refactoring followed the pattern to first write a test for the code that would be refactored, then refactor and ensure the tests still pass.
The goal was to reach a 100% line coverage, which seems like a good enough metric for a somewhat hobbyist project. (The tools used here were [`gcov`](https://gcc.gnu.org/onlinedocs/gcc/Gcov.html) and [`gcovr`](https://gcovr.com/en/stable/))
SomfyShade contains a large switch case and the first step was to extract code and remove duplicates. Looking at the code revealed that many similar actions were performed with nearly identical code. That is obviously a great target for simplification and refactoring.
When following the steps of the refactoring this made a lot of sense because for example, the My case became a function called `processMyCommand`.

This prepared the ground for the next stage: extracting functionality from `SomfyShade` into classes with clear, single responsibilities. It started with `SomfyFlagManager`.

The evolution of the test coverage was such that it started small on `SomfyShade` as only parts of the code were tested based on the refactoring process. But when a threshold was passed it made sense to extend the coverage to those parts that were not refactored eventually bringing the whole `somfy` subdirectory to almost 100% coverage.

This step ended with 635 test cases, a line coverage of 99.9%, 100% function coverage and 74.5% branch coverage.

Most of this was supported by AI, I moved to Claude Code, mostly Sonnet, at this point. This can be nicely done with AI because it is fairly easy to control those steps:
1. Writing unit tests with 100% line coverage.
2. Refactoring and ensuring that the tests pass.

Sometimes it was necessary to make changes to the code or the stubs to make tests pass again, but those changes were minor and could be easily reviewed.

### Porting features from other branches

Several forks of [ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS) had accumulated useful fixes independently of the main repo. Rather than letting those diverge further, I ported fixes from two of them: RF noise detection from Tsury and a command execution queue from Cjkas. Both are credited in the Contributors section above.

### Tidying the code

At this point the major reductions to complexity were done. Still for some functions the cyclomatic complexity was still above 20, but it felt unnecessary to reduce that when those functions were doing their job. 
The code was in manageable sizes and it was time for static code analysis tools, i.e. [cppcheck](http://cppcheck.net) and [clang-tidy](https://clang.llvm.org/extra/clang-tidy/).

The findings from cppcheck were small and those fixes were applied. 

Clang-tidy comes with ESP-IDF, but proved to be a different game. Simply running it, overflows the screen. It needs some ways to limit its output as well some filters to run only on the files that one cares about.
There are a lot of files in `components` and in `managed_components` that produce findings. Those are the concerns of other developers and fixing them appeared to have no major value. Still there were a lot of findings when running it the first time. So I was not sure whether that would be doable.

The most evident warnings were complexity warnings, so I tried tackling them on the `SomfyFlagManager` as all those sunny and windy flags appeared to be lending itself well to some abstraction.

Initially it was not the goal to remove all clang-tidy findings, but that was how it eventually ended. Ensuring test coverage and having the guidance of the tests helped making changes even when working on areas that I could not test on the device with my setup.

### Watchdog

Watchdog resets had been scattered defensively across the codebase. Since the 7 s timeout only triggers when a task genuinely stalls, a single reset point at the bottom of the main loop is sufficient — each subsystem call (web, HomeKit, etc.) completes well within that window.

The one exception is `GitOTA`, where downloading and flashing firmware can legitimately take longer; those resets remain in place. Auditing this also prompted a closer look at the transmission algorithm, which turned out to be timing-sensitive enough to warrant moving to a dedicated hardware peripheral.

### Using [RMT](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/rmt.html) peripheral for transceiver

This change is based on an AI suggestion. It makes sense to use a peripheral as it eases the load of the CPU and it has less interference from other parts of the code. 
First the code was implemented using an `ifdef`, but that was later removed to become the only implementation.
Only the transceiver part was used, mainly because reception does not play the same crucial role. RTS is a one-way transmission anyway. I keep considering extending the RMT approach to reception as well, though proper testing would require hardware I don't have in my setup.

Transmission testing was straightforward as the shades moved or not. 

### Removing unused protocol components

The original firmware targeted a Home Assistant environment and included SSDP for device discovery. That serves no purpose in a HomeKit context where mDNS handles discovery, so SSDP was removed.

MQTT was tempting to remove as well since I don't use it, but it is genuinely useful for others and removing it would be a breaking change for anyone who has it configured. So it remains.

### Improving the transmission

