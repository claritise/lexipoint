#if LEXIRISE

// The device's LexiriseService: the SD settings store, real WiFi, verified TLS, millis().

#include <Arduino.h>

#include "LexiriseService.h"
#include "api/Requests.h"
#include "net/TlsConnection.h"
#include "net/WifiSession.h"

namespace lexipoint {
namespace {

unsigned long nowMs() { return millis(); }

}  // namespace

LexiriseService& service() {
  static net::TlsConnection connection;
  static net::WifiSession wifi;
  static LexiriseService instance(settingsStore(), wifi, connection, api::userAgent(CROSSPOINT_VERSION), nowMs);
  return instance;
}

}  // namespace lexipoint

#endif  // LEXIRISE
