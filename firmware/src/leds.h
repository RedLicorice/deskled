#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

enum class Mode : uint8_t {
  Solid,
  RgbFade,
  Breathe,
  ColorJump,
  Candle,
  Strobe,
  Script,  // user formula effect, named by LedState::script
  Count
};

struct Rgb {
  uint8_t r, g, b;
};

struct LedState {
  bool on = true;
  uint8_t brightness = 255;
  Rgb color = {255, 120, 40};
  Mode mode = Mode::RgbFade;
  uint8_t speed = 128;          // 1..255, higher is faster
  uint16_t transitionMs = 700;  // fade time for on/off, color and brightness changes
  String script;                // effect script name when mode is Script
};

// Effect name as used by the API: built-in mode name, or the script name.
String effectName(const LedState &s);
// Sets mode (and script) from a built-in or script name. Returns false if unknown.
bool effectFromName(const char *name, LedState &s);
// Built-in effects plus saved scripts.
void listEffects(JsonArray out);

const char *modeName(Mode m);
bool modeFromName(const char *name, Mode &out);

class Leds {
 public:
  void begin();
  void loop();

  const LedState &state() const { return state_; }
  // transitionMs >= 0 overrides the state's transition time for this change only
  void apply(const LedState &next, int32_t transitionMs = -1);

 private:
  Rgb effectColor(uint32_t now);
  void write(float r, float g, float b);

  LedState state_;
  float cur_[3] = {0, 0, 0};    // output currently shown, 0..255 linear
  float from_[3] = {0, 0, 0};   // output when the transition started
  uint32_t transitionStart_ = 0;
  uint32_t activeTransitionMs_ = 0;
  uint32_t lastFrame_ = 0;
  uint32_t effectStart_ = 0;
  uint16_t effectStep_ = 0;
  uint32_t nextStepAt_ = 0;
  float candle_ = 1.0f;
};
