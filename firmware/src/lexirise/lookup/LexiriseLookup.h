#pragma once

// The Lexirise side of a lookup (lookup-flow.md §5): analyze the sentence, match the tap, look up the
// lemma. Pure: the network is behind api::LexiriseApi (LexiriseService on the device, a fake in tests).
// Tests: test/lexirise_lookup.

#include <string_view>

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

// Needs a sentence and a language to send (TapContext): without them Lexirise isn't asked at all.
LookupReport lookupWithLexirise(api::LexiriseApi& api, const text::TapContext& tap, LookupCard& card);

// What word select does after asking Lexirise (lookup-flow.md §4): a Lexirise answer is final (card or
// not found); with none, StarDict runs if a dictionary is set, else "No dictionary set".
enum class ChainStep { ShowCard, ShowNotFound, RunStarDict, NoDictionary };
inline ChainStep chainStep(const LookupOutcome lexirise, const bool starDictSet) {
  switch (lexirise) {
    case LookupOutcome::Card:
      return ChainStep::ShowCard;
    case LookupOutcome::NotFound:
      return ChainStep::ShowNotFound;
    case LookupOutcome::Unavailable:
      break;
  }
  return starDictSet ? ChainStep::RunStarDict : ChainStep::NoDictionary;
}

}  // namespace lexipoint::lookup
