#include "aurora/model.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace aurora {
bool validSettings(const Settings& s) {
  return std::isfinite(s.latitude) && std::isfinite(s.longitude) &&
         s.latitude >= 0 && s.latitude <= 90 && s.longitude >= -180 &&
         s.longitude <= 180 && s.brightness >= 5 && s.brightness <= 255 &&
         s.pollMinutes >= 5 && s.pollMinutes <= 120;
}
static bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
int64_t parseUtc(const char* text) {
  if (!text) return 0;
  const size_t n = std::strlen(text);
  if (n != 19 && !(n == 20 && text[19] == 'Z')) return 0;
  if (text[4] != '-' || text[7] != '-' || (text[10] != 'T' && text[10] != ' ') ||
      text[13] != ':' || text[16] != ':') return 0;
  for (int i = 0; i < 19; ++i) {
    if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
    if (text[i] < '0' || text[i] > '9') return 0;
  }
  int y, m, d, h, min, sec;
  if (std::sscanf(text, "%4d-%2d-%2d%*c%2d:%2d:%2d", &y,&m,&d,&h,&min,&sec) != 6 ||
      y < 2000 || y > 2099 || m < 1 || m > 12 || h > 23 || min > 59 || sec > 59) return 0;
  const int monthDays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (d < 1 || d > monthDays[m-1] + (m == 2 && leap(y))) return 0;
  int days = 0;
  for (int year = 1970; year < y; ++year) days += leap(year) ? 366 : 365;
  for (int month = 1; month < m; ++month) days += monthDays[month-1] + (month == 2 && leap(y));
  return (int64_t(days + d - 1) * 24 + h) * 3600 + min * 60 + sec;
}
bool parseGrid(JsonVariantConst json, Grid& out) {
  out.valid = false;
  const char* observation = json["Observation Time"];
  const char* forecast = json["Forecast Time"];
  const int64_t epoch = parseUtc(forecast);
  if (!epoch || !parseUtc(observation) || !json["coordinates"].is<JsonArrayConst>()) return false;
  out.cells.fill(missing);
  size_t seen = 0;
  for (JsonArrayConst row : json["coordinates"].as<JsonArrayConst>()) {
    if (row.size() != 3 || !row[0].is<int>() || !row[1].is<int>() || !row[2].is<float>()) return false;
    const int lon = row[0], lat = row[1];
    const float value = row[2];
    if (lon < 0 || lon >= 360 || lat < -90 || lat > 90 || !std::isfinite(value) || value < 0 || value > 100) return false;
    if (lat < 0) continue;
    auto& cell = out.cells[lat * 360 + lon];
    if (cell != missing) return false;
    cell = uint8_t(std::lround(value));
    ++seen;
  }
  // Never publish a partial map or silently turn missing cells into zero percent.
  if (seen != out.cells.size()) return false;
  std::snprintf(out.observation, sizeof(out.observation), "%s", observation);
  std::snprintf(out.forecast, sizeof(out.forecast), "%s", forecast);
  out.forecastEpoch = epoch;
  out.valid = true;
  return true;
}
static bool number(JsonVariantConst v, float& out) {
  if (v.is<float>()) out = v.as<float>();
  else if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    char* end;
    out = std::strtof(s, &end);
    if (end == s || *end) return false;
  } else return false;
  return std::isfinite(out);
}
bool parseKp(JsonVariantConst json, KpHistory& out) {
  out = {};
  if (!json.is<JsonArrayConst>()) return false;
  int timeColumn = -1, valueColumn = -1;
  for (JsonVariantConst row : json.as<JsonArrayConst>()) {
    const char* time = nullptr;
    JsonVariantConst value;
    if (row.is<JsonObjectConst>()) { time = row["time_tag"]; value = row["Kp"]; }
    else if (row.is<JsonArrayConst>()) {
      if (timeColumn < 0) {
        for (size_t i = 0; i < row.size(); ++i) {
          const char* name = row[i] | "";
          if (!std::strcmp(name, "time_tag")) timeColumn = int(i);
          if (!std::strcmp(name, "Kp") || !std::strcmp(name, "kp")) valueColumn = int(i);
        }
        if (timeColumn < 0 || valueColumn < 0) return false;
        continue;
      }
      time = row[timeColumn]; value = row[valueColumn];
    } else return false;
    const int64_t epoch = parseUtc(time);
    float kp;
    if (!epoch || !number(value, kp) || kp < 0 || kp > 9) return false;
    // Keep the newest eight unique slots, independent of server row ordering.
    int pos = 0;
    while (pos < out.count && out.samples[pos].epoch < epoch) ++pos;
    if (pos < out.count && out.samples[pos].epoch == epoch) return false;
    if (out.count == 8) {
      if (pos == 0) continue;
      for (int i = 1; i < 8; ++i) out.samples[i-1] = out.samples[i];
      --out.count; --pos;
    }
    for (int i = out.count; i > pos; --i) out.samples[i] = out.samples[i-1];
    out.samples[pos] = {epoch, kp}; ++out.count;
  }
  if (!out.count) return false;
  const int64_t newest = out.samples[out.count-1].epoch;
  while (out.count && out.samples[0].epoch <= newest - 86400) {
    for (int i = 1; i < out.count; ++i) out.samples[i-1] = out.samples[i];
    --out.count;
  }
  return out.count > 0;
}
float probability(const Grid& grid, double lat, double lon) {
  if (!grid.valid || !std::isfinite(lat) || !std::isfinite(lon) || lat < 0 || lat > 90) return -1;
  lon = std::fmod(lon, 360.0); if (lon < 0) lon += 360;
  const int x0 = int(lon), x1 = (x0 + 1) % 360, y0 = int(lat), y1 = std::min(y0 + 1, 90);
  const float a = grid.cells[y0*360+x0], b = grid.cells[y0*360+x1];
  const float c = grid.cells[y1*360+x0], d = grid.cells[y1*360+x1];
  if (a == missing || b == missing || c == missing || d == missing) return -1;
  const float x = float(lon-x0), y = float(lat-y0);
  return (a*(1-x)+b*x)*(1-y) + (c*(1-x)+d*x)*y;
}
const char* activity(float kp) { return kp < 0 ? "Awaiting Kp" : kp < 4 ? "Quiet" : kp < 5 ? "Unsettled" : "Storm"; }
const char* outsideHint(float kp, float local) {
  if (kp < 0 || local < 0) return "Waiting for NOAA data";
  if (local >= 30 || (kp >= 5 && local >= 10)) return "Worth a look if dark and clear";
  if (local >= 5 || kp >= 5) return "Maybe: try a dark northern horizon";
  return "Low chance here right now";
}
bool dataStale(bool connected, bool failed, bool cached, int64_t forecast, int64_t kp, int64_t now) {
  return !connected || failed || cached || !forecast || !kp || now < 1700000000 ||
         now - forecast > 3600 || now - kp > 6*3600 || forecast - now > 3*3600 || kp - now > 3600;
}
}
