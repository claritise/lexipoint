#include <gtest/gtest.h>

#include <string>

#include "lexirise/net/JsonReader.h"
#include "lexirise/web/WebApi.h"

using lexipoint::Language;
using lexipoint::Reading;
using lexipoint::Settings;
using lexipoint::SettingsPatch;
using lexipoint::api::KeyState;
using lexipoint::api::KeyStatus;
using lexipoint::web::errorJson;
using lexipoint::web::parsePatch;
using lexipoint::web::stateJson;

namespace {
const std::string kKey = "lx_TESTKEYtestkey0123456789";

bool isValidJson(const std::string& text) {
  struct Ignore : lexipoint::json::Visitor {
    void onValue(const lexipoint::json::Path&, lexipoint::json::Type, std::string_view) override {}
  } v;
  return lexipoint::json::read(text, v) == lexipoint::json::Result::Ok;
}
}  // namespace

TEST(WebApiState, KeyIsOnlyEverMasked) {
  Settings s;
  s.apiKey = kKey;
  const std::string json = stateJson(s, KeyStatus(), {});
  ASSERT_TRUE(isValidJson(json));
  EXPECT_EQ(json.find(kKey), std::string::npos);
  EXPECT_EQ(json.find(kKey.substr(3, 10)), std::string::npos);  // no fragment of the secret part either
  EXPECT_NE(json.find(lexipoint::maskApiKey(kKey)), std::string::npos);
  EXPECT_NE(json.find(R"("hasKey":true)"), std::string::npos);
  Settings none;
  EXPECT_NE(stateJson(none, KeyStatus(), {}).find(R"("hasKey":false,"key":"")"), std::string::npos);
}

TEST(WebApiState, SaysWhichRowsShowByTheDeviceScreensRule) {
  // The page hides its data-when rows by these (test_lexirise_page.py pins the keys); the rule is
  // settings_screen::visibleRows (SettingsScreenTest).
  Settings s;
  EXPECT_NE(stateJson(s, KeyStatus(), {})
                .find(R"("shows":{"jaLookups":true,"jaReading":true,"zhLookups":true,"defaultLanguage":true,)"
                      R"("tags":true,"tagBook":true,"wifiIdle":true})"),
            std::string::npos);
  s.enabled = false;  // P13: the offline dictionaries answer; the default language picks for Han-only text
  EXPECT_NE(stateJson(s, KeyStatus(), {})
                .find(R"("shows":{"jaLookups":false,"jaReading":false,"zhLookups":false,"defaultLanguage":true,)"
                      R"("tags":false,"tagBook":false,"wifiIdle":false})"),
            std::string::npos);
  s.enabled = true;
  s.chinese.enabled = false;  // one language on: it is the answer
  EXPECT_NE(stateJson(s, KeyStatus(), {}).find(R"("jaReading":true,"zhLookups":true,"defaultLanguage":false,)"),
            std::string::npos);
}

TEST(WebApiState, CarriesSettingsChoicesAndStatus) {
  Settings s;
  s.japaneseReading = Reading::Romaji;
  s.chinese.enabled = false;
  s.tags = "a,\"b";
  s.tagBook = false;
  KeyStatus connected;
  connected.state = KeyState::Connected;
  connected.me.name = "Reader";
  connected.me.plan = "pro";
  const std::string json = stateJson(s, connected, {"jmdict", "cedict"});
  ASSERT_TRUE(isValidJson(json)) << json;
  EXPECT_NE(json.find(R"("reading":"romaji")"), std::string::npos);
  EXPECT_NE(json.find(R"("zh":{"enabled":false)"), std::string::npos);
  EXPECT_NE(json.find(R"("tags":"a,\"b","tagBook":false)"), std::string::npos);
  EXPECT_NE(json.find(R"("wifiIdleMin":[0,1,2,5,10])"), std::string::npos);
  EXPECT_NE(json.find(R"("dictionaries":["jmdict","cedict"])"), std::string::npos);
  EXPECT_NE(json.find(R"("status":{"state":"connected","name":"Reader","plan":"pro"})"), std::string::npos);

  KeyStatus offline;
  offline.state = KeyState::Offline;
  offline.error = lexipoint::api::ApiError::NoWifi;
  EXPECT_NE(stateJson(s, offline, {}).find(R"("status":{"state":"offline","error":"no-wifi"})"), std::string::npos);
  EXPECT_NE(json.find(R"("settingsReset":false)"), std::string::npos);
  EXPECT_NE(json.find(std::string(R"("defaultBaseUrl":")") + lexipoint::config::kDefaultBaseUrl + "\""),
            std::string::npos);
  EXPECT_NE(stateJson(s, offline, {}, true).find(R"("settingsReset":true)"), std::string::npos);
  EXPECT_NE(json.find(R"("checkTimeoutS":)" + std::to_string((lexipoint::config::kMaxCallMs + 999) / 1000)),
            std::string::npos);
}

TEST(WebApiPatch, ReadsEveryField) {
  SettingsPatch p;
  ASSERT_EQ(
      parsePatch(
          R"({"enabled":false,"key":"lx_x","clearKey":true,"ja":{"enabled":true,"reading":"romaji",)"
          R"("stardict":"jmdict"},"zh":{"enabled":false,"stardict":""},"defaultLanguage":"zh",)"
          R"("tags":"a,b","tagBook":false,"wifiIdleMin":10,"baseUrl":"https://x.example","unknown":{"deep":[1]}})",
          p),
      nullptr);
  EXPECT_EQ(p.enabled, false);
  EXPECT_EQ(p.apiKey, "lx_x");
  EXPECT_TRUE(p.clearApiKey);
  EXPECT_EQ(p.japaneseEnabled, true);
  EXPECT_EQ(p.japaneseReading, Reading::Romaji);
  EXPECT_EQ(p.japaneseStardict, "jmdict");
  EXPECT_EQ(p.chineseEnabled, false);
  EXPECT_EQ(p.chineseStardict, "");
  EXPECT_EQ(p.defaultLanguage, Language::Chinese);
  EXPECT_EQ(p.tags, "a,b");
  EXPECT_EQ(p.tagBook, false);
  EXPECT_EQ(p.wifiIdleMin, 10);
  EXPECT_EQ(p.baseUrl, "https://x.example");
}

TEST(WebApiPatch, AbsentFieldsStayUnset) {
  SettingsPatch p;
  ASSERT_EQ(parsePatch(R"({"ja":{"reading":"kana"}})", p), nullptr);
  EXPECT_FALSE(p.enabled);
  EXPECT_FALSE(p.apiKey);
  EXPECT_FALSE(p.japaneseEnabled);
  EXPECT_EQ(p.japaneseReading, Reading::Kana);
  EXPECT_FALSE(p.clearApiKey);
}

TEST(WebApiPatch, WrongTypesNameTheField) {
  const std::pair<const char*, const char*> cases[] = {
      {R"({"enabled":1})", "enabled"},
      {R"({"key":5})", "key"},
      {R"({"clearKey":"yes"})", "clearKey"},
      {R"({"ja":{"reading":"hiragana"}})", "ja.reading"},
      {R"({"ja":{"reading":{}}})", "ja.reading"},
      {R"({"ja":{"enabled":"true"}})", "ja.enabled"},
      {R"({"zh":{"stardict":null}})", "zh.stardict"},
      {R"({"ja":[1]})", "ja"},
      {R"({"zh":true})", "zh"},
      {R"({"defaultLanguage":"ko"})", "defaultLanguage"},
      {R"({"wifiIdleMin":5.5})", "wifiIdleMin"},
      {R"({"wifiIdleMin":-1})", "wifiIdleMin"},
      {R"({"wifiIdleMin":"5"})", "wifiIdleMin"},
      {R"({"tags":["a"]})", "tags"},
      {R"({"tagBook":1})", "tagBook"},
      {R"({"baseUrl":{"x":1}})", "baseUrl"},
  };
  for (const auto& [body, field] : cases) {
    SettingsPatch p;
    const char* got = parsePatch(body, p);
    ASSERT_NE(got, nullptr) << body;
    EXPECT_STREQ(got, field) << body;
  }
}

TEST(WebApiPatch, NotAnObjectIsInvalidJson) {
  for (const char* body : {"", "[]", "5", R"("x")", "{", R"({"a":1,})"}) {
    SettingsPatch p;
    EXPECT_STREQ(parsePatch(body, p), "json") << body;
  }
}

TEST(WebApiError, Shape) {
  EXPECT_EQ(errorJson("invalid", "baseUrl"), R"({"error":"invalid","field":"baseUrl"})");
  EXPECT_EQ(errorJson("cross-origin"), R"({"error":"cross-origin"})");
}
