#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "harbor_breeze.h"
#include "rf_capture.h"

#ifndef WIFI_SSID
#define WIFI_SSID "your-ssid"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "your-password"
#endif

#define TX_PIN 6
// Default: 1 = active-low (many 315 MHz modules). Overridable at runtime via GET/POST /settings.
#define TX_INVERT_DEFAULT 1

static int g_txInvert = TX_INVERT_DEFAULT;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

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

static void sendPulses(uint16_t* pulses, int len) {
  // #region agent log
  uint32_t totalUs = 0;
  for (int i = 0; i < len; i++) totalUs += pulses[i];
  printf("[HB-DEBUG-79164a] sendPulses pin=%d len=%d totalUs=%lu short=%d long=%d\n",
         TX_PIN, len, (unsigned long)totalUs, HB_SHORT_US, HB_LONG_US);
  printf("[HB-DEBUG-79164a] first10: %u %u %u %u %u %u %u %u %u %u\n",
         len > 0 ? pulses[0] : 0, len > 1 ? pulses[1] : 0,
         len > 2 ? pulses[2] : 0, len > 3 ? pulses[3] : 0,
         len > 4 ? pulses[4] : 0, len > 5 ? pulses[5] : 0,
         len > 6 ? pulses[6] : 0, len > 7 ? pulses[7] : 0,
         len > 8 ? pulses[8] : 0, len > 9 ? pulses[9] : 0);
  // #endregion
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
  req->send(LittleFS, "/index.html", "text/html");
}

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

static void handleAppCss(AsyncWebServerRequest* req) {
  if (!LittleFS.exists("/app.css")) {
    req->send(404, "text/plain", "Not found");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(LittleFS, "/app.css", "text/css");
  resp->addHeader("Cache-Control", "max-age=86400");
  req->send(resp);
}

static void handleAppJs(AsyncWebServerRequest* req) {
  if (!LittleFS.exists("/app.js")) {
    req->send(404, "text/plain", "Not found");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(LittleFS, "/app.js", "application/javascript");
  resp->addHeader("Cache-Control", "max-age=86400");
  req->send(resp);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  printf("[HB] --- Harbor Breeze Fan Control boot ---\n");
  // #region agent log
  printf("[HB-DEBUG-79164a] TX_PIN=%d tx_invert=%d\n", TX_PIN, g_txInvert);
  printf("[HB-DEBUG-79164a] Expected: short=%d long=%d gap=%d repeats=%d\n", HB_SHORT_US, HB_LONG_US, HB_GAP_MS, HB_REPEATS);
  printf("[HB-DEBUG-79164a] Preamble DIP1=%s\n", HB_PREAMBLE_DIP1);
  // #endregion
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

  rfCaptureBegin();
  printf("[HB] RF receive on GPIO 5 (point remote and press a button, then GET /last-rf)\n");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  printf("[HB] Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    printf(".");
  }
  printf("\n[HB] IP: %s\n", WiFi.localIP().toString().c_str());
  printf("[HB] http://%s/\n", WiFi.localIP().toString().c_str());

  server.on(AsyncURIMatcher::exact("/"), HTTP_GET, handleRoot);
  server.on(AsyncURIMatcher::exact("/app.css"), HTTP_GET, handleAppCss);
  server.on(AsyncURIMatcher::exact("/app.js"), HTTP_GET, handleAppJs);
  server.on("/ip", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/plain", WiFi.localIP().toString());
  });

  // Hub protocol: same as github.com/enlilodisho/harbor-breeze-hub (400/500/850/950 µs, 12 repeats, no gap).
  server.on("/send-hub", HTTP_GET, [](AsyncWebServerRequest* req) {
    String cmd = req->hasParam("cmd") ? req->getParam("cmd")->value() : "light_toggle";
    uint16_t pulses[HB_HUB_MAX_PULSES];
    int n = 0;
    if (cmd == "light_toggle") {
      n = harborBreezeHubLightTogglePulses(pulses, HB_HUB_MAX_PULSES);
    } else if (cmd == "fan_off" || cmd == "fan_power") {
      n = harborBreezeHubFanPowerPulses(pulses, HB_HUB_MAX_PULSES);
    }
    if (n <= 0) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Hub protocol: use cmd=light_toggle or cmd=fan_off\"}");
      return;
    }
    sendPulses(pulses, n);
    printf("[HB] Sent hub protocol: %s (%d pulses)\n", cmd.c_str(), n);
    req->send(200, "application/json", "{\"ok\":true,\"cmd\":\"" + cmd + "\",\"protocol\":\"hub\"}");
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
    // #region agent log
    printf("[HB] GET /send cmd=%s func=%s n=%d\n", cmd.c_str(), func ? func : "null", n);
    // #endregion
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
      static String body;
      if (index == 0) body = "";
      if (len) body.concat((const char*)data, len);
      if (index + len != total) return;

      JsonDocument doc;
      if (deserializeJson(doc, body)) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
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
      // #region agent log
      printf("[HB] POST /send cmd=%s func=%s n=%d\n", cmd ? cmd : "null", func ? func : "null", n);
      // #endregion
      if (n <= 0) {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"Encode failed\"}");
        return;
      }
      sendPulses(pulses, n);
      printf("[HB] Sent %s (%d pulses)\n", cmd, n);
      req->send(200, "application/json", "{\"ok\":true,\"cmd\":\"" + String(cmd) + "\"}");
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

  // Debug: return encoded pulse array for a command (no TX). Same JSON shape as irproject /last-rf for comparison.
  server.on("/last-rf", HTTP_GET, handleLastRf);
  server.on("/last-rf-event", HTTP_GET, handleLastRfEvent);

  // #region agent log
  // Debug: replay the last captured RF signal (useful for testing if captured remote works)
  server.on("/replay-last-rf", HTTP_GET, [](AsyncWebServerRequest* req) {
    int len = rfCaptureGetLastLength();
    if (len == 0) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"No RF captured yet\"}");
      return;
    }
    static uint16_t pulses[RF_CAPTURE_MAX_PULSES];
    int n = rfCaptureGetLastPulses(pulses, RF_CAPTURE_MAX_PULSES);
    printf("[HB-DEBUG-79164a] Replaying %d captured pulses\n", n);
    sendPulses(pulses, n);
    JsonDocument doc;
    doc["ok"] = true;
    doc["replayed_pulses"] = n;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

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
    printf("[HB-DEBUG-79164a] /debug-code: %s\n", out.c_str());
    req->send(200, "application/json", out);
  });
  
  // Debug: send raw pulses from JSON array - POST body: {"pulses": [940, 430, 940, 430, ...]}
  server.on("/send-raw", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      static String body;
      if (index == 0) body = "";
      if (len) body.concat((const char*)data, len);
      if (index + len != total) return;

      JsonDocument doc;
      if (deserializeJson(doc, body)) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
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
      printf("[HB-DEBUG-79164a] Sending %d raw pulses\n", n);
      sendPulses(pulses, n);
      
      JsonDocument resp;
      resp["ok"] = true;
      resp["sent_pulses"] = n;
      String out;
      serializeJson(resp, out);
      req->send(200, "application/json", out);
    });
  // #endregion

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

  // Verify TX: send a command, wait for receiver to capture, return expected vs captured for comparison.
  server.on("/verify-tx", HTTP_GET, [](AsyncWebServerRequest* req) {
    String cmd = req->hasParam("cmd") ? req->getParam("cmd")->value() : "light_toggle";
    const char* func = findFunc(cmd.c_str());
    if (!func) {
      req->send(400, "application/json", "{\"ok\":false,\"error\":\"Unknown command\"}");
      return;
    }
    uint16_t expectedPulses[HB_MAX_PULSES];
    int expectedLen = harborBreezeCommandPulses(func, expectedPulses, HB_MAX_PULSES);
    if (expectedLen <= 0) {
      req->send(500, "application/json", "{\"ok\":false,\"error\":\"Encode failed\"}");
      return;
    }
    uint32_t seqBefore = rfCaptureGetLastSeq();
    sendPulses(expectedPulses, expectedLen);
    delay(350);
    for (int i = 0; i < 30; i++) {
      rfCaptureLoop();
      delay(10);
    }
    uint32_t seqAfter = rfCaptureGetLastSeq();
    int capturedLen = rfCaptureGetLastLength();
    uint16_t capturedPulses[RF_CAPTURE_MAX_PULSES];
    int nCaptured = rfCaptureGetLastPulses(capturedPulses, RF_CAPTURE_MAX_PULSES);

    JsonDocument doc;
    doc["ok"] = true;
    doc["cmd"] = cmd;
    doc["expected_length"] = expectedLen;
    doc["captured_length"] = nCaptured;
    doc["seq_before"] = seqBefore;
    doc["seq_after"] = seqAfter;
    doc["new_capture_during_test"] = (seqAfter > seqBefore);
    bool txSeen = (nCaptured >= 20 && nCaptured >= (expectedLen / 2) && nCaptured <= (expectedLen * 2));
    doc["tx_seen_by_receiver"] = txSeen;
    JsonArray expArr = doc["expected_sample"].to<JsonArray>();
    int expSample = (expectedLen > 15) ? 15 : expectedLen;
    for (int i = 0; i < expSample; i++) expArr.add(expectedPulses[i]);
    JsonArray capArr = doc["captured_sample"].to<JsonArray>();
    int capSample = (nCaptured > 15) ? 15 : nCaptured;
    for (int i = 0; i < capSample; i++) capArr.add(capturedPulses[i]);
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["tx_invert"] = g_txInvert;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/settings", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      static String body;
      if (index == 0) body = "";
      if (len) body.concat((const char*)data, len);
      if (index + len != total) return;

      JsonDocument doc;
      if (deserializeJson(doc, body)) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
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

  // #region agent log
  // Debug endpoint: shows GPIO states and config without transmitting
  server.on("/debug-gpio", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["tx_pin"] = TX_PIN;
    doc["tx_invert"] = g_txInvert;
    doc["tx_pin_state"] = digitalRead(TX_PIN);
    doc["rx_pin"] = 5;
    doc["rx_pin_state"] = digitalRead(5);
    doc["short_us"] = HB_SHORT_US;
    doc["long_us"] = HB_LONG_US;
    doc["gap_ms"] = HB_GAP_MS;
    doc["repeats"] = HB_REPEATS;
    doc["preamble"] = HB_PREAMBLE_DIP1;
    String out;
    serializeJson(doc, out);
    printf("[HB-DEBUG-79164a] /debug-gpio: %s\n", out.c_str());
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
    printf("[HB-DEBUG-79164a] /debug-toggle-tx: set GPIO%d to %d, readback=%d\n", TX_PIN, state, readBack);
    JsonDocument doc;
    doc["tx_pin"] = TX_PIN;
    doc["set_to"] = state;
    doc["readback"] = readBack;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });
  // #endregion

  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "Not found");
  });

  server.begin();
  printf("[HB] HTTP server started\n");
}

void loop() {
  rfCaptureLoop();

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
