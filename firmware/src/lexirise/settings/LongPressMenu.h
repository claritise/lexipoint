#pragma once

// CrossPoint's Settings → Controls → Long-press Menu with Lexipoint (claritise, 2026-09-25: "get rid of
// lookup mode since we have hold to look up"): the Dictionary choice, which opened word select to pick a word,
// isn't offered, since a long-press on a word looks it up. The setting keeps upstream's stored values
// (CrossPointSettings::LP_MENU_*, mirrored below and checked in SettingsList.h), so its file stays compatible;
// the screens show the other choices, and a stored Dictionary reads as Reader Menu (Disabled on a board
// without a Home key, where Reader Menu isn't offered). Pure; tests: test/lexirise_settings/LongPressMenuTest.cpp.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lexipoint::long_press_menu {

// CrossPointSettings::LP_MENU_* (upstream's stored values).
constexpr uint8_t kKoSync = 0;
constexpr uint8_t kDisabled = 1;
constexpr uint8_t kBookmark = 2;
constexpr uint8_t kDictionary = 3;
constexpr uint8_t kReaderMenu = 4;

// The stored values offered, in upstream's order without Dictionary; Reader Menu only with a Home key.
inline std::vector<uint8_t> offered(const bool hasHomeKey) {
  std::vector<uint8_t> values = {kKoSync, kDisabled, kBookmark};
  if (hasHomeKey) values.push_back(kReaderMenu);
  return values;
}

// What a stored value means now: Dictionary (or anything not offered) becomes Reader Menu, or Disabled
// without a Home key.
inline uint8_t migrated(const uint8_t stored, const bool hasHomeKey) {
  for (const uint8_t value : offered(hasHomeKey)) {
    if (value == stored) return stored;
  }
  return hasHomeKey ? kReaderMenu : kDisabled;
}

// Loading the setting: its value now, and whether the file must be rewritten (it held Dictionary, or a value
// no choice has).
struct Loaded {
  uint8_t value;
  bool resave;
};
inline Loaded load(const uint8_t stored, const bool hasHomeKey) {
  const uint8_t value = migrated(stored, hasHomeKey);
  return {value, value != stored};
}

// A stored value's position among the offered choices (the settings screens show positions).
inline uint8_t indexOf(const uint8_t stored, const bool hasHomeKey) {
  const auto values = offered(hasHomeKey);
  const uint8_t value = migrated(stored, hasHomeKey);
  for (size_t i = 0; i < values.size(); i++) {
    if (values[i] == value) return static_cast<uint8_t>(i);
  }
  return 0;  // not reached: migrated() always gives an offered value
}

// The stored value of the choice at `index` (past the end: Disabled).
inline uint8_t valueAt(const size_t index, const bool hasHomeKey) {
  const auto values = offered(hasHomeKey);
  return index < values.size() ? values[index] : kDisabled;
}

}  // namespace lexipoint::long_press_menu
