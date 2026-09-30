#include "LexiriseLookup.h"

#include <algorithm>

#include "Match.h"
#include "WholeWords.h"
#include "lexirise/LexiriseConfig.h"
#include "lexirise/text/Utf8Prefix.h"

namespace lexipoint::lookup {
namespace {

// ② the occurrence under the tap.
LookupReport matchTap(const text::TapContext& tap, AnalyzedSentence sentence, AnalyzedSentence& out, size_t& word) {
  LookupReport report;
  const auto match = matchOccurrence(sentence.analysis.occurrences, tap.sentence->tapOffset);
  if (!match) {
    report.outcome = LookupOutcome::NotFound;
    return report;
  }
  for (size_t i = 0; i < sentence.analysis.occurrences.size(); i++) {
    if (sentence.analysis.occurrences[i].wordLike) sentence.words.push_back(i);
  }
  word = static_cast<size_t>(std::find(sentence.words.begin(), sentence.words.end(), *match) - sentence.words.begin());
  out = std::move(sentence);
  report.outcome = LookupOutcome::Card;
  return report;
}

}  // namespace

LookupReport analyzeTap(api::LexiriseApi& api, const text::TapContext& tap, AnalyzedSentence& out, size_t& word,
                        const api::AnalyzeResult* known) {
  LookupReport report;
  if (!tap.sentence || !tap.language.language) return report;  // not a Lexirise lookup
  const Language language = *tap.language.language;
  AnalyzedSentence sentence;
  sentence.language = language;
  if (known) {
    sentence.analysis = *known;  // the page's analysis: V1's merge already made where it was refined
    LookupReport fromPage = matchTap(tap, std::move(sentence), out, word);
    // The page's slice has no word here (tokens crossing the sentence's cut edges are left out): ask ① after all.
    if (fromPage.outcome != LookupOutcome::NotFound) {
      fromPage.fromPage = true;
      return fromPage;
    }
    sentence = AnalyzedSentence();
    sentence.language = language;
  }

  // ① analyze the sentence
  const api::ApiResponse analyzed = api.analyze(language, tap.sentence->text);
  if (!analyzed.ok()) {
    report.error = analyzed.error;
    return report;
  }
  if (api::parseAnalyze(analyzed.body, sentence.analysis) != api::ParseStatus::Ok) {
    report.error = api::ApiError::Malformed;
    report.bodyHead = bodyHead(analyzed.body);
    return report;
  }
  // An answer that came back already refined has cut the sentence's words into morphemes: its word-level
  // split puts them back together (v0.2 V1). If that call fails, the refined answer stands.
  if (!sentence.analysis.morphoPending) {
    const api::ApiResponse words = api.analyzeWords(language, tap.sentence->text);
    api::AnalyzeResult wordLevel;
    if (words.ok() && api::parseAnalyze(words.body, wordLevel) == api::ParseStatus::Ok) {
      sentence.analysis = wholeWords(sentence.analysis, std::move(wordLevel));
    }
  }

  return matchTap(tap, std::move(sentence), out, word);
}

uint32_t entryKeyOf(const api::Occurrence& occ) { return occ.lemmaEntryId != 0 ? occ.lemmaEntryId : occ.entryId; }

LookupCard cardFor(const AnalyzedSentence& sentence, const size_t word) {
  const api::AnalyzeResult& analysis = sentence.analysis;
  const api::Occurrence& occ = analysis.occurrences[sentence.words[word]];
  LookupCard out;
  out.language = sentence.language;
  out.surface = occ.word;
  out.surfaceReading = occ.reading;
  out.lemma = occ.lemma;
  out.charStart = occ.charStart;
  out.charEnd = occ.charEnd;
  // The headword's reading until the lookup gives the lemma's: the surface's only when they're the same
  // word (食べる must not show "tabesaserareta").
  if (out.headword() == occ.word) out.reading = occ.reading;
  out.entryId = occ.entryId;
  out.lemmaEntryId = entryKeyOf(occ);
  if (const api::EntryMeta* meta = analysis.metaFor(occ.entryId)) {
    out.partOfSpeech = meta->partOfSpeech;
    out.rank = meta->rank;
    out.frequency = meta->frequency;  // phase B's replaces it; without B, the bars still match the rank
  }
  // The lemma's own reading, when the server named its entry (without one, lemmaEntryId is the surface's).
  if (const api::EntryMeta* lemmaMeta = occ.lemmaEntryId != 0 ? analysis.metaFor(occ.lemmaEntryId) : nullptr) {
    if (out.reading.empty()) out.reading = lemmaMeta->reading;
    if (lemmaMeta->rank != 0) {
      out.rank = lemmaMeta->rank;
      out.frequency = lemmaMeta->frequency;
    }
  }
  // Saved: the lemma's entry first (saved 食べる shows on 食べた), then the surface's.
  if (const api::EntryState* state = analysis.stateFor(out.lemmaEntryId)) {
    out.saved = *state;
    out.savedEntryId = out.lemmaEntryId;
  } else if (const api::EntryState* surfaceState = analysis.stateFor(occ.entryId)) {
    out.saved = *surfaceState;
    out.savedEntryId = occ.entryId;
  }
  return out;
}

std::string bodyHead(const std::string_view body) {
  return std::string(text::utf8Prefix(body, config::kLoggedBodyBytes));
}

api::ApiError completeCard(api::LexiriseApi& api, LookupCard& card, std::string* unreadable,
                           std::optional<api::LookupResult>* answer) {
  card.complete = true;
  const api::ApiResponse looked = api.lookup(card.language, card.headword());
  api::LookupResult entry;
  if (!looked.ok() || api::parseLookup(looked.body, entry) != api::ParseStatus::Ok) {
    card.translationUnavailable = true;
    card.translationError = looked.ok() ? api::ApiError::Malformed : looked.error;
    if (looked.ok() && unreadable) *unreadable = bodyHead(looked.body);
    return card.translationError;
  }
  if (answer) *answer = entry;
  applyLookup(card, std::move(entry));
  return api::ApiError::None;
}

void applyLookup(LookupCard& card, api::LookupResult entry) {
  card.complete = true;
  card.translationUnavailable = false;
  card.translationError = api::ApiError::None;
  if (!entry.reading.empty()) card.reading = entry.reading;
  card.alternatives = std::move(entry.alternatives);
  card.senses = std::move(entry.senses);
  card.level = std::move(entry.level);
  if (entry.rank != 0) card.rank = entry.rank;
  if (entry.frequency > 0) card.frequency = entry.frequency;
  card.translationPending = entry.translationPending && card.senses.empty();
  if (card.partOfSpeech.empty() && !card.senses.empty()) card.partOfSpeech = card.senses.front().partOfSpeech;
}

LookupReport lookupWithLexirise(api::LexiriseApi& api, const text::TapContext& tap, LookupCard& card) {
  AnalyzedSentence sentence;
  size_t word = 0;
  LookupReport report = analyzeTap(api, tap, sentence, word);
  if (report.outcome != LookupOutcome::Card) return report;
  LookupCard out = cardFor(sentence, word);
  report.error = completeCard(api, out);  // a failure here still leaves a card: word, reading, saved
  card = std::move(out);
  return report;
}

}  // namespace lexipoint::lookup
