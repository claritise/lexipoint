#if LEXIRISE

#include "Settings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "lexirise/net/Http.h"

namespace lexipoint {
namespace {

// UTF-8 bytes of the mask character "•" (U+2022).
constexpr const char* kMaskDot = "\xE2\x80\xA2";

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

bool parseBool(std::string_view v, bool& out) {
  const std::string l = lower(v);
  if (l == "1" || l == "true" || l == "on" || l == "yes") {
    out = true;
    return true;
  }
  if (l == "0" || l == "false" || l == "off" || l == "no") {
    out = false;
    return true;
  }
  return false;
}

bool parseInt(std::string_view v, int& out) {
  if (v.empty() || v.size() > 6) return false;
  int value = 0;
  for (const char c : v) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  out = value;
  return true;
}

// One (section, key, value) from the file, in canonical lower case for section and key.
struct Entry {
  std::string section;
  std::string key;
  std::string_view value;
};

class Applier {
 public:
  explicit Applier(ParseResult& r) : r_(r), s_(r.settings) {}

  void apply(const Entry& e) {
    if (e.section.empty()) return applyLegacy(e);
    if (e.section == "account") return account(e);
    if (e.section == "ja") return language(e, s_.japanese, true);
    if (e.section == "zh") return language(e, s_.chinese, false);
    if (e.section == "general") return general(e);
    if (e.section == "advanced") return advanced(e);
    keep(e);
  }

 private:
  void warn(const Entry& e) {
    // Never echo api_key values into warnings.
    r_.warnings.push_back("invalid " + (e.section.empty() ? std::string() : e.section + ".") + e.key);
  }
  void keep(const Entry& e) { s_.extras.push_back({e.section, e.key, std::string(e.value)}); }

  void setBool(const Entry& e, bool& field) {
    if (!parseBool(e.value, field)) warn(e);
  }

  void setKey(const Entry& e) {
    if (e.value.empty()) {
      s_.apiKey.clear();
    } else if (isPlausibleApiKey(e.value)) {
      s_.apiKey = std::string(e.value);
    } else {
      warn(e);
    }
  }

  void setReading(const Entry& e) {
    const std::string v = lower(e.value);
    if (v == "kana") {
      s_.japaneseReading = Reading::Kana;
    } else if (v == "romaji") {
      s_.japaneseReading = Reading::Romaji;
    } else {
      warn(e);
    }
  }

  void setDictionary(const Entry& e, LanguageSettings& lang) {
    if (e.value.empty()) {
      lang.stardict.clear();
    } else if (isSafeDictionaryName(e.value)) {
      lang.stardict = std::string(e.value);
    } else {
      warn(e);
    }
  }

  void setDefaultLanguage(const Entry& e) {
    if (const auto language = languageFromCode(e.value)) {
      s_.defaultLanguage = *language;
    } else {
      warn(e);
    }
  }

  void setWifiIdle(const Entry& e) {
    int minutes = 0;
    if (parseInt(e.value, minutes) && isAllowedWifiIdle(minutes)) {
      s_.wifiIdleMin = minutes;
    } else {
      warn(e);
    }
  }

  void setBaseUrl(const Entry& e) {
    if (isValidBaseUrl(e.value)) {
      s_.baseUrl = canonicalBaseUrl(e.value);
    } else {
      warn(e);
    }
  }

  void account(const Entry& e) {
    if (e.key == "enabled") return setBool(e, s_.enabled);
    if (e.key == "api_key") return setKey(e);
    keep(e);
  }

  void language(const Entry& e, LanguageSettings& lang, const bool japanese) {
    if (e.key == "enabled") return setBool(e, lang.enabled);
    if (e.key == "stardict") return setDictionary(e, lang);
    if (japanese && e.key == "reading") return setReading(e);
    keep(e);
  }

  void general(const Entry& e) {
    if (e.key == "default_language") return setDefaultLanguage(e);
    if (e.key == "tags") {
      s_.tags = normaliseTags(e.value);
      return;
    }
    if (e.key == "wifi_idle_min") return setWifiIdle(e);
    keep(e);
  }

  void advanced(const Entry& e) {
    if (e.key == "base_url") return setBaseUrl(e);
    keep(e);
  }

  // Flat keys from the first config format (lexirise-client.md §5, languages.md §1): migrated into
  // their sections on the next save.
  void applyLegacy(const Entry& e) {
    r_.migratedLegacyKeys = true;
    if (e.key == "enabled") return setBool(e, s_.enabled);
    if (e.key == "api_key") return setKey(e);
    if (e.key == "language" || e.key == "default_language") return setDefaultLanguage(e);
    if (e.key == "languages") {
      const std::string v = lower(e.value);
      s_.japanese.enabled = v.find("ja") != std::string::npos;
      s_.chinese.enabled = v.find("zh") != std::string::npos;
      return;
    }
    if (e.key == "reading") return setReading(e);
    if (e.key == "stardict_ja") return setDictionary(e, s_.japanese);
    if (e.key == "stardict_zh") return setDictionary(e, s_.chinese);
    if (e.key == "tags") {
      s_.tags = normaliseTags(e.value);
      return;
    }
    if (e.key == "wifi_idle_min") return setWifiIdle(e);
    if (e.key == "base_url") return setBaseUrl(e);
    keep(e);  // unknown flat key: kept under the empty section
  }

  ParseResult& r_;
  Settings& s_;
};

void appendLine(std::string& out, std::string_view key, std::string_view value) {
  out.append(key);
  out.push_back('=');
  out.append(value);
  out.push_back('\n');
}

void appendExtras(std::string& out, const Settings& s, std::string_view section) {
  for (const auto& e : s.extras) {
    if (e.section == section) appendLine(out, e.key, e.value);
  }
}

bool isKnownSection(std::string_view s) {
  return s == "account" || s == "ja" || s == "zh" || s == "general" || s == "advanced";
}

}  // namespace

bool isAllowedWifiIdle(const int minutes) {
  for (const int allowed : config::kWifiIdleChoicesMin) {
    if (allowed == minutes) return true;
  }
  return false;
}

bool isSafeDictionaryName(const std::string_view name) {
  if (name.empty() || name.size() > config::kMaxDictionaryNameLength) return false;
  if (name == "." || name == "..") return false;
  for (const char c : name) {
    if (c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x20) return false;
  }
  return true;
}

bool isValidBaseUrl(const std::string_view url) {
  net::Endpoint endpoint;
  return net::parseBaseUrl(url, endpoint);
}

std::string canonicalBaseUrl(std::string_view url) {
  while (!url.empty() && url.back() == '/') url.remove_suffix(1);
  return std::string(url);
}

const char* languageCode(const Language language) {
  switch (language) {  // no default: -Wswitch names a language added without its code
    case Language::Japanese:
      return "ja";
    case Language::Chinese:
      return "zh";
  }
  return "ja";  // not reached
}

std::optional<Language> languageFromCode(const std::string_view code) {
  const std::string lowered = lower(code);
  for (const Language language : kLanguages) {
    if (lowered == languageCode(language)) return language;
  }
  return std::nullopt;
}

ParseResult parseSettings(std::string_view text) {
  ParseResult result;
  Applier applier(result);
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF) {
    text.remove_prefix(3);  // UTF-8 BOM
  }
  std::string section;
  while (!text.empty()) {
    const size_t nl = text.find('\n');
    std::string_view line = trim(text.substr(0, nl));
    text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    if (line.empty() || line.front() == '#' || line.front() == ';') continue;
    if (line.front() == '[') {
      const size_t close = line.find(']');
      if (close == std::string_view::npos) {
        result.warnings.push_back("malformed section header");
        continue;
      }
      section = lower(trim(line.substr(1, close - 1)));
      continue;
    }
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      result.warnings.push_back("line without '='");
      continue;
    }
    applier.apply(Entry{section, lower(trim(line.substr(0, eq))), trim(line.substr(eq + 1))});
  }
  return result;
}

std::string serializeSettings(const Settings& s) {
  std::string out;
  out.reserve(512);
  out.append("# Lexipoint settings. Written by the reader; hand edits (with the reader off) are kept.\n");
  for (const auto& e : s.extras) {
    if (e.section.empty()) appendLine(out, e.key, e.value);  // unknown legacy flat keys, verbatim
  }
  out.append("\n[account]\n");
  appendLine(out, "enabled", s.enabled ? "1" : "0");
  appendLine(out, "api_key", s.apiKey);
  appendExtras(out, s, "account");

  out.append("\n[ja]\n");
  appendLine(out, "enabled", s.japanese.enabled ? "1" : "0");
  appendLine(out, "reading", s.japaneseReading == Reading::Romaji ? "romaji" : "kana");
  appendLine(out, "stardict", s.japanese.stardict);
  appendExtras(out, s, "ja");

  out.append("\n[zh]\n");
  appendLine(out, "enabled", s.chinese.enabled ? "1" : "0");
  appendLine(out, "stardict", s.chinese.stardict);
  appendExtras(out, s, "zh");

  out.append("\n[general]\n");
  appendLine(out, "default_language", languageCode(s.defaultLanguage));
  appendLine(out, "tags", s.tags);
  appendLine(out, "wifi_idle_min", std::to_string(s.wifiIdleMin));
  appendExtras(out, s, "general");

  out.append("\n[advanced]\n");
  appendLine(out, "base_url", s.baseUrl);
  appendExtras(out, s, "advanced");

  // Unknown sections, in first-seen order.
  std::vector<std::string> written;
  for (const auto& e : s.extras) {
    if (e.section.empty() || isKnownSection(e.section)) continue;
    if (std::find(written.begin(), written.end(), e.section) != written.end()) continue;
    written.push_back(e.section);
    out.append("\n[").append(e.section).append("]\n");
    appendExtras(out, s, e.section);
  }
  return out;
}

std::string maskApiKey(std::string_view key) {
  if (key.empty()) return {};
  std::string out(config::kApiKeyPrefix);
  for (int i = 0; i < config::kMaskedKeyDots; i++) out.append(kMaskDot);
  if (key.size() > config::kMaskedKeyTail + std::string_view(config::kApiKeyPrefix).size()) {
    out.append(key.substr(key.size() - config::kMaskedKeyTail));
  }
  return out;
}

bool isPlausibleApiKey(std::string_view key) {
  const std::string_view prefix = config::kApiKeyPrefix;
  if (key.size() < config::kApiKeyMinLength || key.size() > config::kApiKeyMaxLength) return false;
  if (key.substr(0, prefix.size()) != prefix) return false;
  for (const char c : key.substr(prefix.size())) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
  }
  return true;
}

std::string normaliseTags(std::string_view tags) {
  std::vector<std::string> out;
  while (!tags.empty() && out.size() < config::kMaxTags) {
    const size_t comma = tags.find(',');
    std::string_view tag = trim(tags.substr(0, comma));
    tags = comma == std::string_view::npos ? std::string_view{} : tags.substr(comma + 1);
    std::string clean;
    for (const char c : tag) {
      // Printable, and nothing that would need escaping in JSON or break the INI line.
      if (static_cast<unsigned char>(c) < 0x20 || c == '"' || c == '\\' || c == '=' || c == ';' || c == '#') continue;
      clean.push_back(c);
    }
    while (!clean.empty() && clean.back() == ' ') clean.pop_back();
    if (clean.empty() || clean.size() > config::kMaxTagLength) continue;
    if (std::find(out.begin(), out.end(), clean) == out.end()) out.push_back(clean);
  }
  std::string joined;
  for (const auto& t : out) {
    if (!joined.empty()) joined.push_back(',');
    joined.append(t);
  }
  return joined;
}

std::vector<std::string> tagList(std::string_view normalised) {
  std::vector<std::string> out;
  while (!normalised.empty()) {
    const size_t comma = normalised.find(',');
    if (comma != 0) out.emplace_back(normalised.substr(0, comma));
    if (comma == std::string_view::npos) break;
    normalised.remove_prefix(comma + 1);
  }
  return out;
}

}  // namespace lexipoint

#endif  // LEXIRISE
