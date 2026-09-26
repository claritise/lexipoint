#if LEXIRISE

#include "LiveWord.h"

#include <algorithm>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/lookup/Fallback.h"
#include "lexirise/text/CharClass.h"
#include "lexirise/text/Conjugation.h"
#include "lexirise/text/Kana.h"
#include "lexirise/text/SentenceBuilder.h"
#include "lexirise/text/Utf8Prefix.h"

namespace lexipoint::card {

namespace {

constexpr std::string_view kJlptPrefix = "JLPT-";
constexpr std::string_view kHskPrefix = "HSK-";
// The longest run where the end of `a` is the start of `b`, in bytes, at a character boundary. Quadratic in the
// shorter's length, which is bounded: a saved note is cut to config::kMaxSavedNoteBytes, the page's sentence to
// kMaxSentenceUnits, and it runs only for a saved word.
size_t overlapInto(const std::string_view a, const std::string_view b) {
  if (b.empty() || text::isContinuationByte(b[0])) return 0;
  for (size_t k = std::min(a.size(), b.size()); k > 0; k--) {
    if (k < b.size() && text::isContinuationByte(b[k])) continue;
    if (a.substr(a.size() - k) == b.substr(0, k)) return k;
  }
  return 0;
}

}  // namespace

bool sameSentence(const std::string_view saved, const SentenceText& here) {
  const std::string_view page = here.text;
  if (saved == page) return true;
  const size_t longer = std::max(saved.size(), page.size());
  const auto most = [longer](const size_t part) { return part * 100 >= longer * config::kSameSentenceOverlapPercent; };
  if (const size_t at = saved.find(page); at != std::string_view::npos) {  // the page's inside the note
    if (most(page.size())) return true;
    const bool before = at > 0;
    const bool after = at + page.size() < saved.size();
    return (!before || here.cutAtStart) && (!after || here.cutAtEnd);
  }
  if (page.find(saved) != std::string_view::npos) {  // the note's inside the page's
    return most(saved.size()) ||
           text::utf16Length(saved) * 100 >= config::kMaxSentenceUnits * config::kCutNoteMinPercentOfCap;
  }
  return most(std::max(overlapInto(saved, page), overlapInto(page, saved)));  // two cuts of one long sentence
}

std::optional<std::string_view> nextOnPage(const lookup::LookupCard& card, const PageSentence& sentence) {
  if (card.charEnd <= card.charStart) return std::nullopt;
  const std::string_view next = text::utf8CharAtUtf16(sentence.text, card.charEnd);
  if (!next.empty()) return next;
  // Nothing after it: the sentence's end, unless the page or the cap cut it there (or the offset is off).
  const bool atEnd = text::utf16Length(sentence.text) == card.charEnd;
  if (!atEnd || sentence.cutAtEnd) return std::nullopt;
  return std::string_view{};
}

FormName formNameOf(const lookup::LookupCard& card, const PageSentence& sentence) {
  FormName out;
  out.surface = card.surface;
  out.word = card.headword();
  if (card.language != Language::Japanese || card.surface == out.word) return out;
  if (const auto conjugation = text::conjugationOf(card.surface, out.word, nextOnPage(card, sentence))) {
    out.conjugation = conjugation->name;
    out.forms.reserve(conjugation->steps.size() + 1);
    out.forms.push_back({conjugation->dictionaryForm, "", true});  // 勉強する for a する verb given as its noun
    for (const auto& step : conjugation->steps) out.forms.push_back({step.form, step.label});
  } else {
    out.forms.push_back({card.surface, ""});  // the form in this book, unnamed
  }
  return out;
}

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

std::optional<MetBefore> metBefore(const lookup::LookupCard& card, const std::span<const SentenceText> here,
                                   const BookTagList* titles) {
  if (!card.saved) return std::nullopt;
  const std::string_view notes = text::trimmedSpaces(card.saved->notes);
  if (notes.empty()) return std::nullopt;
  for (const SentenceText& text : here) {
    const SentenceText page{text::trimmedSpaces(text.text), text.cutAtStart, text.cutAtEnd};
    if (!page.text.empty() && sameSentence(notes, page)) return std::nullopt;
  }
  MetBefore met;
  met.sentence.text = std::string(notes);
  // The word as it stood there: the dictionary form, the form here, or (Japanese) the dictionary form's stem, which
  // every conjugated form keeps (煩わし- in 煩わしかった).
  const std::string headword = card.headword();
  std::string_view candidates[] = {headword, card.surface, {}};
  if (card.language == Language::Japanese) {
    // A する verb's noun (勉強 of 勉強する); else the stem only when it's at least two characters: one alone
    // (す of する, 見 of 見る) matches too much.
    const std::string_view view = headword;
    const std::string_view suru = text::kSuruEnding;
    const std::string_view stem = view.size() > suru.size() && view.ends_with(suru)
                                      ? view.substr(0, view.size() - suru.size())
                                      : text::utf8WithoutLastChar(view);
    if (!text::utf8WithoutLastChar(stem).empty() || stem != text::utf8WithoutLastChar(view)) candidates[2] = stem;
  }
  for (const std::string_view candidate : candidates) {
    if (candidate.empty()) continue;
    const size_t at = notes.find(candidate);
    if (at == std::string_view::npos) continue;
    met.sentence.markStart = at;
    met.sentence.markLength = candidate.size();
    break;
  }
  if (titles) {
    const std::string_view prefix = config::kBookTagPrefix;
    for (const std::string& tag : card.saved->userTags) {
      if (tag.size() <= prefix.size() || tag.compare(0, prefix.size(), prefix) != 0) continue;
      if (auto title = bookTitleIn(*titles, std::string_view(tag).substr(prefix.size()))) {
        met.book = std::move(*title);
        break;
      }
    }
  }
  return met;
}

CardWord cardWord(const lookup::LookupCard& card, const PageSentence& sentence, const BookTagList* titles,
                  const FormName* named) {
  CardWord w;
  w.language = card.language;
  w.word = card.headword();
  if (card.language == Language::Japanese) {
    w.romaji = card.reading;
    // Kana when the romaji converts cleanly (a katakana word is its own reading), else the romaji.
    w.reading = text::kanaReading(w.word, card.reading).value_or(card.reading);
    if (card.surface != w.word) {
      w.surface = card.surface;
      const bool same = named && named->surface == w.surface && named->word == w.word;
      FormName name = same ? *named : formNameOf(card, sentence);
      w.conjugation = std::move(name.conjugation);
      w.forms = std::move(name.forms);
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
  if (auto met = metBefore(card, sentence.whole, titles)) {
    w.metBefore = std::move(met->sentence);
    w.metBeforeBook = std::move(met->book);
  }
  return w;
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
