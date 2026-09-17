#include "console.h"

#include <ESP8266WiFi.h>

#include "auth.h"

namespace {

const size_t HISTORY_SIZE = 2048;
const size_t MAX_LINE = 160;
const uint8_t MAX_LOGIN_TRIES = 3;

WiFiServer telnetServer(23);
WiFiClient telnet;
bool telnetAuthed = false;
uint8_t loginTries = 0;

char history[HISTORY_SIZE];
size_t historyHead = 0;  // next write position
bool historyFull = false;

class TeeLog : public Print {
 public:
  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t *buf, size_t len) override {
    Serial.write(buf, len);
    for (size_t i = 0; i < len; i++) {
      history[historyHead] = buf[i];
      historyHead = (historyHead + 1) % HISTORY_SIZE;
      if (historyHead == 0) historyFull = true;
    }
    if (telnetAuthed && telnet.connected()) {
      // telnet clients expect CRLF line endings
      for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n' && (i == 0 || buf[i - 1] != '\r')) telnet.write('\r');
        telnet.write(buf[i]);
      }
    }
    return len;
  }
};

TeeLog teeLog;

void sendHistory() {
  auto send = [](const char *start, size_t len) {
    for (size_t i = 0; i < len; i++) {
      if (start[i] == '\n' && (i == 0 || start[i - 1] != '\r')) telnet.write('\r');
      telnet.write(start[i]);
    }
  };
  if (historyFull) send(history + historyHead, HISTORY_SIZE - historyHead);
  send(history, historyHead);
}

// Collects bytes into lines; returns true when a full line is ready.
bool feed(String &line, char c) {
  if (c == '\r' || c == '\n') return line.length() > 0;
  if (line.length() < MAX_LINE) line += c;
  return false;
}

void acceptTelnet() {
  WiFiClient incoming = telnetServer.accept();
  if (!incoming) return;
  if (telnet.connected()) {
    telnet.print(F("\r\n[console] replaced by a new connection\r\n"));
    telnet.stop();
  }
  telnet = incoming;
  telnet.setNoDelay(true);
  telnetAuthed = false;
  loginTries = 0;
  telnet.print(F("DeskLED console\r\npassword: "));
}

void handleTelnetLine(const String &line, const console::LineHandler &onLine) {
  if (!telnetAuthed) {
    if (auth::checkPassword(line)) {
      telnetAuthed = true;
      telnet.print(F("\r\n--- recent log ---\r\n"));
      sendHistory();
      telnet.print(F("--- live (type help, exit to disconnect) ---\r\n"));
      Log.printf("[console] telnet login from %s\n", telnet.remoteIP().toString().c_str());
    } else if (++loginTries >= MAX_LOGIN_TRIES) {
      telnet.print(F("\r\ntoo many attempts\r\n"));
      telnet.stop();
    } else {
      telnet.print(F("\r\nwrong password\r\npassword: "));
    }
    return;
  }
  if (line == "exit" || line == "quit") {
    telnet.print(F("bye\r\n"));
    telnet.stop();
    telnetAuthed = false;
    return;
  }
  onLine(line);
}

}  // namespace

Print &Log = teeLog;

namespace console {

void beginTelnet() {
  telnetServer.begin();
  telnetServer.setNoDelay(true);
}

void loop(const LineHandler &onLine) {
  static String serialLine;
  while (Serial.available()) {
    if (feed(serialLine, Serial.read())) {
      onLine(serialLine);
      serialLine = "";
    }
  }

  acceptTelnet();
  if (!telnet.connected()) {
    telnetAuthed = false;
    return;
  }
  static String telnetLine;
  static uint8_t skipIac = 0;
  while (telnet.available()) {
    uint8_t c = telnet.read();
    // drop telnet option negotiation (IAC + command + option)
    if (skipIac) {
      skipIac--;
      continue;
    }
    if (c == 0xFF) {
      skipIac = 2;
      continue;
    }
    if (c == 0) continue;
    if (feed(telnetLine, c)) {
      String line = telnetLine;
      telnetLine = "";
      handleTelnetLine(line, onLine);
      if (!telnet.connected()) return;
    }
  }
}

}  // namespace console
