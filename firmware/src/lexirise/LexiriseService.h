#pragma once

// The device's one Lexirise session: settings snapshot → WiFi → verified TLS → client. Used by the
// /lexirise web page (key check), the dev harness (LX:LEXI), and from P3 the lookup provider.
// Main task only (web handlers run there too, from CrossPointWebServerActivity's loop).

#include <cstdint>
#include <string_view>

#include "api/KeyCheck.h"
#include "api/LexiriseClient.h"
#include "net/TlsConnection.h"
#include "settings/Settings.h"

namespace lexipoint {

class LexiriseService {
 public:
  LexiriseService();

  // GET /v1/me with the current key; the result is cached for keyStatus().
  api::KeyStatus checkKey();
  api::KeyStatus keyStatus() const { return status_; }
  // The key or server changed: the cached status no longer applies.
  void invalidateKeyStatus() { status_ = api::KeyStatus(); }

  // POST /v1/analyze/text (the raw response; api::parseAnalyze reads it).
  api::ApiResponse analyze(Language language, std::string_view text);

  // Main-loop tick: WiFi idle teardown.
  void tick();

 private:
  api::ApiResponse send(const net::Request& request);

  net::TlsConnection connection_;
  api::LexiriseClient client_;
  api::KeyStatus status_;
  bool wifiIdleKnown_ = false;
  uint32_t wifiIdleRevision_ = 0;
  int wifiIdleMin_ = 0;
};

LexiriseService& service();

}  // namespace lexipoint
