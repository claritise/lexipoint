// P11: the last connection's access point and channel, and how a join uses them.

#include <gtest/gtest.h>

#include <vector>

#include "lexirise/net/WifiHint.h"

using lexipoint::net::AttemptStep;
using lexipoint::net::attemptStep;
using lexipoint::net::ConnectionWatch;
using lexipoint::net::LinkState;
using lexipoint::net::WifiHint;
using lexipoint::net::WifiHints;
namespace config = lexipoint::config;

namespace {
WifiHint home() { return {"home", {0x40, 0xb0, 0x76, 0xa9, 0xf0, 0x60}, 10}; }

// A scripted radio: each attempt's result in turn; records what was asked and the restarts between.
struct Radio {
  std::vector<AttemptStep> results;
  struct Call {
    bool direct;
    uint32_t scanUntilMs;
  };
  std::vector<Call> calls;
  int restarts = 0;
  uint32_t clockMs = 0;
  uint32_t msPerAttempt = 0;
  AttemptStep operator()(const WifiHint* hint, const uint32_t scanUntilMs) {
    calls.push_back({hint != nullptr, scanUntilMs});
    clockMs += msPerAttempt;
    const AttemptStep r = results.front();
    results.erase(results.begin());
    return r;
  }
};
bool run(WifiHints& hints, Radio& radio, const char* ssid = "home") {
  return lexipoint::net::join(
      hints, ssid, [&radio](const WifiHint* h, uint32_t until) { return radio(h, until); },
      [&radio] { radio.restarts++; }, [&radio] { return radio.clockMs; });
}
}  // namespace

TEST(WifiHint, RememberedForItsNetworkOnly) {
  WifiHints hints;
  EXPECT_FALSE(hints.forSsid("home").has_value());  // nothing yet: scan
  hints.remember(home());
  ASSERT_TRUE(hints.forSsid("home").has_value());
  EXPECT_EQ(hints.forSsid("home")->channel, 10);
  EXPECT_FALSE(hints.forSsid("cafe").has_value());  // another network: scan
}

TEST(WifiHint, TheLatestConnectionWinsAndAnUnusableOneIsIgnored) {
  WifiHints hints;
  hints.remember(home());
  hints.remember({"", {1, 2, 3, 4, 5, 6}, 1});      // no name
  hints.remember({"home", {1, 2, 3, 4, 5, 6}, 0});  // no channel
  hints.remember({"home", {}, 3});                  // no access point
  EXPECT_EQ(hints.forSsid("home")->channel, 10);
  hints.remember({"cafe", {1, 2, 3, 4, 5, 6}, 1});
  EXPECT_FALSE(hints.forSsid("home").has_value());  // one network at a time: the one last connected to
}

TEST(WifiHint, AConnectionIsNoticedOnceWhoeverMadeIt) {
  // WifiSession::tick asks every loop pass: File Transfer's or KOSync's connection is remembered too.
  ConnectionWatch watch;
  EXPECT_FALSE(watch.newlyConnected(false));
  EXPECT_TRUE(watch.newlyConnected(true));
  EXPECT_FALSE(watch.newlyConnected(true));  // still the same connection
  EXPECT_FALSE(watch.newlyConnected(false));
  EXPECT_TRUE(watch.newlyConnected(true));  // a new one
}

TEST(WifiJoin, NoHintScansWithTheFullScanTime) {
  WifiHints hints;
  Radio radio{{AttemptStep::Up}};
  EXPECT_TRUE(run(hints, radio));
  ASSERT_EQ(radio.calls.size(), 1u);
  EXPECT_FALSE(radio.calls[0].direct);
  EXPECT_EQ(radio.calls[0].scanUntilMs, config::kWifiConnectMs);
  EXPECT_EQ(radio.restarts, 0);  // the radio was off: nothing to restart
}

TEST(WifiJoin, AHintThatWorksNeedsNoScan) {
  WifiHints hints;
  hints.remember(home());
  Radio radio{{AttemptStep::Up}};
  EXPECT_TRUE(run(hints, radio));
  ASSERT_EQ(radio.calls.size(), 1u);
  EXPECT_TRUE(radio.calls[0].direct);
  EXPECT_TRUE(hints.forSsid("home").has_value());  // kept for next time
}

TEST(WifiJoin, AFailedDirectJoinIsForgottenAndTheScanGetsWhatsLeft) {
  WifiHints hints;
  hints.remember(home());
  Radio radio{{AttemptStep::Failed, AttemptStep::Up}};
  radio.msPerAttempt = 400;  // the driver said "not found" before its retry (a best case; usually it's 3 s)
  EXPECT_TRUE(run(hints, radio));
  ASSERT_EQ(radio.calls.size(), 2u);
  EXPECT_TRUE(radio.calls[0].direct);
  EXPECT_FALSE(radio.calls[1].direct);
  EXPECT_EQ(radio.restarts, 1);                                         // the radio stopped between the two
  EXPECT_EQ(radio.calls[1].scanUntilMs, 400 + config::kWifiConnectMs);  // the full scan still fits
  EXPECT_FALSE(hints.forSsid("home").has_value());
  // A direct attempt that ran long leaves the scan only until the join's budget ends (one clock).
  hints.remember(home());
  Radio slow{{AttemptStep::Failed, AttemptStep::Failed}};
  slow.msPerAttempt = config::kWifiJoinMaxMs - 2000;
  EXPECT_FALSE(run(hints, slow));
  EXPECT_EQ(slow.calls[1].scanUntilMs, config::kWifiJoinMaxMs);
}

TEST(WifiJoin, AssociatedWithNoAddressIsTheEndNotAScan) {
  WifiHints hints;
  hints.remember(home());
  Radio radio{{AttemptStep::OutOfTime}};
  EXPECT_FALSE(run(hints, radio));
  EXPECT_EQ(radio.calls.size(), 1u);  // a scan wouldn't bring DHCP back, and the time is gone
  EXPECT_EQ(radio.restarts, 0);
  EXPECT_FALSE(hints.forSsid("home").has_value());
}

TEST(WifiJoin, AnotherNetworkScans) {
  WifiHints hints;
  hints.remember(home());
  Radio radio{{AttemptStep::Up}};
  EXPECT_TRUE(run(hints, radio, "cafe"));
  EXPECT_FALSE(radio.calls[0].direct);
}

TEST(WifiAttempt, DirectGivesUpAtOnceWhenTheAccessPointIsntThere) {
  EXPECT_EQ(attemptStep(true, LinkState::NotFound, 200, 0), AttemptStep::Failed);
  EXPECT_EQ(attemptStep(true, LinkState::Refused, 200, 0), AttemptStep::Failed);
  EXPECT_EQ(attemptStep(true, LinkState::Connected, 200, 0), AttemptStep::Up);
}

TEST(WifiAttempt, DirectWaitsItsTimeUnlessAssociated) {
  EXPECT_EQ(attemptStep(true, LinkState::Searching, config::kWifiDirectJoinMs - 1, 0), AttemptStep::Wait);
  EXPECT_EQ(attemptStep(true, LinkState::Searching, config::kWifiDirectJoinMs, 0), AttemptStep::Failed);
  // On the access point, waiting for DHCP: not a moved router, so it waits up to the join's whole budget, and
  // running out there ends the join (OutOfTime: no scan).
  EXPECT_EQ(attemptStep(true, LinkState::Associated, config::kWifiDirectJoinMs + 1000, 0), AttemptStep::Wait);
  EXPECT_EQ(attemptStep(true, LinkState::Associated, config::kWifiJoinMaxMs, 0), AttemptStep::OutOfTime);
}

TEST(WifiAttempt, TheLibrarysStatusMapsToTheJoinsStates) {
  using lexipoint::net::linkStateOf;
  namespace wl = lexipoint::net::wl;
  EXPECT_EQ(linkStateOf(wl::kConnected), LinkState::Connected);
  EXPECT_EQ(linkStateOf(wl::kIdle), LinkState::Associated);
  EXPECT_EQ(linkStateOf(wl::kNoSsid), LinkState::NotFound);
  EXPECT_EQ(linkStateOf(wl::kConnectFailed), LinkState::Refused);
  EXPECT_EQ(linkStateOf(6), LinkState::Searching);    // WL_DISCONNECTED
  EXPECT_EQ(linkStateOf(254), LinkState::Searching);  // WL_STOPPED
}

TEST(WifiAttempt, TheScanWaitsItsTimeWhateverTheState) {
  EXPECT_EQ(attemptStep(false, LinkState::NotFound, 100, 5000), AttemptStep::Wait);  // the scan is still going
  EXPECT_EQ(attemptStep(false, LinkState::Searching, 5000, 5000), AttemptStep::Failed);
  EXPECT_EQ(attemptStep(false, LinkState::Associated, 5000, 5000), AttemptStep::Failed);  // no OutOfTime: it's last
  EXPECT_EQ(attemptStep(false, LinkState::Connected, 100, 5000), AttemptStep::Up);
}
