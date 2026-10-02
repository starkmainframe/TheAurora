#pragma once
// Wiring/alignment confirmed by capsule-radar; see THIRD_PARTY.md.
namespace board {
constexpr const char* name = "ESP32-S3-Touch-AMOLED-1.75";
constexpr int width = 466, height = 466;
constexpr int cs = 12, sclk = 38, d0 = 4, d1 = 5, d2 = 6, d3 = 7;
constexpr int reset = 39, columnOffset = 6, rowOffset = 0;
constexpr int sda = 15, scl = 14, touchAddress = 0x5a;
constexpr int touchReset = 40, touchInterrupt = 11;
constexpr bool mirrorTouch = true;
// Vendor rate; reference allows 80 MHz subject to artifact testing.
constexpr int qspiHz = 40000000;
}
