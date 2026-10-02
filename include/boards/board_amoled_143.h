#pragma once
// Wiring/alignment confirmed by capsule-radar; see THIRD_PARTY.md.
namespace board {
constexpr const char* name = "ESP32-S3-Touch-AMOLED-1.43";
constexpr int width = 466, height = 466;
constexpr int cs = 9, sclk = 10, d0 = 11, d1 = 12, d2 = 13, d3 = 14;
constexpr int reset = 21, columnOffset = 6, rowOffset = 0;
constexpr int sda = 47, scl = 48, touchAddress = 0x38;
constexpr int touchReset = -1, touchInterrupt = -1;
constexpr bool mirrorTouch = false;
constexpr int qspiHz = 40000000;
// FT3168 reset is shared with panel reset; initialize LCD before touch.
}
