#pragma once

// When Lexipoint may use, bring up, and give back WiFi (offline-and-errors.md §5). Pure; WifiSession
// and LexiriseService feed it. Tests: test/lexirise_net/WifiLeaseTest.cpp.
//
// The rules:
//   - Lexipoint only ever *brings WiFi up* from radio-off. A connected station is used as it is; a
//     radio that's on but not connected (the web server's hotspot, someone else's join in progress)
//     is somebody else's and is left alone.
//   - WiFi Lexipoint brought up is its own (the lease) until it idles out, or until the screen
//     leaves reading (no reader activity on screen or under it). ActivityManager reports that before
//     the next activity's onEnter, so any other activity (KOSync, the web server, OTA, ...) starts
//     with the radio off and brings WiFi up itself: Lexipoint never turns WiFi off under it. This
//     relies on nothing that uses WiFi being pushed over the reader (KOSync replaces it).

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::net {

enum class WifiAction { UseExisting, Busy, Join, Rejoin };

// owned: Lexipoint brought the radio up itself. Its own link dropping (AP restart, out of range) is
// Lexipoint's to fix: turn the radio off and join again, rather than calling its own radio "busy".
inline WifiAction decideEnsureUp(const bool stationConnected, const bool radioOff, const bool owned) {
  if (stationConnected) return WifiAction::UseExisting;
  if (radioOff) return WifiAction::Join;
  return owned ? WifiAction::Rejoin : WifiAction::Busy;
}

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
