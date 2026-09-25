#pragma once

// Blocking waits in the Lexirise network code run on the main loop task (lookups, the dev harness,
// and the web page's key check, queued to LexiriseService::tick). A TLS handshake or WiFi join can
// take seconds, so every poll feeds the task watchdog the way CrossPoint's loops do.

#include <Arduino.h>

#include "util/TaskWatchdog.h"

namespace lexipoint::net {

inline void pollWait(const uint32_t ms) {
  resetTaskWatchdogIfSubscribed();
  delay(ms);
}

}  // namespace lexipoint::net
