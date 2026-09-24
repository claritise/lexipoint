#include <gtest/gtest.h>

#include "lexirise/settings/SettingsPatch.h"

using lexipoint::applyPatch;
using lexipoint::Language;
using lexipoint::Reading;
using lexipoint::Settings;
using lexipoint::SettingsPatch;

namespace {
const std::string kKey = "lx_TESTKEYtestkey0123456789";
const std::string kOther = "lx_OTHERkeyOTHERkey000000";

Settings withKey() {
  Settings s;
  s.apiKey = kKey;
  return s;
}
}  // namespace

TEST(SettingsPatch, EmptyOrMaskedKeyLeavesKeyAlone) {
  for (const std::string& box : {std::string(), std::string("   "), lexipoint::maskApiKey(kKey)}) {
    Settings s = withKey();
    SettingsPatch p;
    p.apiKey = box;
    const auto r = applyPatch(s, p);
    EXPECT_TRUE(r.ok);
    EXPECT_FALSE(r.keyChanged);
    EXPECT_EQ(s.apiKey, kKey);
  }
}

TEST(SettingsPatch, PastedKeyIsTrimmedAndFlagsAChange) {
  Settings s = withKey();
  SettingsPatch p;
  p.apiKey = "  " + kOther + "\n";
  const auto r = applyPatch(s, p);
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.keyChanged);
  EXPECT_EQ(s.apiKey, kOther);
  // Pasting the same key again is not a change.
  p.apiKey = kOther;
  EXPECT_FALSE(applyPatch(s, p).keyChanged);
}

TEST(SettingsPatch, ClearRemovesTheKey) {
  Settings s = withKey();
  SettingsPatch p;
  p.clearApiKey = true;
  const auto r = applyPatch(s, p);
  EXPECT_TRUE(r.keyChanged);
  EXPECT_FALSE(s.hasApiKey());
}

TEST(SettingsPatch, InvalidFieldRejectsTheWholePatch) {
  Settings s = withKey();
  SettingsPatch p;
  p.enabled = false;
  p.tags = "new";
  p.baseUrl = "http://insecure.example";
  const auto r = applyPatch(s, p);
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ(r.field, "baseUrl");
  EXPECT_TRUE(s.enabled);  // nothing applied
  EXPECT_EQ(s.tags, lexipoint::config::kDefaultTags);

  const std::pair<SettingsPatch, const char*> bad[] = {
      {[] {
         SettingsPatch q;
         q.apiKey = "not a key";
         return q;
       }(),
       "apiKey"},
      {[] {
         SettingsPatch q;
         q.japaneseStardict = "../etc";
         return q;
       }(),
       "ja.stardict"},
      {[] {
         SettingsPatch q;
         q.chineseStardict = "a/b";
         return q;
       }(),
       "zh.stardict"},
      {[] {
         SettingsPatch q;
         q.wifiIdleMin = 3;
         return q;
       }(),
       "wifiIdleMin"},
  };
  for (const auto& [patch, field] : bad) {
    Settings t = withKey();
    const auto rr = applyPatch(t, patch);
    EXPECT_FALSE(rr.ok);
    EXPECT_STREQ(rr.field, field);
  }
}

TEST(SettingsPatch, AppliesEveryField) {
  Settings s;
  SettingsPatch p;
  p.enabled = false;
  p.japaneseEnabled = false;
  p.japaneseReading = Reading::Romaji;
  p.japaneseStardict = "jmdict";
  p.chineseEnabled = true;
  p.chineseStardict = "";
  p.defaultLanguage = Language::Chinese;
  p.tags = " a , b ,a ";
  p.wifiIdleMin = 0;
  p.baseUrl = " https://staging.example.com/ ";
  ASSERT_TRUE(applyPatch(s, p).ok);
  EXPECT_FALSE(s.enabled);
  EXPECT_FALSE(s.japanese.enabled);
  EXPECT_EQ(s.japaneseReading, Reading::Romaji);
  EXPECT_EQ(s.japanese.stardict, "jmdict");
  EXPECT_EQ(s.chinese.stardict, "");
  EXPECT_EQ(s.defaultLanguage, Language::Chinese);
  EXPECT_EQ(s.tags, "a,b");
  EXPECT_EQ(s.wifiIdleMin, 0);
  EXPECT_EQ(s.baseUrl, "https://staging.example.com");
}

TEST(SettingsPatch, NewServerNeedsTheKeyPastedAgain) {
  Settings s = withKey();
  SettingsPatch p;
  p.baseUrl = "https://elsewhere.example";
  auto r = applyPatch(s, p);
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ(r.field, "apiKeyForServer");
  EXPECT_EQ(s.baseUrl, lexipoint::config::kDefaultBaseUrl);
  // The same server (even spelled with a slash) needs nothing.
  p.baseUrl = std::string(lexipoint::config::kDefaultBaseUrl) + "/";
  EXPECT_TRUE(applyPatch(s, p).ok);
  // With a key in the same save, or with the key cleared, it goes through.
  p.baseUrl = "https://elsewhere.example";
  p.apiKey = kOther;
  EXPECT_TRUE(applyPatch(s, p).ok);
  Settings t = withKey();
  SettingsPatch q;
  q.baseUrl = "https://elsewhere.example";
  q.clearApiKey = true;
  EXPECT_TRUE(applyPatch(t, q).ok);
  // Without a stored key there is nothing to leak.
  Settings u;
  SettingsPatch v;
  v.baseUrl = "https://elsewhere.example";
  EXPECT_TRUE(applyPatch(u, v).ok);
}
