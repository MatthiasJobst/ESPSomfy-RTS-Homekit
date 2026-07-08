# Third-Party Notices

This project's own source is released into the public domain under [The Unlicense](LICENSE).

That dedication applies **only to the original code in this repository**. The project
also bundles and distributes third-party components that remain under their own
licenses — these are **not** public domain. Their copyright and permission notices are
retained below, and the full license texts ship alongside each component
(`components/<name>/LICENSE*`, `managed_components/<name>/`).

## Web assets (`data/`)

| Component | License | Copyright | Source |
|---|---|---|---|
| Bootstrap Icons (SVG files in `data/`: house-fill, house-gear-fill, pencil-fill, trash-fill, sun, sun-fill, file-earmark-arrow-up) | MIT | Copyright (c) 2019-2024 The Bootstrap Authors | https://github.com/twbs/icons |
| QR Code Generator for JavaScript (`qrcode.min.js`) | MIT | Copyright (c) 2009 Kazuhiko Arase | http://www.d-project.com/ |
| Material Symbols (`settings_remote.svg`) | Apache-2.0 | Copyright Google LLC | https://github.com/google/material-design-icons |

## Firmware libraries (`components/`)

| Component | License | Copyright | Source |
|---|---|---|---|
| ArduinoJson | MIT | Copyright © 2014-2025 Benoit Blanchon | https://arduinojson.org |
| PubSubClient | MIT | Copyright (c) 2008-2020 Nicholas O'Leary | https://github.com/knolleary/pubsubclient |
| arduinoWebSockets | LGPL-2.1 | Copyright (c) Markus Sattler | https://github.com/Links2004/arduinoWebSockets |
| ELECHOUSE CC1101 driver | see component source headers | Copyright (c) 2010 Michael (ELECHOUSE) | https://github.com/LSatan/SmartRC-CC1101-Driver-Lib |
| esp-homekit-sdk | Apache-2.0 | Copyright (c) Espressif Systems | https://github.com/espressif/esp-homekit-sdk |

## Managed components (`managed_components/`, via the ESP-IDF Component Manager)

| Component | License | Copyright |
|---|---|---|
| arduino-esp32 | LGPL-2.1 | Espressif Systems |
| joltwallet/esp_littlefs (bundles `littlefs`) | MIT (esp_littlefs) / BSD-3-Clause (littlefs core) | Brian Pugh / Christopher Haster |
| Espressif ESP-IDF components (mdns, json_parser, libsodium, etc.) | Apache-2.0 unless otherwise noted in the component | Espressif Systems |

The MIT License (used by several components above) reads:

```
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

Refer to each component's bundled `LICENSE` file for the authoritative, full text of
its license (including the LGPL-2.1, Apache-2.0, and BSD-3-Clause terms).
