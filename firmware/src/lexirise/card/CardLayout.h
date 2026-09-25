#pragma once

// The card and its expanded view, laid out exactly as popup-ui.md §1.1 / reference/card-reference.html:
// the model and state in, draw commands and touch targets out. Every number comes from CardMetrics.h;
// line boxes are the device fonts' own (CSS line-height: normal) unless §1.1 sets one. Pure; the device
// paints the result (CardPainter). Tests: test/lexirise_card.

#include <string>
#include <vector>

#include "CardModel.h"
#include "DisplayList.h"

namespace lexipoint::card {

DisplayList layoutCard(const CardWord& word, const CardState& state, const TextMetrics& metrics,
                       const CardStrings& strings = {});

// Greedy line breaking for the card's text: at spaces for Latin text, between any two characters for
// CJK. With maxLines > 0, the last line is cut with … when the text doesn't fit.
// firstIndent: the first line is that much narrower (the Meaning tab's bold "1.").
// A single unit wider than the line (a long Latin word) is cut with … wherever it falls. sourceBytes, when
// given, gets each line's length in `text` before any cut (the bytes it covers, for marking a span).
std::vector<std::string> wrapText(const TextMetrics& metrics, Font font, const std::string& text, int width,
                                  int maxLines, int firstIndent = 0, std::vector<size_t>* sourceBytes = nullptr);

// The card-view meaning line (popup-ui.md §1): the first sense, then "; " and the second when both fit
// in kMeaningMaxLines.
std::string meaningText(const std::vector<std::string>& senses, const TextMetrics& metrics, int width);

// "#29,774"
std::string formatRank(uint32_t rank);

}  // namespace lexipoint::card
