#include "app.h"
#include "board.h"
#include "web_page.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cmath>
#include <cstdlib>

namespace app {
aurora::Settings settings;
namespace {
WebServer server(80);
DNSServer dns;
Preferences prefs;
String ssid, password, token;
bool ap = false, wasConnected = false;
uint32_t disconnectedAt = 0, connectedAt = 0, changeAt = 0;
bool connectPending = false, resetPending = false;
void startAP() {
  if (ap) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("TheAurora-Setup");
  dns.start(53,"*",WiFi.softAPIP());
  ap = true;
  Serial.println("Setup: TheAurora-Setup -> http://192.168.4.1/");
}
void json(int code, const String& text) {
  server.sendHeader("Cache-Control","no-store");
  server.sendHeader("X-Content-Type-Options","nosniff");
  server.send(code,"application/json",text);
}
bool authorized() {
  if (server.arg("token") == token && token.length()) return true;
  json(403,"{\"error\":\"Reload the settings page and try again.\"}"); return false;
}
bool field(const char* name, double& value) {
  if (!server.hasArg(name)) return false;
  String text = server.arg(name); char* end;
  value = std::strtod(text.c_str(),&end);
  return end != text.c_str() && !*end && std::isfinite(value);
}
void configuration() {
  JsonDocument doc;
  doc["latitude"] = settings.latitude; doc["longitude"] = settings.longitude;
  doc["brightness"] = settings.brightness; doc["pollMinutes"] = settings.pollMinutes;
  doc["ssid"] = ssid; doc["token"] = token; doc["board"] = board::name;
  doc["status"] = networkStatus();
  String body; serializeJson(doc,body); json(200,body);
}
void saveSettings() {
  if (!authorized()) return;
  double lat, lon, brightness, poll;
  if (!field("latitude",lat) || !field("longitude",lon) || !field("brightness",brightness) ||
      !field("pollMinutes",poll) || brightness < 5 || brightness > 255 || poll < 5 || poll > 120 ||
      std::floor(brightness) != brightness || std::floor(poll) != poll) {
    json(400,"{\"error\":\"Enter valid coordinates, brightness 5–255 and an interval of 5–120 minutes.\"}"); return;
  }
  aurora::Settings candidate{lat,lon,int(brightness),int(poll)};
  if (!aurora::validSettings(candidate)) { json(400,"{\"error\":\"Latitude must be 0–90° N; longitude must be −180–180°.\"}"); return; }
  if (prefs.putBytes("settings",&candidate,sizeof(candidate)) != sizeof(candidate)) {
    json(500,"{\"error\":\"Could not store settings.\"}"); return;
  }
  settings = candidate; ++generation; requestRefresh();
  json(200,"{\"message\":\"Saved on your display.\"}");
}
void saveWifi() {
  if (!authorized()) return;
  const String nextSsid = server.arg("ssid"), nextPass = server.arg("password");
  if (nextSsid.length() < 1 || nextSsid.length() > 32 ||
      (nextPass.length() && (nextPass.length() < 8 || nextPass.length() > 63))) {
    json(400,"{\"error\":\"SSID must be 1–32 bytes; password must be empty or 8–63 characters.\"}"); return;
  }
  // Store credentials as one NVS value so power loss cannot mix SSID/password pairs.
  JsonDocument doc; doc["ssid"] = nextSsid; doc["password"] = nextPass;
  String credentials; serializeJson(doc,credentials);
  if (!prefs.putString("wifi",credentials)) { json(500,"{\"error\":\"Could not store WiFi credentials.\"}"); return; }
  ssid = nextSsid; password = nextPass;
  connectPending = true; changeAt = millis();
  json(200,"{\"message\":\"Connecting to home WiFi. Open http://theaurora.local/ after reconnecting your phone.\"}");
}
}
bool setupActive() { return ap; }
String networkStatus() {
  if (WiFi.status() == WL_CONNECTED) return String("WiFi: ")+WiFi.localIP().toString();
  if (ap) return "Setup: TheAurora-Setup / 192.168.4.1";
  return "Connecting to WiFi...";
}
void networkBegin() {
  prefs.begin("theaurora",false);
  if (prefs.getBytesLength("settings") == sizeof(settings)) {
    aurora::Settings saved;
    prefs.getBytes("settings",&saved,sizeof(saved));
    if (aurora::validSettings(saved)) settings = saved;
  }
  JsonDocument doc;
  if (!deserializeJson(doc,prefs.getString("wifi","{}"))) {
    ssid = doc["ssid"] | ""; password = doc["password"] | "";
  }
  char randomToken[33];
  snprintf(randomToken,sizeof(randomToken),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),
    (unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
  token = randomToken;
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setHostname("theaurora");
  WiFi.setAutoReconnect(true);
  pinMode(0,INPUT_PULLUP);
  if (ssid.isEmpty() || digitalRead(0) == LOW) startAP();
  if (!ssid.isEmpty()) WiFi.begin(ssid.c_str(),password.c_str());
  disconnectedAt = millis();
  server.on("/",HTTP_GET,[] {
    server.sendHeader("Cache-Control","no-store");
    server.sendHeader("Referrer-Policy","no-referrer");
    server.sendHeader("X-Frame-Options","DENY");
    server.send_P(200,"text/html; charset=utf-8",configPage);
  });
  server.on("/api/config",HTTP_GET,configuration);
  server.on("/api/settings",HTTP_POST,saveSettings);
  server.on("/api/wifi",HTTP_POST,saveWifi);
  server.on("/api/reset",HTTP_POST,[] {
    if (!authorized()) return;
    if (prefs.isKey("wifi") && !prefs.remove("wifi")) { json(500,"{\"error\":\"Could not clear WiFi.\"}"); return; }
    resetPending = true; changeAt = millis();
    json(200,"{\"message\":\"WiFi forgotten. Join TheAurora-Setup and open http://192.168.4.1/.\"}");
  });
  server.onNotFound([] {
    if (ap) { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
    else server.send(404,"text/plain","Not found");
  });
  server.begin();
}
void networkLoop() {
  server.handleClient(); if (ap) dns.processNextRequest();
  if ((connectPending || resetPending) && uint32_t(millis()-changeAt) > 700) {
    WiFi.disconnect(false,true); MDNS.end(); wasConnected = false;
    startAP(); disconnectedAt = millis();
    if (resetPending) { ssid = ""; password = ""; }
    else WiFi.begin(ssid.c_str(),password.c_str());
    connectPending = resetPending = false;
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    connectedAt = millis();
    MDNS.end(); if (MDNS.begin("theaurora")) MDNS.addService("http","tcp",80);
    configTime(0,0,"pool.ntp.org","time.nist.gov"); requestRefresh();
    Serial.println(networkStatus());
  }
  if (!connected && wasConnected) disconnectedAt = millis();
  if (connected && ap && uint32_t(millis()-connectedAt) > 15000) {
    dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); ap = false;
  }
  if (!connected && uint32_t(millis()-disconnectedAt) > 30000) startAP();
  wasConnected = connected;
}
}
