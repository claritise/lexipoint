// An SD card's settings.json written before the v0.2 slimming (V8) still loads: every setting V8 kept keeps its
// value, the keys of the settings it removed are ignored, and the next save drops only those keys
// (docs/v0.2/slimming.md §2, "Settings files on existing SD cards"). The fixture holds a value for every key the
// settings file carried before V8, none of them the default, so a setting that fell back to its default shows.
#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "CrossPointSettings.h"
#include "LastWrite.h"
#include "SettingsList.h"

namespace {

// The keys of the settings V8 removed (slimming.md §8 "As built (V8)"): a file from before V8 may hold them.
const std::set<std::string> kRemovedKeys = {
    // Step 2, code for devices without touch.
    "fadingFix",
    "frontButtonFollowOrientation",
    "backShortToFileBrowser",
    "tiltPageTurn",
    // Step 3, KOReader sync, OPDS, Calibre and WebDAV (KOReader sync's settings were in their own file).
    "opdsDownloadFolder",
    "opdsFilenameFormat",
    // Step 4, the UI in English only (with it, the keyboard layouts).
    "language",
    "keyboardLayouts",
    // Step 5, the settings screens (the front button remap).
    "frontButtonBack",
    "frontButtonConfirm",
    "frontButtonLeft",
    "frontButtonRight",
    // Step 5c, the themes (claritise 2026-09-29, "Keep Lyra"): Lyra is the only one, whatever the file said.
    "uiTheme",
};

// Keys CrossPointSettings saves and loads by hand, outside the generic loop over getSettingsList().
const std::set<std::string> kManualKeys = {
    "fontFamily", "fontSize", "longPressMenuFunction", "sdFontFamilyName", "dictionaryName",
};

JsonDocument readFixture() {
  std::ifstream in(SETTINGS_FIXTURE);
  std::stringstream text;
  text << in.rdbuf();
  JsonDocument doc;
  EXPECT_EQ(deserializeJson(doc, text.str()), DeserializationError::Ok) << SETTINGS_FIXTURE;
  return doc;
}

// The settings as a fresh boot has them, taken before any test changes the singleton (its constructor is private).
const JsonDocument kDefaults = [] {
  JsonDocument doc;
  SETTINGS.toJson(doc);
  return doc;
}();

// Every test starts from the defaults: fromJson falls back to the value in memory, so one test's load mustn't leak
// into the next.
void resetSettings() { ASSERT_TRUE(SETTINGS.fromJson(kDefaults.as<JsonVariantConst>())); }

class SettingsUpgradeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    resetSettings();
    old = readFixture();
    ASSERT_TRUE(SETTINGS.fromJson(old.as<JsonVariantConst>()));
  }
  JsonDocument old;
};

TEST_F(SettingsUpgradeTest, EveryKeyInTheOldFileIsReadOrKnownRemoved) {
  std::set<std::string> read = kManualKeys;
  for (const auto& info : getSettingsList()) {
    if (info.key && (info.valuePtr || info.stringOffset)) read.insert(info.key);
  }
  for (const JsonPairConst pair : old.as<JsonObjectConst>()) {
    const std::string key = pair.key().c_str();
    EXPECT_TRUE(read.count(key) != 0 || kRemovedKeys.count(key) != 0) << key << ": neither read nor listed as removed";
    EXPECT_FALSE(read.count(key) != 0 && kRemovedKeys.count(key) != 0) << key << ": listed as removed but still read";
  }
}

TEST_F(SettingsUpgradeTest, EveryKeptSettingKeepsItsValue) {
  std::set<std::string> checked;
  for (const auto& info : getSettingsList()) {
    if (!info.key || old[info.key].isNull()) continue;
    if (info.valuePtr) {
      EXPECT_EQ(SETTINGS.*(info.valuePtr), old[info.key].as<uint8_t>()) << info.key;
      checked.insert(info.key);
    } else if (info.stringOffset) {
      EXPECT_STREQ(reinterpret_cast<const char*>(&SETTINGS) + info.stringOffset, old[info.key].as<const char*>())
          << info.key;
      checked.insert(info.key);
    }
  }
  // Every kept key of the old file is checked: here, or by hand below (kManualKeys; fontFamily is both).
  for (const JsonPairConst pair : old.as<JsonObjectConst>()) {
    const std::string key = pair.key().c_str();
    if (kRemovedKeys.count(key) != 0) continue;
    EXPECT_TRUE(checked.count(key) != 0 || kManualKeys.count(key) != 0) << key << " isn't checked";
  }

  EXPECT_EQ(SETTINGS.fontFamily, CrossPointSettings::NOTOSANS);
  EXPECT_EQ(SETTINGS.fontPointSize, 16);
  EXPECT_EQ(SETTINGS.longPressMenuFunction, CrossPointSettings::LP_MENU_BOOKMARK);
  EXPECT_STREQ(SETTINGS.sdFontFamilyName, "NotoSerifCJK");
  EXPECT_STREQ(SETTINGS.dictionaryName, "jmdict");
}

// V8 R5: through the real fromJson, an enum value past its choices loads as the default, and a stored Long-press Menu
// of KOReader Sync (0) as Disabled.
TEST(SettingsUpgradeValues, AnOutOfRangeChoiceAndAStoredKoSyncLoadAsDefaults) {
  resetSettings();
  const uint8_t sleepScreenDefault = SETTINGS.sleepScreen;
  JsonDocument old = readFixture();
  old["sleepScreen"] = 200;
  old["longPressMenuFunction"] = CrossPointSettings::LP_MENU_KOSYNC;
  ASSERT_TRUE(SETTINGS.fromJson(old.as<JsonVariantConst>()));
  EXPECT_EQ(SETTINGS.sleepScreen, sleepScreenDefault);
  EXPECT_EQ(SETTINGS.longPressMenuFunction, CrossPointSettings::LP_MENU_DISABLED);
  EXPECT_EQ(SETTINGS.screenInverted, old["screenInverted"].as<uint8_t>());  // the rest kept
}

TEST_F(SettingsUpgradeTest, TheNextSaveDropsOnlyTheRemovedKeys) {
  ASSERT_TRUE(SETTINGS.saveToFile());
  JsonDocument saved;
  ASSERT_EQ(deserializeJson(saved, settings_upgrade::lastWrittenJson), DeserializationError::Ok);
  for (const JsonPairConst pair : old.as<JsonObjectConst>()) {
    const std::string key = pair.key().c_str();
    if (kRemovedKeys.count(key) != 0) {
      EXPECT_TRUE(saved[key].isNull()) << key << " is still saved";
    } else {
      EXPECT_EQ(saved[key], pair.value()) << key;
    }
  }
}

}  // namespace

// Step 5c: a file that chose another theme (Classic 0, Lyra 3 Covers 2, RoundedRaff 3) loads like any other: the key
// is ignored (Lyra is the only theme, UITheme's own), every other setting is kept, and the next save drops it.
TEST(SettingsUpgradeThemes, AnyStoredThemeIsIgnoredAndDropped) {
  for (const uint8_t stored : {0, 1, 2, 3}) {
    SCOPED_TRACE(stored);
    resetSettings();
    JsonDocument old = readFixture();
    old["uiTheme"] = stored;
    ASSERT_TRUE(SETTINGS.fromJson(old.as<JsonVariantConst>()));
    EXPECT_EQ(SETTINGS.sleepScreen, old["sleepScreen"].as<uint8_t>());
    EXPECT_EQ(SETTINGS.screenInverted, old["screenInverted"].as<uint8_t>());
    ASSERT_TRUE(SETTINGS.saveToFile());
    JsonDocument saved;
    ASSERT_EQ(deserializeJson(saved, settings_upgrade::lastWrittenJson), DeserializationError::Ok);
    EXPECT_TRUE(saved["uiTheme"].isNull());
  }
}

// V8 R2: Long-press Menu's choices on the X4 Pro (a Home key): Disabled, Bookmark, Reader Menu, labelled by value.
TEST(SettingsLongPressMenu, TheChoicesAndTheirLabels) {
  EXPECT_EQ(lexiriseLongPressMenuLabels(),
            (std::vector<StrId>{StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION, StrId::STR_READER_MENU}));
  EXPECT_EQ(longPressMenuLabel(CrossPointSettings::LP_MENU_KOSYNC), StrId::STR_DISABLED);  // loads as Disabled
}
