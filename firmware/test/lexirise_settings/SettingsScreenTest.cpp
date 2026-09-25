#include <gtest/gtest.h>

#include <vector>

#include "lexirise/settings/SettingsScreen.h"

using lexipoint::applyPatch;
using lexipoint::Language;
using lexipoint::Reading;
using lexipoint::Settings;
namespace screen = lexipoint::settings_screen;
using screen::Group;
using screen::Row;

namespace {
std::vector<Row> rowsOf(const Settings& s) {
  const auto rows = screen::visibleRows(s);
  return {rows.rows.begin(), rows.rows.begin() + static_cast<std::ptrdiff_t>(rows.count)};
}

// Taps a Patch row the way the screen does: through applyPatch.
void tap(Settings& s, const Row row, const std::vector<std::string>& dictionaries = {}) {
  const auto patch = screen::tapPatch(row, s, dictionaries);
  ASSERT_TRUE(patch.has_value());
  ASSERT_TRUE(applyPatch(s, *patch).ok);
}
}  // namespace

TEST(SettingsScreen, AllRowsShowWithDefaults) {
  EXPECT_EQ(rowsOf(Settings()), (std::vector<Row>{Row::Lookups, Row::ApiKey, Row::Account, Row::TestConnection,
                                                  Row::JaLookups, Row::JaReading, Row::JaDictionary, Row::ZhLookups,
                                                  Row::ZhDictionary, Row::DefaultLanguage, Row::Tags, Row::WifiIdle}));
}

TEST(SettingsScreen, LexiriseOffLeavesOnlyTheAccountGroup) {
  Settings s;
  s.enabled = false;
  EXPECT_EQ(rowsOf(s), (std::vector<Row>{Row::Lookups, Row::ApiKey, Row::Account, Row::TestConnection}));
}

TEST(SettingsScreen, ALanguageOffCollapsesToItsToggleAndHidesTheDefaultLanguage) {
  Settings s;
  s.chinese.enabled = false;
  EXPECT_EQ(rowsOf(s), (std::vector<Row>{Row::Lookups, Row::ApiKey, Row::Account, Row::TestConnection, Row::JaLookups,
                                         Row::JaReading, Row::JaDictionary, Row::ZhLookups, Row::Tags, Row::WifiIdle}));
  s.chinese.enabled = true;
  s.japanese.enabled = false;
  EXPECT_EQ(rowsOf(s), (std::vector<Row>{Row::Lookups, Row::ApiKey, Row::Account, Row::TestConnection, Row::JaLookups,
                                         Row::ZhLookups, Row::ZhDictionary, Row::Tags, Row::WifiIdle}));
}

TEST(SettingsScreen, TurningALanguageOffAndOnKeepsItsValues) {
  Settings s;
  s.chinese.stardict = "cedict";
  tap(s, Row::ZhLookups);
  EXPECT_FALSE(s.chinese.enabled);
  tap(s, Row::ZhLookups);
  EXPECT_TRUE(s.chinese.enabled);
  EXPECT_EQ(s.chinese.stardict, "cedict");
  EXPECT_EQ(rowsOf(s).size(), screen::kRowCount);
}

TEST(SettingsScreen, GroupsStartWhereTheGroupChanges) {
  Settings s;
  s.japanese.enabled = false;
  const auto rows = screen::visibleRows(s);
  std::vector<Group> starts;
  for (size_t i = 0; i < rows.count; i++) {
    if (screen::startsGroup(rows, i)) starts.push_back(screen::groupOf(rows[i]));
  }
  EXPECT_EQ(starts, (std::vector<Group>{Group::Account, Group::Japanese, Group::Chinese, Group::General}));
  EXPECT_FALSE(screen::startsGroup(rows, rows.count));
}

TEST(SettingsScreen, EditsByRow) {
  EXPECT_EQ(screen::editFor(Row::ApiKey), screen::Edit::Keyboard);
  EXPECT_EQ(screen::editFor(Row::Tags), screen::Edit::Keyboard);
  EXPECT_EQ(screen::editFor(Row::TestConnection), screen::Edit::Test);
  EXPECT_EQ(screen::editFor(Row::Account), screen::Edit::None);
  for (const Row row : {Row::Lookups, Row::JaLookups, Row::JaReading, Row::JaDictionary, Row::ZhLookups,
                        Row::ZhDictionary, Row::DefaultLanguage, Row::WifiIdle}) {
    EXPECT_EQ(screen::editFor(row), screen::Edit::Patch);
    EXPECT_TRUE(screen::tapPatch(row, Settings(), {}).has_value());
  }
  for (const Row row : {Row::ApiKey, Row::Account, Row::TestConnection, Row::Tags}) {
    EXPECT_FALSE(screen::tapPatch(row, Settings(), {}).has_value());
  }
}

TEST(SettingsScreen, TogglesAndChoicesFlip) {
  Settings s;
  tap(s, Row::Lookups);
  EXPECT_FALSE(s.enabled);
  tap(s, Row::JaLookups);
  EXPECT_FALSE(s.japanese.enabled);
  tap(s, Row::JaReading);
  EXPECT_EQ(s.japaneseReading, Reading::Romaji);
  tap(s, Row::JaReading);
  EXPECT_EQ(s.japaneseReading, Reading::Kana);
  tap(s, Row::DefaultLanguage);
  EXPECT_EQ(s.defaultLanguage, Language::Chinese);
  tap(s, Row::DefaultLanguage);
  EXPECT_EQ(s.defaultLanguage, Language::Japanese);
}

TEST(SettingsScreen, DictionaryCyclesThroughTheGlobalOneAndEachFolder) {
  const std::vector<std::string> found = {"cedict", "jmdict"};
  Settings s;
  tap(s, Row::JaDictionary, found);
  EXPECT_EQ(s.japanese.stardict, "cedict");
  tap(s, Row::JaDictionary, found);
  EXPECT_EQ(s.japanese.stardict, "jmdict");
  tap(s, Row::JaDictionary, found);
  EXPECT_EQ(s.japanese.stardict, "");
  EXPECT_EQ(s.chinese.stardict, "");  // the other language's choice is its own
  tap(s, Row::ZhDictionary, found);
  EXPECT_EQ(s.chinese.stardict, "cedict");
}

TEST(SettingsScreen, NextDictionaryEdges) {
  EXPECT_EQ(screen::nextDictionary("", {}), "");
  EXPECT_EQ(screen::nextDictionary("gone", {}), "");
  EXPECT_EQ(screen::nextDictionary("gone", {"a", "b"}), "a");
  EXPECT_EQ(screen::nextDictionary("b", {"a", "b"}), "");
}

TEST(SettingsScreen, WifiIdleCyclesThroughTheChoices) {
  Settings s;
  ASSERT_EQ(s.wifiIdleMin, 5);
  std::vector<int> seen;
  for (int i = 0; i < 5; i++) {
    tap(s, Row::WifiIdle);
    seen.push_back(s.wifiIdleMin);
  }
  EXPECT_EQ(seen, (std::vector<int>{10, 0, 1, 2, 5}));
  EXPECT_EQ(screen::nextWifiIdle(7), 0);  // not a choice (a hand-edited file): back to the first
}

TEST(SettingsScreen, AccountLineFollowsTheKeyCheck) {
  using lexipoint::api::KeyState;
  using screen::AccountLine;
  EXPECT_EQ(screen::accountLine(false, KeyState::Connected), AccountLine::NotSet);  // the key was removed since
  EXPECT_EQ(screen::accountLine(true, KeyState::Unchecked), AccountLine::NotChecked);
  EXPECT_EQ(screen::accountLine(true, KeyState::Checking), AccountLine::Checking);
  EXPECT_EQ(screen::accountLine(true, KeyState::NoKey), AccountLine::NotSet);
  EXPECT_EQ(screen::accountLine(true, KeyState::Connected), AccountLine::Connected);
  EXPECT_EQ(screen::accountLine(true, KeyState::Rejected), AccountLine::KeyRejected);
  EXPECT_EQ(screen::accountLine(true, KeyState::Offline), AccountLine::NoNetwork);
  EXPECT_EQ(screen::accountLine(true, KeyState::Error), AccountLine::CouldNotConnect);
}

TEST(SettingsScreen, ATapMapsThroughTheRowsThatWereDrawn) {
  // A tap during the refresh after Japanese was switched off lands on the rows that were built with it:
  // index 5 is Readings there, and never the Chinese toggle that index 5 is now.
  Settings before;
  const auto drawn = screen::visibleRows(before);
  Settings after = before;
  after.japanese.enabled = false;
  EXPECT_EQ(screen::rowAt(drawn, 5), Row::JaReading);
  EXPECT_EQ(screen::rowAt(screen::visibleRows(after), 5), Row::ZhLookups);
  EXPECT_FALSE(screen::rowAt(drawn, -1).has_value());
  EXPECT_FALSE(screen::rowAt(drawn, static_cast<int>(drawn.count)).has_value());
}

TEST(SettingsScreen, EditResultsByRow) {
  using lexipoint::SettingsStore;
  using screen::EditResult;
  EXPECT_EQ(screen::editResult(Row::ApiKey, SettingsStore::UpdateResult::Saved), EditResult::Saved);
  EXPECT_EQ(screen::editResult(Row::ApiKey, SettingsStore::UpdateResult::Declined), EditResult::NotAKey);
  EXPECT_EQ(screen::editResult(Row::ApiKey, SettingsStore::UpdateResult::SaveFailed), EditResult::NotSaved);
  EXPECT_EQ(screen::editResult(Row::Tags, SettingsStore::UpdateResult::Declined), EditResult::NotSaved);
  EXPECT_EQ(screen::editResult(Row::JaDictionary, SettingsStore::UpdateResult::SaveFailed), EditResult::NotSaved);
}

TEST(SettingsScreen, OnlyDictionariesASettingCanHoldAreOffered) {
  const std::string tooLong(lexipoint::config::kMaxDictionaryNameLength + 1, 'd');
  EXPECT_EQ(screen::offeredDictionaries({"cedict", tooLong, "jmdict"}), (std::vector<std::string>{"cedict", "jmdict"}));
  // So the cycle never sticks on one applyPatch refuses.
  Settings s;
  const auto offered = screen::offeredDictionaries({"cedict", tooLong, "jmdict"});
  tap(s, Row::JaDictionary, offered);
  tap(s, Row::JaDictionary, offered);
  EXPECT_EQ(s.japanese.stardict, "jmdict");
}

TEST(SettingsScreen, TapsWaitForTheFrameAnEditAskedFor) {
  screen::TapGate gate;
  EXPECT_TRUE(gate.accepts());
  gate.redrawAsked();  // a toggle: its rows are laid out, the panel still shows the old ones
  EXPECT_FALSE(gate.accepts());
  gate.building();
  EXPECT_FALSE(gate.accepts());  // refreshing
  gate.frameShown();
  EXPECT_TRUE(gate.accepts());
}

TEST(SettingsScreen, ARenderAlreadyRunningDoesNotOpenTheGateForTheNextFrame) {
  screen::TapGate gate;
  gate.building();     // a render lays out the old rows...
  gate.redrawAsked();  // ...an edit comes in meanwhile
  gate.frameShown();   // the old frame is up: the edit's isn't
  EXPECT_FALSE(gate.accepts());
  gate.building();
  gate.frameShown();
  EXPECT_TRUE(gate.accepts());
}
