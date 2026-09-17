#include "auth.h"

#include <LittleFS.h>
#include <MD5Builder.h>
#include <bearssl/bearssl_hash.h>

#include "config.h"
#include "console.h"

namespace auth {
namespace {

const char *AUTH_PATH = "/auth.json";
const char *SESSIONS_PATH = "/sessions.json";
const char *TOKENS_PATH = "/tokens.json";
const char *REALM = "DeskLED";
const char *COOKIE = "dl_session";
const char *UI_HEADER = "X-DeskLED";  // sent by the web UI; its 401s must not trigger the browser's login dialog
const uint32_t NONCE_LIFETIME_S = 300;
const size_t MAX_SESSIONS = 4;
const size_t USED_NONCES = 8;
const uint32_t SESSION_MAX_AGE_S = 30UL * 24 * 3600;

String ha1;      // MD5(user:realm:password)
String otaHash;  // MD5(password)
String apPass;   // empty = compiled-in default
bool usingDefault = true;
String nonceSecret;
bool staleNonce = false;

String sessions[MAX_SESSIONS];  // MD5 of session tokens, oldest first

struct Token {
  String id;    // short public identifier, used to revoke
  String name;  // what it is for, chosen by the user
  String hash;  // SHA-256 of the secret; the secret itself is never stored
  bool admin = false;
};
Token tokens[MAX_TOKENS];
String usedNonces[USED_NONCES];
size_t usedNonceNext = 0;

String md5(const String &s) {
  MD5Builder b;
  b.begin();
  b.add(s);
  b.calculate();
  return b.toString();
}

String randomHex(int words) {
  String out;
  char buf[9];
  for (int i = 0; i < words; i++) {
    snprintf(buf, sizeof(buf), "%08x", RANDOM_REG32);
    out += buf;
  }
  return out;
}

String sha256hex(const String &s) {
  br_sha256_context ctx;
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, s.c_str(), s.length());
  uint8_t out[32];
  br_sha256_out(&ctx, out);
  String hex;
  hex.reserve(64);
  char buf[3];
  for (uint8_t b : out) {
    snprintf(buf, sizeof(buf), "%02x", b);
    hex += buf;
  }
  return hex;
}

void deriveHashes(const String &password) {
  ha1 = md5(String(USERNAME) + ":" + REALM + ":" + password);
  otaHash = md5(password);
}

// nonce = <issue time, 8 hex><random, 8 hex><MAC over both with the boot secret, 16 hex>
// The random part keeps nonces unique within the same second, which single-use login nonces rely on.
String makeNonce() {
  char head[17];
  snprintf(head, sizeof(head), "%08lx%08x", millis() / 1000, RANDOM_REG32);
  return String(head) + md5(nonceSecret + ":" + head).substring(0, 16);
}

enum class NonceCheck { Valid, Expired, Invalid };

NonceCheck checkNonce(const String &nonce) {
  if (nonce.length() != 32) return NonceCheck::Invalid;
  String head = nonce.substring(0, 16);
  if (md5(nonceSecret + ":" + head).substring(0, 16) != nonce.substring(16)) return NonceCheck::Invalid;
  uint32_t issued = strtoul(nonce.substring(0, 8).c_str(), nullptr, 16);
  return millis() / 1000 - issued <= NONCE_LIFETIME_S ? NonceCheck::Valid : NonceCheck::Expired;
}

// For login and password change: valid, not expired, and not seen before (blocks replaying a captured login).
bool consumeNonce(const String &nonce) {
  if (checkNonce(nonce) != NonceCheck::Valid) return false;
  for (const String &used : usedNonces) {
    if (used == nonce) return false;
  }
  usedNonces[usedNonceNext] = nonce;
  usedNonceNext = (usedNonceNext + 1) % USED_NONCES;
  return true;
}

// Extracts key="value" or key=value from a Digest Authorization header.
String param(const String &header, const char *key) {
  String k = String(key) + "=";
  int pos = 0;
  while ((pos = header.indexOf(k, pos)) >= 0) {
    // must start a parameter, not be the tail of another name (e.g. nonce inside cnonce)
    char before = pos > 0 ? header[pos - 1] : ' ';
    if (before == ' ' || before == ',') break;
    pos += k.length();
  }
  if (pos < 0) return "";
  pos += k.length();
  if (header[pos] == '"') {
    int end = header.indexOf('"', pos + 1);
    return end < 0 ? "" : header.substring(pos + 1, end);
  }
  int end = header.indexOf(',', pos);
  String v = end < 0 ? header.substring(pos) : header.substring(pos, end);
  v.trim();
  return v;
}

const char *methodName(HTTPMethod m) {
  switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_DELETE: return "DELETE";
    case HTTP_PATCH: return "PATCH";
    case HTTP_HEAD: return "HEAD";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "GET";
  }
}

bool writeJson(const char *path, const JsonDocument &doc) {
  String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  LittleFS.remove(path);
  return ok && LittleFS.rename(tmp, path);
}

bool saveAuth() {
  JsonDocument doc;
  if (!usingDefault) {
    doc["ha1"] = ha1;
    doc["ota"] = otaHash;
  }
  if (apPass.length()) doc["ap"] = apPass;
  return writeJson(AUTH_PATH, doc);
}

void saveTokens() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const Token &t : tokens) {
    if (!t.hash.length()) continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"] = t.id;
    o["name"] = t.name;
    o["hash"] = t.hash;
    o["admin"] = t.admin;
  }
  writeJson(TOKENS_PATH, doc);
}

void loadTokens() {
  File f = LittleFS.open(TOKENS_PATH, "r");
  if (!f) return;
  JsonDocument doc;
  if (!deserializeJson(doc, f)) {
    size_t i = 0;
    for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
      if (i >= MAX_TOKENS) break;
      tokens[i].id = o["id"] | "";
      tokens[i].name = o["name"] | "";
      tokens[i].hash = o["hash"] | "";
      tokens[i].admin = o["admin"] | false;
      if (tokens[i].hash.length()) i++;
    }
  }
  f.close();
}

// Authorization: Bearer <secret>
Level bearerLevel(ESP8266WebServer &server) {
  String header = server.header("Authorization");
  if (!header.startsWith("Bearer ")) return Level::None;
  String secret = header.substring(7);
  secret.trim();
  if (secret.length() < 8 || secret.length() > 80) return Level::None;
  String hash = sha256hex(secret);
  for (const Token &t : tokens) {
    if (t.hash.length() && t.hash == hash) return t.admin ? Level::Admin : Level::Control;
  }
  return Level::None;
}

void saveSessions() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const String &s : sessions) {
    if (s.length()) arr.add(s);
  }
  writeJson(SESSIONS_PATH, doc);
}

void clearSessions() {
  for (String &s : sessions) s = "";
  saveSessions();
}

String sessionToken(ESP8266WebServer &server) {
  String cookies = server.header("Cookie");
  String key = String(COOKIE) + "=";
  int pos = cookies.indexOf(key);
  if (pos < 0) return "";
  pos += key.length();
  int end = cookies.indexOf(';', pos);
  return end < 0 ? cookies.substring(pos) : cookies.substring(pos, end);
}

bool validSession(ESP8266WebServer &server) {
  String token = sessionToken(server);
  if (token.length() != 32) return false;
  String hash = md5(token);
  for (const String &s : sessions) {
    if (s == hash) return true;
  }
  return false;
}

bool verifyDigest(ESP8266WebServer &server) {
  String header = server.header("Authorization");
  if (!header.startsWith("Digest ")) return false;

  String nonce = param(header, "nonce");
  switch (checkNonce(nonce)) {
    case NonceCheck::Invalid: return false;
    case NonceCheck::Expired: staleNonce = true; return false;
    case NonceCheck::Valid: break;
  }
  if (param(header, "username") != USERNAME || param(header, "realm") != REALM) return false;

  // the digest uri may include the query string, server.uri() does not
  String uri = param(header, "uri");
  int q = uri.indexOf('?');
  if ((q < 0 ? uri : uri.substring(0, q)) != server.uri()) return false;

  String ha2 = md5(String(methodName(server.method())) + ":" + uri);
  String qop = param(header, "qop");
  String expected = qop == "auth"
                        ? md5(ha1 + ":" + nonce + ":" + param(header, "nc") + ":" + param(header, "cnonce") + ":auth:" + ha2)
                        : md5(ha1 + ":" + nonce + ":" + ha2);
  return expected == param(header, "response");
}

void sendJsonText(ESP8266WebServer &server, int code, const String &json) {
  server.send(code, F("application/json"), json);
}

}  // namespace

void begin() {
  nonceSecret = randomHex(3);

  File f = LittleFS.open(AUTH_PATH, "r");
  if (f) {
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
      if (doc["ha1"].is<const char *>() && doc["ota"].is<const char *>()) {
        ha1 = doc["ha1"].as<const char *>();
        otaHash = doc["ota"].as<const char *>();
        usingDefault = false;
      }
      apPass = doc["ap"] | "";
    }
    f.close();
  }
  // hashes saved from the default password still count as default
  if (!usingDefault && checkPassword(DEFAULT_ADMIN_PASSWORD)) usingDefault = true;
  if (usingDefault) {
    deriveHashes(DEFAULT_ADMIN_PASSWORD);
    Log.println(F("[auth] using the default admin password; change it in the web UI or with: password <new>"));
  }

  loadTokens();

  f = LittleFS.open(SESSIONS_PATH, "r");
  if (f) {
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
      size_t i = 0;
      for (JsonVariantConst s : doc.as<JsonArrayConst>()) {
        if (i < MAX_SESSIONS) sessions[i++] = s.as<const char *>();
      }
    }
    f.close();
  }
}

void collectHeaders(ESP8266WebServer &server) {
  server.collectHeaders("Cookie", UI_HEADER);  // Authorization is always collected
}

Level level(ESP8266WebServer &server) {
  staleNonce = false;
  if (validSession(server) || verifyDigest(server)) return Level::Admin;
  return bearerLevel(server);
}

bool verify(ESP8266WebServer &server, Level needed) {
  return (uint8_t)level(server) >= (uint8_t)needed;
}

void challenge(ESP8266WebServer &server) {
  if (!server.hasHeader(UI_HEADER)) {
    String value = String(F("Digest realm=\"")) + REALM + F("\", qop=\"auth\", algorithm=MD5, nonce=\"") +
                   makeNonce() + '"' + (staleNonce ? F(", stale=true") : F(""));
    server.sendHeader(F("WWW-Authenticate"), value);
  }
  sendJsonText(server, 401, F("{\"error\":\"authentication required\"}"));
}

bool check(ESP8266WebServer &server, Level needed) {
  Level have = level(server);
  if ((uint8_t)have >= (uint8_t)needed) return true;
  if (have == Level::Control && needed == Level::Admin) {
    server.send(403, F("application/json"), F("{\"error\":\"this token may only control the light\"}"));
    return false;
  }
  challenge(server);
  return false;
}

void listTokens(JsonArray out) {
  for (const Token &t : tokens) {
    if (!t.hash.length()) continue;
    JsonObject o = out.add<JsonObject>();
    o["id"] = t.id;
    o["name"] = t.name;
    o["scope"] = t.admin ? "admin" : "control";
  }
}

bool createToken(const String &name, bool admin, String &secret, String &err) {
  if (name.length() == 0 || name.length() > 24) {
    err = F("name must be 1-24 characters");
    return false;
  }
  Token *slot = nullptr;
  for (Token &t : tokens) {
    if (!t.hash.length()) {
      slot = &t;
      break;
    }
  }
  if (!slot) {
    err = String(F("at most ")) + MAX_TOKENS + F(" tokens; revoke one first");
    return false;
  }
  secret = "dl_" + randomHex(4);
  slot->id = randomHex(1).substring(0, 6);
  slot->name = name;
  slot->hash = sha256hex(secret);
  slot->admin = admin;
  saveTokens();
  Log.printf("[auth] created %s token \"%s\" (%s)\n", admin ? "admin" : "control", name.c_str(), slot->id.c_str());
  return true;
}

bool deleteToken(const String &id) {
  for (Token &t : tokens) {
    if (t.hash.length() && t.id == id) {
      Log.printf("[auth] revoked token \"%s\" (%s)\n", t.name.c_str(), t.id.c_str());
      t = Token();
      saveTokens();
      return true;
    }
  }
  return false;
}

void handleLoginChallenge(ESP8266WebServer &server) {
  sendJsonText(server, 200, String(F("{\"nonce\":\"")) + makeNonce() + F("\",\"realm\":\"") + REALM +
                                F("\",\"user\":\"") + USERNAME + F("\"}"));
}

void handleLogin(ESP8266WebServer &server) {
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain"))) {
    sendJsonText(server, 400, F("{\"error\":\"body must be JSON\"}"));
    return;
  }
  String nonce = body["nonce"] | "";
  String response = body["response"] | "";
  Log.printf("[dbg] nonce %s check %d used? resp %s expected %s\n", nonce.c_str(), (int)checkNonce(nonce), response.c_str(), md5(ha1 + ":" + nonce).c_str());
  if (!consumeNonce(nonce) || md5(ha1 + ":" + nonce) != response) {
    Log.printf("[auth] failed web login from %s\n", server.client().remoteIP().toString().c_str());
    sendJsonText(server, 403, F("{\"error\":\"wrong password\"}"));
    return;
  }
  String token = randomHex(4);
  // evict the oldest session
  for (size_t i = 0; i + 1 < MAX_SESSIONS; i++) sessions[i] = sessions[i + 1];
  sessions[MAX_SESSIONS - 1] = md5(token);
  saveSessions();
  server.sendHeader(F("Set-Cookie"), String(COOKIE) + "=" + token + F("; Path=/; HttpOnly; SameSite=Strict; Max-Age=") +
                                         SESSION_MAX_AGE_S);
  Log.printf("[auth] web login from %s\n", server.client().remoteIP().toString().c_str());
  sendJsonText(server, 200, F("{\"ok\":true}"));
}

void handleLogout(ESP8266WebServer &server) {
  String token = sessionToken(server);
  if (token.length()) {
    String hash = md5(token);
    for (String &s : sessions) {
      if (s == hash) s = "";
    }
    saveSessions();
  }
  server.sendHeader(F("Set-Cookie"), String(COOKIE) + F("=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0"));
  sendJsonText(server, 200, F("{\"ok\":true}"));
}

bool setPasswordHashes(JsonObjectConst body, String &err) {
  String nonce = body["nonce"] | "";
  String proof = body["proof"] | "";
  String newHa1 = body["ha1"] | "";
  String newOta = body["ota"] | "";
  if (!consumeNonce(nonce) || md5(ha1 + ":" + nonce) != proof) {
    err = F("current password is wrong");
    return false;
  }
  if (newHa1.length() != 32 || newOta.length() != 32) {
    err = F("ha1 and ota must be MD5 hex digests");
    return false;
  }
  String oldHa1 = ha1, oldOta = otaHash;
  bool oldDefault = usingDefault;
  ha1 = newHa1;
  otaHash = newOta;
  usingDefault = false;
  if (!saveAuth()) {
    ha1 = oldHa1;
    otaHash = oldOta;
    usingDefault = oldDefault;
    err = F("failed to save password");
    return false;
  }
  clearSessions();
  return true;
}

bool checkPassword(const String &password) {
  return md5(String(USERNAME) + ":" + REALM + ":" + password) == ha1;
}

bool setPassword(const String &password, String &err) {
  if (password.length() < 8 || password.length() > 63) {
    err = F("password must be 8-63 characters");
    return false;
  }
  deriveHashes(password);
  usingDefault = false;
  if (!saveAuth()) {
    err = F("failed to save password");
    return false;
  }
  clearSessions();
  return true;
}

bool isDefaultPassword() {
  return usingDefault;
}

String apPassword() {
  return apPass.length() ? apPass : String(SETUP_AP_PASSWORD);
}

bool setApPassword(const String &password, String &err) {
  if (password.length() < 8 || password.length() > 63) {
    err = F("setup hotspot password must be 8-63 characters");
    return false;
  }
  apPass = password;
  if (!saveAuth()) {
    err = F("failed to save password");
    return false;
  }
  return true;
}

const String &otaPasswordHash() {
  return otaHash;
}

}  // namespace auth
