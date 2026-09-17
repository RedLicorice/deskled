#include "storage.h"

#include <LittleFS.h>

#include "console.h"

namespace {

const char *STATE_PATH = "/state.json";
const char *WIFI_PATH = "/wifi.json";
const char *MQTT_PATH = "/mqtt.json";

bool readJson(const char *path, JsonDocument &doc) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  return !e;
}

bool writeJson(const char *path, const JsonDocument &doc) {
  // write to temp file then rename, so power loss never leaves a half-written file
  String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  if (!ok) return false;
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}

}  // namespace

void stateToJson(const LedState &s, JsonObject obj) {
  obj["on"] = s.on;
  obj["brightness"] = s.brightness;
  JsonObject c = obj["color"].to<JsonObject>();
  c["r"] = s.color.r;
  c["g"] = s.color.g;
  c["b"] = s.color.b;
  obj["mode"] = effectName(s);
  obj["speed"] = s.speed;
  obj["transition"] = s.transitionMs;
}

bool stateFromJson(JsonObjectConst obj, LedState &s, String &err) {
  LedState next = s;

  if (obj["on"].is<bool>()) next.on = obj["on"];
  if (obj["toggle"].is<bool>() && obj["toggle"].as<bool>()) next.on = !next.on;

  if (!obj["brightness"].isNull()) {
    int v = obj["brightness"] | -1;
    if (v < 0 || v > 255) { err = "brightness must be 0-255"; return false; }
    next.brightness = v;
  }

  JsonVariantConst color = obj["color"];
  if (!color.isNull()) {
    int r, g, b;
    if (color.is<JsonArrayConst>() && color.size() == 3) {
      r = color[0] | -1; g = color[1] | -1; b = color[2] | -1;
    } else if (color.is<JsonObjectConst>()) {
      r = color["r"] | -1; g = color["g"] | -1; b = color["b"] | -1;
    } else if (color.is<const char *>()) {
      const char *hex = color.as<const char *>();
      if (*hex == '#') hex++;
      if (strlen(hex) != 6) { err = "color hex must be RRGGBB"; return false; }
      long v = strtol(hex, nullptr, 16);
      r = (v >> 16) & 0xff; g = (v >> 8) & 0xff; b = v & 0xff;
    } else {
      err = "color must be {r,g,b}, [r,g,b] or \"#RRGGBB\""; return false;
    }
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
      err = "color channels must be 0-255"; return false;
    }
    next.color = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
  }

  if (!obj["mode"].isNull()) {
    if (!effectFromName(obj["mode"].as<const char *>(), next)) { err = "unknown mode"; return false; }
  }

  if (!obj["speed"].isNull()) {
    int v = obj["speed"] | -1;
    if (v < 1 || v > 255) { err = "speed must be 1-255"; return false; }
    next.speed = v;
  }

  if (!obj["transition"].isNull()) {
    long v = obj["transition"] | -1L;
    if (v < 0 || v > 60000) { err = "transition must be 0-60000 ms"; return false; }
    next.transitionMs = v;
  }

  s = next;
  return true;
}

namespace storage {

void begin() {
  if (!LittleFS.begin()) {
    Log.println(F("[fs] mount failed, formatting"));
    LittleFS.format();
    LittleFS.begin();
  }
}

bool loadState(LedState &out) {
  JsonDocument doc;
  if (!readJson(STATE_PATH, doc)) return false;
  String err;
  return stateFromJson(doc.as<JsonObjectConst>(), out, err);
}

bool saveState(const LedState &s) {
  JsonDocument doc;
  stateToJson(s, doc.to<JsonObject>());
  return writeJson(STATE_PATH, doc);
}

bool loadWifi(WifiConfig &out) {
  JsonDocument doc;
  if (!readJson(WIFI_PATH, doc)) return false;
  out.ssid = doc["ssid"] | "";
  out.password = doc["password"] | "";
  out.hostname = doc["hostname"] | "";
  out.ip = doc["ip"] | "";
  out.gateway = doc["gateway"] | "";
  out.subnet = doc["subnet"] | "";
  out.dns = doc["dns"] | "";
  return out.ssid.length() > 0;
}

bool saveWifi(const WifiConfig &w) {
  JsonDocument doc;
  doc["ssid"] = w.ssid;
  doc["password"] = w.password;
  doc["hostname"] = w.hostname;
  if (w.ip.length()) {
    doc["ip"] = w.ip;
    doc["gateway"] = w.gateway;
    doc["subnet"] = w.subnet;
    doc["dns"] = w.dns;
  }
  return writeJson(WIFI_PATH, doc);
}

void clearWifi() {
  LittleFS.remove(WIFI_PATH);
}

bool loadMqtt(MqttConfig &out) {
  JsonDocument doc;
  if (!readJson(MQTT_PATH, doc)) return false;
  out.enabled = doc["enabled"] | false;
  out.host = doc["host"] | "";
  out.port = doc["port"] | 1883;
  out.user = doc["user"] | "";
  out.password = doc["password"] | "";
  out.discoveryPrefix = doc["discovery_prefix"] | "homeassistant";
  return true;
}

bool saveMqtt(const MqttConfig &m) {
  JsonDocument doc;
  doc["enabled"] = m.enabled;
  doc["host"] = m.host;
  doc["port"] = m.port;
  doc["user"] = m.user;
  doc["password"] = m.password;
  doc["discovery_prefix"] = m.discoveryPrefix;
  return writeJson(MQTT_PATH, doc);
}

}  // namespace storage
