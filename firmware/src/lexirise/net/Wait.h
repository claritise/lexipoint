#pragma once

// Blocking waits in the Lexirise network code run on the main loop task (lookups, and web handlers,
// which CrossPointWebServerActivity drives from its loop). A TLS handshake or WiFi join can take
// seconds, so every poll feeds the task watchdog the way the upstream loops do.

#include <Arduino.h>

#include "util/TaskWatchdog.h"

namespace lexipoint::net {

inline void pollWait(const uint32_t ms) {
  resetTaskWatchdogIfSubscribed();
  delay(ms);
}

}  // namespace lexipoint::net
