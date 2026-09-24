#pragma once

// Lexirise settings model and INI (de)serialisation: pure, host-testable (settings.md in the lexipoint
// repo). Persistence lives in SettingsStore. Tests: test/lexirise_settings.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint {

enum class Language { Japanese, Chinese };
enum class Reading { Kana, Romaji };

const char* languageCode(Language language);  // "ja" / "zh"

struct LanguageSettings {
  bool enabled = true;
  std::string stardict;  // offline fallback dictionary folder; empty = the global dictionary
};

struct Settings {
  // [account]
  bool enabled = true;
  std::string apiKey;
  // [ja]
  LanguageSettings japanese;
  Reading japaneseReading = Reading::Kana;
  // [zh]
  LanguageSettings chinese;
  // [general]
  Language defaultLanguage = Language::Japanese;
  std::string tags = config::kDefaultTags;  // comma-separated, normalised
  int wifiIdleMin = config::kWifiIdleDefaultMin;
  // [advanced]
  std::string baseUrl = config::kDefaultBaseUrl;

  // Keys and sections this firmware doesn't know, kept verbatim so a newer firmware's settings survive
  // a downgrade: {section, key, value}.
  struct Extra {
    std::string section;
    std::string key;
    std::string value;
  };
  std::vector<Extra> extras;

  bool hasApiKey() const { return !apiKey.empty(); }
  int enabledLanguageCount() const { return (japanese.enabled ? 1 : 0) + (chinese.enabled ? 1 : 0); }
};

struct ParseResult {
  Settings settings;
  bool migratedLegacyKeys = false;    // flat pre-section keys were found (settings.md §3)
  std::vector<std::string> warnings;  // invalid values that fell back to defaults (never contains the key)
};

// Parses config.ini text. Never fails: unreadable values fall back to defaults with a warning.
ParseResult parseSettings(std::string_view text);

// Canonical config.ini text: one section per group, extras kept.
std::string serializeSettings(const Settings& settings);

// "lx_••••••••nas": the only form in which a key is ever shown or served (settings.md §2).
std::string maskApiKey(std::string_view key);

// Shape check for a pasted key: prefix, length, and [A-Za-z0-9_-] only.
bool isPlausibleApiKey(std::string_view key);

// Field validators shared by the file parser and the web page (SettingsPatch).
bool isAllowedWifiIdle(int minutes);                 // one of config::kWifiIdleChoicesMin
bool isSafeDictionaryName(std::string_view name);    // a plain folder name, no path parts
bool isValidBaseUrl(std::string_view url);           // https only (net::parseBaseUrl)
std::string canonicalBaseUrl(std::string_view url);  // trailing slashes dropped

// Normalises a comma-separated tag list: trims, drops empties/duplicates and unsafe characters, caps
// count and length. Returns the canonical "a,b,c" form.
std::string normaliseTags(std::string_view tags);

}  // namespace lexipoint
