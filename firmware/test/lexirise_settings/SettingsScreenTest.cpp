#include <gtest/gtest.h>

#include <algorithm>
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
  EXPECT_EQ(rowsOf(Settings()),
            (std::vector<Row>{Row::Lookups, Row::ApiKey, Row::Account, Row::TestConnection, Row::JaLookups,
                              Row::JaReading, Row::JaDictionary, Row::ZhLookups, Row::ZhDictionary,
                              Row::DefaultLanguage, Row::Tags, Row::TagBook, Row::WifiIdle}));
}

TEST(SettingsScreen, LexiriseOffLeavesTheAccountGroupAndWhatTheOfflineDictionariesUse) {
  // P13 (claritise): the offline dictionaries answer every tap while Lexirise is off, so they stay, and so
  // does the language for Han-only text, which picks between them.
  Settings s;
  s.enabled = false;
  const std::vector<Row> off = {Row::Lookups,      Row::ApiKey,       Row::Account,        Row::TestConnection,
                                Row::JaDictionary, Row::ZhDictionary, Row::DefaultLanguage};
  EXPECT_EQ(rowsOf(s), off);
  s.japanese.enabled = false;  // a language's own switch doesn't matter then
  EXPECT_EQ(rowsOf(s), off);
}

TEST(SettingsScreen, ALanguageOffCollapsesToItsToggleAndDictionaryAndHidesTheDefaultLanguage) {
  Settings s;
  s.chinese.enabled = false;
  EXPECT_EQ(rowsOf(s), (std::vector<Row>{Row::Lookups, Row::ApiKey, Row::Account, Row::TestConnection, Row::JaLookups,
                                         Row::JaReading, Row::JaDictionary, Row::ZhLookups, Row::ZhDictionary,
                                         Row::Tags, Row::TagBook, Row::WifiIdle}));
  s.chinese.enabled = true;
  s.japanese.enabled = false;
  EXPECT_EQ(rowsOf(s), (std::vector<Row>{Row::Lookups, Row::ApiKey, Row::Account, Row::TestConnection, Row::JaLookups,
                                         Row::JaDictionary, Row::ZhLookups, Row::ZhDictionary, Row::Tags, Row::TagBook,
                                         Row::WifiIdle}));
  s.chinese.enabled = false;  // none on: StarDict answers Han-only text by the default language, so it shows
  EXPECT_EQ(rowsOf(s), (std::vector<Row>{Row::Lookups, Row::ApiKey, Row::Account, Row::TestConnection, Row::JaLookups,
                                         Row::JaDictionary, Row::ZhLookups, Row::ZhDictionary, Row::DefaultLanguage,
                                         Row::Tags, Row::TagBook, Row::WifiIdle}));
}

TEST(SettingsScreen, EverySwitchCombinationShowsSevenToThirteenRows) {
  // lxctl's SETTINGS_ROWS_MIN / MAX (settings-smoke) are these bounds; test_lxctl pins them to the Row list.
  size_t fewest = screen::kRowCount, most = 0;
  for (int bits = 0; bits < 8; bits++) {
    Settings s;
    s.enabled = bits & 1;
    s.japanese.enabled = bits & 2;
    s.chinese.enabled = bits & 4;
    const auto rows = rowsOf(s);
    fewest = std::min(fewest, rows.size());
    most = std::max(most, rows.size());
    // The offline dictionaries always show (P13), and the default language whenever Han-only text uses it:
    // everywhere but Lexirise on with one language on, where that language is the answer.
    EXPECT_EQ(std::count(rows.begin(), rows.end(), Row::JaDictionary), 1) << bits;
    EXPECT_EQ(std::count(rows.begin(), rows.end(), Row::ZhDictionary), 1) << bits;
    EXPECT_EQ(std::count(rows.begin(), rows.end(), Row::DefaultLanguage),
              s.enabled && s.enabledLanguageCount() == 1 ? 0 : 1)
        << bits;
  }
  EXPECT_EQ(fewest, 7u);
  EXPECT_EQ(most, screen::kRowCount);
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
                        Row::ZhDictionary, Row::DefaultLanguage, Row::TagBook, Row::WifiIdle}) {
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

TEST(SettingsScreen, TagWithBookTitleTogglesInTheGeneralGroupAfterTags) {
  Settings s;
  ASSERT_TRUE(s.tagBook);  // on by default (C2)
  tap(s, Row::TagBook);
  EXPECT_FALSE(s.tagBook);
  EXPECT_TRUE(screen::shows(screen::visibleRows(s), Row::TagBook));  // off still shows, to turn it back on
  tap(s, Row::TagBook);
  EXPECT_TRUE(s.tagBook);
  EXPECT_EQ(screen::groupOf(Row::TagBook), Group::General);
  s.enabled = false;  // a Lexirise save's tag: hidden with Lexirise, like Tags
  EXPECT_FALSE(screen::shows(screen::visibleRows(s), Row::TagBook));
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
  // index 5 is Readings there, and never Japanese's offline dictionary that index 5 is now.
  Settings before;
  const auto drawn = screen::visibleRows(before);
  Settings after = before;
  after.japanese.enabled = false;
  EXPECT_EQ(screen::rowAt(drawn, 5), Row::JaReading);
  EXPECT_EQ(screen::rowAt(screen::visibleRows(after), 5), Row::JaDictionary);
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
