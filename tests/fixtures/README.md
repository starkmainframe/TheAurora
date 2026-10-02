# NOAA fixtures

Saved from the public NOAA SWPC endpoints on 2026-10-02, unchanged:

- `ovation.json`: https://services.swpc.noaa.gov/json/ovation_aurora_latest.json — observation 10:50 UTC, forecast 12:21 UTC; 65,160 global 1° cells.
- `kp.json`: https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json — object-array history ending at 06:00 UTC.

These are historical regression inputs, not live forecasts. The host test also constructs a small header-row Kp example and invalid inputs in memory. No NOAA raster image is used. Tests render only northern grid cells. Fetch timestamps remain fixed so tests need no internet and the rendered artifact is reproducible.
