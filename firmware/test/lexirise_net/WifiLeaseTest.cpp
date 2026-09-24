#include <gtest/gtest.h>

#include <climits>

#include "lexirise/net/WifiLease.h"

using lexipoint::config::kMsPerMinute;
using lexipoint::net::WifiLease;

TEST(WifiLease, NeverExpiresWhatItDoesNotOwn) {
  WifiLease lease;
  EXPECT_FALSE(lease.expired(10 * kMsPerMinute, 0));
  lease.acquired(0);
  lease.lost();  // the web server reconnected, say
  EXPECT_FALSE(lease.expired(10 * kMsPerMinute, 5));
}

TEST(WifiLease, ExpiresAfterIdleMinutesSinceLastUse) {
  WifiLease lease;
  lease.acquired(1000);
  EXPECT_FALSE(lease.expired(1000 + 5 * kMsPerMinute - 1, 5));
  lease.used(1000 + 4 * kMsPerMinute);
  EXPECT_FALSE(lease.expired(1000 + 8 * kMsPerMinute, 5));
  EXPECT_TRUE(lease.expired(1000 + 9 * kMsPerMinute, 5));
}

TEST(WifiLease, ZeroMinutesMeansRightAfterUse) {
  WifiLease lease;
  lease.acquired(500);
  EXPECT_TRUE(lease.expired(500, 0));
  EXPECT_TRUE(lease.expired(500, -3));  // a bad value never keeps WiFi on forever
}

TEST(WifiLease, SurvivesMillisWrap) {
  WifiLease lease;
  lease.acquired(ULONG_MAX - 1000);
  EXPECT_FALSE(lease.expired(500, 1));  // 1.5 s later, across the wrap
  EXPECT_TRUE(lease.expired(kMsPerMinute, 1));
}

TEST(WifiPolicy, OnlyBringsWifiUpFromRadioOff) {
  using lexipoint::net::decideEnsureUp;
  using lexipoint::net::WifiAction;
  EXPECT_EQ(decideEnsureUp(/*stationConnected=*/true, /*radioOff=*/false), WifiAction::UseExisting);
  EXPECT_EQ(decideEnsureUp(false, true), WifiAction::Join);
  // The web server's hotspot, or someone else's join in progress: never touched.
  EXPECT_EQ(decideEnsureUp(false, false), WifiAction::Busy);
}

TEST(WifiPolicy, ReadingActivitiesKeepLookupWifi) {
  using lexipoint::net::keepsLookupWifi;
  EXPECT_TRUE(keepsLookupWifi("AnythingReader", true));
  for (const char* name : lexipoint::config::kLookupActivityNames) EXPECT_TRUE(keepsLookupWifi(name, false)) << name;
  for (const char* name : {"KOReaderSync", "CrossPointWebServer", "Home", "Settings", "OtaUpdate", ""}) {
    EXPECT_FALSE(keepsLookupWifi(name, false)) << name;
  }
}
