#pragma once

#include <Arduino.h>

#include <functional>

// Log output goes to Serial, a history buffer, and the telnet client (port 23) once it has logged in.
extern Print &Log;

namespace console {

using LineHandler = std::function<void(const String &line)>;

void beginTelnet();
// Reads command lines from Serial and telnet and passes them to onLine.
void loop(const LineHandler &onLine);

}  // namespace console
