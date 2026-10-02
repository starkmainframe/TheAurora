#pragma once
#include "model.h"
namespace aurora {
struct Point { int x, y; };
Point project(double latitude, double longitude, double centralLongitude, int size);
// RGB565, host-endian. Exactly the same renderer is used on the ESP32 and in tests.
void renderGlobe(uint16_t* pixels, int size, const Grid& grid, double latitude, double longitude);
}
