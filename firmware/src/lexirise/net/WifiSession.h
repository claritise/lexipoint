#pragma once

// Brings WiFi up for a Lexirise call using the saved credentials (the store WifiSelectionActivity
// writes), without any UI, and gives it back when idle or when the screen leaves reading
// (offline-and-errors.md §5). The rules are in WifiLease.h; WifiControl is the seam LexiriseService
// is tested through. Main task only.

#include <cstdint>

#include "WifiHint.h"
#include "WifiLease.h"

namespace lexipoint::net {

enum class WifiResult {
  Up,
  NotConfigured,  // no saved network
  Busy,           // the radio is someone else's (the web server's hotspot, a join in progress)
  Failed,         // the join timed out
};

class WifiControl {
 public:
  virtual ~WifiControl() = default;
  // Connected already (by anyone) → Up. Radio off → joins the last-used saved network. Never opens
  // the WiFi selection screen and never touches a radio that's on for someone else.
  virtual WifiResult ensureUp() = 0;
  // A Lexirise call just used WiFi: the idle clock restarts.
  virtual void touch() = 0;
  // Gives WiFi back if Lexipoint owns it and it has been idle `idleMin` minutes. True if it did.
  virtual bool tick(int idleMin) = 0;
  // Gives WiFi back now if Lexipoint owns it. True if it did.
  virtual bool release() = 0;
};

class WifiSession final : public WifiControl {
 public:
  WifiResult ensureUp() override;
  void touch() override;
  bool tick(int idleMin) override;
  bool release() override;

 private:
  void tearDown();
  // One join attempt of the saved network, straight to `hint`'s access point or (nullptr) scanning every
  // channel; net::attemptStep decides when it's over, timed from `joinStarted` (millis()). The radio is left as
  // it ends.
  AttemptStep attempt(const std::string& ssid, const std::string& password, const WifiHint* hint, uint32_t scanUntilMs,
                      unsigned long joinStarted);
  // The station off, and waited for until it says so (up to config::kWifiStopWaitMs): its events come from
  // another task, and one still queued from a direct attempt would otherwise disturb the next attempt.
  void stopRadio();
  void rememberConnection();  // the connection now up, as the next join's hint

  WifiLease lease_;
  WifiHints hints_;
  ConnectionWatch watch_;
};

}  // namespace lexipoint::net
