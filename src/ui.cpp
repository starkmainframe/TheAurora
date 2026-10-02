#include "app.h"
#include "board.h"
#include "aurora/render.h"
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <WiFi.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <ctime>

namespace app {
namespace {
Arduino_ESP32QSPI bus(board::cs,board::sclk,board::d0,board::d1,board::d2,board::d3);
Arduino_CO5300 panel(&bus,board::reset,0,board::width,board::height,
                     board::columnOffset,board::rowOffset,board::columnOffset,board::rowOffset);
lv_obj_t *screens[3], *globeImage, *hud, *state[3], *kpValue, *kpLabel, *chart, *chartTime,
         *localValue, *coordinates, *forecast, *hint, *setup;
lv_chart_series_t* series;
lv_img_dsc_t globeDescriptor{};
lv_disp_draw_buf_t drawBuffer;
uint16_t* globePixels;
int active = 0;
bool initialized = false;
uint32_t lastGeneration = UINT32_MAX, lastStatus = 0;
int lastBrightness = -1;
void fail(const char* text) {
  Serial.println(text); panel.fillScreen(0); panel.setCursor(60,220);
  panel.setTextColor(0xffff); panel.setTextSize(2); panel.println(text);
}
void flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* pixels) {
  panel.draw16bitRGBBitmap(area->x1,area->y1,reinterpret_cast<uint16_t*>(pixels),
                          area->x2-area->x1+1,area->y2-area->y1+1);
  lv_disp_flush_ready(driver);
}
bool touch(int& x, int& y) {
  Wire.beginTransmission(board::touchAddress);
#ifdef BOARD_AMOLED_143
  Wire.write(uint8_t(0));
  if (Wire.endTransmission(false)) return false;
  constexpr uint8_t length = 7;
#else
  Wire.write(uint8_t(0xd0)); Wire.write(uint8_t(0));
  if (Wire.endTransmission(true)) return false;
  delayMicroseconds(500);
  constexpr uint8_t length = 10;
#endif
  uint8_t bytes[length];
  if (Wire.requestFrom(uint8_t(board::touchAddress),length) != length) return false;
  for (auto& byte : bytes) byte = Wire.read();
#ifdef BOARD_AMOLED_143
  const int count = bytes[2]&15;
  if (!count || count > 5 || (bytes[3]>>6) == 1) return false;
  x = (bytes[3]&15)*256+bytes[4]; y = (bytes[5]&15)*256+bytes[6];
#else
  if (bytes[6] != 0xab || !(bytes[5]&0x7f) || (bytes[0]&15) != 6) return false;
  x = bytes[1]*16+(bytes[3]>>4); y = bytes[2]*16+(bytes[3]&15);
#endif
  x = std::min(x,board::width-1); y = std::min(y,board::height-1);
  if (board::mirrorTouch) { x = board::width-1-x; y = board::height-1-y; }
  return true;
}
void readTouch(lv_indev_drv_t*, lv_indev_data_t* data) {
  static lv_point_t previous{0,0};
  int x, y;
  if (touch(x,y)) { previous = {lv_coord_t(x),lv_coord_t(y)}; data->state = LV_INDEV_STATE_PR; }
  else data->state = LV_INDEV_STATE_REL;
  data->point = previous;
}
void gesture(lv_event_t*) {
  lv_indev_t* input = lv_indev_get_act();
  if (!input) return;
  const auto direction = lv_indev_get_gesture_dir(input);
  if (direction == LV_DIR_LEFT) active = (active+1)%3;
  else if (direction == LV_DIR_RIGHT) active = (active+2)%3;
  else return;
  lv_scr_load(screens[active]); lv_indev_wait_release(input);
}
lv_obj_t* label(lv_obj_t* parent, int y, int width, const lv_font_t* font, uint32_t color = 0xe7f4ed) {
  auto* object = lv_label_create(parent);
  lv_obj_set_width(object,width); lv_obj_align(object,LV_ALIGN_TOP_MID,0,y);
  lv_obj_set_style_text_align(object,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_text_font(object,font,0); lv_obj_set_style_text_color(object,lv_color_hex(color),0);
  lv_obj_add_flag(object,LV_OBJ_FLAG_GESTURE_BUBBLE); lv_label_set_text(object,"");
  return object;
}
void buildScreens() {
  const char* titles[] = {"T H E A U R O R A", "PLANETARY K-INDEX", "H E R E"};
  for (int i = 0; i < 3; ++i) {
    screens[i] = lv_obj_create(nullptr); lv_obj_clear_flag(screens[i],LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screens[i],lv_color_hex(0x071018),0);
    lv_obj_add_event_cb(screens[i],gesture,LV_EVENT_GESTURE,nullptr);
    if (!i) {
      globeImage = lv_img_create(screens[i]); lv_img_set_src(globeImage,&globeDescriptor);
      lv_obj_add_flag(globeImage,LV_OBJ_FLAG_GESTURE_BUBBLE);
    }
    auto* title = label(screens[i],38,290,&lv_font_montserrat_18,0xa5efbb); lv_label_set_text(title,titles[i]);
    state[i] = label(screens[i],65,300,&lv_font_montserrat_14,0xb1c9c1);
    auto* nav = label(screens[i],415,240,&lv_font_montserrat_14,0x94b4a8);
    lv_label_set_text(nav,i == 0 ? "GLOBE  /  kp  /  here" : i == 1 ? "globe  /  KP  /  here" : "globe  /  kp  /  HERE");
  }
  hud = label(screens[0],365,310,&lv_font_montserrat_24);
  lv_obj_set_style_bg_color(hud,lv_color_hex(0x071018),0); lv_obj_set_style_bg_opa(hud,LV_OPA_80,0);
  lv_obj_set_style_radius(hud,12,0); lv_obj_set_style_pad_ver(hud,5,0);
  setup = label(screens[0],270,330,&lv_font_montserrat_18);
  lv_obj_set_style_bg_color(setup,lv_color_hex(0x071018),0); lv_obj_set_style_bg_opa(setup,LV_OPA_90,0);
  lv_obj_set_style_pad_all(setup,8,0); lv_obj_set_style_radius(setup,10,0);
  kpValue = label(screens[1],98,300,&lv_font_montserrat_48);
  kpLabel = label(screens[1],156,300,&lv_font_montserrat_24,0xa5efbb);
  chart = lv_chart_create(screens[1]); lv_obj_set_size(chart,300,146); lv_obj_align(chart,LV_ALIGN_TOP_MID,0,210);
  lv_obj_clear_flag(chart,LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(chart,LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_style_bg_opa(chart,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(chart,0,0);
  lv_obj_set_style_line_color(chart,lv_color_hex(0x254039),LV_PART_MAIN);
  lv_chart_set_type(chart,LV_CHART_TYPE_BAR); lv_chart_set_point_count(chart,8);
  lv_chart_set_range(chart,LV_CHART_AXIS_PRIMARY_Y,0,90); lv_chart_set_div_line_count(chart,4,0);
  series = lv_chart_add_series(chart,lv_color_hex(0xa5efbb),LV_CHART_AXIS_PRIMARY_Y);
  chartTime = label(screens[1],370,340,&lv_font_montserrat_14,0xb1c9c1);
  localValue = label(screens[2],112,330,&lv_font_montserrat_48);
  auto* caption = label(screens[2],172,340,&lv_font_montserrat_18,0xa5efbb); lv_label_set_text(caption,"LOCAL AURORA PROBABILITY");
  coordinates = label(screens[2],213,340,&lv_font_montserrat_18);
  forecast = label(screens[2],253,340,&lv_font_montserrat_14,0xb1c9c1);
  hint = label(screens[2],311,320,&lv_font_montserrat_24);
  lv_scr_load(screens[0]);
}
void updateValues() {
  const float local = aurora::probability(grid,settings.latitude,settings.longitude), k = kp.latest();
  char localText[16], kpText[16];
  if (local >= 0) snprintf(localText,sizeof(localText),"%.0f%%",local); else snprintf(localText,sizeof(localText),"--%%");
  if (k >= 0) snprintf(kpText,sizeof(kpText),"%.2f",k); else snprintf(kpText,sizeof(kpText),"--");
  lv_label_set_text_fmt(hud,"Kp %s   |   Here %s",kpText,localText);
  lv_label_set_text(kpValue,kpText); lv_label_set_text(kpLabel,aurora::activity(k));
  lv_obj_set_style_text_color(kpLabel,lv_color_hex(k >= 5 ? 0xffaf83 : 0xa5efbb),0);
  lv_label_set_text(localValue,localText);
  lv_label_set_text_fmt(coordinates,"%.4f N   %.4f %s",settings.latitude,std::abs(settings.longitude),settings.longitude < 0 ? "W" : "E");
  lv_label_set_text_fmt(forecast,"Forecast (UTC)\n%s",grid.valid ? grid.forecast : "Awaiting NOAA");
  lv_label_set_text(hint,aurora::outsideHint(k,local));
  lv_chart_set_all_value(chart,series,LV_CHART_POINT_NONE);
  if (kp.count) {
    const int64_t latest = kp.samples[kp.count-1].epoch;
    for (int i = 0; i < kp.count; ++i) {
      const int slot = 7-int((latest-kp.samples[i].epoch)/10800);
      if (slot >= 0 && slot < 8) lv_chart_set_value_by_id(chart,series,slot,lv_coord_t(kp.samples[i].value*10));
    }
    time_t oldestTime = latest-7*10800, latestTime = latest;
    tm oldTm{}, newTm{}; gmtime_r(&oldestTime,&oldTm); gmtime_r(&latestTime,&newTm);
    lv_label_set_text_fmt(chartTime,"%02d:00  -  %02d:00 UTC  |  3h bars, 0-9",oldTm.tm_hour,newTm.tm_hour);
  } else lv_label_set_text(chartTime,"Last 24h of available data / UTC");
  aurora::renderGlobe(globePixels,board::width,grid,settings.latitude,settings.longitude);
  lv_obj_invalidate(globeImage);
}
void updateStatus() {
  const auto now = std::time(nullptr);
  const bool stale = aurora::dataStale(WiFi.status() == WL_CONNECTED,fetchFailed,cachedOnly,
      grid.forecastEpoch,kp.count ? kp.samples[kp.count-1].epoch : 0,now);
  const char* text = !grid.valid ? "WAITING FOR NOAA" : stale ? "STALE / last good data" : "NOAA SWPC / updated";
  for (auto* object : state) lv_label_set_text(object,text);
  if (setupActive() || !grid.valid) {
    lv_obj_clear_flag(setup,LV_OBJ_FLAG_HIDDEN);
    const String status = networkStatus();
    lv_label_set_text_fmt(setup,"%s\n%s",status.c_str(),setupActive() ? "Open setup in your browser" : "theaurora.local");
  } else lv_obj_add_flag(setup,LV_OBJ_FLAG_HIDDEN);
}
}
void uiBegin() {
  if (!panel.begin(board::qspiHz)) { Serial.println("Panel initialization failed"); return; }
  panel.setBrightness(settings.brightness);
  if (!psramFound()) { fail("PSRAM not found"); return; }
  lv_init();
  auto* frame = static_cast<lv_color_t*>(heap_caps_malloc(board::width*board::height*sizeof(lv_color_t),MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  globePixels = static_cast<uint16_t*>(heap_caps_calloc(board::width*board::height,sizeof(uint16_t),MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frame || !globePixels) { fail("Display memory error"); return; }
  lv_disp_draw_buf_init(&drawBuffer,frame,nullptr,board::width*board::height);
  static lv_disp_drv_t display;
  lv_disp_drv_init(&display); display.hor_res = board::width; display.ver_res = board::height;
  display.flush_cb = flush; display.draw_buf = &drawBuffer; display.full_refresh = 1;
  lv_disp_drv_register(&display);
  Wire.begin(board::sda,board::scl,400000); Wire.setTimeOut(15);
  if (board::touchReset >= 0) {
    pinMode(board::touchReset,OUTPUT); digitalWrite(board::touchReset,LOW); delay(10);
    digitalWrite(board::touchReset,HIGH); delay(50); pinMode(board::touchInterrupt,INPUT);
  }
  static lv_indev_drv_t input;
  lv_indev_drv_init(&input); input.type = LV_INDEV_TYPE_POINTER; input.read_cb = readTouch;
  lv_indev_drv_register(&input);
  globeDescriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
  globeDescriptor.header.w = board::width; globeDescriptor.header.h = board::height;
  globeDescriptor.data_size = board::width*board::height*2;
  globeDescriptor.data = reinterpret_cast<uint8_t*>(globePixels);
  buildScreens(); initialized = true;
}
void uiLoop() {
  if (!initialized) return;
  if (lastBrightness != settings.brightness) { panel.setBrightness(settings.brightness); lastBrightness = settings.brightness; }
  if (lastGeneration != generation) { updateValues(); lastGeneration = generation; }
  if (uint32_t(millis()-lastStatus) >= 1000) { updateStatus(); lastStatus = millis(); }
  lv_timer_handler();
}
}
