#if LEXIRISE

#include "SettingsScreen.h"

#include <algorithm>
#include <iterator>

namespace lexipoint::settings_screen {

Group groupOf(const Row row) {
  switch (row) {
    case Row::Lookups:
    case Row::ApiKey:
    case Row::Account:
    case Row::TestConnection:
      return Group::Account;
    case Row::JaLookups:
    case Row::JaReading:
    case Row::JaDictionary:
      return Group::Japanese;
    case Row::ZhLookups:
    case Row::ZhDictionary:
      return Group::Chinese;
    case Row::DefaultLanguage:
    case Row::Tags:
    case Row::TagBook:
    case Row::DeckPerBook:
    case Row::WifiIdle:
      break;
  }
  return Group::General;
}

Edit editFor(const Row row) {
  switch (row) {
    case Row::ApiKey:
    case Row::Tags:
      return Edit::Keyboard;
    case Row::TestConnection:
      return Edit::Test;
    case Row::Account:
      return Edit::None;
    case Row::Lookups:
    case Row::JaLookups:
    case Row::JaReading:
    case Row::JaDictionary:
    case Row::ZhLookups:
    case Row::ZhDictionary:
    case Row::DefaultLanguage:
    case Row::TagBook:
    case Row::DeckPerBook:
    case Row::WifiIdle:
      break;
  }
  return Edit::Patch;
}

Rows visibleRows(const Settings& settings) {
  Rows out;
  const auto add = [&out](const Row row) { out.rows[out.count++] = row; };
  add(Row::Lookups);
  add(Row::ApiKey);
  add(Row::Account);
  add(Row::TestConnection);
  // Each language's offline dictionary always shows: it answers that language's taps whenever Lexirise
  // doesn't (Lexirise off, the language's lookups off, offline), so it matters most exactly when the
  // Lexirise rows are hidden (P13, claritise). "Language when a book doesn't say" shows whenever Han-only text
  // uses it, Lexirise off included (Settings::defaultLanguageApplies). The rest is Lexirise's.
  const bool lexirise = settings.enabled;
  if (lexirise) add(Row::JaLookups);
  if (lexirise && settings.japanese.enabled) add(Row::JaReading);
  add(Row::JaDictionary);
  if (lexirise) add(Row::ZhLookups);
  add(Row::ZhDictionary);
  if (settings.defaultLanguageApplies()) add(Row::DefaultLanguage);
  if (lexirise) {
    add(Row::Tags);
    add(Row::TagBook);
    if (settings.tagBook) add(Row::DeckPerBook);
    add(Row::WifiIdle);
  }
  return out;
}

std::optional<Row> rowAt(const Rows& rows, const int index) {
  if (index < 0 || static_cast<size_t>(index) >= rows.count) return std::nullopt;
  return rows[static_cast<size_t>(index)];
}

bool shows(const Rows& rows, const Row row) {
  const auto end = rows.rows.begin() + static_cast<std::ptrdiff_t>(rows.count);
  return std::find(rows.rows.begin(), end, row) != end;
}

bool startsGroup(const Rows& rows, const size_t i) {
  return i < rows.count && (i == 0 || groupOf(rows[i]) != groupOf(rows[i - 1]));
}

std::vector<std::string> offeredDictionaries(std::vector<std::string> found) {
  found.erase(std::remove_if(found.begin(), found.end(), [](const std::string& n) { return !isSafeDictionaryName(n); }),
              found.end());
  return found;
}

std::string nextDictionary(const std::string_view current, const std::vector<std::string>& dictionaries) {
  if (dictionaries.empty()) return {};
  if (current.empty()) return dictionaries.front();
  const auto it = std::find(dictionaries.begin(), dictionaries.end(), current);
  if (it == dictionaries.end()) return dictionaries.front();
  const auto next = std::next(it);
  return next == dictionaries.end() ? std::string() : *next;
}

int nextWifiIdle(const int current) {
  const auto* begin = std::begin(config::kWifiIdleChoicesMin);
  const auto* end = std::end(config::kWifiIdleChoicesMin);
  const auto* it = std::find(begin, end, current);
  return (it == end || std::next(it) == end) ? *begin : *std::next(it);
}

EditResult editResult(const Row row, const SettingsStore::UpdateResult result) {
  switch (result) {
    case SettingsStore::UpdateResult::Saved:
      return EditResult::Saved;
    case SettingsStore::UpdateResult::Declined:
      return row == Row::ApiKey ? EditResult::NotAKey : EditResult::NotSaved;
    case SettingsStore::UpdateResult::SaveFailed:
      break;
  }
  return EditResult::NotSaved;
}

AccountLine accountLine(const bool hasKey, const api::KeyState state) {
  if (!hasKey) return AccountLine::NotSet;
  switch (state) {
    case api::KeyState::Unchecked:
      return AccountLine::NotChecked;
    case api::KeyState::Checking:
      return AccountLine::Checking;
    case api::KeyState::NoKey:
      return AccountLine::NotSet;
    case api::KeyState::Connected:
      return AccountLine::Connected;
    case api::KeyState::Rejected:
      return AccountLine::KeyRejected;
    case api::KeyState::Offline:
      return AccountLine::NoNetwork;
    case api::KeyState::Error:
      break;
  }
  return AccountLine::CouldNotConnect;
}

std::optional<SettingsPatch> tapPatch(const Row row, const Settings& settings,
                                      const std::vector<std::string>& dictionaries) {
  SettingsPatch patch;
  switch (row) {
    case Row::Lookups:
      patch.enabled = !settings.enabled;
      break;
    case Row::JaLookups:
      patch.japaneseEnabled = !settings.japanese.enabled;
      break;
    case Row::JaReading:
      patch.japaneseReading = settings.japaneseReading == Reading::Kana ? Reading::Romaji : Reading::Kana;
      break;
    case Row::JaDictionary:
      patch.japaneseStardict = nextDictionary(settings.japanese.stardict, dictionaries);
      break;
    case Row::ZhLookups:
      patch.chineseEnabled = !settings.chinese.enabled;
      break;
    case Row::ZhDictionary:
      patch.chineseStardict = nextDictionary(settings.chinese.stardict, dictionaries);
      break;
    case Row::DefaultLanguage:
      patch.defaultLanguage = settings.defaultLanguage == Language::Japanese ? Language::Chinese : Language::Japanese;
      break;
    case Row::TagBook:
      patch.tagBook = !settings.tagBook;
      break;
    case Row::DeckPerBook:
      patch.deckPerBook = !settings.deckPerBook;
      break;
    case Row::WifiIdle:
      patch.wifiIdleMin = nextWifiIdle(settings.wifiIdleMin);
      break;
    case Row::ApiKey:
    case Row::Account:
    case Row::TestConnection:
    case Row::Tags:
      return std::nullopt;
  }
  return patch;
}

}  // namespace lexipoint::settings_screen

#endif  // LEXIRISE
