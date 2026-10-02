# Sources and notices

TheAurora application, UI, renderer, portal, scripts and flasher are newly written for this project. No ADS-B, radar, weather, GPS or image assets from capsule-radar are included.

## Board reference

Confirmed wiring, panel alignment and touch register protocols were checked against [socquique/capsule-radar](https://github.com/socquique/capsule-radar), at commit `d3dc05adcee455d0e41268e046b4f9817aee7f86`, specifically `src/boards/board_amoled_143.h`, `src/boards/board_amoled_175.h`, `src/touch_ft3168.cpp` and `src/touch_cst9217.cpp`. Our board headers express those same hardware facts. Both display interfaces use the vendor's 40 MHz rate. We include the reference project's MIT notice below for the board/protocol adaptations.

MIT License

Copyright (c) 2026 Quique Tortosa (socquique)

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

## Coastlines

`include/aurora/coastlines.h` is generated from [Natural Earth 1:110m coastline data](https://github.com/nvkelso/natural-earth-vector/blob/master/geojson/ne_110m_coastline.geojson), simplified and limited to northern latitudes using `tools/make_coastlines.py`. Natural Earth data is [public domain](https://www.naturalearthdata.com/about/terms-of-use/). It is independent of capsule-radar's assets.

## Data and libraries

NOAA SWPC supplies the public OVATION and planetary Kp data. Attribution links are in the README and flasher. Fixture timestamps are documented in `tests/fixtures/README.md`.

PlatformIO downloads the pinned libraries rather than vendoring them: LVGL (MIT), ArduinoJson (MIT), Arduino_GFX (its upstream license), and the pioarduino Arduino ESP32 framework/toolchain (upstream licenses). ESP Web Tools (Apache-2.0) is loaded by the flasher from its pinned public distribution. Refer to each installed dependency for its complete notices.
