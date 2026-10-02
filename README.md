# TheAurora

Aurora forecast on Waveshare ESP32-S3 round AMOLED (1.43″ and 1.75″).

Northern-hemisphere NOAA OVATION globe, Kp, and local probability. WiFi setup like [capsule-radar](https://github.com/socquique/capsule-radar), plus a browser web flasher with board picker.

Spec: [BRIEF.md](BRIEF.md). Firmware lands on `main` as it is built.

## Hardware

Waveshare ESP32-S3-Touch-AMOLED-1.43 and 1.75 (board picker in the web flasher).

## Data

[NOAA SWPC OVATION](https://services.swpc.noaa.gov/json/ovation_aurora_latest.json) and [planetary K-index](https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json). Public, no API key.
