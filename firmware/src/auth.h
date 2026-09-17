#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

// Single admin account protecting the web UI, REST API, telnet console and OTA.
// Only password hashes are stored on flash. Two ways to authenticate over HTTP:
// - session cookie from the web UI login (challenge-response, the password never crosses the network)
// - Digest auth for scripts and tools (stateless expiring nonces, so several clients work at once)
// Basic auth is refused.
namespace auth {

const char *const USERNAME = "admin";

void begin();

// Headers the web server must collect for authentication.
void collectHeaders(ESP8266WebServer &server);

// True if the request carries a valid session cookie or Digest credentials. Sends nothing.
bool verify(ESP8266WebServer &server);
// Sends a 401: a Digest challenge for tools, or a plain 401 for the web UI (so the browser shows no dialog).
void challenge(ESP8266WebServer &server);
// verify() or challenge(); returns whether the handler may proceed.
bool check(ESP8266WebServer &server);

// Web UI login: GET returns a single-use nonce; POST {nonce, response = MD5(HA1 ":" nonce)} sets a session cookie.
void handleLoginChallenge(ESP8266WebServer &server);
void handleLogin(ESP8266WebServer &server);
void handleLogout(ESP8266WebServer &server);

// Password change from the web UI without sending passwords: {nonce, proof = MD5(oldHA1 ":" nonce), ha1, ota}.
// ha1 = MD5("admin:DeskLED:" new), ota = MD5(new). The client enforces the length rule.
bool setPasswordHashes(JsonObjectConst body, String &err);

bool checkPassword(const String &password);
// Stores new password hashes. Password must be 8-63 characters.
bool setPassword(const String &password, String &err);
bool isDefaultPassword();

// Password of the setup access point (plain text, the SDK needs it).
String apPassword();
bool setApPassword(const String &password, String &err);

// MD5(password), as ArduinoOTA expects.
const String &otaPasswordHash();

}  // namespace auth
