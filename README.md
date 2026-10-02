# TheAurora

Aurora forecast on a round Waveshare ESP32-S3 1.43″ AMOLED.

Northern-hemisphere NOAA OVATION globe, Kp, and local probability. WiFi setup like [capsule-radar](https://github.com/socquique/capsule-radar), plus a browser web flasher.

Spec: [BRIEF.md](BRIEF.md). Firmware lands on `main` as it is built.

## Hardware

Waveshare ESP32-S3-Touch-AMOLED-1.43 (466×466). Not the 1.75C.

## Data

[NOAA SWPC OVATION](https://services.swpc.noaa.gov/json/ovation_aurora_latest.json) and [planetary K-index](https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json). Public, no API key.
