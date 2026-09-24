#pragma once

// An edit from the /lexirise web page (settings.md §1a-2), validated before it touches Settings.
// Pure: the web handler only converts JSON to and from this. Tests: test/lexirise_settings.

#include <optional>
#include <string>

#include "Settings.h"

namespace lexipoint {

struct SettingsPatch {
  std::optional<bool> enabled;
  // The key box: empty, or still showing the masked key, means "leave the key alone" (the page never
  // has the real key). clearApiKey removes it.
  std::optional<std::string> apiKey;
  bool clearApiKey = false;
  std::optional<bool> japaneseEnabled;
  std::optional<Reading> japaneseReading;
  std::optional<std::string> japaneseStardict;  // "" = the global dictionary
  std::optional<bool> chineseEnabled;
  std::optional<std::string> chineseStardict;
  std::optional<Language> defaultLanguage;
  std::optional<std::string> tags;
  std::optional<int> wifiIdleMin;
  std::optional<std::string> baseUrl;
};

struct PatchResult {
  bool ok = true;
  const char* field = nullptr;  // the first rejected field (the patch is then not applied at all)
  bool keyChanged = false;      // a new key was set: test it straight away
};

// Validates the whole patch first and applies it only if every field is valid.
PatchResult applyPatch(Settings& settings, const SettingsPatch& patch);

}  // namespace lexipoint
