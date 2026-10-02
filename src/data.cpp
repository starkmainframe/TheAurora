#include "app.h"
#include <LittleFS.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <atomic>
#include <ctime>

namespace app {
aurora::Grid grid;
aurora::KpHistory kp;
bool fetchFailed = false, cachedOnly = true;
uint32_t generation = 0;
namespace {
constexpr const char* gridUrl = "https://services.swpc.noaa.gov/json/ovation_aurora_latest.json";
constexpr const char* kpUrl = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json";
struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
} allocator;
struct Snapshot { aurora::Grid grid; aurora::KpHistory kp; bool failed = false, cached = true; };
Snapshot* pending;
SemaphoreHandle_t lock;
std::atomic<bool> refresh{true};
std::atomic<int> pollMinutes{15};
bool changed = false;
bool filesystemReady = false;
struct Download { File file; size_t bytes = 0, limit = 0; bool failed = false; };
esp_err_t receive(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
  auto* d = static_cast<Download*>(event->user_data);
  if (d->bytes + event->data_len > d->limit ||
      d->file.write(reinterpret_cast<const uint8_t*>(event->data),event->data_len) != size_t(event->data_len)) {
    d->failed = true; return ESP_FAIL;
  }
  d->bytes += event->data_len;
  return ESP_OK;
}
bool download(const char* url, const char* path, size_t limit) {
  Download d{LittleFS.open(path,"w"),0,limit,false};
  if (!d.file) return false;
  esp_http_client_config_t config{};
  config.url = url;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 12000;
  config.buffer_size = 4096;
  config.event_handler = receive;
  config.user_data = &d;
  config.disable_auto_redirect = true;
  config.user_agent = "TheAurora/1.0 (+https://github.com/starkmainframe/TheAurora)";
  auto client = esp_http_client_init(&config);
  if (!client) { d.file.close(); return false; }
  const esp_err_t result = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  const int64_t length = esp_http_client_get_content_length(client);
  esp_http_client_cleanup(client);
  d.file.close();
  return result == ESP_OK && status == 200 && !d.failed && d.bytes > 0 &&
         (length < 0 || uint64_t(length) == d.bytes);
}
bool readGrid(const char* path, aurora::Grid& output) {
  File file = LittleFS.open(path,"r"); if (!file) return false;
  JsonDocument doc(&allocator);
  auto error = deserializeJson(doc,file,DeserializationOption::NestingLimit(5));
  return !error && aurora::parseGrid(doc.as<JsonVariantConst>(),output);
}
bool readKp(const char* path, aurora::KpHistory& output) {
  File file = LittleFS.open(path,"r"); if (!file) return false;
  JsonDocument doc(&allocator);
  auto error = deserializeJson(doc,file,DeserializationOption::NestingLimit(5));
  return !error && aurora::parseKp(doc.as<JsonVariantConst>(),output);
}
void worker(void*) {
  // Allocate large candidates in PSRAM, never on a FreeRTOS task's stack.
  void* currentMemory = heap_caps_malloc(sizeof(Snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  void* candidateMemory = heap_caps_malloc(sizeof(aurora::Grid), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!currentMemory || !candidateMemory) {
    heap_caps_free(currentMemory); heap_caps_free(candidateMemory);
    xSemaphoreTake(lock,portMAX_DELAY); pending->failed = true; changed = true; xSemaphoreGive(lock);
    Serial.println("NOAA memory allocation failed"); vTaskDelete(nullptr); return;
  }
  auto* current = new (currentMemory) Snapshot;
  auto* candidate = new (candidateMemory) aurora::Grid;
  if (filesystemReady) {
    if (readGrid("/ovation.json",*candidate)) current->grid = *candidate;
    aurora::KpHistory savedKp;
    if (readKp("/kp.json",savedKp)) current->kp = savedKp;
  }
  auto publish = [&] {
    xSemaphoreTake(lock,portMAX_DELAY); *pending = *current; changed = true; xSemaphoreGive(lock);
  };
  publish();
  uint32_t previous = 0;
  bool everFetched = false;
  for (;;) {
    const bool connected = WiFi.status() == WL_CONNECTED;
    const bool clockReady = std::time(nullptr) > 1700000000;
    const uint32_t interval = current->failed ? 60000 : uint32_t(pollMinutes.load())*60000;
    if (connected && clockReady && (refresh.load() || !everFetched || uint32_t(millis()-previous) >= interval)) {
      refresh.store(false);
      bool goodGrid = false, goodKp = false;
      if (filesystemReady && download(gridUrl,"/ovation.tmp",1500000) && readGrid("/ovation.tmp",*candidate) &&
          (!current->grid.valid || candidate->forecastEpoch >= current->grid.forecastEpoch)) {
        current->grid = *candidate;
        goodGrid = LittleFS.rename("/ovation.tmp","/ovation.json");
      }
      aurora::KpHistory next;
      if (filesystemReady && download(kpUrl,"/kp.tmp",100000) && readKp("/kp.tmp",next) &&
          (!current->kp.count || next.samples[next.count-1].epoch >= current->kp.samples[current->kp.count-1].epoch)) {
        current->kp = next;
        goodKp = LittleFS.rename("/kp.tmp","/kp.json");
      }
      if (filesystemReady) { LittleFS.remove("/ovation.tmp"); LittleFS.remove("/kp.tmp"); }
      current->failed = !goodGrid || !goodKp;
      if (goodGrid && goodKp) current->cached = false;
      previous = millis(); everFetched = true;
      Serial.printf("NOAA: grid=%s kp=%s; free PSRAM=%u\n",goodGrid?"ok":"failed",goodKp?"ok":"failed",ESP.getFreePsram());
      publish();
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
}
void requestRefresh() { refresh.store(true); }
void dataBegin() {
  filesystemReady = LittleFS.begin(true);
  lock = xSemaphoreCreateMutex();
  void* memory = heap_caps_malloc(sizeof(Snapshot),MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory || !lock) { Serial.println("Data allocation failed"); return; }
  pending = new (memory) Snapshot;
  if (xTaskCreatePinnedToCore(worker,"noaa",12288,nullptr,1,nullptr,0) != pdPASS) {
    fetchFailed = true; Serial.println("NOAA task could not start");
  }
}
void dataLoop() {
  pollMinutes.store(settings.pollMinutes);
  if (!pending || xSemaphoreTake(lock,0) != pdTRUE) return;
  if (changed) {
    grid = pending->grid; kp = pending->kp;
    fetchFailed = pending->failed; cachedOnly = pending->cached;
    changed = false; ++generation;
  }
  xSemaphoreGive(lock);
}
}
