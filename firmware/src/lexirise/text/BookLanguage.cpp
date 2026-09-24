#if LEXIRISE

#include "BookLanguage.h"

#include <Utf8.h>

#include <cctype>
#include <string>

namespace lexipoint::text {
namespace {

std::string lower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// "zh-Hant-TW" → subtags {"zh", "hant", "tw"}; '_' is accepted as a separator too.
bool hasSubtag(const std::string& tag, const std::string_view subtag) {
  size_t start = 0;
  while (start <= tag.size()) {
    size_t end = tag.find_first_of("-_", start);
    if (end == std::string::npos) end = tag.size();
    if (std::string_view(tag).substr(start, end - start) == subtag) return true;
    start = end + 1;
  }
  return false;
}

std::string primarySubtag(const std::string& tag) { return tag.substr(0, tag.find_first_of("-_")); }

// Hiragana and katakana letters and their iteration marks; not ・ (U+30FB) or ー (U+30FC).
bool isKana(const uint32_t cp) {
  return (cp >= 0x3041 && cp <= 0x3096) || (cp >= 0x309D && cp <= 0x309F) || (cp >= 0x30A1 && cp <= 0x30FA) ||
         (cp >= 0x30FD && cp <= 0x30FF);
}
bool isHan(const uint32_t cp) {
  return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0x20000 && cp <= 0x323AF);
}

bool isEnabled(const Language language, const Settings& settings) {
  return settings.enabled && (language == Language::Japanese ? settings.japanese.enabled : settings.chinese.enabled);
}

LanguageDecision decision(const Language language, const LanguageSource source, const Settings& settings) {
  LanguageDecision out;
  out.detected = language;
  out.source = source;
  if (isEnabled(language, settings)) out.language = language;
  return out;
}

}  // namespace

TaggedLanguage parseLanguageTag(const std::string_view dcLanguage) {
  std::string tag = lower(dcLanguage);
  while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.back()))) tag.pop_back();
  while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.front()))) tag.erase(0, 1);
  const std::string primary = primarySubtag(tag);
  if (primary == "ja" || primary == "jpn") return TaggedLanguage::Japanese;
  if (primary == "zh" || primary == "zho" || primary == "chi" || primary == "cmn") {
    if (hasSubtag(tag, "hans")) return TaggedLanguage::Chinese;
    if (hasSubtag(tag, "hant") || hasSubtag(tag, "tw") || hasSubtag(tag, "hk") || hasSubtag(tag, "mo")) {
      return TaggedLanguage::ChineseTraditional;
    }
    return TaggedLanguage::Chinese;  // zh, zh-CN, zh-SG, cmn: Simplified
  }
  return TaggedLanguage::Unknown;
}

LanguageDecision BookLanguage::decide(const std::string_view sentence, const Settings& settings) const {
  if (override_) return decision(*override_, LanguageSource::Override, settings);
  switch (tagged_) {
    case TaggedLanguage::Japanese:
      return decision(Language::Japanese, LanguageSource::Metadata, settings);
    case TaggedLanguage::Chinese:
      return decision(Language::Chinese, LanguageSource::Metadata, settings);
    case TaggedLanguage::ChineseTraditional: {
      LanguageDecision out;  // H8 (parked): not sent (StarDict answers), but cut as Chinese
      out.detected = Language::Chinese;
      out.source = LanguageSource::Metadata;
      return out;
    }
    case TaggedLanguage::Unknown:
    default:
      break;
  }
  bool han = false;
  const std::string text(sentence);
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    if (isKana(cp)) return decision(Language::Japanese, LanguageSource::Kana, settings);
    han = han || isHan(cp);
  }
  if (han) return decision(settings.defaultLanguage, LanguageSource::DefaultForHan, settings);
  return {};
}

const char* languageSourceName(const LanguageSource source) {
  switch (source) {
    case LanguageSource::Override:
      return "override";
    case LanguageSource::Metadata:
      return "metadata";
    case LanguageSource::Kana:
      return "kana";
    case LanguageSource::DefaultForHan:
      return "default";
    case LanguageSource::None:
    default:
      return "none";
  }
}

}  // namespace lexipoint::text

#endif  // LEXIRISE
