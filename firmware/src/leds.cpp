#include "leds.h"

#include <math.h>

#include "config.h"
#include "scripts.h"

namespace {

const char *const MODE_NAMES[] = {"solid", "rgbfade", "breathe", "colorjump", "candle", "strobe", "script"};

const uint32_t FRAME_MS = 10;

// speed 1..255 -> duration between slow and fast, on a log-ish curve
uint32_t speedToMs(uint8_t speed, uint32_t slowMs, uint32_t fastMs) {
  float t = (constrain(speed, 1, 255) - 1) / 254.0f;
  return (uint32_t)(slowMs * pow((float)fastMs / slowMs, t));
}

Rgb hsv(float h, float s, float v) {
  h = fmod(h, 360.0f);
  if (h < 0) h += 360.0f;
  float c = v * s;
  float x = c * (1 - fabs(fmod(h / 60.0f, 2) - 1));
  float m = v - c;
  float r = 0, g = 0, b = 0;
  if (h < 60)       { r = c; g = x; }
  else if (h < 120) { r = x; g = c; }
  else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; }
  else if (h < 300) { r = x; b = c; }
  else              { r = c; b = x; }
  return {(uint8_t)((r + m) * 255), (uint8_t)((g + m) * 255), (uint8_t)((b + m) * 255)};
}

}  // namespace

const char *modeName(Mode m) {
  return MODE_NAMES[(uint8_t)m < (uint8_t)Mode::Count ? (uint8_t)m : 0];
}

bool modeFromName(const char *name, Mode &out) {
  if (!name) return false;
  for (uint8_t i = 0; i < (uint8_t)Mode::Count; i++) {
    if (strcasecmp(name, MODE_NAMES[i]) == 0) {
      out = (Mode)i;
      return true;
    }
  }
  return false;
}

String effectName(const LedState &s) {
  return s.mode == Mode::Script ? s.script : String(modeName(s.mode));
}

bool effectFromName(const char *name, LedState &s) {
  Mode m;
  if (modeFromName(name, m) && m != Mode::Script) {
    s.mode = m;
    s.script = "";
    return true;
  }
  if (name && scripts::exists(name)) {
    s.mode = Mode::Script;
    s.script = name;
    return true;
  }
  return false;
}

void listEffects(JsonArray out) {
  for (uint8_t i = 0; i < (uint8_t)Mode::Script; i++) out.add(modeName((Mode)i));
  scripts::list(out, false);
}

void Leds::begin() {
  analogWriteRange(PWM_RANGE);
  analogWriteFreq(PWM_FREQ_HZ);
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_BLUE, OUTPUT);
  write(0, 0, 0);
  effectStart_ = transitionStart_ = millis();
}

void Leds::apply(const LedState &next, int32_t transitionMs) {
  if (next.mode == Mode::Script && (state_.mode != Mode::Script || next.script != state_.script)) {
    scripts::activate(next.script);
  }
  if (next.mode != state_.mode || next.script != state_.script) {
    effectStart_ = millis();
    effectStep_ = 0;
    nextStepAt_ = 0;
  }
  state_ = next;
  memcpy(from_, cur_, sizeof(from_));
  transitionStart_ = millis();
  activeTransitionMs_ = transitionMs >= 0 ? transitionMs : next.transitionMs;
}

Rgb Leds::effectColor(uint32_t now) {
  const Rgb &c = state_.color;
  uint32_t t = now - effectStart_;

  switch (state_.mode) {
    case Mode::RgbFade: {
      uint32_t period = speedToMs(state_.speed, 120000, 2000);
      return hsv(360.0f * (t % period) / period, 1.0f, 1.0f);
    }
    case Mode::Breathe: {
      uint32_t period = speedToMs(state_.speed, 10000, 800);
      float phase = (float)(t % period) / period;
      float k = 0.06f + 0.94f * (0.5f - 0.5f * cos(phase * 2 * PI));
      return {(uint8_t)(c.r * k), (uint8_t)(c.g * k), (uint8_t)(c.b * k)};
    }
    case Mode::ColorJump: {
      if (now >= nextStepAt_) {
        effectStep_++;
        nextStepAt_ = now + speedToMs(state_.speed, 6000, 250);
      }
      return hsv(effectStep_ * 60.0f, 1.0f, 1.0f);
    }
    case Mode::Candle: {
      if (now >= nextStepAt_) {
        float target = 0.55f + random(0, 450) / 1000.0f;
        candle_ += (target - candle_) * 0.5f;
        nextStepAt_ = now + speedToMs(state_.speed, 160, 25);
      }
      return {(uint8_t)(c.r * candle_), (uint8_t)(c.g * candle_), (uint8_t)(c.b * candle_)};
    }
    case Mode::Strobe: {
      uint32_t period = speedToMs(state_.speed, 1500, 60);
      return (t % period) < min<uint32_t>(40, period / 2) ? c : Rgb{0, 0, 0};
    }
    case Mode::Script: {
      Rgb out;
      // falls back to the base color if the script is missing or failed to compile
      return scripts::color(t / 1000.0f, state_, out) ? out : c;
    }
    case Mode::Solid:
    default:
      return c;
  }
}

void Leds::loop() {
  uint32_t now = millis();
  if (now - lastFrame_ < FRAME_MS) return;
  lastFrame_ = now;

  float target[3] = {0, 0, 0};
  if (state_.on) {
    Rgb e = effectColor(now);
    float k = state_.brightness / 255.0f;
    target[0] = e.r * k;
    target[1] = e.g * k;
    target[2] = e.b * k;
  }

  uint32_t elapsed = now - transitionStart_;
  // strobe ignores transitions so flashes stay sharp
  if (activeTransitionMs_ > 0 && elapsed < activeTransitionMs_ && state_.mode != Mode::Strobe) {
    float p = (float)elapsed / activeTransitionMs_;
    p = p * p * (3 - 2 * p);  // smoothstep
    for (int i = 0; i < 3; i++) cur_[i] = from_[i] + (target[i] - from_[i]) * p;
  } else {
    memcpy(cur_, target, sizeof(cur_));
  }

  write(cur_[0], cur_[1], cur_[2]);
}

void Leds::write(float r, float g, float b) {
  // gamma 2.2 so brightness steps look even to the eye
  auto out = [](float v) -> int {
    v = constrain(v, 0.0f, 255.0f) / 255.0f;
    return (int)lround(pow(v, 2.2f) * PWM_RANGE);
  };
  analogWrite(PIN_RED, out(r));
  analogWrite(PIN_GREEN, out(g));
  analogWrite(PIN_BLUE, out(b));
}
