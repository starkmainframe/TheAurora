# TheAurora

A northern aurora globe for the **Waveshare ESP32-S3-Touch-AMOLED-1.43 and 1.75**. Both are 466×466 CO5300 displays; they need different firmware images.

TheAurora draws NOAA’s OVATION JSON grid on the ESP32, adds faint coastlines and your location, and lets you swipe through **Globe → Kp → Here**. No account or API key. Default polling: 15 minutes. Firmware, config page and flasher are original code; [capsule-radar](https://github.com/socquique/capsule-radar) supplied the architectural inspiration and confirmed board wiring.

[Open the web flasher](https://starkmainframe.github.io/TheAurora/flash/) · [Product brief](BRIEF.md) · [Credits](THIRD_PARTY.md)

## Supported boards

| | 1.43″ | 1.75″ |
|---|---|---|
| PlatformIO environment | `esp32-s3-amoled-143` | `esp32-s3-amoled-175` |
| Touch | FT3168, `0x38` | CST9217, `0x5A` |
| QSPI CS / clock / D0–D3 | 9 / 10 / 11,12,13,14 | 12 / 38 / 4,5,6,7 |
| LCD reset | 21 | 39 |
| I²C SDA / SCL | 47 / 48 | 15 / 14 |
| Touch reset / interrupt | shared LCD reset / none | 40 / 11 |
| Touch mirror X / Y | no / no | yes / yes |

Both builds use 16 MB flash, 8 MB OPI PSRAM, a 6-column panel offset, and 40 MHz QSPI. Board headers live in `include/boards/`. The 1.75’s display uses its always-on supply; no PMIC library is needed for USB-powered operation. Battery reporting, audio, GPS, the southern hemisphere and other board models are outside v1.

**Validation status:** both firmware environments compile, the shared renderer/parsers pass host tests, and both merged images are verified. Physical display alignment, swipes, captive portal behavior, TLS on-device and NVS persistence still need testing on the two actual boards. Separate Grok Vier-Augen review is required before pushing this builder’s changes to `main`.

## Flash from the browser

1. Open the [HTTPS flasher](https://starkmainframe.github.io/TheAurora/flash/) in desktop Chrome or Edge. Firefox, Safari and most mobile browsers do not provide the required Web Serial support.
2. Connect the board with a **USB data cable**. Close serial monitors, select the exact **1.43** or **1.75** model in the picker, then choose **Connect & install**.
3. Select the USB serial port and install. The chip identifier is the same for both boards, so automatic ESP32-S3 detection cannot choose the correct pin map for you.
4. Press RESET after installation if the device stays in its bootloader.

If no port appears, hold **BOOT**, tap **RESET**, then release BOOT and reconnect. Check the cable if the port is still absent. Web images are merged at address `0x0`; they include bootloader, partition table, OTA initialization and the application. Installing a merged image resets NVS (WiFi and settings). Selecting full erase also clears the forecast cache.

The flasher becomes available after Pages is enabled and the first successful Actions deployment.

## Build and flash over USB

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html). The project pins pioarduino `53.03.13` (Arduino ESP32 3.1.3), Arduino_GFX 1.6.4, LVGL 8.4.0 and ArduinoJson 7.4.2. Dependencies download on the first build.

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e esp32-s3-amoled-143
pio run -e esp32-s3-amoled-175
# Upload only the environment matching your board:
pio run -e esp32-s3-amoled-143 -t upload
# Or: pio run -e esp32-s3-amoled-175 -t upload
pio device monitor -b 115200
```

Use `--upload-port /dev/ttyACM0` (or your actual port) when multiple devices are attached. PlatformIO upload normally preserves NVS and the cache. Switching from firmware with a different partition layout may require `pio run -e <your-env> -t erase` first. Erasing resets all device data.

## WiFi and configuration

1. On first boot, join the open **TheAurora-Setup** network from your phone or laptop. Stay connected if your phone warns that it has no internet.
2. Open the captive setup page, or go to **http://192.168.4.1/** manually. Enter your **2.4 GHz** home SSID and password. An empty password is supported for open networks.
3. Rejoin your home WiFi and open **http://theaurora.local/**. If mDNS is unavailable, use the IP printed on the setup screen/serial monitor or listed by your router.
4. Save latitude (0–90° N), longitude (−180–180°), brightness (5–255), and polling interval (5–120 minutes). The initial location is Schenefeld, **53.60 N, 9.82 E**; set your own coordinates.

Settings and WiFi credentials are saved in NVS. The password is never returned by the configuration API. **Forget WiFi & open setup** clears credentials while retaining location and display settings. If connection fails for 30 seconds, the setup AP reopens automatically; it closes 15 seconds after home WiFi connects. Holding BOOT while starting the application also opens setup.

Configuration is intended for your trusted local network. The setup AP is open for onboarding, and the settings page uses HTTP. Settings writes require a per-boot request token. There is no remote account or internet-facing configuration service.

### Use this phone’s location

On your home WiFi, open the device settings and tap **Use this phone’s location**. Browsers require a [secure context for geolocation](https://developer.mozilla.org/en-US/docs/Web/API/Geolocation_API), so the HTTP device page opens a small HTTPS helper on this project’s GitHub Pages site. Tap the location button there, allow access, then tap **Save on my display**. The browser returns to the device and saves the coordinates in NVS.

Coordinates and the request token travel in URL fragments, which are not sent to the Pages server. The helper makes no analytics or location-upload requests. It can return only to `theaurora.local` or a private IPv4 address. Keep the display powered on during the handoff; rebooting expires the token. If location permission, internet access, Pages or local-device navigation is unavailable, copy the coordinates shown by the helper or enter them manually. Captive AP mode generally has no internet, so finish WiFi setup first.

## Swipe views

- **Globe:** north polar view, green → yellow → red aurora intensity, Natural Earth coastlines, approximate solar shading, a warm location pin, Kp and local percentage. Your longitude is at the bottom of the globe. The Earth fills the round display.
- **Kp:** latest planetary index, quiet (`Kp < 4`), unsettled (`4 ≤ Kp < 5`) or storm (`Kp ≥ 5`), with up to eight 3-hour bars covering the latest 24 hours of available data. Gaps remain empty. The vertical scale is 0–9 and times are UTC.
- **Here:** interpolated local percentage, coordinates, UTC forecast time and a simple Kp/probability hint. “Worth a look” means local probability ≥30%, or Kp ≥5 with probability ≥10%. Lower thresholds give a cautious “maybe.” Darkness, cloud cover and horizon visibility are not measured.

Swipe left or right anywhere across the screen. No GPS, clock configuration, audio or battery UI is required.

## Data, cache and memory

[NOAA OVATION JSON](https://services.swpc.noaa.gov/json/ovation_aurora_latest.json) provides 1° `[longitude, latitude, Aurora]` cells. The JSON Aurora field uses a **0–100 intensity/probability scale**, not a 0–4 energy scale. The local value is bilinear interpolation of the four nearest cells, with longitude wrapping at 0/360°. Only northern latitudes are retained/rendered. A complete northern grid is required; malformed, out-of-range or partial grids cannot replace the last good map. No NOAA JPG or other raster map is downloaded.

[NOAA planetary Kp](https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json) is accepted in object-array or header-row table form. The latest eight chronological samples are retained. Forecast and Kp feeds update independently. Probabilities describe a model estimate, not a calibrated guarantee that an observer will see aurora; see [NOAA’s forecast description](https://www.swpc.noaa.gov/products/aurora-30-minute-forecast).

HTTPS downloads use the ESP-IDF certificate bundle and require SNTP time. Fetching/parsing runs on a separate task so the UI and portal keep servicing input. The full JSON document is temporarily allocated in PSRAM; **32-bit ArduinoJson slot IDs are required** because the global payload exceeds the default ESP32 slot limit. The compact northern grid is 32,760 bytes; each 466×466 RGB565 framebuffer is about 424 KiB. JSON memory is freed before each next feed is fetched.

Validated payloads are atomically renamed into LittleFS and restored after reboot. Failed downloads, schema errors and regressing timestamps retain the previous feed. The device marks data **STALE** when offline, a fetch fails, only boot cache is available, the clock is unknown, forecast time is over an hour old, or Kp is over six hours old. Retries after fetch failures occur once a minute. A complete subsequent success clears the fetch-failure marker; old source timestamps remain stale. The default filesystem is formatted on its first mount if uninitialized; a corrupt/unmountable filesystem can also lose its cache.

## Headless tests and render

Requires Python 3, a native C++17 compiler (GCC with ASan/UBSan), Node.js 22+, and the installed PlatformIO dependencies. This runs **the same `src/model.cpp` and `src/render.cpp` used by the firmware**, against saved NOAA JSON under `tests/fixtures/`.

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio pkg install -e esp32-s3-amoled-143
python3 tools/test_host.py
node tests/web.mjs
python3 tools/package_firmware.py  # after building BOTH boards
```

`build/globe.png` and `build/globe.ppm` are the 466×466 headless render. Tests cover full-grid validation, invalid/truncated feeds, both Kp formats, timestamp parsing, longitude wrapping, bilinear interpolation, pole handling, invalid coordinates, stale states, hint thresholds, deterministic rendering, coast/globe bounds and the location pin. Native tests run with address and undefined-behavior sanitizers. In a ptrace-based sandbox, LeakSanitizer cannot run: use `ASAN_OPTIONS=detect_leaks=0 python3 tools/test_host.py`; address/UB checks remain enabled.

An optional real-browser smoke test is available as `node tests/browser.mjs` with Playwright and Chromium installed. It exercises desktop/mobile layouts, both board selections, settings saves and the complete HTTPS location roundtrip with a mocked device API. `PLAYWRIGHT_MODULE` and `CHROME_PATH` can point to existing local installations. It loads the pinned ESP Web Tools script from its CDN.

Web tests cover both board manifests and the secure location handoff’s destination, token and coordinate validation. Packaging checks binary offsets and contents, writes SHA-256 hashes and commit metadata to `web/flash/build.json`, and copies the headless preview into the flasher. Generated binaries and renders are ignored by git.

The builder verifies compilation and headless behavior. On actual hardware, confirm both display/touch mappings, onboarding and reconnect recovery, phone location handoff, NVS persistence after unplugging, brightness, swipes, and stale/cache behavior with WiFi removed. The separate Grok review is performed outside this builder run.

## GitHub Pages / CI

In **Settings → Pages → Build and deployment → Source**, select **GitHub Actions** once. The workflow `.github/workflows/webflasher.yml` then:

1. Builds **both** PlatformIO environments on a `main` push, pull request or manual run.
2. Runs native render/parser tests and web tests.
3. Merges and verifies both ESP32-S3 images, then uploads binaries, ELF files and the globe preview as a build artifact.
4. Deploys `web/` to Pages only on `main`, using separate 1.43 and 1.75 manifests and an explicit board picker.

No secrets are needed beyond the workflow’s scoped GitHub token. PR builds do not deploy. The Pages URL is **https://starkmainframe.github.io/TheAurora/flash/**; the HTTPS location helper lives beside the flasher. Forks must update the Pages links in `web/config.html`, the web pages and this README to their own owner/repository.
