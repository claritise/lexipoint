// lookup-flow.md §5 ②: which analyzed occurrence a tap is on.

#include <gtest/gtest.h>

#include "lexirise/lookup/Match.h"

using lexipoint::api::Occurrence;
using lexipoint::lookup::matchOccurrence;

namespace {

Occurrence occ(const uint32_t start, const uint32_t end, const bool wordLike = true) {
  Occurrence o;
  o.charStart = start;
  o.charEnd = end;
  o.wordLike = wordLike;
  return o;
}

}  // namespace

TEST(Match, TheOccurrenceCoveringTheTap) {
  // 食べさせられた / を / 見た: a tap anywhere inside a word is that word (さ at 2 → 食べさせられた).
  const std::vector<Occurrence> s = {occ(0, 7), occ(7, 8), occ(8, 10)};
  EXPECT_EQ(matchOccurrence(s, 0), 0u);
  EXPECT_EQ(matchOccurrence(s, 2), 0u);
  EXPECT_EQ(matchOccurrence(s, 6), 0u);
  EXPECT_EQ(matchOccurrence(s, 7), 1u);  // a particle is a word too
  EXPECT_EQ(matchOccurrence(s, 9), 2u);
}

TEST(Match, PunctuationTakesTheNearestWord) {
  // 彼(0,1) 、(1,2) 東京(2,4) 。(4,5)
  const std::vector<Occurrence> s = {occ(0, 1), occ(1, 2, false), occ(2, 4), occ(4, 5, false)};
  EXPECT_EQ(matchOccurrence(s, 1), 0u);  // tie between 彼 and 東京: the earlier
  EXPECT_EQ(matchOccurrence(s, 4), 2u);  // only a word before
  EXPECT_EQ(matchOccurrence(s, 9), 2u);  // past the end
  const std::vector<Occurrence> gap = {occ(0, 1), occ(1, 4, false), occ(4, 5)};
  EXPECT_EQ(matchOccurrence(gap, 3), 2u);  // closer to the word after
  EXPECT_EQ(matchOccurrence(gap, 2), 0u);  // equally close: the earlier
  const std::vector<Occurrence> leading = {occ(0, 1, false), occ(1, 3)};
  EXPECT_EQ(matchOccurrence(leading, 0), 1u);  // only a word after (「 before a word)
}

TEST(Match, NoWordAtAll) {
  EXPECT_FALSE(matchOccurrence({}, 0));
  EXPECT_FALSE(matchOccurrence({occ(0, 1, false), occ(1, 2, false)}, 0));
}
