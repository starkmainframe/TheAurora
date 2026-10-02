#include "aurora/render.h"
#include "aurora/coastlines.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aurora {
static constexpr double pi = 3.14159265358979323846;
static uint16_t rgb(float r, float g, float b) {
  const auto c = [](float x) { return int(std::max(0.0f, std::min(255.0f, x))); };
  return uint16_t((c(r)>>3)<<11 | (c(g)>>2)<<5 | c(b)>>3);
}
Point project(double lat, double lon, double central, int size) {
  const double radius = (size/2.0-3) * (90-lat)/90;
  const double angle = (lon-central)*pi/180;
  return {int(std::lround(size/2.0+radius*std::sin(angle))),
          int(std::lround(size/2.0+radius*std::cos(angle)))};
}
static void pixel(uint16_t* dst, int size, int x, int y, uint16_t color) {
  if (x >= 0 && y >= 0 && x < size && y < size) dst[y*size+x] = color;
}
static void line(uint16_t* dst, int size, Point a, Point b, uint16_t color) {
  int dx = std::abs(b.x-a.x), sx = a.x < b.x ? 1 : -1;
  int dy = -std::abs(b.y-a.y), sy = a.y < b.y ? 1 : -1, error = dx+dy;
  for (;;) {
    pixel(dst,size,a.x,a.y,color);
    if (a.x == b.x && a.y == b.y) break;
    int e = 2*error;
    if (e >= dy) { error += dy; a.x += sx; }
    if (e <= dx) { error += dx; a.y += sy; }
  }
}
void renderGlobe(uint16_t* dst, int size, const Grid& grid, double lat, double lon) {
  const float center = size/2.0f, radius = center-3;
  // Approximate solar shading using forecast UTC, retained on the night side.
  char yearStart[25];
  std::snprintf(yearStart,sizeof(yearStart),"%.4s-01-01T00:00:00Z",grid.forecast);
  const double day = (grid.forecastEpoch-parseUtc(yearStart)) / 86400.0 + 1;
  const double declination = 0.409 * std::sin(2*pi*(day-79)/365.2422);
  const double sunLon = 180 - (grid.forecastEpoch % 86400)/240.0;
  for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
    const float dx = x-center, dy = y-center;
    const float r = std::sqrt(dx*dx+dy*dy)/radius;
    if (r > 1) { dst[y*size+x] = 0; continue; }
    const double latitude = 90*(1-r), longitude = lon+std::atan2(dx,dy)*180/pi;
    const double phi = latitude*pi/180;
    const double light = std::max(0.0, std::sin(phi)*std::sin(declination) +
      std::cos(phi)*std::cos(declination)*std::cos((longitude-sunLon)*pi/180));
    float red = 3+4*light, green = 9+11*light, blue = 17+16*light;
    // Quiet graticule and a narrow blue atmospheric rim.
    const double longitudeLine = std::remainder(longitude,30.0);
    if (std::abs(std::remainder(latitude,30.0)) < 0.22 ||
        (std::abs(longitudeLine) < 0.25 && latitude < 85)) { green += 6; blue += 8; }
    const float edge = std::max(0.0f,(r-0.975f)/0.025f);
    green += edge*14; blue += edge*25;
    const float p = probability(grid,latitude,longitude);
    if (p > 0) {
      // NOAA's JSON intensity/probability scale is 0..100, not a 0..4 energy scale.
      const float strength = std::min(1.0f,std::sqrt(p/100.0f)*1.65f);
      const float hot = std::min(1.0f,p/50.0f);
      const float ar = p < 50 ? 25+230*hot : 255;
      const float ag = p < 50 ? 245 : 245-210*(p-50)/50;
      red += ar*strength; green += ag*strength; blue += 28*strength;
    }
    dst[y*size+x] = rgb(red,green,blue);
  }
  bool first = true; Point previous{};
  for (const auto& coordinate : coastline) {
    if (coordinate[0] == 32767) { first = true; continue; }
    Point p = project(coordinate[1]/10.0,coordinate[0]/10.0,lon,size);
    if (!first) line(dst,size,previous,p,rgb(43,67,76));
    previous = p; first = false;
  }
  if (lat >= 0 && lat <= 90) {
    const Point pin = project(lat,lon,lon,size);
    for (int y = -6; y <= 6; ++y) for (int x = -6; x <= 6; ++x) {
      const int d = x*x+y*y;
      if (d >= 20 && d <= 36) pixel(dst,size,pin.x+x,pin.y+y,rgb(255,240,210));
      if (d <= 4) pixel(dst,size,pin.x+x,pin.y+y,rgb(255,151,100));
    }
  }
}
}
