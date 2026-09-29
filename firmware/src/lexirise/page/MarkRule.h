#pragma once

// V9a's one rule for which mark a word carries at its level (page-annotations.md §2 A1, "V9a decisions"): the page's
// marks (PageMarks.h) and A3's stepping on a card (card::CardController) both ask it. Header-only, no dependencies.

#include <cstdint>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::page {

enum class Mark : uint8_t {
  None,      // fresh, known, ignored, suspended; punctuation
  New,       // not saved, or saved at level 0: solid underline
  Learning,  // tracked or learning (levels 1-2): dotted underline
};

// `proficiency`: Lexirise's 0-4 (the card's T L F K are 1-4); `saved` false: not saved.
constexpr Mark markForLevel(const bool saved, const int proficiency) {
  if (!saved || proficiency <= config::kMarkNewMaxLevel) return Mark::New;
  if (proficiency <= config::kMarkLearningMaxLevel) return Mark::Learning;
  return Mark::None;
}

// The underline's top under the baseline for a page font whose CJK advance is `em` px: CJK ink ends about
// kMarkInkBelowBaselinePercent of an em under the baseline, and the mark sits kMarkGapBelowInk under that (6 px at the
// reader's default 14 pt, 29 px em; 7 px at 18 pt).
constexpr int markBelowBaseline(const int em) {
  return (em * config::kMarkInkBelowBaselinePercent + 50) / 100 + config::kMarkGapBelowInk;
}

}  // namespace lexipoint::page
