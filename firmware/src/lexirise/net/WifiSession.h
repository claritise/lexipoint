#pragma once

// Brings WiFi up for a Lexirise call using the saved credentials (the store WifiSelectionActivity
// writes), without any UI, and takes it down again when idle (offline-and-errors.md §5). Device
// only; the ownership rules live in WifiLease. Main task only.

#include <atomic>
#include <cstdint>
#include <string>

#include "WifiLease.h"

namespace lexipoint::net {

enum class WifiResult { Up, NotConfigured, Failed };

class WifiSession {
 public:
  // Connected already (by anyone) → Up. Otherwise joins the last-used saved network within
  // config::kWifiConnectMs. Never opens the WiFi selection screen.
  WifiResult ensureUp();

  // Marks a Lexirise call so the idle clock restarts.
  void touch();

  // Main-loop tick: gives WiFi back after `idleMin` minutes without a call, if Lexipoint owns it.
  void tick(int idleMin);

  // Takes WiFi down now if Lexipoint owns it (leaving the reader, before sleep).
  void release();

 private:
  void registerEvents();
  void tearDown();

  // Our own transition in flight. The event task ends it on its terminal event (GOT_IP / STA_STOP),
  // which the driver emits after the transition's other events, so none of ours reads as foreign.
  enum class Phase : uint8_t { Idle, Connecting, Disconnecting };
  void waitForPhaseIdle();

  WifiLease lease_;
  bool eventsRegistered_ = false;
  std::atomic<Phase> phase_{Phase::Idle};
  std::atomic<bool> foreignEvent_{false};  // someone else (re)connected or the link dropped
};

WifiSession& wifiSession();

}  // namespace lexipoint::net
