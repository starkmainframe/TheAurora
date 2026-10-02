#include "aurora/model.h"
#include "aurora/render.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

using namespace aurora;
static void near(float actual, float expected) { assert(std::abs(actual-expected) < 0.001f); }
static JsonDocument load(const char* path) {
  std::ifstream input(path); assert(input.good()); JsonDocument doc;
  const auto error = deserializeJson(doc,input); assert(!error); return doc;
}
int main() {
  auto json = load("tests/fixtures/ovation.json");
  Grid grid; assert(parseGrid(json.as<JsonVariantConst>(),grid));
  assert(grid.valid && grid.forecastEpoch == parseUtc(grid.forecast));
  assert(parseUtc("2024-02-29T00:00:00Z")-parseUtc("2024-02-28T00:00:00Z") == 86400);
  assert(parseUtc("2026-10-02T12:21:00Z") == 1790943660);
  for (const char* text : {"2023-02-29T00:00:00Z","2026-10-02T24:00:00Z","2026-10-02T12:21:00evil","2026-13-02T00:00:00Z"}) assert(!parseUtc(text));
  for (int lat : {0,30,60,90}) for (int lon : {0,9,180,359}) {
    near(probability(grid,lat,lon),grid.cells[lat*360+lon]);
    near(probability(grid,lat,lon-360),probability(grid,lat,lon));
  }
  assert(probability(grid,-1,0) == -1);
  assert(probability(grid,91,0) == -1);
  assert(probability(grid,50,std::numeric_limits<double>::quiet_NaN()) == -1);
  Grid test = grid;
  test.cells[60*360+359]=10; test.cells[60*360]=30;
  test.cells[61*360+359]=50; test.cells[61*360]=70;
  near(probability(test,60.5,-0.5),40);
  near(probability(test,60,359.5),20);
  test.cells[60*360]=missing; assert(probability(test,60.5,-0.5) == -1);
  JsonArray cells = json["coordinates"].as<JsonArray>();
  const auto last = cells[cells.size()-1][2].as<int>();
  cells[cells.size()-1][2] = 101; assert(!parseGrid(json.as<JsonVariantConst>(),test));
  cells[cells.size()-1][2] = last;
  cells.remove(cells.size()-1); assert(!parseGrid(json.as<JsonVariantConst>(),test));
  assert(!test.valid); assert(grid.valid); // rejected candidate never replaces live frame
  JsonDocument truncated; assert(deserializeJson(truncated,"{\"coordinates\":[[1,2,"));
  auto kpJson = load("tests/fixtures/kp.json"); KpHistory kp;
  assert(parseKp(kpJson.as<JsonVariantConst>(),kp) && kp.count == 8);
  for (int i=1;i<kp.count;++i) assert(kp.samples[i-1].epoch < kp.samples[i].epoch);
  assert(kp.samples[7].epoch-kp.samples[0].epoch < 86400);
  JsonDocument table;
  assert(!deserializeJson(table,R"([["time_tag","Kp","a_running","station_count"],["2026-10-02 03:00:00","5.33","0","8"],["2026-10-02 00:00:00","2.67","0","8"]])"));
  assert(parseKp(table.as<JsonVariantConst>(),kp) && kp.count == 2); near(kp.latest(),5.33f);
  table[1][1] = "garbage"; assert(!parseKp(table.as<JsonVariantConst>(),kp));
  table[1][1] = "10"; assert(!parseKp(table.as<JsonVariantConst>(),kp));
  table[1][1] = nullptr; assert(!parseKp(table.as<JsonVariantConst>(),kp));
  table[1][1] = "5.33"; table[2][0] = table[1][0]; assert(!parseKp(table.as<JsonVariantConst>(),kp));
  Settings settings; assert(validSettings(settings)); settings.latitude=-0.1; assert(!validSettings(settings));
  settings={90,180,5,120}; assert(validSettings(settings)); settings.pollMinutes=0; assert(!validSettings(settings));
  settings={}; settings.longitude=std::numeric_limits<double>::infinity(); assert(!validSettings(settings));
  const int64_t now=parseUtc("2026-10-02T12:00:00Z");
  assert(!dataStale(true,false,false,now,now-10800,now));
  assert(dataStale(false,false,false,now,now,now));
  assert(dataStale(true,true,false,now,now,now));
  assert(dataStale(true,false,true,now,now,now));
  assert(dataStale(true,false,false,now-3601,now,now));
  assert(dataStale(true,false,false,now,now-21601,now));
  assert(dataStale(true,false,false,now,now,0));
  assert(std::string(activity(3.99f)) == "Quiet"); assert(std::string(activity(4)) == "Unsettled");
  assert(std::string(activity(5)) == "Storm");
  assert(std::string(outsideHint(6,15)) == "Worth a look if dark and clear");
  assert(std::string(outsideHint(2,0)) == "Low chance here right now");
  constexpr int size=466;
  const Point pole=project(90,0,0,size); assert(pole.x==233 && pole.y==233);
  const Point pin=project(53.6,9.82,9.82,size); assert(pin.x==233 && pin.y>233);
  std::vector<uint16_t> pixels(size*size), again(size*size);
  renderGlobe(pixels.data(),size,grid,53.6,9.82);
  renderGlobe(again.data(),size,grid,53.6,9.82); assert(pixels==again);
  assert(pixels[0]==0 && pixels.back()==0 && pixels[233*size+233]!=0);
  size_t lit=0, green=0;
  for (auto pixel:pixels) {
    if (pixel) ++lit;
    const int r=(pixel>>11)*8,g=((pixel>>5)&63)*4,b=(pixel&31)*8;
    if (g>r*2 && g>b*2 && g>50) ++green;
  }
  assert(lit>160000 && lit<170000); assert(green>100);
  assert(pixels[pin.y*size+pin.x] == uint16_t((255>>3)<<11 | (151>>2)<<5 | 100>>3));
  std::ofstream ppm("build/globe.ppm",std::ios::binary); ppm << "P6\n466 466\n255\n";
  uint64_t hash=14695981039346656037ULL;
  for (auto pixel:pixels) {
    unsigned char rgb[3]={static_cast<unsigned char>((pixel>>11)*255/31),
      static_cast<unsigned char>(((pixel>>5)&63)*255/63),static_cast<unsigned char>((pixel&31)*255/31)};
    ppm.write(reinterpret_cast<char*>(rgb),3);
    hash^=pixel; hash*=1099511628211ULL;
  }
  std::cout << "PASS: grid validation, Kp formats, UTC, NVS settings bounds, interpolation, stale state, hints, globe/pin\n";
  std::cout << "466x466 render: " << lit << " lit pixels, " << green << " aurora pixels, hash " << hash << '\n';
}
