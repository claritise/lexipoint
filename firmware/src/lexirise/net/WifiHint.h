#pragma once

// Where the last connection to a network was, and how a join uses it (P11, found on the device: a join spent
// ~3.5 s scanning every channel, and twice ran out of time right after another WiFi user let go). With a hint,
// a join first asks the driver for that access point, starting on its channel (a fast scan with the BSSID:
// the driver starts there and goes on to the other channels until it finds it, so a router that moved channel
// is still joined directly, and the hint updates), and only if that fails scans every channel as before. The
// hint is kept for the boot: an access point that's gone (a different place) usually costs the direct
// attempt's whole config::kWifiDirectJoinMs (the driver reports "not found" only after its own scan, and
// retries at once), then the hint is forgotten. Pure; WifiSession feeds it the radio. Tests:
// test/lexirise_net/WifiHintTest.cpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::net {

constexpr size_t kBssidLength = 6;  // an access point's MAC address
using Bssid = std::array<uint8_t, kBssidLength>;

struct WifiHint {
  std::string ssid;
  Bssid bssid{};
  int32_t channel = 0;
};

class WifiHints {
 public:
  // A connection seen on `hint.ssid` (ours, or anyone's: the web server's, KOSync's). An unusable one
  // (no network name, no channel, an all-zero access point) is ignored.
  void remember(const WifiHint& hint) {
    if (hint.ssid.empty() || hint.channel <= 0 || hint.bssid == Bssid{}) return;
    hint_ = hint;
  }
  // The direct attempt failed: the router moved, or this is somewhere else. The next join scans.
  void forget() { hint_.reset(); }
  // The hint for joining `ssid`, if the last connection seen was to it.
  std::optional<WifiHint> forSsid(const std::string_view ssid) const {
    if (hint_ && hint_->ssid == ssid) return hint_;
    return std::nullopt;
  }

 private:
  std::optional<WifiHint> hint_;
};

// Notices a connection appearing, whoever made it: WifiSession::tick asks every loop pass, so a connection
// the web server or KOSync made is remembered too, not only Lexipoint's own.
class ConnectionWatch {
 public:
  // True once per connection: on the first pass it's seen connected.
  bool newlyConnected(const bool connected) {
    const bool fresh = connected && !was_;
    was_ = connected;
    return fresh;
  }

 private:
  bool was_ = false;
};

// The station's state during a join (from the WiFi library's status).
enum class LinkState : uint8_t {
  Searching,   // looking for the access point, or not started
  Associated,  // on the access point, waiting for an address (DHCP)
  Connected,   // an address: up
  NotFound,    // no such access point (on that channel)
  Refused,     // the access point turned the station away (a wrong password, say)
};

// What a join attempt does now: wait, or it's over (up, failed, or failed with the join's time spent).
enum class AttemptStep : uint8_t { Wait, Up, Failed, OutOfTime };

// The WiFi library's station status (wl_status_t, WiFiType.h; WifiSession.cpp checks these against it) as a
// join sees it.
namespace wl {
constexpr int kIdle = 0;           // WL_IDLE_STATUS: STA_CONNECTED, on the access point, no address yet
constexpr int kNoSsid = 1;         // WL_NO_SSID_AVAIL
constexpr int kConnected = 3;      // WL_CONNECTED: an address
constexpr int kConnectFailed = 4;  // WL_CONNECT_FAILED
}  // namespace wl
inline LinkState linkStateOf(const int status) {
  switch (status) {
    case wl::kConnected:
      return LinkState::Connected;
    case wl::kIdle:
      return LinkState::Associated;
    case wl::kNoSsid:
      return LinkState::NotFound;
    case wl::kConnectFailed:
      return LinkState::Refused;
    default:
      return LinkState::Searching;  // disconnected, stopped, scan done, lost: still looking
  }
}

// The attempt's step in `state`, `elapsedMs` since the join began (one clock for the whole join, radio restarts
// included). The direct attempt (the first) gives up when the driver reports the access point not found or
// refusing (if the poll sees it before the driver's own retry), and at
// config::kWifiDirectJoinMs unless it has associated: then it waits for its address (a slow DHCP isn't a moved
// router) up to config::kWifiJoinMaxMs, and running out there is OutOfTime (a scan wouldn't bring DHCP back).
// The scan waits until `scanUntilMs`.
inline AttemptStep attemptStep(const bool direct, const LinkState state, const uint32_t elapsedMs,
                               const uint32_t scanUntilMs) {
  if (state == LinkState::Connected) return AttemptStep::Up;
  if (!direct) return elapsedMs < scanUntilMs ? AttemptStep::Wait : AttemptStep::Failed;
  if (state == LinkState::NotFound || state == LinkState::Refused) return AttemptStep::Failed;
  if (state == LinkState::Associated) {
    return elapsedMs < config::kWifiJoinMaxMs ? AttemptStep::Wait : AttemptStep::OutOfTime;
  }
  return elapsedMs < config::kWifiDirectJoinMs ? AttemptStep::Wait : AttemptStep::Failed;
}

// A join of `ssid`: straight to the hint's access point when there is one, forgetting it if that fails, then
// `restart()` (the radio stopped, for a fresh start) and the scan, until config::kWifiConnectMs after it
// starts or config::kWifiJoinMaxMs after the join began, whichever is first. `attempt(const WifiHint* hint,
// uint32_t scanUntilMs)` runs one (hint: direct; nullptr: scan) to an end other than Wait; `elapsed()` is the
// time since the join began. True when up.
template <typename Attempt, typename Restart, typename Elapsed>
bool join(WifiHints& hints, const std::string_view ssid, Attempt&& attempt, Restart&& restart, Elapsed&& elapsed) {
  if (const auto hint = hints.forSsid(ssid)) {
    const AttemptStep direct = attempt(&*hint, 0u);
    if (direct == AttemptStep::Up) return true;
    hints.forget();
    if (direct == AttemptStep::OutOfTime) return false;
    restart();
  }
  const uint32_t start = elapsed();
  const uint32_t until =
      start + config::kWifiConnectMs < config::kWifiJoinMaxMs ? start + config::kWifiConnectMs : config::kWifiJoinMaxMs;
  return attempt(nullptr, until) == AttemptStep::Up;
}

}  // namespace lexipoint::net
