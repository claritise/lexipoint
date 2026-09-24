#if LEXIRISE

#include "LiveWord.h"

#include "lexirise/LexiriseConfig.h"
#include "lexirise/lookup/Fallback.h"
#include "lexirise/text/Kana.h"

namespace lexipoint::card {

namespace {

constexpr std::string_view kJlptPrefix = "JLPT-";
constexpr std::string_view kHskPrefix = "HSK-";

}  // namespace

std::string badgeFor(const std::string_view level) {
  if (level.substr(0, kJlptPrefix.size()) == kJlptPrefix) return std::string(level.substr(kJlptPrefix.size()));
  if (level.substr(0, kHskPrefix.size()) == kHskPrefix) return "HSK " + std::string(level.substr(kHskPrefix.size()));
  return {};
}

Level levelOf(const std::optional<api::EntryState>& saved) {
  if (!saved || saved->proficiency < 1 || saved->proficiency > metrics::kLevelCells) return Level::None;
  return static_cast<Level>(saved->proficiency - 1);
}

int proficiencyOf(const Level level) { return level == Level::None ? 0 : static_cast<int>(level) + 1; }

NoMeaning noMeaningFor(const api::ApiError error) {
  if (error == api::ApiError::Unauthorized) return NoMeaning::KeyRejected;
  if (error == api::ApiError::RateLimited) return NoMeaning::RateLimited;
  return lookup::fallbackFor(error).offline ? NoMeaning::Offline : NoMeaning::Unavailable;
}

Phase phaseOf(const lookup::LookupCard& card) {
  if (!card.complete) return Phase::Analyzed;
  if (card.translationUnavailable) return Phase::Unanswered;
  return card.translationPending ? Phase::TranslationPending : Phase::Complete;
}

CardWord cardWord(const lookup::LookupCard& card) {
  CardWord w;
  w.language = card.language;
  w.word = card.headword();
  if (card.language == Language::Japanese) {
    w.romaji = card.reading;
    // Kana when the romaji converts cleanly (a katakana word is its own reading), else the romaji.
    w.reading = text::kanaReading(w.word, card.reading).value_or(card.reading);
    if (card.surface != w.word) {
      w.surface = card.surface;
      w.forms.push_back({card.surface, ""});  // the form in this book (Lexirise names no conjugation)
    }
  } else {
    w.reading = card.reading;  // pinyin with tone marks
  }
  w.badge = badgeFor(card.level);
  w.partOfSpeech = card.partOfSpeech;
  for (const api::Sense& sense : card.senses) w.senses.push_back(sense.translation);
  w.rank = card.rank;
  w.frequency = card.frequency;
  w.noMeaning = noMeaningFor(card.translationError);
  return w;
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
