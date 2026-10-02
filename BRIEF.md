# TheAurora

Round aurora forecast for Waveshare **ESP32-S3-Touch-AMOLED-1.43** and **1.75** (round AMOLED + touch). Visual cousin of [capsule-radar](https://github.com/socquique/capsule-radar): browser web flasher, captive WiFi setup, on-device config page.

Owner: Christian Reese (`starkmainframe`). Built for home use (Schenefeld / wherever the phone sets the pin).

## Product

North-hemisphere NOAA OVATION aurora, drawn as a circular Earth that fills the round panel (same look as the SWPC north animation still: green oval over the pole, coasts faint, night-side readable).

### Views (swipe)

1. **Globe** (home): northern hemisphere, polar-ish view, aurora energy as green→yellow→red, location pin, HUD with Kp + local probability.
2. **Kp**: current planetary K-index, 3-hour bars for the last day, simple “quiet / unsettled / storm” label.
3. **Here**: probability at the saved coordinates (big %, lat/lon, forecast time, “worth going outside” hint from Kp + local %.

Long-press optional later; v1 is swipe only.

### Config (same pattern as capsule-radar)

- First boot: captive AP `TheAurora-Setup`.
- Then `http://theaurora.local/` (mDNS) on the home WiFi.
- Fields: SSID reset, **latitude / longitude** (manual), **“Use this phone’s location”** (browser geolocation, one tap, saved on device), brightness, poll interval (default 15 min).
- Persist in NVS. No account, no cloud of our own.

### Data (NOAA SWPC, no key)

- Grid: `https://services.swpc.noaa.gov/json/ovation_aurora_latest.json`
  - Format: `{ Observation Time, Forecast Time, Data Format: "[Longitude, Latitude, Aurora]", coordinates: [[lon, lat, energy], ...] }`
  - Grid is 1°. The JSON Aurora value uses a 0–100 intensity/probability scale; do not rescale it as 0–4 energy. Render **northern latitudes only** (lat ≥ 0).
  - Do **not** download `images/animations/ovation/north/latest.jpg` on device (too heavy). Draw the grid ourselves so the pin and local % stay consistent.
- Kp: `https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json` (array of `{time_tag, Kp, a_running, station_count}`; also accept the header-row table format).
- Local probability: nearest grid cell(s) to saved lat/lon (bilinear if cheap). Show that number on Globe HUD and Here view.
- Cache last good payload; if WiFi fails, keep last frame and mark stale.

### Hardware (1.43 and 1.75)

Both Waveshare ESP32-S3 round AMOLED boards, same pattern as capsule-radar: separate PlatformIO envs + web-flasher board picker. Reuse capsule-radar’s confirmed pin maps (do not guess).

| | 1.43 | 1.75 |
|---|---|---|
| Env | `esp32-s3-amoled-143` | `esp32-s3-amoled-175` |
| Panel | 466×466 CO5300 | 466×466 CO5300 |
| Touch | FT3168 @ 0x38 | CST9217 @ 0x5A |
| QSPI CS / SCLK / D0–D3 | 9 / 10 / 11,12,13,14 | 12 / 38 / 4,5,6,7 |
| LCD reset | 21 | 39 |
| I2C SDA / SCL | 47 / 48 | 15 / 14 |
| Touch reset / interrupt | shared LCD reset / none | 40 / 11 |
| Touch mirror X/Y | no / no | yes / yes |

Battery reporting and audio remain out of scope on both boards. The 1.43 has no speaker or PMIC. Use separate firmware images; the pin maps are not interchangeable.

Reference firmware (ideas + pins, **do not copy their ADS-B code or assets wholesale**): https://github.com/socquique/capsule-radar — MIT, keep notices if any snippet is reused. Prefer a clean project that mirrors their *shape*: PlatformIO, LVGL, board header, captive portal, config page, ESP Web Tools flasher, GitHub Actions pages.

### Web flasher

ESP Web Tools, Chrome/Edge, **board picker with 1.43″ and 1.75″** (like capsule-radar). Build both envs in CI; GitHub Pages via Actions on `main`. README: enable Pages → GitHub Actions once.

### Look

Dark, round, coastlines subtle, aurora glow (not a flat heatmap blob). Clock/Kp small so the Earth stays the hero. Match the attached SWPC still the user sent (green oval, NOAA-like, not a cartoon).

### Out of scope for v1

Southern hemisphere, GPS module, audio alerts, battery reporting, 3D case.

## Done when

- `pio run -e esp32-s3-amoled-143` and `-e esp32-s3-amoled-175` both build.
- Desktop/native sim or a documented headless render test for the globe from a saved JSON fixture.
- README: flash (USB + web flasher), WiFi setup, set location from phone, swipe map.
- Separate Vier-Augen by Grok after the builder’s green build/tests, before pushing `main`.
- Pushed to `main` on this repo after that review passes.

## Closeout

When done, paused, blocked, or Vier-Augen lands: **priority-true** message back to Jarvis. Do not wait for Christian to open Herdr.
