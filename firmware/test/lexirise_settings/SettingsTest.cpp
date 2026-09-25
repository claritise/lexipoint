// Lexirise settings model: parsing, canonical serialisation, legacy migration, masking, validation.

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

#include "lexirise/settings/Settings.h"

using namespace lexipoint;

namespace {

const std::string kKey = "lx_AbCdEfGhIjKlMnOpQrStUvWxYz0123456789";  // fake, shaped like a real key

TEST(Settings, DefaultsFromEmptyFile) {
  const ParseResult r = parseSettings("");
  const Settings& s = r.settings;
  EXPECT_TRUE(s.enabled);
  EXPECT_FALSE(s.hasApiKey());
  EXPECT_TRUE(s.japanese.enabled);
  EXPECT_TRUE(s.chinese.enabled);
  EXPECT_EQ(s.japaneseReading, Reading::Kana);
  EXPECT_EQ(s.defaultLanguage, Language::Japanese);
  EXPECT_EQ(s.tags, config::kDefaultTags);
  EXPECT_EQ(s.wifiIdleMin, config::kWifiIdleDefaultMin);
  EXPECT_EQ(s.baseUrl, config::kDefaultBaseUrl);
  EXPECT_FALSE(r.migratedLegacyKeys);
  EXPECT_TRUE(r.warnings.empty());
}

TEST(Settings, ParsesSections) {
  const ParseResult r =
      parseSettings("[account]\nenabled=0\napi_key=" + kKey +
                    "\n[ja]\nenabled=1\nreading=romaji\nstardict=jmdict\n[zh]\nenabled=off\nstardict=cedict\n"
                    "[general]\ndefault_language=zh\ntags=xteink, book:test\nwifi_idle_min=10\n"
                    "[advanced]\nbase_url=https://example.test/\n");
  const Settings& s = r.settings;
  EXPECT_FALSE(s.enabled);
  EXPECT_EQ(s.apiKey, kKey);
  EXPECT_EQ(s.japaneseReading, Reading::Romaji);
  EXPECT_EQ(s.japanese.stardict, "jmdict");
  EXPECT_FALSE(s.chinese.enabled);
  EXPECT_EQ(s.chinese.stardict, "cedict");
  EXPECT_EQ(s.defaultLanguage, Language::Chinese);
  EXPECT_EQ(s.tags, "xteink,book:test");
  EXPECT_EQ(s.wifiIdleMin, 10);
  EXPECT_EQ(s.baseUrl, "https://example.test");  // trailing slash dropped
  EXPECT_TRUE(r.warnings.empty());
  EXPECT_EQ(s.enabledLanguageCount(), 1);
}

TEST(Settings, RoundTripIsStable) {
  Settings s;
  s.apiKey = kKey;
  s.japaneseReading = Reading::Romaji;
  s.chinese.enabled = false;
  s.tags = "a,b";
  const std::string text = serializeSettings(s);
  const ParseResult r = parseSettings(text);
  EXPECT_EQ(serializeSettings(r.settings), text);
  EXPECT_TRUE(r.warnings.empty());
}

TEST(Settings, HandEditingQuirks) {
  // BOM, CRLF, comments, whitespace, upper-case keys and sections.
  const std::string text = "\xEF\xBB\xBF# comment\r\n; another\r\n  [ Account ] \r\n  API_KEY =  " + kKey +
                           "  \r\n[JA]\r\nReading=Romaji\r\n";
  const ParseResult r = parseSettings(text);
  EXPECT_EQ(r.settings.apiKey, kKey);
  EXPECT_EQ(r.settings.japaneseReading, Reading::Romaji);
  EXPECT_TRUE(r.warnings.empty());
}

TEST(Settings, InvalidValuesFallBackWithWarningsThatNeverContainTheKey) {
  const ParseResult r =
      parseSettings("[account]\nenabled=maybe\napi_key=not-a-key-" + kKey +
                    "\n[ja]\nreading=hiragana\nstardict=../etc\n[general]\ndefault_language=ko\nwifi_idle_min=7\n"
                    "[advanced]\nbase_url=http://plain.test\n");
  const Settings& s = r.settings;
  EXPECT_TRUE(s.enabled);
  EXPECT_FALSE(s.hasApiKey());
  EXPECT_EQ(s.japaneseReading, Reading::Kana);
  EXPECT_TRUE(s.japanese.stardict.empty());
  EXPECT_EQ(s.defaultLanguage, Language::Japanese);
  EXPECT_EQ(s.wifiIdleMin, config::kWifiIdleDefaultMin);
  EXPECT_EQ(s.baseUrl, config::kDefaultBaseUrl);  // plain http is refused: the key would cross the LAN in clear
  EXPECT_EQ(r.warnings.size(), 7u);
  for (const auto& w : r.warnings) EXPECT_EQ(w.find("lx_"), std::string::npos) << w;
}

TEST(Settings, MalformedLinesWarn) {
  const ParseResult r = parseSettings("[account\njust text\n");
  EXPECT_EQ(r.warnings.size(), 2u);
}

TEST(Settings, LegacyFlatKeysMigrate) {
  const ParseResult r = parseSettings("api_key=" + kKey +
                                      "\nlanguage=zh\nlanguages=ja\nreading=romaji\nstardict_ja=jmdict\n"
                                      "stardict_zh=cedict\ntags=xteink\nwifi_idle_min=2\n");
  const Settings& s = r.settings;
  EXPECT_TRUE(r.migratedLegacyKeys);
  EXPECT_EQ(s.apiKey, kKey);
  EXPECT_EQ(s.defaultLanguage, Language::Chinese);
  EXPECT_TRUE(s.japanese.enabled);
  EXPECT_FALSE(s.chinese.enabled);
  EXPECT_EQ(s.japaneseReading, Reading::Romaji);
  EXPECT_EQ(s.japanese.stardict, "jmdict");
  EXPECT_EQ(s.chinese.stardict, "cedict");
  EXPECT_EQ(s.wifiIdleMin, 2);
  const std::string out = serializeSettings(s);
  EXPECT_NE(out.find("[ja]\nenabled=1\nreading=romaji\nstardict=jmdict\n"), std::string::npos);
  EXPECT_EQ(out.find("stardict_ja"), std::string::npos);  // migrated, not duplicated
}

TEST(Settings, UnknownKeysAndSectionsSurvive) {
  const ParseResult r = parseSettings("mystery=1\n[account]\nfuture_flag=on\n[ko]\nenabled=1\n[general]\nx=y\n");
  const std::string out = serializeSettings(r.settings);
  EXPECT_NE(out.find("mystery=1\n"), std::string::npos);
  EXPECT_NE(out.find("[account]\nenabled=1\napi_key=\nfuture_flag=on\n"), std::string::npos);
  EXPECT_NE(out.find("[ko]\nenabled=1\n"), std::string::npos);
  EXPECT_NE(out.find("wifi_idle_min=5\nx=y\n"), std::string::npos);
  EXPECT_EQ(serializeSettings(parseSettings(out).settings), out);  // stable
}

TEST(Settings, MaskShowsOnlyTheTail) {
  EXPECT_EQ(maskApiKey(""), "");
  const std::string m = maskApiKey(kKey);
  EXPECT_EQ(m.rfind("lx_", 0), 0u);
  EXPECT_EQ(m.substr(m.size() - config::kMaskedKeyTail), "789");
  EXPECT_EQ(m.find(kKey.substr(3, 10)), std::string::npos);
  EXPECT_EQ(maskApiKey("lx_ab").find("ab"), std::string::npos);  // too short: no tail leaks
}

TEST(Settings, KeyShape) {
  EXPECT_TRUE(isPlausibleApiKey(kKey));
  EXPECT_FALSE(isPlausibleApiKey("lx_short"));
  EXPECT_FALSE(isPlausibleApiKey("xx_" + kKey.substr(3)));
  EXPECT_FALSE(isPlausibleApiKey(kKey + " "));
  EXPECT_FALSE(isPlausibleApiKey(kKey + "\""));
  EXPECT_FALSE(isPlausibleApiKey(std::string(200, 'a')));
}

TEST(Settings, TagNormalisation) {
  EXPECT_EQ(normaliseTags(" a , b,,a, c "), "a,b,c");
  EXPECT_EQ(normaliseTags("x\"y,z=w"), "xy,zw");
  EXPECT_EQ(normaliseTags(""), "");
  std::string many;
  for (int i = 0; i < 20; i++) many += "t" + std::to_string(i) + ",";
  const std::string capped = normaliseTags(many);
  EXPECT_EQ(std::count(capped.begin(), capped.end(), ',') + 1, static_cast<long>(config::kMaxTags));
  EXPECT_EQ(normaliseTags(std::string(config::kMaxTagLength + 1, 'x')), "");
}

TEST(Settings, TagListSplitsTheNormalisedForm) {
  EXPECT_EQ(tagList("xteink,book:x"), (std::vector<std::string>{"xteink", "book:x"}));
  EXPECT_EQ(tagList(normaliseTags(" a , b,,a ")), (std::vector<std::string>{"a", "b"}));
  EXPECT_TRUE(tagList("").empty());
}

}  // namespace

TEST(Settings, FallbackLanguageIsTheOnlyOneOnElseTheChosenOne) {
  lexipoint::Settings s;
  s.defaultLanguage = lexipoint::Language::Chinese;
  EXPECT_EQ(s.fallbackLanguage(), lexipoint::Language::Chinese);  // both on: the choice
  s.chinese.enabled = false;
  EXPECT_EQ(s.fallbackLanguage(), lexipoint::Language::Japanese);  // one on: that one
  s.japanese.enabled = false;
  EXPECT_EQ(s.fallbackLanguage(), lexipoint::Language::Chinese);  // none on: the choice
  // P13: Lexirise off: only the offline dictionaries answer, the per-language switches don't apply, the
  // choice (its row shows then) does.
  s.japanese.enabled = true;
  s.enabled = false;
  EXPECT_EQ(s.fallbackLanguage(), lexipoint::Language::Chinese);
}

TEST(Settings, TheDefaultLanguageAppliesUnlessOneLanguageIsTheAnswer) {
  // Its row shows exactly then (settings_screen::visibleRows).
  lexipoint::Settings s;
  EXPECT_TRUE(s.defaultLanguageApplies());  // both on
  s.chinese.enabled = false;
  EXPECT_FALSE(s.defaultLanguageApplies());  // one on
  s.japanese.enabled = false;
  EXPECT_TRUE(s.defaultLanguageApplies());  // none on: StarDict answers Han-only text by the choice
  s.japanese.enabled = true;
  s.enabled = false;
  EXPECT_TRUE(s.defaultLanguageApplies());  // Lexirise off
}

TEST(Settings, EveryLanguageIsListedOnceWithItsOwnGroup) {
  std::set<std::string> codes;
  for (const lexipoint::Language l : lexipoint::kLanguages) codes.insert(lexipoint::languageCode(l));
  EXPECT_EQ(codes, (std::set<std::string>{"ja", "zh"}));
  lexipoint::Settings s;
  EXPECT_EQ(&s.language(lexipoint::Language::Japanese), &s.japanese);
  EXPECT_EQ(&s.language(lexipoint::Language::Chinese), &s.chinese);
  EXPECT_EQ(s.enabledLanguageCount(), 2);
}
