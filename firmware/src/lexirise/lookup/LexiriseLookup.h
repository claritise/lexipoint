#pragma once

// The Lexirise side of a lookup (lookup-flow.md §5): analyze the sentence, match the tap, look up the
// lemma. Pure: the network is behind api::LexiriseApi (LexiriseService on the device, a fake in tests).
// Tests: test/lexirise_lookup.

#include <string_view>
#include <vector>

#include "LookupCard.h"
#include "lexirise/api/LexiriseApi.h"
#include "lexirise/text/TapContext.h"

namespace lexipoint::lookup {

enum class LookupOutcome {
  Card,         // `card` is filled (phase B may still be pending or offline)
  NotFound,     // Lexirise answered: no word at the tap. Final (no StarDict)
  Unavailable,  // no answer (no key, no WiFi, network, 401/429/5xx, bad response): StarDict runs
};

struct LookupReport {
  LookupOutcome outcome = LookupOutcome::Unavailable;
  api::ApiError error = api::ApiError::None;  // why it was Unavailable (for the log and, in P6, the UI)
};

// The tapped sentence as Lexirise analyzed it: every word in it can become a card without asking again
// (lookup-flow.md §6: Left/Right re-run only dictionary/lookup).
struct AnalyzedSentence {
  Language language = Language::Japanese;
  api::AnalyzeResult analysis;
  std::vector<size_t> words;  // the word-like occurrences, in order (indices into analysis.occurrences)
};

// Whether a tap goes to Lexirise at all (the card opens): a sentence and a language to send, and Lexirise
// usable for the book (on, a key, the language not switched off: lookup::lexiriseUsable). Otherwise word
// select goes straight to StarDict, with no card flashing up first.
inline bool asksLexirise(const text::TapContext& tap, const bool usable) {
  return usable && tap.sentence && tap.language.language;
}

// ① analyze the sentence and ② match the tap. On Card, `out` holds the sentence and `word` the tapped
// word's index in out.words; NotFound: no word in it; Unavailable: no answer (report.error says why).
// Needs a sentence and a language to send (TapContext): without them Lexirise isn't asked at all.
LookupReport analyzeTap(api::LexiriseApi& api, const text::TapContext& tap, AnalyzedSentence& out, size_t& word);

// Phase A: the card for out.words[word] from the analysis alone: the word, reading, POS, saved state.
LookupCard cardFor(const AnalyzedSentence& sentence, size_t word);

// ③ Phase B: the headword's dictionary entry into `card` (meaning, level, rank, the lemma's reading).
// A failure still leaves the card, with translationUnavailable set; the error is returned.
api::ApiError completeCard(api::LexiriseApi& api, LookupCard& card);

// All three at once, blocking (the tests' one-call form; the card runs them one per loop pass).
LookupReport lookupWithLexirise(api::LexiriseApi& api, const text::TapContext& tap, LookupCard& card);

}  // namespace lexipoint::lookup
