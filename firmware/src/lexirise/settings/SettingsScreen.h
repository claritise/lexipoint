#pragma once

// The device's Settings → System → Lexirise screen, as data (settings.md §1): which rows show, which
// group each belongs to, and the edit a tap on a toggle or choice row makes. Pure; the activity
// (LexiriseSettingsActivity) only draws the rows and runs the keyboard and the connection test.
// Every edit is a SettingsPatch, validated by applyPatch like the web page's.
// Tests: test/lexirise_settings/SettingsScreenTest.cpp.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "SettingsPatch.h"
#include "SettingsStore.h"
#include "lexirise/api/KeyCheck.h"

namespace lexipoint::settings_screen {

enum class Row : uint8_t {
  // Account
  Lookups,
  ApiKey,
  Account,  // read-only: the cached /v1/me result
  TestConnection,
  // Japanese
  JaLookups,
  JaReading,
  JaDictionary,
  // Chinese (Simplified)
  ZhLookups,
  ZhDictionary,
  // General
  DefaultLanguage,
  Tags,
  TagBook,
  DeckPerBook,  // shown while TagBook is on: the deck is filled by the book tag
  WifiIdle,
};
constexpr size_t kRowCount = static_cast<size_t>(Row::WifiIdle) + 1;

enum class Group : uint8_t { Account, Japanese, Chinese, General };
Group groupOf(Row row);

// What a tap on a row does.
enum class Edit : uint8_t {
  Patch,     // toggles or cycles a value: tapPatch()
  Keyboard,  // the API key, the tags
  Test,      // Test connection
  None,      // the read-only Account row
};
Edit editFor(Row row);

struct Rows {
  std::array<Row, kRowCount> rows{};
  size_t count = 0;
  Row operator[](const size_t i) const { return rows[i]; }
};

// The rows that show (settings.md §1): with Lexirise lookups off, the Account group, each language's Offline
// dictionary and "Language when a book doesn't say" (P13: they decide every tap then); a language that's off
// collapses to its Lookups toggle and its Offline dictionary; "Language when a book doesn't say" whenever
// Han-only text uses it (Settings::defaultLanguageApplies). Hidden rows keep their values.
Rows visibleRows(const Settings& settings);

// Taps wait for the frame an edit asked for. The list lays out its new rows and touch targets before the
// panel refresh starts, so during that refresh (~0.5 s) a tap would be matched against rows the user can't
// see yet (a toggle that shows or hides rows moves every row below it). Main task: redrawAsked() with each
// edit's requestUpdate, accepts() before a tap; render task: building() as it lays the rows out, frameShown()
// once that frame's refresh is done. Counted, so a render already running when an edit came in doesn't open
// the gate for the frame after it.
class TapGate {
 public:
  void redrawAsked() { ++asked_; }
  void building() { building_ = asked_.load(); }
  void frameShown() { shown_ = building_.load(); }
  bool accepts() const { return shown_.load() == asked_.load(); }

 private:
  std::atomic<uint32_t> asked_{0};
  std::atomic<uint32_t> building_{0};
  std::atomic<uint32_t> shown_{0};
};

// The row at a list index (a tap's), nullopt past the end.
std::optional<Row> rowAt(const Rows& rows, int index);

// Whether `row` is among `rows` (the web page's rows follow the screen's: web::stateJson "shows").
bool shows(const Rows& rows, Row row);

// Whether row i opens a group (it gets the group's heading).
bool startsGroup(const Rows& rows, size_t i);

// The edit a tap on a Patch row makes (nullopt for the other rows). `dictionaries` are the StarDict
// folders found on the card, in the order they're offered.
std::optional<SettingsPatch> tapPatch(Row row, const Settings& settings, const std::vector<std::string>& dictionaries);

// The StarDict folders the screen offers: those a setting can hold (isSafeDictionaryName), in the order found.
std::vector<std::string> offeredDictionaries(std::vector<std::string> found);

// The choice after `current`: "" (the global dictionary), then each folder, then "" again. A folder
// that's no longer on the card steps to the first one.
std::string nextDictionary(std::string_view current, const std::vector<std::string>& dictionaries);
// What an edit on a row came to (SettingsStore::update of its patch), for the row's value: a key that
// isn't one on the key row (its patch holds nothing else), or any other refusal or failed save.
enum class EditResult : uint8_t { Saved, NotAKey, NotSaved };
EditResult editResult(Row row, SettingsStore::UpdateResult result);

// The Account row's value, from the cached key check (settings.md §1: `Not checked` until the first test).
enum class AccountLine : uint8_t { NotSet, NotChecked, Checking, Connected, KeyRejected, NoNetwork, CouldNotConnect };
AccountLine accountLine(bool hasKey, api::KeyState state);

// The next "Keep WiFi on after a lookup" choice (config::kWifiIdleChoicesMin), wrapping.
int nextWifiIdle(int current);

}  // namespace lexipoint::settings_screen
