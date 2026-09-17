#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <functional>

#include "leds.h"
#include "storage.h"

namespace mqtt {

// Receives a command in the REST /api/state format, plus a one-off transition time (-1 = state default).
using CommandHandler = std::function<void(JsonObjectConst cmd, int32_t transitionMs)>;

// (Re)configures the client. Safe to call again after the settings change.
void begin(const MqttConfig &cfg, const String &hostname, CommandHandler onCommand);
void loop();
bool connected();
// Re-sends Home Assistant discovery, e.g. after the effect list changed.
void refreshDiscovery();

// Publishes the state currently shown on the LEDs (retained).
void publishState(const LedState &shown);

}  // namespace mqtt
