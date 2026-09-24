// LexiriseService orchestration with fakes: settings → WiFi → client, the queued key check, the
// idle TLS close, and giving WiFi back when the screen leaves reading.

#include <gtest/gtest.h>

#include "Fakes.h"
#include "lexirise/LexiriseService.h"

using lexipoint::LexiriseService;
using lexipoint::Settings;
using lexipoint::SettingsStore;
using lexipoint::api::ApiError;
using lexipoint::api::KeyState;
using lexipoint::fakes::FakeClock;
using lexipoint::fakes::FakeConnection;
using lexipoint::fakes::FakeFiles;
using lexipoint::fakes::FakeWifi;
using lexipoint::fakes::httpOk;
using lexipoint::net::WifiResult;
namespace config = lexipoint::config;

namespace {

constexpr const char* kMe = R"({"user":{"name":"R","plan":"pro"},"apiKey":{"rateLimitMax":1200}})";

struct Rig {
  FakeFiles files;
  SettingsStore store{files};
  FakeWifi wifi;
  FakeConnection conn;
  LexiriseService service{store, wifi, conn, "UA", FakeClock::now};

  explicit Rig(bool withKey = true) {
    store.load();
    if (withKey) {
      store.update([](Settings& s) {
        s.apiKey = "lx_TESTKEYtestkey0123456789";
        return true;
      });
    }
  }
};

}  // namespace

TEST(Service, NoKeyNeverTouchesWifi) {
  Rig rig(false);
  EXPECT_EQ(rig.service.checkKey().state, KeyState::NoKey);
  EXPECT_EQ(rig.wifi.ensures, 0);
  EXPECT_EQ(rig.conn.opens, 0);
}

TEST(Service, NoWifiIsOfflineAndOpensNothing) {
  for (const auto result : {WifiResult::Busy, WifiResult::NotConfigured, WifiResult::Failed}) {
    Rig rig;
    rig.wifi.result = result;
    const auto status = rig.service.checkKey();
    EXPECT_EQ(status.state, KeyState::Offline);
    EXPECT_EQ(status.error, ApiError::NoWifi);
    EXPECT_EQ(rig.conn.opens, 0);
  }
}

TEST(Service, CheckKeyGoesThroughWifiAndCaches) {
  Rig rig;
  rig.conn.reads = {httpOk(kMe)};
  const auto status = rig.service.checkKey();
  EXPECT_EQ(status.state, KeyState::Connected);
  EXPECT_EQ(status.me.name, "R");
  EXPECT_EQ(rig.service.keyStatus().state, KeyState::Connected);
  EXPECT_EQ(rig.wifi.touches, 1);
  ASSERT_EQ(rig.conn.written.size(), 1u);
  EXPECT_EQ(rig.conn.written[0].rfind("GET /v1/me HTTP/1.1", 0), 0u);
}

TEST(Service, QueuedCheckRunsOnTheNextTick) {
  Rig rig;
  rig.conn.reads = {httpOk(kMe)};
  rig.service.requestKeyCheck();
  EXPECT_EQ(rig.service.keyStatus().state, KeyState::Checking);
  EXPECT_EQ(rig.conn.opens, 0);  // nothing ran inside the "request"
  rig.service.tick();
  EXPECT_EQ(rig.service.keyStatus().state, KeyState::Connected);
  rig.service.tick();  // once only
  EXPECT_EQ(rig.conn.written.size(), 1u);
}

TEST(Service, IdleSessionIsClosedAfterTheTimeout) {
  Rig rig;
  rig.conn.reads = {httpOk(kMe)};
  rig.service.checkKey();
  ASSERT_TRUE(rig.conn.isOpen());
  FakeClock::nowMs += config::kTlsIdleCloseMs - 1;
  rig.service.tick();
  EXPECT_TRUE(rig.conn.isOpen());
  FakeClock::nowMs += 1;
  rig.service.tick();
  EXPECT_FALSE(rig.conn.isOpen());
}

TEST(Service, WifiTeardownClosesTheSession) {
  Rig rig;
  rig.conn.reads = {httpOk(kMe)};
  rig.service.checkKey();
  rig.wifi.owned = true;
  rig.wifi.expireOnNextTick = true;
  rig.service.tick();
  EXPECT_FALSE(rig.conn.isOpen());
}

TEST(Service, LeavingReadingGivesWifiBack) {
  Rig rig;
  rig.conn.reads = {httpOk(kMe)};
  rig.service.checkKey();
  rig.wifi.owned = true;
  // Reader, word select and the reader's menus keep it...
  rig.service.onActivityChanged("Reader", true);
  rig.service.onActivityChanged("DictionaryWordSelect", false);
  rig.service.onActivityChanged("EpubReaderMenu", false);
  EXPECT_EQ(rig.wifi.releases, 0);
  EXPECT_TRUE(rig.conn.isOpen());
  // ...anything else (KOSync here) gets the radio back before it starts.
  rig.service.onActivityChanged("KOReaderSync", false);
  EXPECT_EQ(rig.wifi.releases, 1);
  EXPECT_FALSE(rig.wifi.owned);
  EXPECT_FALSE(rig.conn.isOpen());
}

TEST(Service, AnalyzeSendsTheRequest) {
  Rig rig;
  rig.conn.reads = {httpOk(R"({"occurrences":[]})")};
  const auto r = rig.service.analyze(lexipoint::Language::Japanese, "東京");
  EXPECT_TRUE(r.ok());
  ASSERT_EQ(rig.conn.written.size(), 1u);
  EXPECT_NE(rig.conn.written[0].find(R"({"text":"東京","language":"ja"})"), std::string::npos);
}
