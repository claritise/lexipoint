#pragma once

// The card bench's data (popup-ui.md §5, P4): the reference's own two books, word for word
// (reference/card-reference.html, B and FILL). Synthetic: nothing here came from the API.
// Tests: test/lexirise_card.

#include <string>
#include <vector>

#include "CardModel.h"

namespace lexipoint::card {

struct BenchToken {
  std::string text;
  int word = -1;  // the book's word index, -1: plain text
};
using BenchLine = std::vector<BenchToken>;

struct BenchBook {
  Language language = Language::Japanese;
  std::vector<BenchLine> lines;   // the sentence's page ("High on page")
  std::vector<BenchLine> filler;  // lines above it for "Low on page" (D17)
  int contextFirst = 0;           // the sentence's lines [contextFirst, contextEnd)
  int contextEnd = 0;
  int start = 0;  // the word the card opens on
  std::vector<CardWord> words;
  std::vector<Level> saved;  // per word
  int pageNumber = 84;
};

const BenchBook& benchJapanese();
const BenchBook& benchChinese();

}  // namespace lexipoint::card
