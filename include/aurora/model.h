#pragma once
#include <ArduinoJson.h>
#include <array>
#include <cstdint>

static_assert(ARDUINOJSON_SLOT_ID_SIZE >= 4, "The complete NOAA grid needs more than 65535 JSON slots");

namespace aurora {
constexpr int longitudeCount = 360, latitudeCount = 91;
constexpr uint8_t missing = 255;
struct Grid {
  std::array<uint8_t, longitudeCount * latitudeCount> cells{};
  char observation[25]{}, forecast[25]{};
  int64_t forecastEpoch = 0;
  bool valid = false;
  Grid() { cells.fill(missing); }
};
struct KpSample { int64_t epoch = 0; float value = 0; };
struct KpHistory {
  std::array<KpSample, 8> samples{};
  int count = 0;
  float latest() const { return count ? samples[count - 1].value : -1; }
};
struct Settings {
  double latitude = 53.60, longitude = 9.82; // Schenefeld; explicitly editable
  int brightness = 150, pollMinutes = 15;
};
bool validSettings(const Settings& settings);
int64_t parseUtc(const char* text);
bool parseGrid(JsonVariantConst json, Grid& output);
bool parseKp(JsonVariantConst json, KpHistory& output);
float probability(const Grid& grid, double latitude, double longitude);
const char* activity(float kp);
const char* outsideHint(float kp, float local);
bool dataStale(bool connected, bool fetchFailed, bool cachedOnly,
               int64_t forecast, int64_t kpTime, int64_t now);
}
