#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "leds.h"

// User effect scripts: three TinyExpr formulas evaluated every frame, stored on LittleFS.
//
// A script is {"name", "space": "hsv"|"rgb", "a", "b", "c"}:
//   hsv: a = hue in degrees (wraps), b = saturation 0..1, c = value 0..1
//   rgb: a, b, c = red, green, blue 0..1
// Variables: t (seconds since the effect started), speed (1..255), k (speed / 128),
//            cr, cg, cb (base color 0..1), ch, cs, cv (base color as HSV, h in degrees)
// Functions: frac, tri, sq, pulse(x,duty), wave, clamp(x,lo,hi), mix(a,b,f), smooth,
//            noise(x), rand(), sel(cond,a,b), min, max, plus TinyExpr's math (sin, cos, abs, pow, ...)
//            and operators + - * / ^ % < <= > >= == != && || !
namespace scripts {

const size_t MAX_SCRIPTS = 12;
const size_t MAX_EXPR_LEN = 200;

bool validName(const String &name);
bool exists(const String &name);
void list(JsonArray out, bool withSource);
// Returns false and sets err (with the position of a syntax error) if invalid.
bool save(JsonObjectConst script, String &err);
bool remove(const String &name);

// Compiles a stored script as the active effect, or a preview script that is not saved.
bool activate(const String &name);
bool activatePreview(JsonObjectConst script, String &err);
const String &activeName();

// Evaluates the active script. Returns false if none is compiled.
bool color(float seconds, const LedState &state, Rgb &out);

}  // namespace scripts
