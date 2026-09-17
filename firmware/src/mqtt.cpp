#include "mqtt.h"

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "console.h"

// Home Assistant MQTT integration: JSON-schema light with discovery, plus an effect speed number.
// Topics under deskled/<hostname>/: state (retained), set, speed/set, availability (retained, LWT).

namespace mqtt {
namespace {

const uint32_t RECONNECT_MS = 5000;
const uint32_t FLASH_SHORT_MS = 2000;
const uint32_t FLASH_LONG_MS = 10000;

WiFiClient net;
PubSubClient client(net);

MqttConfig cfg;
String host;
String base;
String deviceId;
CommandHandler handler;
uint32_t lastAttempt = 0;
bool haveState = false;
LedState lastState;

String topic(const char *suffix) {
  return base + "/" + suffix;
}

void addDevice(JsonDocument &doc) {
  JsonObject dev = doc["dev"].to<JsonObject>();
  dev["ids"].to<JsonArray>().add(deviceId);
  dev["name"] = host;
  dev["mf"] = "DIY";
  dev["mdl"] = "DeskLED TYWE3L RGB";
  dev["sw"] = FW_VERSION;
  dev["cu"] = "http://" + WiFi.localIP().toString() + "/";
  doc["avty_t"] = topic("availability");
}

bool publishJson(const String &t, const JsonDocument &doc, bool retained) {
  String payload;
  serializeJson(doc, payload);
  return client.publish(t.c_str(), payload.c_str(), retained);
}

void publishDiscovery() {
  String node = cfg.discoveryPrefix + "/light/" + deviceId + "/light/config";
  JsonDocument light;
  light["name"] = nullptr;  // entity takes the device name
  light["uniq_id"] = deviceId + "_light";
  light["schema"] = "json";
  light["stat_t"] = topic("state");
  light["cmd_t"] = topic("set");
  light["brightness"] = true;
  light["sup_clrm"].to<JsonArray>().add("rgb");
  light["effect"] = true;
  listEffects(light["effect_list"].to<JsonArray>());
  light["flash_time_short"] = FLASH_SHORT_MS / 1000;
  light["flash_time_long"] = FLASH_LONG_MS / 1000;
  addDevice(light);
  publishJson(node, light, true);

  String speedNode = cfg.discoveryPrefix + "/number/" + deviceId + "/speed/config";
  JsonDocument speed;
  speed["name"] = "Effect speed";
  speed["uniq_id"] = deviceId + "_speed";
  speed["stat_t"] = topic("state");
  speed["val_tpl"] = "{{ value_json.speed }}";
  speed["cmd_t"] = topic("speed/set");
  speed["min"] = 1;
  speed["max"] = 255;
  speed["mode"] = "slider";
  speed["ent_cat"] = "config";
  addDevice(speed);
  publishJson(speedNode, speed, true);
}

void publishStateNow() {
  if (!haveState || !client.connected()) return;
  JsonDocument doc;
  doc["state"] = lastState.on ? "ON" : "OFF";
  doc["brightness"] = lastState.brightness;
  doc["color_mode"] = "rgb";
  JsonObject c = doc["color"].to<JsonObject>();
  c["r"] = lastState.color.r;
  c["g"] = lastState.color.g;
  c["b"] = lastState.color.b;
  doc["effect"] = effectName(lastState);
  doc["speed"] = lastState.speed;
  publishJson(topic("state"), doc, true);
}

// Translates a Home Assistant JSON-schema command into the REST state format.
void onLightCommand(JsonObjectConst ha) {
  JsonDocument cmd;
  int32_t transitionMs = -1;

  if (ha["state"].is<const char *>()) cmd["on"] = strcasecmp(ha["state"], "ON") == 0;
  if (!ha["brightness"].isNull()) cmd["brightness"] = ha["brightness"];
  if (ha["color"].is<JsonObjectConst>()) cmd["color"] = ha["color"];
  if (ha["effect"].is<const char *>()) cmd["mode"] = ha["effect"];
  if (!ha["transition"].isNull()) transitionMs = (int32_t)(ha["transition"].as<float>() * 1000);
  // extensions for automations publishing directly: temporary effects and speed
  if (!ha["duration"].isNull()) cmd["duration"] = ha["duration"];
  if (!ha["speed"].isNull()) cmd["speed"] = ha["speed"];

  if (ha["flash"].is<const char *>()) {
    cmd["mode"] = "strobe";
    cmd["duration"] = strcmp(ha["flash"], "long") == 0 ? FLASH_LONG_MS : FLASH_SHORT_MS;
  }

  handler(cmd.as<JsonObjectConst>(), transitionMs);
}

void onMessage(char *rawTopic, byte *payload, unsigned int length) {
  String t(rawTopic);
  if (t == topic("speed/set")) {
    JsonDocument cmd;
    char buf[8];
    size_t n = min<size_t>(length, sizeof(buf) - 1);
    memcpy(buf, payload, n);
    buf[n] = 0;
    cmd["speed"] = atoi(buf);
    handler(cmd.as<JsonObjectConst>(), -1);
    return;
  }
  if (t == topic("set")) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length) || !doc.is<JsonObject>()) {
      Log.println(F("[mqtt] ignoring invalid JSON command"));
      return;
    }
    onLightCommand(doc.as<JsonObjectConst>());
  }
}

void tryConnect() {
  lastAttempt = millis();
  String availability = topic("availability");
  const char *user = cfg.user.length() ? cfg.user.c_str() : nullptr;
  const char *pass = cfg.password.length() ? cfg.password.c_str() : nullptr;
  if (!client.connect(deviceId.c_str(), user, pass, availability.c_str(), 1, true, "offline")) {
    Log.printf("[mqtt] connect to %s:%u failed, state %d\n", cfg.host.c_str(), cfg.port, client.state());
    return;
  }
  Log.printf("[mqtt] connected to %s:%u as %s\n", cfg.host.c_str(), cfg.port, deviceId.c_str());
  client.publish(availability.c_str(), "online", true);
  publishDiscovery();
  client.subscribe(topic("set").c_str());
  client.subscribe(topic("speed/set").c_str());
  publishStateNow();
}

}  // namespace

void begin(const MqttConfig &c, const String &hostname, CommandHandler onCommand) {
  if (client.connected()) {
    client.publish(topic("availability").c_str(), "offline", true);
    client.disconnect();
  }
  cfg = c;
  host = hostname;
  handler = onCommand;
  base = "deskled/" + hostname;
  char id[20];
  snprintf(id, sizeof(id), "deskled_%06x", ESP.getChipId());
  deviceId = id;

  net.setTimeout(2000);
  client.setBufferSize(1024);
  client.setServer(cfg.host.c_str(), cfg.port);
  client.setCallback(onMessage);
  lastAttempt = millis() - RECONNECT_MS;  // connect on the next loop
}

void loop() {
  if (!cfg.enabled || cfg.host.length() == 0 || WiFi.status() != WL_CONNECTED) return;
  if (!client.connected()) {
    if (millis() - lastAttempt >= RECONNECT_MS) tryConnect();
    return;
  }
  client.loop();
}

void refreshDiscovery() {
  if (client.connected()) publishDiscovery();
}

bool connected() {
  return client.connected();
}

void publishState(const LedState &shown) {
  lastState = shown;
  haveState = true;
  publishStateNow();
}

}  // namespace mqtt
