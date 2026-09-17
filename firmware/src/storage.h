#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "leds.h"

struct WifiConfig {
  String ssid;
  String password;
  String hostname;
  // static IP; empty ip means DHCP
  String ip;
  String gateway;
  String subnet;
  String dns;
};

struct MqttConfig {
  bool enabled = false;
  String host;
  uint16_t port = 1883;
  String user;
  String password;
  String discoveryPrefix = "homeassistant";
};

namespace storage {

void begin();

bool loadState(LedState &out);
bool saveState(const LedState &s);

bool loadWifi(WifiConfig &out);
bool saveWifi(const WifiConfig &w);
void clearWifi();

bool loadMqtt(MqttConfig &out);
bool saveMqtt(const MqttConfig &m);

}  // namespace storage

// JSON <-> LedState, shared by storage and the REST API
void stateToJson(const LedState &s, JsonObject obj);
// Merges only the fields present in obj. Returns false and sets err on invalid input.
bool stateFromJson(JsonObjectConst obj, LedState &s, String &err);
