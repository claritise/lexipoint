#if LEXIRISE

#include "SettingsPatch.h"

#include <cctype>

namespace lexipoint {
namespace {

std::string trimmed(const std::string& s) {
  size_t start = 0;
  size_t end = s.size();
  while (start < end && std::isspace(static_cast<unsigned char>(s[start]))) start++;
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
  return s.substr(start, end - start);
}

bool validDictionary(const std::optional<std::string>& name) {
  return !name || name->empty() || isSafeDictionaryName(*name);
}

}  // namespace

PatchResult applyPatch(Settings& settings, const SettingsPatch& patch) {
  PatchResult result;
  const auto reject = [&result](const char* field) {
    result.ok = false;
    result.field = field;
    return result;
  };

  // The key box: a pasted key often carries a trailing newline or spaces.
  std::optional<std::string> newKey;
  if (patch.apiKey) {
    const std::string key = trimmed(*patch.apiKey);
    const bool unchanged = key.empty() || (settings.hasApiKey() && key == maskApiKey(settings.apiKey));
    if (!unchanged) {
      if (!isPlausibleApiKey(key)) return reject("apiKey");
      newKey = key;
    }
  }
  if (!validDictionary(patch.japaneseStardict)) return reject("ja.stardict");
  if (!validDictionary(patch.chineseStardict)) return reject("zh.stardict");
  if (patch.wifiIdleMin && !isAllowedWifiIdle(*patch.wifiIdleMin)) return reject("wifiIdleMin");
  std::optional<std::string> baseUrl;
  if (patch.baseUrl) {
    const std::string url = trimmed(*patch.baseUrl);
    if (!isValidBaseUrl(url)) return reject("baseUrl");
    baseUrl = canonicalBaseUrl(url);
    // A new server only ever gets a key pasted alongside it: an edit that just points the stored key
    // somewhere else (a cross-site request, say) must never send it there.
    if (*baseUrl != settings.baseUrl && settings.hasApiKey() && !newKey && !patch.clearApiKey) {
      return reject("apiKeyForServer");
    }
  }

  if (patch.enabled) settings.enabled = *patch.enabled;
  if (patch.clearApiKey) {
    result.keyChanged = settings.hasApiKey();
    settings.apiKey.clear();
  } else if (newKey && *newKey != settings.apiKey) {
    settings.apiKey = *newKey;
    result.keyChanged = true;
  }
  if (patch.japaneseEnabled) settings.japanese.enabled = *patch.japaneseEnabled;
  if (patch.japaneseReading) settings.japaneseReading = *patch.japaneseReading;
  if (patch.japaneseStardict) settings.japanese.stardict = *patch.japaneseStardict;
  if (patch.chineseEnabled) settings.chinese.enabled = *patch.chineseEnabled;
  if (patch.chineseStardict) settings.chinese.stardict = *patch.chineseStardict;
  if (patch.defaultLanguage) settings.defaultLanguage = *patch.defaultLanguage;
  if (patch.tags) settings.tags = normaliseTags(*patch.tags);
  if (patch.wifiIdleMin) settings.wifiIdleMin = *patch.wifiIdleMin;
  if (baseUrl) settings.baseUrl = *baseUrl;
  return result;
}

}  // namespace lexipoint

#endif  // LEXIRISE
