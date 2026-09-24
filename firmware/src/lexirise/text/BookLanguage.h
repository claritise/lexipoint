#pragma once

// Which language to send Lexirise for a lookup (languages.md §1): per book, not global. Pure.
// Tests: test/lexirise_language.

#include <optional>
#include <string_view>

#include "lexirise/settings/Settings.h"

namespace lexipoint::text {

// What a book's EPUB <dc:language> says.
enum class TaggedLanguage {
  Japanese,            // ja, ja-*
  Chinese,             // zh, zh-CN, zh-SG, zh-Hans(-*), cmn, cmn-Hans
  ChineseTraditional,  // zh-TW, zh-HK, zh-MO, zh-Hant(-*): not sent until H8 says Lexirise handles it
  Unknown,             // missing, "und", or any other language (often wrong on converted CJK books)
};

TaggedLanguage parseLanguageTag(std::string_view dcLanguage);

// Why the language was chosen (for the debug log and the card's language badge).
enum class LanguageSource { Override, Metadata, Kana, DefaultForHan, None };

struct LanguageDecision {
  std::optional<Language> language;  // what to send Lexirise; nullopt: StarDict answers
  std::optional<Language> detected;  // what the text is, even when that language is switched off
                                     // (it still picks the punctuation rules)
  LanguageSource source = LanguageSource::None;
};

// Decided once per book when it opens; sentences are only scanned when the metadata doesn't say.
class BookLanguage {
 public:
  BookLanguage(std::string_view dcLanguage, std::optional<Language> override)
      : tagged_(parseLanguageTag(dcLanguage)), override_(override) {}

  // Precedence: the per-book override (set from the card), then the metadata, then the sentence
  // itself: any kana → Japanese; Han without kana → settings.defaultLanguage; no CJK → none. A
  // language that is switched off in settings (or Lexirise itself) isn't sent, but is still `detected`.
  // Kana means real hiragana/katakana: the middle dot ・ and the long-vowel mark ー also appear in
  // Chinese transliterated names (哈利・波特), so they don't count.
  LanguageDecision decide(std::string_view sentence, const Settings& settings) const;
  // Whether decide() looks at the sentence at all (no override, and the metadata doesn't say).
  bool dependsOnSentence() const { return !override_ && tagged_ == TaggedLanguage::Unknown; }

 private:
  TaggedLanguage tagged_;
  std::optional<Language> override_;
};

const char* languageSourceName(LanguageSource source);

}  // namespace lexipoint::text
