#pragma once

// A lookup's answer as the card shows it (popup-ui.md §1, languages.md §3): the reading in kana, the
// level as a badge, T L F K from Lexirise's proficiency. Pure; tests: test/lexirise_card.

#include <optional>
#include <string>
#include <string_view>

#include "CardModel.h"
#include "lexirise/lookup/LookupCard.h"

namespace lexipoint::card {

// The card's word. Not filled from the API in v0.1: the Kanji/Chars tab (Lexirise's breakdown needs a
// lookup per character), other forms, and sentences met before.
CardWord cardWord(const lookup::LookupCard& card);

// Where the lookup is: A until phase B has run, then B, or B′ while the server still translates.
Phase phaseOf(const lookup::LookupCard& card);

// "JLPT-N1" → "N1", "HSK-4" → "HSK 4", "HSK-7+" → "HSK 7+"; anything else: none.
std::string badgeFor(std::string_view level);

// Lexirise's proficiency (1-4 saved; 0 unknown) as T L F K, and back.
Level levelOf(const std::optional<api::EntryState>& saved);
int proficiencyOf(Level level);

}  // namespace lexipoint::card
