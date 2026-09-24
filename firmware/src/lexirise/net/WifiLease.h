#pragma once

// Who owns the WiFi connection, and when Lexipoint should give it back (offline-and-errors.md §5).
// Pure; WifiSession feeds it. Tests: test/lexirise_net/WifiLeaseTest.cpp.
//
// Lexipoint tears WiFi down only if it brought WiFi up itself, and only after the configured idle
// time. The moment anything else touches the connection (the web server or KOSync reconnecting, or
// the link dropping) the lease is lost for good and WiFi is theirs.

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::net {

class WifiLease {
 public:
  void acquired(const unsigned long nowMs) {
    owned_ = true;
    lastUseMs_ = nowMs;
  }
  void used(const unsigned long nowMs) { lastUseMs_ = nowMs; }
  void lost() { owned_ = false; }
  bool owned() const { return owned_; }

  // idleMin 0 means "off right after a lookup". Unsigned arithmetic keeps a millis() wrap harmless.
  bool expired(const unsigned long nowMs, const int idleMin) const {
    if (!owned_) return false;
    const unsigned long idleMs = static_cast<unsigned long>(idleMin < 0 ? 0 : idleMin) * config::kMsPerMinute;
    return nowMs - lastUseMs_ >= idleMs;
  }

 private:
  bool owned_ = false;
  unsigned long lastUseMs_ = 0;
};

}  // namespace lexipoint::net
