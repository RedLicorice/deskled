#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include <vector>

#include "auth.h"
#include "config.h"
#include "console.h"
#include "leds.h"
#include "mqtt.h"
#include "scripts.h"
#include "storage.h"
#include "web_ui.h"
#include "openapi_json.h"
#include "signing_pubkey.h"

namespace {

Leds leds;
ESP8266WebServer server(80);
DNSServer dns;

WifiConfig wifiCfg;
MqttConfig mqttCfg;
String hostname;
bool apMode = false;
uint32_t lastStaRetry = 0;
const uint32_t STA_RETRY_MS = 120000;

bool stateDirty = false;
uint32_t stateChangedAt = 0;
uint32_t rebootAt = 0;

// Temporary effect: shown until tempUntil, then baseState is restored. Never persisted.
const uint32_t MAX_TEMP_DURATION_MS = 3600000;
bool tempActive = false;
uint32_t tempUntil = 0;
LedState baseState;

const LedState &persistentState() {
  return tempActive ? baseState : leds.state();
}

String chipSuffix() {
  char buf[7];
  snprintf(buf, sizeof(buf), "%06x", ESP.getChipId());
  return String(buf).substring(2);
}

bool validHostname(const String &h) {
  if (h.length() == 0 || h.length() > 32 || h[0] == '-' || h[h.length() - 1] == '-') return false;
  for (char c : h) {
    if (!isalnum(c) && c != '-') return false;
  }
  return true;
}

// Returns an error message, or empty if the static IP settings are valid. dns is optional.
String staticIpError(const String &ip, const String &gateway, const String &subnet, const String &dns) {
  IPAddress a;
  if (!a.fromString(ip)) return F("invalid ip");
  if (!a.fromString(gateway)) return F("invalid gateway");
  if (!a.fromString(subnet)) return F("invalid subnet");
  if (dns.length() && !a.fromString(dns)) return F("invalid dns");
  return "";
}

// ---------- HTTP helpers ----------

void sendJson(int code, const JsonDocument &doc) {
  String body;
  serializeJson(doc, body);
  server.send(code, F("application/json"), body);
}

void sendError(int code, const String &msg) {
  JsonDocument doc;
  doc["error"] = msg;
  sendJson(code, doc);
}

bool parseBody(JsonDocument &doc) {
  if (!server.hasArg("plain")) {
    sendError(400, F("missing JSON body"));
    return false;
  }
  DeserializationError e = deserializeJson(doc, server.arg("plain"));
  if (e || !doc.is<JsonObject>()) {
    sendError(400, F("body must be a JSON object"));
    return false;
  }
  return true;
}

void sendState() {
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  stateToJson(leds.state(), obj);
  if (tempActive) {
    JsonObject temp = obj["temporary"].to<JsonObject>();
    temp["remaining"] = tempUntil - millis();
    stateToJson(baseState, temp["restore"].to<JsonObject>());
  } else {
    obj["temporary"] = nullptr;
  }
  sendJson(200, doc);
}

// ---------- API handlers ----------

void showState(const LedState &next, int32_t transitionMs = -1) {
  leds.apply(next, transitionMs);
  mqtt::publishState(leds.state());
}

// Applies a state change. Fields: on, toggle, brightness, color, mode, speed, transition (see stateFromJson).
// With "duration" (ms), the change is temporary: shown for that long, then the previous state returns.
// Without it, the change is permanent and cancels any running temporary effect.
// transitionMs >= 0 overrides the fade time for this change only. Returns false and sets err on invalid input.
bool changeState(JsonObjectConst cmd, String &err, int32_t transitionMs = -1) {
  long duration = 0;
  if (!cmd["duration"].isNull()) {
    duration = cmd["duration"] | -1L;
    if (duration < 1 || duration > (long)MAX_TEMP_DURATION_MS) {
      err = F("duration must be 1-3600000 ms");
      return false;
    }
  }

  // changes always build on the persistent state, not on a temporary effect being shown
  LedState next = persistentState();
  if (!stateFromJson(cmd, next, err)) return false;

  if (duration > 0) {
    if (!tempActive) baseState = leds.state();
    // an alert should show even when the lights are off, unless "on" was given explicitly
    if (cmd["on"].isNull() && cmd["toggle"].isNull()) next.on = true;
    tempActive = true;
    tempUntil = millis() + duration;
  } else {
    tempActive = false;
    stateDirty = true;
    stateChangedAt = millis();
  }
  showState(next, transitionMs);
  return true;
}

void endTemporary() {
  if (!tempActive) return;
  tempActive = false;
  showState(baseState);
}

void onMqttCommand(JsonObjectConst cmd, int32_t transitionMs) {
  JsonDocument doc;
  doc.set(cmd);
  // picking a color in Home Assistant while a color-cycling effect runs switches to solid
  Mode current = persistentState().mode;
  if (!cmd["color"].isNull() && cmd["mode"].isNull() && (current == Mode::RgbFade || current == Mode::ColorJump)) {
    doc["mode"] = "solid";
  }
  String err;
  if (!changeState(doc.as<JsonObjectConst>(), err, transitionMs)) {
    Log.printf("[mqtt] rejected command: %s\n", err.c_str());
    mqtt::publishState(leds.state());  // resync Home Assistant with the real state
  }
}

void handleSetState() {
  JsonDocument doc;
  if (!parseBody(doc)) return;
  String err;
  if (!changeState(doc.as<JsonObjectConst>(), err)) {
    sendError(400, err);
    return;
  }
  sendState();
}

void handleCancelTemp() {
  endTemporary();
  sendState();
}

void sendMqttConfig() {
  JsonDocument doc;
  doc["enabled"] = mqttCfg.enabled;
  doc["host"] = mqttCfg.host;
  doc["port"] = mqttCfg.port;
  doc["user"] = mqttCfg.user;
  doc["password_set"] = mqttCfg.password.length() > 0;
  doc["discovery_prefix"] = mqttCfg.discoveryPrefix;
  doc["connected"] = mqtt::connected();
  doc["base_topic"] = "deskled/" + hostname;
  sendJson(200, doc);
}

// Body: enabled, host, port, user, password (omit to keep the saved one), discovery_prefix
void handleSetMqtt() {
  JsonDocument doc;
  if (!parseBody(doc)) return;
  MqttConfig next = mqttCfg;
  if (doc["enabled"].is<bool>()) next.enabled = doc["enabled"];
  if (doc["host"].is<const char *>()) next.host = doc["host"].as<const char *>();
  if (!doc["port"].isNull()) next.port = doc["port"] | 0;
  if (doc["user"].is<const char *>()) next.user = doc["user"].as<const char *>();
  if (doc["password"].is<const char *>()) next.password = doc["password"].as<const char *>();
  if (doc["discovery_prefix"].is<const char *>()) next.discoveryPrefix = doc["discovery_prefix"].as<const char *>();
  if (doc["enabled"].isNull() && doc["host"].is<const char *>()) next.enabled = next.host.length() > 0;

  if (next.enabled && next.host.length() == 0) {
    sendError(400, F("host is required when MQTT is enabled"));
    return;
  }
  if (next.port == 0) {
    sendError(400, F("port must be 1-65535"));
    return;
  }
  if (next.discoveryPrefix.length() == 0) next.discoveryPrefix = "homeassistant";
  if (!storage::saveMqtt(next)) {
    sendError(500, F("failed to save MQTT settings"));
    return;
  }
  mqttCfg = next;
  mqtt::begin(mqttCfg, hostname, onMqttCommand);
  mqtt::publishState(leds.state());
  sendMqttConfig();
}

void handleModes() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  listEffects(arr);
  sendJson(200, doc);
}

void handleInfo() {
  JsonDocument doc;
  doc["name"] = "DeskLED";
  doc["version"] = FW_VERSION;
  doc["hostname"] = hostname;
  doc["mac"] = WiFi.macAddress();
  doc["ap_mode"] = apMode;
  doc["ssid"] = apMode ? WiFi.softAPSSID() : WiFi.SSID();
  doc["ip"] = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["uptime"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["reset_reason"] = ESP.getResetReason();
  doc["mqtt_connected"] = mqtt::connected();
  doc["default_password"] = auth::isDefaultPassword();
  sendJson(200, doc);
}

void handleScan() {
  int n = WiFi.scanComplete();
  JsonDocument doc;
  if (n == WIFI_SCAN_RUNNING) {
    doc["scanning"] = true;
    sendJson(200, doc);
    return;
  }
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    doc["scanning"] = true;
    sendJson(200, doc);
    return;
  }
  doc["scanning"] = false;
  JsonArray nets = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    // keep only the strongest entry per SSID
    bool dup = false;
    for (JsonObject existing : nets) {
      if (ssid == existing["ssid"].as<const char *>()) {
        dup = true;
        break;
      }
    }
    if (dup) continue;
    JsonObject o = nets.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  WiFi.scanDelete();
  sendJson(200, doc);
}

void handleSetWifi() {
  JsonDocument doc;
  if (!parseBody(doc)) return;
  String ssid = doc["ssid"] | "";
  String password = doc["password"] | "";
  String host = doc["hostname"] | "";
  host.toLowerCase();
  if (ssid.length() == 0 || ssid.length() > 32) {
    sendError(400, F("ssid must be 1-32 characters"));
    return;
  }
  if (password.length() > 0 && (password.length() < 8 || password.length() > 63)) {
    sendError(400, F("password must be empty or 8-63 characters"));
    return;
  }
  if (host.length() == 0) host = hostname;
  if (!validHostname(host)) {
    sendError(400, F("hostname may contain only letters, digits and dashes (max 32)"));
    return;
  }
  WifiConfig cfg{ssid, password, host};
  // optional static IP: "ip", "gateway", "subnet", "dns"; send "ip": "" for DHCP
  if (!doc["ip"].isNull()) {
    cfg.ip = doc["ip"] | "";
    cfg.gateway = doc["gateway"] | "";
    cfg.subnet = doc["subnet"] | "255.255.255.0";
    cfg.dns = doc["dns"] | "";
    String ipErr = cfg.ip.length() ? staticIpError(cfg.ip, cfg.gateway, cfg.subnet, cfg.dns) : "";
    if (ipErr.length()) {
      sendError(400, ipErr);
      return;
    }
  }
  if (!storage::saveWifi(cfg)) {
    sendError(500, F("failed to save credentials"));
    return;
  }
  JsonDocument res;
  res["saved"] = true;
  res["hostname"] = host;
  res["rebooting"] = true;
  sendJson(200, res);
  rebootAt = millis() + 1500;
}

void handleClearWifi() {
  storage::clearWifi();
  JsonDocument res;
  res["cleared"] = true;
  res["rebooting"] = true;
  sendJson(200, res);
  rebootAt = millis() + 1500;
}

void handleReboot() {
  JsonDocument res;
  res["rebooting"] = true;
  sendJson(200, res);
  rebootAt = millis() + 1000;
}

void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) {
    server.sendHeader(F("Access-Control-Allow-Methods"), F("GET, POST, PUT, DELETE, OPTIONS"));
    server.sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type"));
    server.send(204);
    return;
  }
  // captive portal: send any foreign host back to the setup page
  if (apMode && server.hostHeader() != WiFi.softAPIP().toString()) {
    server.sendHeader(F("Location"), String(F("http://")) + WiFi.softAPIP().toString() + "/", true);
    server.send(302, F("text/plain"), "");
    return;
  }
  sendError(404, F("not found"));
}

// ---------- Firmware update (signed images only) ----------

bool updateAuthorized = false;
String updateError;

void handleUpdateUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    updateError = "";
    // uploads cannot be challenged mid-stream; the final handler answers 401 if this fails
    updateAuthorized = apMode || auth::verify(server);
    if (!updateAuthorized) return;
    Log.printf("[update] receiving %s\n", upload.filename.c_str());
    if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) updateError = Update.getErrorString();
  } else if (!updateAuthorized || updateError.length()) {
    if (upload.status == UPLOAD_FILE_ABORTED) Update.end();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) updateError = Update.getErrorString();
  } else if (upload.status == UPLOAD_FILE_END) {
    // end() checks the RSA signature appended to the image
    if (!Update.end(true)) updateError = Update.getErrorString();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    updateError = F("upload aborted");
  }
}

void handleUpdateDone() {
  if (!updateAuthorized) {
    auth::challenge(server);
    return;
  }
  if (updateError.length() || Update.hasError()) {
    if (!updateError.length()) updateError = Update.getErrorString();
    Log.printf("[update] rejected: %s\n", updateError.c_str());
    sendError(400, "update failed: " + updateError);
    return;
  }
  Log.println(F("[update] signature valid, rebooting into new firmware"));
  JsonDocument res;
  res["updated"] = true;
  res["rebooting"] = true;
  sendJson(200, res);
  if (stateDirty) storage::saveState(persistentState());
  rebootAt = millis() + 1000;
}

// Body: nonce, proof, ha1, ota (see auth::setPasswordHashes); the web UI never sends the passwords.
void handleSetPassword() {
  JsonDocument doc;
  if (!parseBody(doc)) return;
  String err;
  if (!auth::setPasswordHashes(doc.as<JsonObjectConst>(), err)) {
    sendError(err.startsWith("current") ? 403 : 400, err);
    return;
  }
  Log.println(F("[auth] admin password changed, rebooting"));
  JsonDocument res;
  res["changed"] = true;
  res["rebooting"] = true;
  sendJson(200, res);
  rebootAt = millis() + 1000;
}

// Body: password. Used the next time the setup hotspot starts.
void handleSetApPassword() {
  JsonDocument doc;
  if (!parseBody(doc)) return;
  String err;
  if (!auth::setApPassword(doc["password"] | "", err)) {
    sendError(400, err);
    return;
  }
  Log.println(F("[auth] setup hotspot password changed"));
  JsonDocument res;
  res["changed"] = true;
  sendJson(200, res);
}

// ---------- Effect scripts ----------

void handleListScripts() {
  JsonDocument doc;
  scripts::list(doc.to<JsonArray>(), true);
  sendJson(200, doc);
}

// Body: name, space, a, b, c (see scripts.h). Creates or replaces.
void handleSaveScript() {
  JsonDocument doc;
  if (!parseBody(doc)) return;
  String err;
  if (!scripts::save(doc.as<JsonObjectConst>(), err)) {
    sendError(400, err);
    return;
  }
  mqtt::refreshDiscovery();
  JsonDocument res;
  res["saved"] = doc["name"];
  sendJson(200, res);
}

// Query: name
void handleDeleteScript() {
  String name = server.arg("name");
  if (persistentState().mode == Mode::Script && persistentState().script == name) {
    sendError(409, F("script is the current effect; pick another effect first"));
    return;
  }
  if (!scripts::remove(name)) {
    sendError(404, F("no such script"));
    return;
  }
  mqtt::refreshDiscovery();
  JsonDocument res;
  res["deleted"] = name;
  sendJson(200, res);
}

// Body: space, a, b, c, optional duration (ms, default 15000). Runs an unsaved script as a temporary effect.
void handlePreviewScript() {
  JsonDocument doc;
  if (!parseBody(doc)) return;
  long duration = doc["duration"] | 15000L;
  if (duration < 1000 || duration > 120000) {
    sendError(400, F("duration must be 1000-120000 ms"));
    return;
  }
  String err;
  if (!scripts::activatePreview(doc.as<JsonObjectConst>(), err)) {
    sendError(400, err);
    return;
  }
  LedState next = persistentState();
  if (!tempActive) baseState = leds.state();
  next.on = true;
  next.mode = Mode::Script;
  next.script = scripts::activeName();
  tempActive = true;
  tempUntil = millis() + duration;
  showState(next);
  sendState();
}


// Wraps a handler so it requires login, except in setup AP mode where the Wi-Fi password protects access.
std::function<void()> guarded(std::function<void()> handler) {
  return [handler] {
    if (apMode || auth::check(server)) handler();
  };
}

void setupServer() {
  auth::collectHeaders(server);
  server.enableCORS(true);
  // pages are static and hold no secrets; everything they load goes through the API
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/update", HTTP_GET, [] { server.send_P(200, "text/html", UPDATE_HTML); });
  // API description for clients that discover endpoints themselves
  server.on("/openapi.json", HTTP_GET, [] { server.send_P(200, "application/json", OPENAPI_JSON); });
  server.on("/api/login", HTTP_GET, [] { auth::handleLoginChallenge(server); });
  server.on("/api/login", HTTP_POST, [] { auth::handleLogin(server); });
  server.on("/api/logout", HTTP_POST, [] { auth::handleLogout(server); });

  server.on("/api/state", HTTP_GET, guarded(sendState));
  server.on("/api/state", HTTP_POST, guarded(handleSetState));
  server.on("/api/state", HTTP_PUT, guarded(handleSetState));
  server.on("/api/state/temporary", HTTP_DELETE, guarded(handleCancelTemp));
  server.on("/api/modes", HTTP_GET, guarded(handleModes));
  server.on("/api/info", HTTP_GET, guarded(handleInfo));
  server.on("/api/scripts", HTTP_GET, guarded(handleListScripts));
  server.on("/api/scripts", HTTP_POST, guarded(handleSaveScript));
  server.on("/api/scripts", HTTP_DELETE, guarded(handleDeleteScript));
  server.on("/api/scripts/preview", HTTP_POST, guarded(handlePreviewScript));
  server.on("/api/wifi/scan", HTTP_GET, guarded(handleScan));
  server.on("/api/wifi", HTTP_POST, guarded(handleSetWifi));
  server.on("/api/wifi", HTTP_DELETE, guarded(handleClearWifi));
  server.on("/api/mqtt", HTTP_GET, guarded(sendMqttConfig));
  server.on("/api/mqtt", HTTP_POST, guarded(handleSetMqtt));
  server.on("/api/password", HTTP_POST, guarded(handleSetPassword));
  server.on("/api/ap-password", HTTP_POST, guarded(handleSetApPassword));
  server.on("/api/reboot", HTTP_POST, guarded(handleReboot));
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound(handleNotFound);
  server.begin();
}

// ---------- Wi-Fi ----------

void startSetupAp() {
  apMode = true;
  String apSsid = "DeskLED-" + chipSuffix();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  String apPass = auth::apPassword();
  WiFi.softAP(apSsid, apPass);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  lastStaRetry = millis();
  Log.printf("[wifi] setup AP \"%s\" (password \"%s\") at http://%s/\n", apSsid.c_str(), apPass.c_str(),
                WiFi.softAPIP().toString().c_str());
}

WiFiEventHandler staConnectedHandler, staGotIpHandler, staDisconnectedHandler;
uint8_t lastDisconnectReason = 0;

void setupWifiEvents() {
  staConnectedHandler = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected &e) {
    Log.printf("[wifi] joined \"%s\" on channel %u, waiting for IP\n", e.ssid.c_str(), e.channel);
  });
  staGotIpHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP &e) {
    lastDisconnectReason = 0;
    Log.printf("[wifi] got IP %s, gateway %s\n", e.ip.toString().c_str(), e.gw.toString().c_str());
  });
  staDisconnectedHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &e) {
    if (e.reason == lastDisconnectReason) return;  // avoid flooding the log on repeated retries
    lastDisconnectReason = e.reason;
    // common: 2 auth expired, 15 handshake timeout (wrong password), 201 no AP found, 202 auth failed
    Log.printf("[wifi] disconnected from \"%s\", reason %u\n", e.ssid.c_str(), e.reason);
  });
}

void printScan() {
  Log.println(F("[scan] scanning..."));
  int n = WiFi.scanNetworks(false, true);
  for (int i = 0; i < n; i++) {
    Log.printf("[scan] ch %2d  %4d dBm  enc %u  \"%s\"\n", WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i),
                  WiFi.SSID(i).c_str());
  }
  Log.printf("[scan] %d networks\n", n);
  WiFi.scanDelete();
}

bool connectSta() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFi.setAutoReconnect(true);
  if (wifiCfg.ip.length()) {
    IPAddress ip, gw, sn, dns;
    ip.fromString(wifiCfg.ip);
    gw.fromString(wifiCfg.gateway);
    sn.fromString(wifiCfg.subnet);
    if (!dns.fromString(wifiCfg.dns)) dns = gw;
    WiFi.config(ip, gw, sn, dns);
    Log.printf("[wifi] static IP %s, gateway %s, subnet %s, dns %s\n", wifiCfg.ip.c_str(), wifiCfg.gateway.c_str(),
                  wifiCfg.subnet.c_str(), dns.toString().c_str());
  }
  WiFi.begin(wifiCfg.ssid, wifiCfg.password);
  Log.printf("[wifi] connecting to \"%s\"", wifiCfg.ssid.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    leds.loop();
    delay(10);
    if ((millis() - start) % 1000 < 10) Log.print('.');
  }
  Log.println();
  if (WiFi.status() != WL_CONNECTED) {
    // 1 = network not found, 4 = connect failed, 6 = wrong password, 7 = still trying
    Log.printf("[wifi] connection to \"%s\" failed, status %d\n", wifiCfg.ssid.c_str(), (int)WiFi.status());
    return false;
  }
  Log.printf("[wifi] connected, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

// In setup AP mode with saved credentials, retry the network now and then while nobody uses the AP.
void retryStaFromAp() {
  if (!apMode || wifiCfg.ssid.length() == 0) return;
  if (WiFi.status() == WL_CONNECTED) {
    Log.printf("[wifi] joined \"%s\", IP %s; closing setup AP\n", wifiCfg.ssid.c_str(),
                  WiFi.localIP().toString().c_str());
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apMode = false;
    MDNS.notifyAPChange();
    return;
  }
  if (millis() - lastStaRetry < STA_RETRY_MS || WiFi.softAPgetStationNum() > 0) return;
  lastStaRetry = millis();
  Log.println(F("[wifi] retrying saved network in background"));
  WiFi.begin(wifiCfg.ssid, wifiCfg.password);
}

// ---------- Serial console ----------

// Splits a command line into words; double quotes group words containing spaces.
std::vector<String> splitArgs(const String &line) {
  std::vector<String> args;
  String cur;
  bool quoted = false, inWord = false;
  for (size_t i = 0; i < line.length(); i++) {
    char c = line[i];
    if (c == '"') {
      quoted = !quoted;
      inWord = true;
    } else if (c == ' ' && !quoted) {
      if (inWord) args.push_back(cur);
      cur = "";
      inWord = false;
    } else {
      cur += c;
      inWord = true;
    }
  }
  if (inWord) args.push_back(cur);
  return args;
}

void printStatus() {
  Log.printf("[status] DeskLED %s, hostname %s, uptime %lus, free heap %u\n", FW_VERSION, hostname.c_str(),
                millis() / 1000, ESP.getFreeHeap());
  if (apMode) {
    Log.printf("[status] setup AP \"%s\" at %s, saved network \"%s\"\n", WiFi.softAPSSID().c_str(),
                  WiFi.softAPIP().toString().c_str(), wifiCfg.ssid.c_str());
  } else {
    Log.printf("[status] connected to \"%s\", IP %s, RSSI %d dBm\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
}

void runCommand(const String &line) {
  std::vector<String> args = splitArgs(line);
  if (args.empty()) return;
  String cmd = args[0];
  cmd.toLowerCase();

  if (cmd == "wifi" && args.size() == 2 && args[1] == "clear") {
    storage::clearWifi();
    Log.println(F("[wifi] credentials cleared, rebooting"));
    rebootAt = millis() + 500;
  } else if (cmd == "wifi" && (args.size() == 2 || args.size() == 3)) {
    String password = args.size() == 3 ? args[2] : "";
    if (args[1].length() == 0 || args[1].length() > 32 ||
        (password.length() > 0 && (password.length() < 8 || password.length() > 63))) {
      Log.println(F("[wifi] invalid ssid (1-32 chars) or password (empty or 8-63 chars)"));
      return;
    }
    WifiConfig cfg{args[1], password, hostname};
    if (!storage::saveWifi(cfg)) {
      Log.println(F("[wifi] failed to save credentials"));
      return;
    }
    Log.printf("[wifi] saved network \"%s\", rebooting\n", cfg.ssid.c_str());
    rebootAt = millis() + 500;
  } else if (cmd == "ip" && args.size() == 2 && args[1] == "dhcp") {
    wifiCfg.ip = wifiCfg.gateway = wifiCfg.subnet = wifiCfg.dns = "";
    storage::saveWifi(wifiCfg);
    Log.println(F("[wifi] using DHCP, rebooting"));
    rebootAt = millis() + 500;
  } else if (cmd == "ip" && (args.size() == 4 || args.size() == 5)) {
    String err = staticIpError(args[1], args[2], args[3], args.size() == 5 ? args[4] : "");
    if (err.length()) {
      Log.printf("[wifi] %s\n", err.c_str());
      return;
    }
    wifiCfg.ip = args[1];
    wifiCfg.gateway = args[2];
    wifiCfg.subnet = args[3];
    wifiCfg.dns = args.size() == 5 ? args[4] : "";
    storage::saveWifi(wifiCfg);
    Log.printf("[wifi] static IP %s set, rebooting\n", wifiCfg.ip.c_str());
    rebootAt = millis() + 500;
  } else if (cmd == "mqtt" && args.size() == 2 && args[1] == "off") {
    mqttCfg.enabled = false;
    storage::saveMqtt(mqttCfg);
    mqtt::begin(mqttCfg, hostname, onMqttCommand);
    Log.println(F("[mqtt] disabled"));
  } else if (cmd == "mqtt" && args.size() >= 2 && args.size() <= 5) {
    mqttCfg.enabled = true;
    mqttCfg.host = args[1];
    mqttCfg.port = args.size() >= 3 ? args[2].toInt() : 1883;
    mqttCfg.user = args.size() >= 4 ? args[3] : "";
    mqttCfg.password = args.size() >= 5 ? args[4] : "";
    if (mqttCfg.port == 0) mqttCfg.port = 1883;
    storage::saveMqtt(mqttCfg);
    mqtt::begin(mqttCfg, hostname, onMqttCommand);
    mqtt::publishState(leds.state());
    Log.printf("[mqtt] broker %s:%u saved\n", mqttCfg.host.c_str(), mqttCfg.port);
  } else if (cmd == "password" && args.size() == 2) {
    String err;
    if (!auth::setPassword(args[1], err)) {
      Log.printf("[auth] %s\n", err.c_str());
      return;
    }
    Log.println(F("[auth] admin password changed, rebooting"));
    rebootAt = millis() + 500;
  } else if (cmd == "scan") {
    printScan();
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "reboot") {
    rebootAt = millis() + 200;
  } else {
    Log.println(F("commands: wifi \"<ssid>\" [password] | wifi clear | ip <addr> <gateway> <mask> [dns] | ip dhcp | mqtt <host> [port] [user] [pass] | mqtt off | password <new> | scan | status | reboot | exit (telnet)"));
  }
}

void setupOta() {
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPasswordHash(auth::otaPasswordHash().c_str());
  ArduinoOTA.onStart([] {
    Log.println(F("[ota] start"));
    storage::saveState(persistentState());
  });
  ArduinoOTA.onEnd([] { Log.println(F("[ota] done, rebooting")); });
  ArduinoOTA.onError([](ota_error_t e) { Log.printf("[ota] error %u\n", e); });
  ArduinoOTA.begin(false);  // mDNS started separately below
}

}  // namespace

void setup() {
  // before any other WiFi call, or the SDK rewrites its config sectors in flash on every boot
  WiFi.persistent(false);
  WiFi.setOutputPower(WIFI_TX_POWER_DBM);
  Serial.begin(115200);
  Log.println();
  Log.printf("\n[boot] DeskLED %s, chip %06x, reset: %s\n", FW_VERSION, ESP.getChipId(),
                ESP.getResetReason().c_str());
  randomSeed(RANDOM_REG32);

  setupWifiEvents();
  storage::begin();
  auth::begin();

  // the generated key array has no terminating zero, so pass its size explicitly
  static BearSSL::PublicKey signingKey((const uint8_t *)signing_pubkey, sizeof(signing_pubkey));
  static BearSSL::HashSHA256 signingHash;
  static BearSSL::SigningVerifier signingVerifier(&signingKey);
  Update.installSignature(&signingHash, &signingVerifier);

  LedState initial;
  if (storage::loadState(initial)) {
    Log.printf("[state] restored: mode %s, brightness %u\n", effectName(initial).c_str(), initial.brightness);
  }
  leds.begin();
  leds.apply(initial);

  hostname = String(HOSTNAME_PREFIX) + "-" + chipSuffix();
  if (storage::loadWifi(wifiCfg)) {
    if (validHostname(wifiCfg.hostname)) hostname = wifiCfg.hostname;
    if (!connectSta()) startSetupAp();
  } else {
    Log.println(F("[wifi] no saved credentials"));
    startSetupAp();
  }

  setupServer();
  console::beginTelnet();
  setupOta();
  storage::loadMqtt(mqttCfg);
  mqtt::begin(mqttCfg, hostname, onMqttCommand);
  mqtt::publishState(leds.state());

  if (MDNS.begin(hostname)) {
    MDNS.addService("http", "tcp", 80);
    Log.printf("[mdns] http://%s.local/\n", hostname.c_str());
  }
}

void loop() {
  leds.loop();
  server.handleClient();
  ArduinoOTA.handle();
  MDNS.update();
  if (apMode) dns.processNextRequest();
  retryStaFromAp();
  console::loop(runCommand);
  mqtt::loop();

  if (tempActive && (int32_t)(millis() - tempUntil) >= 0) endTemporary();

  if (stateDirty && millis() - stateChangedAt > STATE_SAVE_DELAY_MS) {
    stateDirty = false;
    storage::saveState(persistentState());
  }

  if (rebootAt && millis() > rebootAt) {
    if (stateDirty) storage::saveState(persistentState());
    Log.println(F("[boot] rebooting"));
    delay(100);
    ESP.restart();
  }
}
