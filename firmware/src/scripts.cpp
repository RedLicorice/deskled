#include "scripts.h"

#include <LittleFS.h>
#include <math.h>

#include "console.h"

extern "C" {
#include "tinyexpr.h"
}

namespace scripts {
namespace {

const char *DIR = "/s";
const char *PREVIEW_NAME = "~preview";
const char *const BUILTIN_NAMES[] = {"solid", "rgbfade", "breathe", "colorjump", "candle", "strobe"};

// variables bound into every compiled expression
double vT, vSpeed, vK, vCr, vCg, vCb, vCh, vCs, vCv;

double fFrac(double x) { return x - floor(x); }
double fTri(double x) { double f = fFrac(x); return f < 0.5 ? f * 2 : 2 - f * 2; }
double fSq(double x) { return fFrac(x) < 0.5 ? 1 : 0; }
double fPulse(double x, double duty) { return fFrac(x) < duty ? 1 : 0; }
double fWave(double x) { return 0.5 + 0.5 * sin(x * 2 * M_PI); }
double fClamp(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
double fMix(double a, double b, double f) { return a + (b - a) * f; }
double fSmooth(double x) { x = fClamp(x, 0, 1); return x * x * (3 - 2 * x); }
double fSel(double c, double a, double b) { return c > 0 ? a : b; }
double fMin(double a, double b) { return a < b ? a : b; }
double fMax(double a, double b) { return a > b ? a : b; }
double fRand() { return (double)(RANDOM_REG32 & 0xFFFF) / 65535.0; }

double hash1(double i) {
  uint32_t h = (uint32_t)(int32_t)i * 0x9E3779B1u;
  h ^= h >> 15;
  h *= 0x85EBCA77u;
  h ^= h >> 13;
  return (h & 0xFFFF) / 65535.0;
}
double fNoise(double x) {
  double i = floor(x);
  return fMix(hash1(i), hash1(i + 1), fSmooth(x - i));
}

// TinyExpr stores functions as untyped pointers
const void *fn(double (*f)()) { return (const void *)f; }
const void *fn(double (*f)(double)) { return (const void *)f; }
const void *fn(double (*f)(double, double)) { return (const void *)f; }
const void *fn(double (*f)(double, double, double)) { return (const void *)f; }

const te_variable VARS[] = {
    {"t", &vT, TE_VARIABLE, nullptr},        {"speed", &vSpeed, TE_VARIABLE, nullptr},
    {"k", &vK, TE_VARIABLE, nullptr},        {"cr", &vCr, TE_VARIABLE, nullptr},
    {"cg", &vCg, TE_VARIABLE, nullptr},      {"cb", &vCb, TE_VARIABLE, nullptr},
    {"ch", &vCh, TE_VARIABLE, nullptr},      {"cs", &vCs, TE_VARIABLE, nullptr},
    {"cv", &vCv, TE_VARIABLE, nullptr},
    {"frac", fn(fFrac), TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"tri", fn(fTri), TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"sq", fn(fSq), TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"pulse", fn(fPulse), TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"wave", fn(fWave), TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"clamp", fn(fClamp), TE_FUNCTION3 | TE_FLAG_PURE, nullptr},
    {"mix", fn(fMix), TE_FUNCTION3 | TE_FLAG_PURE, nullptr},
    {"smooth", fn(fSmooth), TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"noise", fn(fNoise), TE_FUNCTION1 | TE_FLAG_PURE, nullptr},
    {"sel", fn(fSel), TE_FUNCTION3 | TE_FLAG_PURE, nullptr},
    {"min", fn(fMin), TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"max", fn(fMax), TE_FUNCTION2 | TE_FLAG_PURE, nullptr},
    {"rand", fn(fRand), TE_FUNCTION0, nullptr},
};

te_expr *active[3] = {nullptr, nullptr, nullptr};
bool activeHsv = true;
String activeScript;

String pathFor(const String &name) {
  return String(DIR) + "/" + name + ".json";
}

void freeActive() {
  for (te_expr *&e : active) {
    if (e) te_free(e);
    e = nullptr;
  }
  activeScript = "";
}

// Compiles a, b, c into out. On failure frees what was compiled and describes the error.
bool compile(JsonObjectConst script, te_expr *out[3], bool &hsv, String &err) {
  const char *space = script["space"] | "hsv";
  if (strcmp(space, "hsv") != 0 && strcmp(space, "rgb") != 0) {
    err = F("space must be hsv or rgb");
    return false;
  }
  hsv = strcmp(space, "hsv") == 0;
  const char *const keys[] = {"a", "b", "c"};
  for (int i = 0; i < 3; i++) out[i] = nullptr;
  for (int i = 0; i < 3; i++) {
    const char *src = script[keys[i]] | "";
    if (strlen(src) == 0 || strlen(src) > MAX_EXPR_LEN) {
      err = String(keys[i]) + F(": formula must be 1-") + MAX_EXPR_LEN + F(" characters");
    } else {
      int pos = 0;
      out[i] = te_compile(src, VARS, sizeof(VARS) / sizeof(VARS[0]), &pos);
      if (!out[i]) err = String(keys[i]) + F(": syntax error at character ") + pos;
    }
    if (!out[i]) {
      for (int j = 0; j < i; j++) te_free(out[j]);
      return false;
    }
  }
  return true;
}

bool install(JsonObjectConst script, const String &name, String &err) {
  te_expr *compiled[3];
  bool hsv;
  if (!compile(script, compiled, hsv, err)) return false;
  freeActive();
  memcpy(active, compiled, sizeof(active));
  activeHsv = hsv;
  activeScript = name;
  return true;
}

bool readScript(const String &name, JsonDocument &doc) {
  File f = LittleFS.open(pathFor(name), "r");
  if (!f) return false;
  bool ok = !deserializeJson(doc, f);
  f.close();
  return ok;
}

size_t count() {
  size_t n = 0;
  // a missing directory would make openDir list the root instead
  if (!LittleFS.exists(DIR)) return 0;
  Dir dir = LittleFS.openDir(DIR);
  while (dir.next()) n++;
  return n;
}

}  // namespace

bool validName(const String &name) {
  if (name.length() == 0 || name.length() > 20) return false;
  for (char c : name) {
    if (!(islower(c) || isdigit(c) || c == '-' || c == '_')) return false;
  }
  for (const char *b : BUILTIN_NAMES) {
    if (name == b) return false;
  }
  return true;
}

bool exists(const String &name) {
  return validName(name) && LittleFS.exists(pathFor(name));
}

void list(JsonArray out, bool withSource) {
  if (!LittleFS.exists(DIR)) return;  // LittleFS drops the directory with its last file
  Dir dir = LittleFS.openDir(DIR);
  while (dir.next()) {
    String name = dir.fileName();
    if (!name.endsWith(".json")) continue;
    name.remove(name.length() - 5);
    if (!withSource) {
      out.add(name);
      continue;
    }
    JsonDocument doc;
    if (readScript(name, doc)) out.add(doc.as<JsonObjectConst>());
  }
}

bool save(JsonObjectConst script, String &err) {
  String name = script["name"] | "";
  if (!validName(name)) {
    err = F("name must be 1-20 of a-z 0-9 - _ and not a built-in effect");
    return false;
  }
  if (!exists(name) && count() >= MAX_SCRIPTS) {
    err = String(F("at most ")) + MAX_SCRIPTS + F(" scripts");
    return false;
  }
  te_expr *compiled[3];
  bool hsv;
  if (!compile(script, compiled, hsv, err)) return false;
  for (te_expr *e : compiled) te_free(e);

  JsonDocument doc;
  doc["name"] = name;
  doc["space"] = hsv ? "hsv" : "rgb";
  doc["a"] = script["a"];
  doc["b"] = script["b"];
  doc["c"] = script["c"];
  File f = LittleFS.open(pathFor(name), "w");
  if (!f || serializeJson(doc, f) == 0) {
    err = F("failed to save script");
    return false;
  }
  f.close();
  Log.printf("[scripts] saved %s\n", name.c_str());
  if (activeScript == name) {  // pick up edits while it runs
    freeActive();
    activate(name);
  }
  return true;
}

bool remove(const String &name) {
  if (!exists(name)) return false;
  if (activeScript == name) freeActive();
  return LittleFS.remove(pathFor(name));
}

bool activate(const String &name) {
  if (activeScript == name && active[0]) return true;
  JsonDocument doc;
  String err;
  if (!readScript(name, doc) || !install(doc.as<JsonObjectConst>(), name, err)) {
    Log.printf("[scripts] cannot run %s: %s\n", name.c_str(), err.length() ? err.c_str() : "not found");
    freeActive();
    return false;
  }
  return true;
}

bool activatePreview(JsonObjectConst script, String &err) {
  return install(script, PREVIEW_NAME, err);
}

const String &activeName() {
  return activeScript;
}

bool color(float seconds, const LedState &state, Rgb &out) {
  if (!active[0]) return false;
  vT = seconds;
  vSpeed = state.speed;
  vK = state.speed / 128.0;
  vCr = state.color.r / 255.0;
  vCg = state.color.g / 255.0;
  vCb = state.color.b / 255.0;
  double mx = fMax(vCr, fMax(vCg, vCb)), mn = fMin(vCr, fMin(vCg, vCb)), d = mx - mn;
  vCv = mx;
  vCs = mx > 0 ? d / mx : 0;
  vCh = d == 0 ? 0 : mx == vCr ? 60 * fmod((vCg - vCb) / d + 6, 6) : mx == vCg ? 60 * ((vCb - vCr) / d + 2) : 60 * ((vCr - vCg) / d + 4);

  double a = te_eval(active[0]), b = te_eval(active[1]), c = te_eval(active[2]);
  if (isnan(a)) a = 0;
  if (isnan(b)) b = 0;
  if (isnan(c)) c = 0;

  double r, g, bl;
  if (activeHsv) {
    double h = fmod(a, 360.0);
    if (h < 0) h += 360;
    double s = fClamp(b, 0, 1), v = fClamp(c, 0, 1);
    double ch = v * s, x = ch * (1 - fabs(fmod(h / 60, 2) - 1)), m = v - ch;
    r = g = bl = 0;
    if (h < 60) { r = ch; g = x; }
    else if (h < 120) { r = x; g = ch; }
    else if (h < 180) { g = ch; bl = x; }
    else if (h < 240) { g = x; bl = ch; }
    else if (h < 300) { r = x; bl = ch; }
    else { r = ch; bl = x; }
    r += m; g += m; bl += m;
  } else {
    r = fClamp(a, 0, 1);
    g = fClamp(b, 0, 1);
    bl = fClamp(c, 0, 1);
  }
  out = {(uint8_t)lround(r * 255), (uint8_t)lround(g * 255), (uint8_t)lround(bl * 255)};
  return true;
}

}  // namespace scripts
