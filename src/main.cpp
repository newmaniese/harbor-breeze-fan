#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <functional>
#include "harbor_breeze.h"
#ifndef TRANSCEIVER_ONLY
#include "rf_capture.h"
#endif

#ifndef WIFI_SSID
#error "WIFI_SSID is not defined. Please create a .env file from .env.example."
#endif
#ifndef WIFI_PASS
#error "WIFI_PASS is not defined. Please create a .env file from .env.example."
#endif

#define TX_PIN 6
// Default: 0 = active-high (works with hub protocol for this fan). 1 = active-low. Overridable via Debug & settings.
#define TX_INVERT_DEFAULT 0

static int g_txInvert = TX_INVERT_DEFAULT;

static Preferences statePrefs;
static const char* STATE_NAMESPACE = "hbstate";
static const char* KEY_DIR = "dir";       // 0=summer, 1=winter
static const char* KEY_SPEED = "speed";  // 0=off, 1-6
static const char* KEY_LIGHT = "light";   // 0=off, 1=on
static const char* KEY_DELAY_END = "dend";  // Unix timestamp when delay turns off, 0=off
static const char* KEY_HS_LEN = "hslen";   // length of learned Home Shield block (20–50)
static const char* KEY_HS = "hs";          // up to 50 * uint16_t = 100 bytes
static const int HOME_SHIELD_MIN_PULSES = 20;
static const int HOME_SHIELD_MAX_PULSES = 50;
static const int HOME_SHIELD_REPEATS = 12;   // match hub (light_toggle etc.): 12 repeats, no gap
static const uint16_t HOME_SHIELD_GAP_US = 8000;

// In-memory copy of learned Home Shield frame so send/verify see it immediately after learn (Preferences read-after-write can fail on same request).
static uint16_t s_homeShieldFrame[HOME_SHIELD_MAX_PULSES];
static int s_homeShieldLen = 0;

static void stateBegin() {
  statePrefs.begin(STATE_NAMESPACE, false);
  // Load learned Home Shield from NVS into RAM so it's available without read-after-write issues
  int n = (int)statePrefs.getUChar(KEY_HS_LEN, 0);
  if (n >= HOME_SHIELD_MIN_PULSES && n <= HOME_SHIELD_MAX_PULSES &&
      statePrefs.getBytes(KEY_HS, (void*)s_homeShieldFrame, (size_t)(n * sizeof(uint16_t))) == (size_t)(n * sizeof(uint16_t))) {
    s_homeShieldLen = n;
  } else {
    s_homeShieldLen = 0;
  }
}
static void stateSetDirection(int summer0_winter1) {
  statePrefs.putUChar(KEY_DIR, (uint8_t)(summer0_winter1 & 1));
}
static void stateSetSpeed(int speed) {
  statePrefs.putUChar(KEY_SPEED, (uint8_t)(speed <= 6 ? speed : 0));
}
static void stateSetLight(int on) {
  statePrefs.putUChar(KEY_LIGHT, (uint8_t)(on ? 1 : 0));
}
static void stateSetDelayEnd(uint32_t utcEnd) {
  statePrefs.putULong(KEY_DELAY_END, (unsigned long)utcEnd);
}
static int stateGetDirection() {
  return (int)statePrefs.getUChar(KEY_DIR, 0);
}
static int stateGetSpeed() {
  return (int)statePrefs.getUChar(KEY_SPEED, 0);
}
static int stateGetLight() {
  return (int)statePrefs.getUChar(KEY_LIGHT, 0);
}
static uint32_t stateGetDelayEnd() {
  return (uint32_t)statePrefs.getULong(KEY_DELAY_END, 0);
}
static void stateUpdateFromCmd(const char* cmd) {
  if (strcmp(cmd, "fan_direction_summer") == 0) stateSetDirection(0);
  else if (strcmp(cmd, "fan_direction_winter") == 0) stateSetDirection(1);
  else if (strcmp(cmd, "fan_off") == 0) stateSetSpeed(0);
  else if (strcmp(cmd, "fan_speed_1") == 0) stateSetSpeed(1);
  else if (strcmp(cmd, "fan_speed_2") == 0) stateSetSpeed(2);
  else if (strcmp(cmd, "fan_speed_3") == 0) stateSetSpeed(3);
  else if (strcmp(cmd, "fan_speed_4") == 0) stateSetSpeed(4);
  else if (strcmp(cmd, "fan_speed_5") == 0) stateSetSpeed(5);
  else if (strcmp(cmd, "fan_speed_6") == 0) stateSetSpeed(6);
  else if (strcmp(cmd, "light_toggle") == 0) stateSetLight(stateGetLight() ? 0 : 1);
  else if (strcmp(cmd, "delay_off") == 0) stateSetDelayEnd(0);
  else if (strcmp(cmd, "delay_2h") == 0) stateSetDelayEnd((uint32_t)time(nullptr) + 2 * 3600);
  else if (strcmp(cmd, "delay_4h") == 0) stateSetDelayEnd((uint32_t)time(nullptr) + 4 * 3600);
  else if (strcmp(cmd, "delay_8h") == 0) stateSetDelayEnd((uint32_t)time(nullptr) + 8 * 3600);
}

// Learned Home Shield: 20–50 pulses stored; sent 24× with 8 ms gap between repeats.
// Skip any leading idle (receiver "time to first edge" > 1 ms) so the stored frame starts with the real burst (~400 µs for hub).
static bool homeShieldSaveFrame(const uint16_t* pulses, int len) {
  if (!pulses || len < HOME_SHIELD_MIN_PULSES + 1) return false;
  int start = 0;
  if (pulses[0] > 1000) start = 1;  // drop leading idle so first pulse is ~400/440 µs (hub start)
  int n = len - start;
  if (n < HOME_SHIELD_MIN_PULSES) return false;
  if (n > HOME_SHIELD_MAX_PULSES) n = HOME_SHIELD_MAX_PULSES;
  statePrefs.putUChar(KEY_HS_LEN, (uint8_t)n);
  statePrefs.putBytes(KEY_HS, (const void*)(pulses + start), (size_t)(n * sizeof(uint16_t)));
  memcpy(s_homeShieldFrame, pulses + start, (size_t)(n * sizeof(uint16_t)));
  s_homeShieldLen = n;
  statePrefs.end();
  statePrefs.begin(STATE_NAMESPACE, false);
  return true;
}
// Save Home Shield frame from an array (for restore from backup).
static bool homeShieldSaveFrameFromArray(const uint16_t* arr, int len) {
  if (!arr || len < HOME_SHIELD_MIN_PULSES || len > HOME_SHIELD_MAX_PULSES) return false;
  statePrefs.putUChar(KEY_HS_LEN, (uint8_t)len);
  statePrefs.putBytes(KEY_HS, (const void*)arr, (size_t)(len * sizeof(uint16_t)));
  memcpy(s_homeShieldFrame, arr, (size_t)(len * sizeof(uint16_t)));
  s_homeShieldLen = len;
  statePrefs.end();
  statePrefs.begin(STATE_NAMESPACE, false);
  return true;
}
// Build Home Shield pulse array. If useGap true: 24 blocks with 8 ms gap between; if false: 24 blocks back-to-back.
static int homeShieldBuildPulsesEx(uint16_t* out, int maxOut, bool useGap) {
  int n = s_homeShieldLen;
  if (n < HOME_SHIELD_MIN_PULSES || n > HOME_SHIELD_MAX_PULSES) return 0;
  int need = useGap ? (HOME_SHIELD_REPEATS * n + (HOME_SHIELD_REPEATS - 1)) : (HOME_SHIELD_REPEATS * n);
  if (maxOut < need) return 0;
  int idx = 0;
  for (int r = 0; r < HOME_SHIELD_REPEATS; r++) {
    for (int i = 0; i < n; i++)
      out[idx++] = s_homeShieldFrame[i];
    if (useGap && r < HOME_SHIELD_REPEATS - 1)
      out[idx++] = HOME_SHIELD_GAP_US;
  }
  return idx;
}
static int homeShieldBuildPulses(uint16_t* out, int maxOut) {
  return homeShieldBuildPulsesEx(out, maxOut, false);
}
static bool homeShieldLearned() {
  return s_homeShieldLen >= HOME_SHIELD_MIN_PULSES && s_homeShieldLen <= HOME_SHIELD_MAX_PULSES;
}

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

struct VerifyTxState {
  bool active = false;
  AsyncWebServerRequest* req = nullptr;
  String cmd;
  int expectedLen = 0;
  uint16_t expectedPulses[HB_HUB_MAX_PULSES];
  uint32_t seqBefore = 0;
  unsigned long waitStartTime = 0;
} g_verifyTx;

struct Command {
  const char* name;
  const char* func;
};

static const Command commands[] = {
  { "light_toggle",    HB_LIGHT_T },
  { "light_dim",       HB_LIGHT_D },
  { "fan_off",         HB_FAN_FST },
  { "fan_speed_1",     HB_FAN_FS1 },
  { "fan_speed_2",     HB_FAN_FS2 },
  { "fan_speed_3",     HB_FAN_FS3 },
  { "fan_speed_4",     HB_FAN_FS4 },
  { "fan_speed_5",     HB_FAN_FS5 },
  { "fan_speed_6",     HB_FAN_FS6 },
  { "fan_direction_summer", HB_FAN_FSD },
  { "fan_direction_winter", HB_FAN_FWD },
  { "nature_breeze",   HB_FAN_FSN },
  { "delay_off",       HB_DELAY_O },
  { "delay_2h",        HB_DELAY_2 },
  { "delay_4h",        HB_DELAY_4 },
  { "delay_8h",        HB_DELAY_8 },
  { "home_shield",     HB_LIGHT_H },
};
static const int numCommands = sizeof(commands) / sizeof(commands[0]);

// Decode received pulses to 25 bits (17 preamble + 8 function). Fills func8 and returns true if preamble matches.
static bool decodePulsesToFunc(const uint16_t* pulses, int len, char* func8) {
  if (!pulses || len < 50 || !func8) return false;
  const int shortMax = 500;
  const int gapMin = 5000;
  char bits[26];
  int bitIdx = 0;
  for (int i = 0; i < len - 1 && bitIdx < 25; i++) {
    uint16_t a = pulses[i], b = pulses[i + 1];
    if (a > gapMin || b > gapMin) { i++; continue; }
    bool ashort = (a < shortMax), bshort = (b < shortMax);
    if (!ashort && bshort) { bits[bitIdx++] = '1'; i++; }
    else if (ashort && !bshort) { bits[bitIdx++] = '0'; i++; }
  }
  if (bitIdx != 25) return false;
  bits[25] = '\0';
  if (strncmp(bits, HB_PREAMBLE_DIP1, 17) != 0) return false;
  memcpy(func8, bits + 17, 9);
  func8[8] = '\0';
  return true;
}

static const char* findNameByFunc(const char* func8) {
  for (int i = 0; i < numCommands; i++)
    if (strcmp(commands[i].func, func8) == 0)
      return commands[i].name;
  return nullptr;
}

static const char* findFunc(const char* cmd) {
  for (int i = 0; i < numCommands; i++)
    if (strcmp(commands[i].name, cmd) == 0)
      return commands[i].func;
  return nullptr;
}

// Helper for JSON POST requests: handles chunk aggregation, parsing, and error reporting.
static void handleJsonRequest(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total, std::function<void(JsonDocument&)> callback) {
  if (index == 0) {
    req->_tempObject = new String("");
  }
  String* body = (String*)req->_tempObject;
  if (len) {
    body->concat((const char*)data, len);
  }
  if (index + len != total) return;

  JsonDocument doc;
  if (deserializeJson(doc, *body)) {
    delete body;
    req->_tempObject = nullptr;
    req->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  callback(doc);
  delete body;
  req->_tempObject = nullptr;
}

static void sendPulses(uint16_t* pulses, int len) {
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, g_txInvert ? HIGH : LOW);
  bool high = true;
  for (int i = 0; i < len; i++) {
    uint16_t us = pulses[i];
    if (us > 0) {
      bool level = high;
      if (g_txInvert) level = !level;
      digitalWrite(TX_PIN, level ? HIGH : LOW);
      delayMicroseconds(us);
    }
    high = !high;
  }
  digitalWrite(TX_PIN, g_txInvert ? HIGH : LOW);
}

static void handleRoot(AsyncWebServerRequest* req) {
  if (!LittleFS.exists("/index.html")) {
    req->send(404, "text/plain", "index.html not found. Run: pio run -t buildfs && pio run -t uploadfs");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(LittleFS, "/index.html", "text/html");
  resp->addHeader("Cache-Control", "max-age=3600");
  req->send(resp);
}

#ifndef TRANSCEIVER_ONLY
static void handleLastRf(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["seq"] = rfCaptureGetLastSeq();
  doc["length"] = rfCaptureGetLastLength();
  JsonArray arr = doc["pulses"].to<JsonArray>();
  uint16_t pulses[RF_CAPTURE_MAX_PULSES];
  int n = rfCaptureGetLastPulses(pulses, RF_CAPTURE_MAX_PULSES);
  for (int i = 0; i < n; i++) arr.add(pulses[i]);
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// Polling endpoint for RF log: same shape as former WebSocket message (event, seq, length, pulses, recognized, command).
static void handleLastRfEvent(AsyncWebServerRequest* req) {
  int n = rfCaptureGetLastLength();
  if (n == 0) {
    req->send(200, "application/json", "{\"event\":\"rf\",\"seq\":0,\"length\":0,\"pulses\":[],\"recognized\":false}");
    return;
  }
  static uint16_t pulses[RF_CAPTURE_MAX_PULSES];
  int len = rfCaptureGetLastPulses(pulses, RF_CAPTURE_MAX_PULSES);
  char func8[10];
  bool recognized = decodePulsesToFunc(pulses, len, func8);
  const char* name = recognized ? findNameByFunc(func8) : nullptr;

  JsonDocument doc;
  doc["event"] = "rf";
  doc["seq"] = rfCaptureGetLastSeq();
  doc["length"] = len;
  JsonArray arr = doc["pulses"].to<JsonArray>();
  int sample = (len > 20) ? 20 : len;
  for (int i = 0; i < sample; i++) arr.add(pulses[i]);
  doc["recognized"] = (name != nullptr);
  if (name) doc["command"] = name;
  if (recognized && !name) doc["func8"] = func8;
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}
#endif

static void handleAppCss(AsyncWebServerRequest* req) {
  if (!LittleFS.exists("/app.css")) {
    req->send(404, "text/plain", "Not found");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(LittleFS, "/app.css", "text/css");
  resp->addHeader("Cache-Control", "max-age=3600");
  req->send(resp);
}

static void handleSettings(AsyncWebServerRequest* req) {
  if (!LittleFS.exists("/settings.html")) {
    req->send(404, "text/plain", "settings.html not found");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(LittleFS, "/settings.html", "text/html");
  resp->addHeader("Cache-Control", "max-age=3600");
  req->send(resp);
}

static void handleDebug(AsyncWebServerRequest* req) {
  if (!LittleFS.exists("/debug.html")) {
    req->send(404, "text/plain", "debug.html not found");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(LittleFS, "/debug.html", "text/html");
  resp->addHeader("Cache-Control", "max-age=3600");
  req->send(resp);
}

static void handleAppJs(AsyncWebServerRequest* req) {
  if (!LittleFS.exists("/app.js")) {
    req->send(404, "text/plain", "Not found");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(LittleFS, "/app.js", "application/javascript");
  resp->addHeader("Cache-Control", "max-age=3600");
  req->send(resp);
}

static void serveJsFile(AsyncWebServerRequest* req, const char* path) {
  if (!LittleFS.exists(path)) {
    req->send(404, "text/plain", "Not found");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(LittleFS, path, "application/javascript");
  resp->addHeader("Cache-Control", "max-age=3600");
  req->send(resp);
}

static void handleManifest(AsyncWebServerRequest* req) {
  if (!LittleFS.exists("/manifest.json")) {
    req->send(404, "text/plain", "Not found");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(LittleFS, "/manifest.json", "application/manifest+json");
  resp->addHeader("Cache-Control", "max-age=86400");
  req->send(resp);
}

static void handleIcon192(AsyncWebServerRequest* req) {
  if (!LittleFS.exists("/icon-192.png")) {
    req->send(404, "text/plain", "Not found");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(LittleFS, "/icon-192.png", "image/png");
  resp->addHeader("Cache-Control", "max-age=86400");
  req->send(resp);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  printf("[HB] --- Harbor Breeze Fan Control boot ---\n");
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, g_txInvert ? HIGH : LOW);

  if (!LittleFS.begin(true)) {
    printf("[HB] LittleFS mount failed\n");
  } else {
    File f = LittleFS.open("/settings.json", "r");
    if (f) {
      JsonDocument doc;
      if (deserializeJson(doc, f.readString()) == DeserializationError::Ok) {
        if (doc["tx_invert"].is<int>()) {
          int v = doc["tx_invert"].as<int>();
          if (v == 0 || v == 1) { g_txInvert = v; printf("[HB] settings: tx_invert=%d\n", g_txInvert); }
        }
      }
      f.close();
    }
  }

#ifndef TRANSCEIVER_ONLY
  rfCaptureBegin();
  printf("[HB] RF receive on GPIO 5 (point remote and press a button, then GET /last-rf)\n");
#else
  printf("[HB] Transceiver-only build: no receiver (GPIO 5 unused)\n");
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  printf("[HB] Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    printf(".");
  }
  printf("\n[HB] IP: %s\n", WiFi.localIP().toString().c_str());
  printf("[HB] http://%s/\n", WiFi.localIP().toString().c_str());

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = 0;
  for (int i = 0; i < 20; i++) {
    delay(500);
    now = time(nullptr);
    if (now > 1600000000) break;  // valid time
  }
  if (now > 1600000000) printf("[HB] NTP synced\n");
  else printf("[HB] NTP not synced, delay countdown may be wrong\n");

  stateBegin();

  server.on(AsyncURIMatcher::exact("/"), HTTP_GET, handleRoot);
  server.on(AsyncURIMatcher::exact("/settings"), HTTP_GET, handleSettings);
  server.on(AsyncURIMatcher::exact("/debug"), HTTP_GET, handleDebug);
  server.on(AsyncURIMatcher::exact("/app.css"), HTTP_GET, handleAppCss);
  server.on(AsyncURIMatcher::exact("/app.js"), HTTP_GET, handleAppJs);
  server.on(AsyncURIMatcher::exact("/controls.js"), HTTP_GET, [](AsyncWebServerRequest* req) { serveJsFile(req, "/controls.js"); });
  server.on(AsyncURIMatcher::exact("/settings.js"), HTTP_GET, [](AsyncWebServerRequest* req) { serveJsFile(req, "/settings.js"); });
  server.on(AsyncURIMatcher::exact("/debug.js"), HTTP_GET, [](AsyncWebServerRequest* req) { serveJsFile(req, "/debug.js"); });
  server.on(AsyncURIMatcher::exact("/manifest.json"), HTTP_GET, handleManifest);
  server.on(AsyncURIMatcher::exact("/icon-192.png"), HTTP_GET, handleIcon192);
  server.on("/ip", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/plain", WiFi.localIP().toString());
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
#ifdef TRANSCEIVER_ONLY
    doc["transceiver_only"] = true;
#else
    doc["transceiver_only"] = false;
#endif
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Hub protocol: same as github.com/enlilodisho/harbor-breeze-hub (400/500/850/950 µs, 12 repeats, no gap).
  // Static buffer avoids 2KB on async handler stack (stack overflow after reflash can break transmission).
  // If it worked before and stopped after uploadfs: try Debug & settings → TX invert (0 vs 1); uploadfs wipes saved settings.
  server.on("/send-hub", HTTP_GET, [](AsyncWebServerRequest* req) {
    String cmd = req->hasParam("cmd") ? req->getParam("cmd")->value() : "light_toggle";
    cmd.trim();
    static uint16_t pulses[HB_HUB_MAX_PULSES];
    int n = 0;
    if (cmd.equals("light_toggle")) {
      n = harborBreezeHubLightTogglePulses(pulses, HB_HUB_MAX_PULSES);
    } else if (cmd.equals("light_dim")) {
      n = harborBreezeHubLightDimPulses(pulses, HB_HUB_MAX_PULSES);
    } else if (cmd.equals("fan_off") || cmd.equals("fan_power")) {
      n = harborBreezeHubFanPowerPulses(pulses, HB_HUB_MAX_PULSES);
    } else if (cmd.equals("fan_speed_1")) n = harborBreezeHubFanSpeedPulses(1, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_speed_2")) n = harborBreezeHubFanSpeedPulses(2, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_speed_3")) n = harborBreezeHubFanSpeedPulses(3, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_speed_4")) n = harborBreezeHubFanSpeedPulses(4, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_speed_5")) n = harborBreezeHubFanSpeedPulses(5, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_speed_6")) n = harborBreezeHubFanSpeedPulses(6, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("nature_breeze")) {
      n = harborBreezeHubBreezePulses(pulses, HB_HUB_MAX_PULSES);
    } else if (cmd.equals("fan_direction_summer")) {
      n = harborBreezeHubRotateCcwPulses(pulses, HB_HUB_MAX_PULSES);
    } else if (cmd.equals("fan_direction_winter")) {
      n = harborBreezeHubRotateCwPulses(pulses, HB_HUB_MAX_PULSES);
    } else if (cmd.equals("home_shield")) {
      bool raw = req->hasParam("raw") && req->getParam("raw")->value().toInt() == 1;
      bool useLearned = req->hasParam("learned") && req->getParam("learned")->value().toInt() == 1;
      if (raw) {
#ifdef TRANSCEIVER_ONLY
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Transceiver-only build: no receiver. Use learned=1 or restore Home Shield from backup.\"}");
        return;
#else
        static uint16_t rawCapBuf[RF_CAPTURE_MAX_PULSES];
        int capLen = rfCaptureGetLastPulses(rawCapBuf, RF_CAPTURE_MAX_PULSES);
        if (capLen < HOME_SHIELD_MIN_PULSES) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"No capture or too short for raw. Press remote Home Shield, then GET /last-rf, then send-hub?cmd=home_shield&raw=1 immediately.\"}");
          return;
        }
        int reps = HB_HUB_MAX_PULSES / capLen;
        if (reps > 12) reps = 12;
        if (reps < 2) reps = 2;
        n = 0;
        for (int r = 0; r < reps && n + capLen <= HB_HUB_MAX_PULSES; r++)
          for (int i = 0; i < capLen; i++) pulses[n++] = rawCapBuf[i];
        if (n == 0) { req->send(500, "application/json", "{\"ok\":false,\"error\":\"Raw build failed\"}"); return; }
#endif
      } else if (useLearned && homeShieldLearned()) {
        bool useGap = req->hasParam("gap") && req->getParam("gap")->value().toInt() == 1;
        n = homeShieldBuildPulsesEx(pulses, HB_HUB_MAX_PULSES, useGap);
      } else {
        n = harborBreezeHubHomeShieldPulses(pulses, HB_HUB_MAX_PULSES);
      }
    }
    if (n <= 0) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Hub: unknown cmd. Use light_toggle, light_dim, fan_off, fan_speed_1..6, nature_breeze, fan_direction_summer, fan_direction_winter, home_shield\"}");
      return;
    }
    sendPulses(pulses, n);
    stateUpdateFromCmd(cmd.c_str());
    printf("[HB] Sent hub: %s (%d pulses)\n", cmd.c_str(), n);
    req->send(200, "application/json", "{\"ok\":true,\"cmd\":\"" + cmd + "\",\"protocol\":\"hub\"}");
  });

  // Return the currently learned Home Shield frame (in-memory; matches what send uses). GET /learned-home-shield
  server.on("/learned-home-shield", HTTP_GET, [](AsyncWebServerRequest* req) {
    int n = s_homeShieldLen;
    if (n < HOME_SHIELD_MIN_PULSES || n > HOME_SHIELD_MAX_PULSES) {
      req->send(200, "application/json", "{\"learned\":false,\"message\":\"No Home Shield frame learned yet.\"}");
      return;
    }
    JsonDocument doc;
    doc["learned"] = true;
    doc["frame_length"] = n;
    JsonArray arr = doc["frame"].to<JsonArray>();
    for (int i = 0; i < n; i++) arr.add(s_homeShieldFrame[i]);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Decode the learned Home Shield frame to hub symbols and return the 10 command symbols (for pasting into HUB_HOME_SHIELD in harbor_breeze.cpp). GET /learned-home-shield-symbols
  server.on("/learned-home-shield-symbols", HTTP_GET, [](AsyncWebServerRequest* req) {
    int n = s_homeShieldLen;
    JsonDocument doc;
    doc["learned"] = (n >= HOME_SHIELD_MIN_PULSES && n <= HOME_SHIELD_MAX_PULSES);
    if (n < HOME_SHIELD_MIN_PULSES || n > HOME_SHIELD_MAX_PULSES) {
      doc["ok"] = false;
      doc["error"] = "No Home Shield frame learned yet.";
      String out;
      serializeJson(doc, out);
      req->send(200, "application/json", out);
      return;
    }
    if (n < 50) {
      doc["ok"] = false;
      doc["error"] = "Stored frame has " + String(n) + " pulses; hub decode needs 50. Re-learn by pressing Home Shield on remote, then Learn from last RF.";
      doc["frame_length"] = n;
      String out;
      serializeJson(doc, out);
      req->send(200, "application/json", out);
      return;
    }
    char symbols_buf[120];
    const char* matched_cmd = nullptr;
    int decoded = harborBreezeHubDecodePulses(s_homeShieldFrame, n, symbols_buf, sizeof(symbols_buf), &matched_cmd);
    doc["ok"] = (decoded == 25);
    doc["frame_length"] = n;
    if (decoded == 25) {
      doc["symbols"] = symbols_buf;
      if (matched_cmd) doc["matched_cmd"] = matched_cmd;
      // Parse "SL, SL, SS, ..." to get last 10 symbols as array for HUB_HOME_SHIELD
      JsonArray cmdArr = doc["command_symbols"].to<JsonArray>();
      const char* p = symbols_buf;
      char sym[25][3];
      int i = 0;
      for (; i < 25 && *p; i++) {
        while (*p == ',' || *p == ' ') p++;
        if (!p[0] || !p[1]) break;
        sym[i][0] = p[0];
        sym[i][1] = p[1];
        sym[i][2] = '\0';
        p += 2;
      }
      if (i >= 25) {
        for (int j = 15; j < 25; j++) cmdArr.add(sym[j]);
      }
    } else {
      doc["error"] = "Stored frame does not decode as hub protocol (expected 400/500/850/950 µs pairs).";
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

#ifndef TRANSCEIVER_ONLY
  // Learn Home Shield from last RF capture (20–50 pulses). Call after pressing remote's Home Shield and refreshing last RF.
  server.on("/learn-home-shield", HTTP_POST, [](AsyncWebServerRequest* req) {
    uint16_t pulses[RF_CAPTURE_MAX_PULSES];
    int n = rfCaptureGetLastPulses(pulses, RF_CAPTURE_MAX_PULSES);
    if (n < HOME_SHIELD_MIN_PULSES) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Capture too short (need at least 20 pulses). Press the remote\\'s Home Shield, click Refresh last RF, then try again.\"}");
      return;
    }
    if (!homeShieldSaveFrame(pulses, n)) {
      req->send(500, "application/json", "{\"ok\":false,\"error\":\"Save failed\"}");
      return;
    }
    printf("[HB] Learned Home Shield from %d pulses\n", n);
    req->send(200, "application/json", "{\"ok\":true,\"message\":\"Home Shield learned. You can now use the Home Shield button.\"}");
  });
  server.on("/learn-home-shield", HTTP_GET, [](AsyncWebServerRequest* req) {
    uint16_t pulses[RF_CAPTURE_MAX_PULSES];
    int n = rfCaptureGetLastPulses(pulses, RF_CAPTURE_MAX_PULSES);
    if (n < HOME_SHIELD_MIN_PULSES) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Capture too short (need at least 20 pulses). Press the remote\\'s Home Shield, click Refresh last RF, then try again.\"}");
      return;
    }
    if (!homeShieldSaveFrame(pulses, n)) {
      req->send(500, "application/json", "{\"ok\":false,\"error\":\"Save failed\"}");
      return;
    }
    printf("[HB] Learned Home Shield from %d pulses\n", n);
    req->send(200, "application/json", "{\"ok\":true,\"message\":\"Home Shield learned. You can now use the Home Shield button.\"}");
  });
#endif

  // Restore Home Shield from backup (e.g. after reflash). POST body: {"frame": [399, 655, 1046, ...]}
  server.on("/restore-home-shield", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      handleJsonRequest(req, data, len, index, total, [req](JsonDocument& doc) {
        JsonArray arr = doc["frame"].as<JsonArray>();
        if (arr.isNull() || arr.size() < (size_t)HOME_SHIELD_MIN_PULSES || arr.size() > (size_t)HOME_SHIELD_MAX_PULSES) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"Need frame array with 20–50 numbers\"}");
          return;
        }
        uint16_t frame[HOME_SHIELD_MAX_PULSES];
        size_t n = arr.size();
        for (size_t i = 0; i < n; i++) frame[i] = (uint16_t)arr[i].as<uint32_t>();
        if (!homeShieldSaveFrameFromArray(frame, (int)n)) {
          req->send(500, "application/json", "{\"ok\":false,\"error\":\"Restore failed\"}");
          return;
        }
        printf("[HB] Restored Home Shield from backup (%zu pulses)\n", n);
        req->send(200, "application/json", "{\"ok\":true,\"message\":\"Home Shield restored from backup.\"}");
      });
    });

  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* req) {
    uint32_t delayEnd = stateGetDelayEnd();
    time_t now = time(nullptr);
    int remaining = 0;
    if (delayEnd > 0 && (time_t)delayEnd > now) {
      remaining = (int)((time_t)delayEnd - now);
    } else if (delayEnd > 0) {
      stateSetDelayEnd(0);
    }
    JsonDocument doc;
    doc["light_on"] = stateGetLight() != 0;
    doc["fan_direction"] = stateGetDirection() == 1 ? "winter" : "summer";
    doc["fan_speed"] = stateGetSpeed();
    doc["delay_active"] = remaining > 0;
    doc["delay_remaining_sec"] = remaining;
    doc["home_shield_learned"] = homeShieldLearned();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/state", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      handleJsonRequest(req, data, len, index, total, [req](JsonDocument& doc) {
        if (doc["light_on"].is<bool>()) {
          stateSetLight(doc["light_on"].as<bool>() ? 1 : 0);
        }
        if (doc["fan_speed"].is<int>()) {
          int s = doc["fan_speed"].as<int>();
          if (s >= 0 && s <= 6) stateSetSpeed(s);
        }
        const char* dir = doc["fan_direction"].as<const char*>();
        if (dir) {
          if (strcmp(dir, "winter") == 0) stateSetDirection(1);
          else if (strcmp(dir, "summer") == 0) stateSetDirection(0);
        }
        req->send(200, "application/json", "{\"ok\":true}");
      });
    });

  server.on("/send", HTTP_GET, [](AsyncWebServerRequest* req) {
    String cmd = req->hasParam("cmd") ? req->getParam("cmd")->value() : "";
    if (cmd.isEmpty()) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing cmd\"}");
      return;
    }
    const char* func = findFunc(cmd.c_str());
    if (!func) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Unknown command\"}");
      return;
    }
    uint16_t pulses[HB_MAX_PULSES];
    int n = harborBreezeCommandPulses(func, pulses, HB_MAX_PULSES);
    if (n <= 0) {
      req->send(500, "application/json", "{\"ok\":false,\"error\":\"Encode failed\"}");
      return;
    }
    sendPulses(pulses, n);
    printf("[HB] Sent %s (%d pulses)\n", cmd.c_str(), n);
    req->send(200, "application/json", "{\"ok\":true,\"cmd\":\"" + cmd + "\"}");
  });

  server.on("/send", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      handleJsonRequest(req, data, len, index, total, [req](JsonDocument& doc) {
        const char* cmd = doc["cmd"].as<const char*>();
        if (!cmd || !*cmd) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing cmd\"}");
          return;
        }
        const char* func = findFunc(cmd);
        if (!func) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"Unknown command\"}");
          return;
        }
        uint16_t pulses[HB_MAX_PULSES];
        int n = harborBreezeCommandPulses(func, pulses, HB_MAX_PULSES);
        if (n <= 0) {
          req->send(500, "application/json", "{\"ok\":false,\"error\":\"Encode failed\"}");
          return;
        }
        sendPulses(pulses, n);
        stateUpdateFromCmd(cmd);
        printf("[HB] Sent %s (%d pulses)\n", cmd, n);
        req->send(200, "application/json", "{\"ok\":true,\"cmd\":\"" + String(cmd) + "\"}");
      });
    });

  server.on("/commands", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray arr = doc["commands"].to<JsonArray>();
    for (int i = 0; i < numCommands; i++)
      arr.add(commands[i].name);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

#ifndef TRANSCEIVER_ONLY
  // Debug: return encoded pulse array for a command (no TX). Same JSON shape as /last-rf for comparison.
  server.on("/last-rf", HTTP_GET, handleLastRf);
  server.on("/last-rf-event", HTTP_GET, handleLastRfEvent);

  // Debug: replay the last captured RF signal (useful for testing if captured remote works)
  server.on("/replay-last-rf", HTTP_GET, [](AsyncWebServerRequest* req) {
    int len = rfCaptureGetLastLength();
    if (len == 0) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"No RF captured yet\"}");
      return;
    }
    static uint16_t pulses[RF_CAPTURE_MAX_PULSES];
    int n = rfCaptureGetLastPulses(pulses, RF_CAPTURE_MAX_PULSES);
    printf("[HB] Replaying %d captured pulses\n", n);
    sendPulses(pulses, n);
    JsonDocument doc;
    doc["ok"] = true;
    doc["replayed_pulses"] = n;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });
#endif

  // Debug: show what preamble and function we're using
  server.on("/debug-code", HTTP_GET, [](AsyncWebServerRequest* req) {
    String cmd = req->hasParam("cmd") ? req->getParam("cmd")->value() : "light_toggle";
    const char* func = findFunc(cmd.c_str());
    if (!func) {
      req->send(400, "application/json", "{\"error\":\"Unknown command\"}");
      return;
    }
    // Build full code string
    char code[26];
    memcpy(code, HB_PREAMBLE_DIP1, 17);
    memcpy(code + 17, func, 8);
    code[25] = '\0';
    
    JsonDocument doc;
    doc["cmd"] = cmd;
    doc["preamble"] = HB_PREAMBLE_DIP1;
    doc["func"] = func;
    doc["full_code"] = code;
    doc["short_us"] = HB_SHORT_US;
    doc["long_us"] = HB_LONG_US;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Debug: send raw pulses from JSON array - POST body: {"pulses": [940, 430, 940, 430, ...]}
  server.on("/send-raw", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      handleJsonRequest(req, data, len, index, total, [req](JsonDocument& doc) {
        JsonArray arr = doc["pulses"].as<JsonArray>();
        if (arr.isNull() || arr.size() == 0) {
          req->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing pulses array\"}");
          return;
        }
        static uint16_t pulses[512];
        int n = 0;
        for (JsonVariant v : arr) {
          if (n >= 512) break;
          pulses[n++] = v.as<uint16_t>();
        }
        printf("[HB] Sent %d raw pulses\n", n);
        sendPulses(pulses, n);

        JsonDocument resp;
        resp["ok"] = true;
        resp["sent_pulses"] = n;
        String out;
        serializeJson(resp, out);
        req->send(200, "application/json", out);
      });
    });

  ws.onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      printf("[HB] WebSocket client connected\n");
    }
  });
  server.addHandler(&ws);

  server.on("/debug-pulses", HTTP_GET, [](AsyncWebServerRequest* req) {
    String cmd = req->hasParam("cmd") ? req->getParam("cmd")->value() : "";
    if (cmd.isEmpty()) {
      req->send(400, "application/json", "{\"error\":\"Missing cmd\"}");
      return;
    }
    const char* func = findFunc(cmd.c_str());
    if (!func) {
      req->send(400, "application/json", "{\"error\":\"Unknown command\"}");
      return;
    }
    uint16_t pulses[HB_MAX_PULSES];
    int n = harborBreezeCommandPulses(func, pulses, HB_MAX_PULSES);
    if (n <= 0) {
      req->send(500, "application/json", "{\"error\":\"Encode failed\"}");
      return;
    }
    JsonDocument doc;
    doc["cmd"] = cmd;
    doc["length"] = n;
    JsonArray arr = doc["pulses"].to<JsonArray>();
    for (int i = 0; i < n; i++) arr.add(pulses[i]);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Debug: hub expected pulses (no TX). Same shape as /debug-pulses but for hub protocol.
  server.on("/debug-pulses-hub", HTTP_GET, [](AsyncWebServerRequest* req) {
    String cmd = req->hasParam("cmd") ? req->getParam("cmd")->value() : "light_toggle";
    cmd.trim();
    static uint16_t pulses[HB_HUB_MAX_PULSES];
    int n = 0;
    if (cmd.equals("light_toggle")) n = harborBreezeHubLightTogglePulses(pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("light_dim")) n = harborBreezeHubLightDimPulses(pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_off") || cmd.equals("fan_power")) n = harborBreezeHubFanPowerPulses(pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_speed_1")) n = harborBreezeHubFanSpeedPulses(1, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_speed_2")) n = harborBreezeHubFanSpeedPulses(2, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_speed_3")) n = harborBreezeHubFanSpeedPulses(3, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_speed_4")) n = harborBreezeHubFanSpeedPulses(4, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_speed_5")) n = harborBreezeHubFanSpeedPulses(5, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_speed_6")) n = harborBreezeHubFanSpeedPulses(6, pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("nature_breeze")) n = harborBreezeHubBreezePulses(pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_direction_summer")) n = harborBreezeHubRotateCcwPulses(pulses, HB_HUB_MAX_PULSES);
    else if (cmd.equals("fan_direction_winter")) n = harborBreezeHubRotateCwPulses(pulses, HB_HUB_MAX_PULSES);
    if (n <= 0) {
      req->send(400, "application/json", "{\"error\":\"Hub: unknown cmd\"}");
      return;
    }
    JsonDocument doc;
    doc["cmd"] = cmd;
    doc["protocol"] = "hub";
    doc["length"] = n;
    JsonArray arr = doc["pulses"].to<JsonArray>();
    for (int i = 0; i < n; i++) arr.add(pulses[i]);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

#ifndef TRANSCEIVER_ONLY
  // Decode last RF capture as hub protocol; return symbols and matched command name.
  server.on("/last-rf-decode-hub", HTTP_GET, [](AsyncWebServerRequest* req) {
    static uint16_t pulses[RF_CAPTURE_MAX_PULSES];
    int n = rfCaptureGetLastPulses(pulses, RF_CAPTURE_MAX_PULSES);
    if (n < 50) {
      req->send(200, "application/json", "{\"ok\":false,\"error\":\"No capture or too short. Point remote at receiver and press a button, then GET /last-rf first.\"}");
      return;
    }
    char symbols_buf[120];
    const char* matched_cmd = nullptr;
    int decoded = harborBreezeHubDecodePulses(pulses, n, symbols_buf, sizeof(symbols_buf), &matched_cmd);
    JsonDocument doc;
    doc["ok"] = (decoded == 25);
    doc["length"] = n;
    if (decoded == 25) {
      doc["symbols"] = symbols_buf;
      if (matched_cmd) doc["matched_cmd"] = matched_cmd;
    } else {
      doc["error"] = "Capture does not look like hub protocol (expected 400/500/850/950 µs pairs). Try legacy compare.";
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Verify TX: send a command, wait for receiver to capture, return expected vs captured for comparison.
  server.on("/verify-tx", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (g_verifyTx.active) {
      req->send(429, "application/json", "{\"ok\":false,\"error\":\"Another verify-tx is currently running\"}");
      return;
    }

    String cmd = req->hasParam("cmd") ? req->getParam("cmd")->value() : "light_toggle";
    static uint16_t expectedPulsesBuf[HB_HUB_MAX_PULSES];
    uint16_t* expectedPulses = expectedPulsesBuf;
    int expectedLen = 0;
    if (cmd.equals("home_shield")) {
      expectedLen = homeShieldBuildPulses(expectedPulsesBuf, HB_HUB_MAX_PULSES);
      if (expectedLen <= 0) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Home Shield not learned\"}");
        return;
      }
    } else {
      const char* func = findFunc(cmd.c_str());
      if (!func) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Unknown command\"}");
        return;
      }
      expectedLen = harborBreezeCommandPulses(func, expectedPulsesBuf, HB_MAX_PULSES);
      if (expectedLen <= 0) {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"Encode failed\"}");
        return;
      }
    }

    g_verifyTx.req = req;
    g_verifyTx.cmd = cmd;
    g_verifyTx.expectedLen = expectedLen;
    memcpy(g_verifyTx.expectedPulses, expectedPulses, expectedLen * sizeof(uint16_t));
    g_verifyTx.seqBefore = rfCaptureGetLastSeq();

    // We handle disconnect to avoid writing to a freed request
    req->onDisconnect([]() {
      if (g_verifyTx.active && g_verifyTx.req != nullptr) {
        g_verifyTx.req = nullptr;
      }
    });

    sendPulses(expectedPulses, expectedLen);
    g_verifyTx.waitStartTime = millis();
    g_verifyTx.active = true;
  });
#endif

  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["tx_invert"] = g_txInvert;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/settings", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      handleJsonRequest(req, data, len, index, total, [req](JsonDocument& doc) {
        if (doc["tx_invert"].is<int>()) {
          int v = doc["tx_invert"].as<int>();
          if (v == 0 || v == 1) {
            g_txInvert = v;
            digitalWrite(TX_PIN, g_txInvert ? HIGH : LOW);
            JsonDocument saveDoc;
            saveDoc["tx_invert"] = g_txInvert;
            String saveStr;
            serializeJson(saveDoc, saveStr);
            File f = LittleFS.open("/settings.json", "w");
            if (f) { f.print(saveStr); f.close(); }
            JsonDocument resp;
            resp["ok"] = true;
            resp["tx_invert"] = g_txInvert;
            String out;
            serializeJson(resp, out);
            req->send(200, "application/json", out);
            return;
          }
        }
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"tx_invert must be 0 or 1\"}");
      });
    });

  // Debug endpoint: shows GPIO states and config without transmitting
  server.on("/debug-gpio", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["tx_pin"] = TX_PIN;
    doc["tx_invert"] = g_txInvert;
    doc["tx_pin_state"] = digitalRead(TX_PIN);
#ifndef TRANSCEIVER_ONLY
    doc["rx_pin"] = 5;
    doc["rx_pin_state"] = digitalRead(5);
#endif
    doc["short_us"] = HB_SHORT_US;
    doc["long_us"] = HB_LONG_US;
    doc["gap_ms"] = HB_GAP_MS;
    doc["repeats"] = HB_REPEATS;
    doc["preamble"] = HB_PREAMBLE_DIP1;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Debug endpoint: toggle TX pin manually to test GPIO electrical connection
  // GET /debug-toggle-tx?state=1 sets HIGH, ?state=0 sets LOW
  server.on("/debug-toggle-tx", HTTP_GET, [](AsyncWebServerRequest* req) {
    int state = req->hasParam("state") ? req->getParam("state")->value().toInt() : -1;
    if (state < 0 || state > 1) {
      req->send(400, "application/json", "{\"error\":\"Use ?state=0 or ?state=1\"}");
      return;
    }
    pinMode(TX_PIN, OUTPUT);
    digitalWrite(TX_PIN, state);
    int readBack = digitalRead(TX_PIN);
    printf("[HB] debug-toggle-tx GPIO%d=%d readback=%d\n", TX_PIN, state, readBack);
    JsonDocument doc;
    doc["tx_pin"] = TX_PIN;
    doc["set_to"] = state;
    doc["readback"] = readBack;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "Not found");
  });

  server.begin();
  printf("[HB] HTTP server started\n");
}

void loop() {
#ifndef TRANSCEIVER_ONLY
  rfCaptureLoop();

  if (g_verifyTx.active && millis() - g_verifyTx.waitStartTime >= 650) {
    if (g_verifyTx.req != nullptr) {
      uint32_t seqAfter = rfCaptureGetLastSeq();
      int capturedLen = rfCaptureGetLastLength();
      uint16_t capturedPulses[RF_CAPTURE_MAX_PULSES];
      int nCaptured = rfCaptureGetLastPulses(capturedPulses, RF_CAPTURE_MAX_PULSES);

      JsonDocument doc;
      doc["ok"] = true;
      doc["cmd"] = g_verifyTx.cmd;
      doc["expected_length"] = g_verifyTx.expectedLen;
      doc["captured_length"] = nCaptured;
      doc["seq_before"] = g_verifyTx.seqBefore;
      doc["seq_after"] = seqAfter;
      doc["new_capture_during_test"] = (seqAfter > g_verifyTx.seqBefore);
      bool txSeen = (nCaptured >= 20 && nCaptured >= (g_verifyTx.expectedLen / 2) && nCaptured <= (g_verifyTx.expectedLen * 2));
      doc["tx_seen_by_receiver"] = txSeen;

      JsonArray expArr = doc["expected_sample"].to<JsonArray>();
      int expSample = (g_verifyTx.expectedLen > 15) ? 15 : g_verifyTx.expectedLen;
      for (int i = 0; i < expSample; i++) expArr.add(g_verifyTx.expectedPulses[i]);

      JsonArray capArr = doc["captured_sample"].to<JsonArray>();
      int capSample = (nCaptured > 15) ? 15 : nCaptured;
      for (int i = 0; i < capSample; i++) capArr.add(capturedPulses[i]);

      String out;
      serializeJson(doc, out);
      g_verifyTx.req->send(200, "application/json", out);
    }
    g_verifyTx.active = false;
  }

  // Broadcast new RF over WebSocket with minimal payload (no pulse array) to reduce allocation and stack.
  if (rfCaptureHasNew()) {
    if (ws.count() > 0) {
      static uint16_t pulses[RF_CAPTURE_MAX_PULSES];
      int n = rfCaptureGetLastPulses(pulses, RF_CAPTURE_MAX_PULSES);
      char func8[10];
      bool recognized = decodePulsesToFunc(pulses, n, func8);
      const char* name = recognized ? findNameByFunc(func8) : nullptr;

      static char buf[192];
      int len;
      if (name) {
        len = snprintf(buf, sizeof(buf),
                      "{\"event\":\"rf\",\"seq\":%u,\"length\":%d,\"recognized\":true,\"command\":\"%s\"}",
                      (unsigned)rfCaptureGetLastSeq(), n, name);
      } else if (recognized) {
        len = snprintf(buf, sizeof(buf),
                      "{\"event\":\"rf\",\"seq\":%u,\"length\":%d,\"recognized\":false,\"func8\":\"%s\"}",
                      (unsigned)rfCaptureGetLastSeq(), n, func8);
      } else {
        len = snprintf(buf, sizeof(buf),
                      "{\"event\":\"rf\",\"seq\":%u,\"length\":%d,\"recognized\":false}",
                      (unsigned)rfCaptureGetLastSeq(), n);
      }
      if (len > 0 && (size_t)len < sizeof(buf)) {
        ws.textAll(buf, (size_t)len);
      }
    }
    rfCaptureClearNew();  // Always clear, even if no WS clients
  }
#endif

  static uint32_t lastStatusPrint = 0;
  if (millis() - lastStatusPrint >= 1000) {
    lastStatusPrint = millis();
    if (WiFi.status() == WL_CONNECTED) {
      printf("[HB] IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      printf("[HB] (WiFi not connected)\n");
    }
  }
}
