#pragma once

// When Lexipoint may use, bring up, and give back WiFi (offline-and-errors.md §5). Pure; WifiSession
// and LexiriseService feed it. Tests: test/lexirise_net/WifiLeaseTest.cpp.
//
// The rules:
//   - Lexipoint only ever *brings WiFi up* from radio-off. A connected station is used as it is; a
//     radio that's on but not connected (the web server's hotspot, someone else's join in progress)
//     is somebody else's and is left alone.
//   - WiFi Lexipoint brought up is its own (the lease) until it idles out, or until the screen
//     leaves reading: any other activity (KOSync, the web server, OTA, ...) gets a radio that's off
//     and brings WiFi up itself, so Lexipoint can never turn WiFi off under it.

#include <string_view>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::net {

enum class WifiAction { UseExisting, Busy, Join };

inline WifiAction decideEnsureUp(const bool stationConnected, const bool radioOff) {
  if (stationConnected) return WifiAction::UseExisting;
  return radioOff ? WifiAction::Join : WifiAction::Busy;
}

// The activities that are "reading": Lexipoint keeps its WiFi across them (a lookup card is pushed
// over the reader, and closing it must not drop WiFi before the next lookup).
inline bool keepsLookupWifi(const std::string_view activityName, const bool isReaderActivity) {
  if (isReaderActivity) return true;
  for (const char* name : config::kLookupActivityNames) {
    if (activityName == name) return true;
  }
  return false;
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
