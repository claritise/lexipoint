// firmware-base.md §6: the fork's `<upstream>-lexi.<n>` versions, and which release the OTA updater offers.

#include <gtest/gtest.h>

#include "lexirise/ota/ReleaseVersion.h"

using lexipoint::ota::isNewerRelease;
using lexipoint::ota::parseReleaseVersion;

TEST(ReleaseVersion, Parses) {
  const auto v = parseReleaseVersion("1.6.5-lexi.2");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->major, 1);
  EXPECT_EQ(v->minor, 6);
  EXPECT_EQ(v->patch, 5);
  EXPECT_EQ(v->lexi, 2);
  EXPECT_FALSE(v->rc);
  EXPECT_EQ(parseReleaseVersion("v1.6.5-lexi.12")->lexi, 12);
  EXPECT_EQ(parseReleaseVersion("1.6.5-lexi.1-x4pro")->lexi, 1);  // a dev build
  EXPECT_TRUE(parseReleaseVersion("1.6.5-lexi.1-rc+abc1234")->rc);
  EXPECT_EQ(parseReleaseVersion("1.6.6")->lexi, 0);  // upstream's
  EXPECT_TRUE(parseReleaseVersion("1.6.5rc")->rc);
  for (const char* bad : {"", "v", "1.6", "1..5", "x.y.z", "lexi.1", "99999999.1.1"}) {
    EXPECT_FALSE(parseReleaseVersion(bad).has_value()) << bad;
  }
}

TEST(ReleaseVersion, TheForksNextReleaseIsOffered) {
  EXPECT_TRUE(isNewerRelease("1.6.5-lexi.2", "1.6.5-lexi.1"));
  EXPECT_TRUE(isNewerRelease("1.6.5-lexi.10", "1.6.5-lexi.9"));  // numbers, not text
  EXPECT_TRUE(isNewerRelease("1.7.0-lexi.1", "1.6.5-lexi.4"));   // rebased onto a newer upstream
  EXPECT_TRUE(isNewerRelease("v1.6.5-lexi.2", "1.6.5-lexi.1-x4pro"));
  EXPECT_FALSE(isNewerRelease("1.6.5-lexi.1", "1.6.5-lexi.1"));
  EXPECT_FALSE(isNewerRelease("1.6.5-lexi.1", "1.6.5-lexi.2"));
  EXPECT_FALSE(isNewerRelease("1.6.5-lexi.9", "1.7.0-lexi.1"));
}

TEST(ReleaseVersion, UpstreamsReleasesAreNeverOffered) {
  EXPECT_FALSE(isNewerRelease("1.6.6", "1.6.5-lexi.1"));  // would uninstall Lexipoint
  EXPECT_FALSE(isNewerRelease("2.0.0", "1.6.5-lexi.1"));
  EXPECT_FALSE(isNewerRelease("1.6.5-lexi.2-rc", "1.6.5-lexi.1"));  // candidates aren't offered either
  EXPECT_FALSE(isNewerRelease("garbage", "1.6.5-lexi.1"));
}

TEST(ReleaseVersion, ACandidateUpdatesToItsRelease) {
  EXPECT_TRUE(isNewerRelease("1.6.5-lexi.2", "1.6.5-lexi.2-rc+abc1234"));
  EXPECT_FALSE(isNewerRelease("1.6.5-lexi.1", "1.6.5-lexi.2-rc+abc1234"));
  EXPECT_TRUE(isNewerRelease("1.6.5-lexi.1", "dev"));  // an unversioned build
}

TEST(ReleaseVersion, EdgeCases) {
  EXPECT_FALSE(isNewerRelease("1.6.5-lexi.1", "1.6.5-lexi.1-x4pro"));  // a dev build of that very release
  EXPECT_TRUE(isNewerRelease("1.6.5-lexi.1", "1.6.5-x4pro"));          // a build from before Lexipoint's versions
  EXPECT_FALSE(isNewerRelease("1.6.5-lexi.0", "1.6.5-x4pro"));         // lexi.0 is never offered
  EXPECT_FALSE(isNewerRelease("1.6.5-lexi.", "1.6.5-lexi.1"));         // no number
  EXPECT_EQ(parseReleaseVersion("1.6.5-lexi.")->lexi, 0);
  // Past kMaxVersionComponent it isn't a version (release_tag.py never tags one).
  EXPECT_FALSE(parseReleaseVersion("1.6.5-lexi.99999999").has_value());
  EXPECT_FALSE(isNewerRelease("1.6.5-lexi.99999999", "1.6.5-lexi.1"));
  EXPECT_FALSE(parseReleaseVersion("1.6.5-lexi.1-src")->rc);  // "rc" inside another word isn't a candidate
  EXPECT_FALSE(parseReleaseVersion("1.6.5-lexi.1-x4pro")->rc);
}

TEST(ReleaseVersion, TheBuildsVersionStringsParse) {
  // platformio.ini's three X4 Pro envs (test_release_tag.py pins the ini to these shapes).
  const auto release = parseReleaseVersion("1.6.5-lexi.1");
  const auto candidate = parseReleaseVersion("1.6.5-lexi.1-rc+abc1234");
  const auto dev = parseReleaseVersion("1.6.5-lexi.1-x4pro");
  ASSERT_TRUE(release && candidate && dev);
  EXPECT_EQ(release->lexi, 1);
  EXPECT_FALSE(release->rc);
  EXPECT_EQ(candidate->lexi, 1);
  EXPECT_TRUE(candidate->rc);
  EXPECT_EQ(dev->lexi, 1);
  EXPECT_FALSE(dev->rc);
}
