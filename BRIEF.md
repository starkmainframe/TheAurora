# TheAurora

Round aurora forecast for the Waveshare **ESP32-S3-Touch-AMOLED-1.43** (466×466 CO5300, FT3168 touch). Visual cousin of [capsule-radar](https://github.com/socquique/capsule-radar): browser web flasher, captive WiFi setup, on-device config page.

Owner: Christian Reese (`starkmainframe`). Built for home use (Schenefeld / wherever the phone sets the pin).

## Product

North-hemisphere NOAA OVATION aurora, drawn as a circular Earth that fills the round panel (same look as the SWPC north animation still: green oval over the pole, coasts faint, night-side readable).

### Views (swipe)

1. **Globe** (home): northern hemisphere, polar-ish view, aurora energy as green→yellow→red, location pin, HUD with Kp + local probability.
2. **Kp**: current planetary K-index, 3-hour bars for the last day, simple “quiet / unsettled / storm” label.
3. **Here**: probability at the saved coordinates (big %), lat/lon, forecast time, “worth going outside” hint from Kp + local %.

Long-press optional later; v1 is swipe only.

### Config (same pattern as capsule-radar)

- First boot: captive AP `TheAurora-Setup`.
- Then `http://theaurora.local/` (mDNS) on the home WiFi.
- Fields: SSID reset, **latitude / longitude** (manual), **“Use this phone’s location”** (browser geolocation, one tap, saved on device), brightness, poll interval (default 15 min).
- Persist in NVS. No account, no cloud of our own.

### Data (NOAA SWPC, no key)

- Grid: `https://services.swpc.noaa.gov/json/ovation_aurora_latest.json`
  - Format: `{ Observation Time, Forecast Time, Data Format: "[Longitude, Latitude, Aurora]", coordinates: [[lon, lat, energy], ...] }`
  - Grid is 1°. Energy is approximate deposition (the color bar on the SWPC still, roughly 0–4+). Render **northern latitudes only** (lat ≥ 0, or ≥ 40 to save RAM if needed).
  - Do **not** download `images/animations/ovation/north/latest.jpg` on device (too heavy). Draw the grid ourselves so the pin and local % stay consistent.
- Kp: `https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json` (array of `{time_tag, Kp, a_running, station_count}`).
- Local probability: nearest grid cell(s) to saved lat/lon (bilinear if cheap). Show that number on Globe HUD and Here view.
- Cache last good payload; if WiFi fails, keep last frame and mark stale.

### Hardware (v1 only 1.43)

Reuse capsule-radar’s confirmed 1.43 pin map (do not guess):

| | 1.43 |
|---|---|
| Env | `esp32-s3-amoled-143` |
| QSPI CS / SCLK / D0–D3 | 9 / 10 / 11,12,13,14 |
| LCD reset | 21 |
| I2C SDA / SCL | 47 / 48 |
| Touch | FT3168 @ 0x38 |

No battery gauge / no speaker on 1.43 (capsule-radar notes). 1.75 can wait.

Reference firmware (ideas + pins, **do not copy their ADS-B code or assets wholesale**): https://github.com/socquique/capsule-radar — MIT, keep notices if any snippet is reused. Prefer a clean project that mirrors their *shape*: PlatformIO, LVGL, board header, captive portal, config page, ESP Web Tools flasher, GitHub Actions pages.

### Web flasher

ESP Web Tools, Chrome/Edge, board picker can be a single 1.43 image for v1. GitHub Pages via Actions on `main` (same idea as capsule-radar `.github/workflows/webflasher.yml`). README: enable Pages → GitHub Actions once.

### Look

Dark, round, coastlines subtle, aurora glow (not a flat heatmap blob). Clock/Kp small so the Earth stays the hero. Match the attached SWPC still the user sent (green oval, NOAA-like, not a cartoon).

### Out of scope for v1

Southern hemisphere, GPS module, audio alerts, 1.75 board, 3D case.

## Done when

- `pio run -e esp32-s3-amoled-143` builds.
- Desktop/native sim or a documented headless render test for the globe from a saved JSON fixture.
- README: flash (USB + web flasher), WiFi setup, set location from phone, swipe map.
- Pushed to `main` on this repo.
- Vier-Augen (Grok reviewer, not Astra) before calling it done.

## Closeout

When done, paused, blocked, or Vier-Augen lands: **priority-true** message back to Jarvis. Do not wait for Christian to open Herdr.
