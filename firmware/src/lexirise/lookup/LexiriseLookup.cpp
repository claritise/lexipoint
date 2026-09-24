#if LEXIRISE

#include "LexiriseLookup.h"

#include "Match.h"

namespace lexipoint::lookup {

LookupReport lookupWithLexirise(api::LexiriseApi& api, const text::TapContext& tap, LookupCard& card) {
  LookupReport report;
  if (!tap.sentence || !tap.language.language) return report;  // not a Lexirise lookup
  const Language language = *tap.language.language;

  // ① analyze the sentence
  const api::ApiResponse analyzed = api.analyze(language, tap.sentence->text);
  if (!analyzed.ok()) {
    report.error = analyzed.error;
    return report;
  }
  api::AnalyzeResult analysis;
  if (api::parseAnalyze(analyzed.body, analysis) != api::ParseStatus::Ok) {
    report.error = api::ApiError::Malformed;
    return report;
  }

  // ② the occurrence under the tap
  const auto match = matchOccurrence(analysis.occurrences, tap.sentence->tapOffset);
  if (!match) {
    report.outcome = LookupOutcome::NotFound;
    return report;
  }
  const api::Occurrence& occ = analysis.occurrences[*match];
  LookupCard out;
  out.language = language;
  out.surface = occ.word;
  out.surfaceReading = occ.reading;
  out.lemma = occ.lemma;
  // The headword's reading until the lookup gives the lemma's: the surface's only when they're the same
  // word (食べる must not show "tabesaserareta").
  if (out.headword() == occ.word) out.reading = occ.reading;
  out.entryId = occ.entryId;
  out.lemmaEntryId = occ.lemmaEntryId != 0 ? occ.lemmaEntryId : occ.entryId;
  if (const api::EntryMeta* meta = analysis.metaFor(occ.entryId)) out.partOfSpeech = meta->partOfSpeech;
  // The lemma's own reading, when the server named its entry (without one, lemmaEntryId is the surface's).
  if (const api::EntryMeta* lemmaMeta = occ.lemmaEntryId != 0 ? analysis.metaFor(occ.lemmaEntryId) : nullptr;
      lemmaMeta && out.reading.empty()) {
    out.reading = lemmaMeta->reading;
  }
  // Saved: the lemma's entry first (saved 食べる shows on 食べた), then the surface's.
  if (const api::EntryState* state = analysis.stateFor(out.lemmaEntryId)) {
    out.saved = *state;
  } else if (const api::EntryState* surfaceState = analysis.stateFor(occ.entryId)) {
    out.saved = *surfaceState;
  }

  // ③ the lemma's dictionary entry (phase B). A failure here still leaves a card: word, reading, saved.
  const api::ApiResponse looked = api.lookup(language, out.headword());
  api::LookupResult entry;
  if (looked.ok() && api::parseLookup(looked.body, entry) == api::ParseStatus::Ok) {
    if (!entry.reading.empty()) out.reading = entry.reading;
    out.senses = std::move(entry.senses);
    out.level = std::move(entry.level);
    out.translationPending = entry.translationPending && out.senses.empty();
    if (out.partOfSpeech.empty() && !out.senses.empty()) out.partOfSpeech = out.senses.front().partOfSpeech;
  } else {
    out.translationUnavailable = true;
    report.error = looked.ok() ? api::ApiError::Malformed : looked.error;
  }
  card = std::move(out);
  report.outcome = LookupOutcome::Card;
  return report;
}

}  // namespace lexipoint::lookup

#endif  // LEXIRISE
