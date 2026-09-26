// v0.2 V1: whole words for a sentence Lexirise returned already refined (lookup/WholeWords.h). Synthetic answers
// shaped like the live ones in lexirise-api-notes.md ("The second analyze/text pass, measured").

#include <gtest/gtest.h>

#include "lexirise/lookup/WholeWords.h"

using lexipoint::api::AnalyzeResult;
using lexipoint::api::Occurrence;
using lexipoint::lookup::wholeWords;

namespace {

Occurrence occ(const char* word, uint32_t start, uint32_t end, uint32_t entry, uint32_t lemmaEntry = 0,
               bool wordLike = true) {
  Occurrence o;
  o.word = word;
  o.lemma = word;
  o.charStart = start;
  o.charEnd = end;
  o.entryId = entry;
  o.lemmaEntryId = lemmaEntry;
  o.wordLike = wordLike;
  return o;
}

// 他一边吃饭。 refined: 他 · 一 · 边 · 吃饭 · 。 (吃饭 with its lemma).
AnalyzeResult refined() {
  AnalyzeResult r;
  r.occurrences = {occ("他", 0, 1, 10), occ("一", 1, 2, 11), occ("边", 2, 3, 12), occ("吃饭", 3, 5, 13, 14),
                   occ("。", 5, 6, 0, 0, false)};
  r.meta[11].rank = 4;
  r.state[12].savedExpressionId = "7";
  return r;
}

// The same sentence's fast answer: 他 · 一边 · 吃饭 · 。, no lemmas.
AnalyzeResult words() {
  AnalyzeResult w;
  w.occurrences = {occ("他", 0, 1, 10), occ("一边", 1, 3, 20), occ("吃饭", 3, 5, 13), occ("。", 5, 6, 0, 0, false)};
  w.meta[20].rank = 1092;
  w.state[20].savedExpressionId = "9";
  w.morphoPending = true;  // whatever the fast answer says, the refined answer's flag is kept
  return w;
}

}  // namespace

TEST(WholeWords, AWordCutIntoPiecesIsWholeAgain) {
  const AnalyzeResult merged = wholeWords(refined(), words());
  ASSERT_EQ(merged.occurrences.size(), 4u);
  EXPECT_EQ(merged.occurrences[1].word, "一边");
  EXPECT_EQ(merged.occurrences[1].charStart, 1u);
  EXPECT_EQ(merged.occurrences[1].charEnd, 3u);
  EXPECT_EQ(merged.occurrences[1].entryId, 20u);
  EXPECT_EQ(merged.occurrences[1].lemmaEntryId, 0u);  // the fast answer has none: the card uses the entry itself
  ASSERT_NE(merged.metaFor(20), nullptr);
  EXPECT_EQ(merged.metaFor(20)->rank, 1092u);
  ASSERT_NE(merged.stateFor(20), nullptr);
  EXPECT_EQ(merged.stateFor(20)->savedExpressionId, "9");
  EXPECT_FALSE(merged.morphoPending);
}

TEST(WholeWords, AWordBothAnswersAgreeOnKeepsTheRefinedLemma) {
  const AnalyzeResult merged = wholeWords(refined(), words());
  EXPECT_EQ(merged.occurrences[2].word, "吃饭");
  EXPECT_EQ(merged.occurrences[2].lemmaEntryId, 14u);
  EXPECT_FALSE(merged.occurrences[3].wordLike);  // punctuation stays punctuation
}

TEST(WholeWords, FactsAndStatesFromBothAnswers) {
  const AnalyzeResult merged = wholeWords(refined(), words());
  ASSERT_NE(merged.metaFor(11), nullptr);  // the pieces' facts stay findable
  ASSERT_NE(merged.stateFor(12), nullptr);
  EXPECT_EQ(merged.stateFor(12)->savedExpressionId, "7");
}

TEST(WholeWords, NoWordLevelAnswerLeavesTheRefinedOne) {
  const AnalyzeResult merged = wholeWords(refined(), AnalyzeResult{});
  ASSERT_EQ(merged.occurrences.size(), 5u);
  EXPECT_EQ(merged.occurrences[1].word, "一");
}

TEST(WholeWords, AnUnrankedMergeKeepsTheRefinedPieces) {
  // 一日中雨が: the fast answer's 一日中雨 has no rank (not a dictionary word); the refined 一日中 · 雨 is right.
  AnalyzeResult pieces;
  pieces.occurrences = {occ("一日中", 0, 3, 30), occ("雨", 3, 4, 31), occ("が", 4, 5, 32)};
  AnalyzeResult fast;
  fast.occurrences = {occ("一日中雨", 0, 4, 40), occ("が", 4, 5, 32)};
  fast.meta[40].partOfSpeech = "noun";  // facts, but no rank
  const AnalyzeResult merged = wholeWords(pieces, fast);
  ASSERT_EQ(merged.occurrences.size(), 3u);
  EXPECT_EQ(merged.occurrences[0].word, "一日中");
  EXPECT_EQ(merged.occurrences[1].word, "雨");
}

TEST(WholeWords, ASplitThatCrossesTheWordKeepsTheWord) {
  // The refined pieces don't tile the word (one crosses its edge): the word-level token stands, ranked or not.
  AnalyzeResult pieces;
  pieces.occurrences = {occ("ab", 0, 2, 1), occ("c", 2, 3, 2)};
  AnalyzeResult fast;
  fast.occurrences = {occ("a", 0, 1, 3), occ("bc", 1, 3, 4)};
  const AnalyzeResult merged = wholeWords(pieces, fast);
  ASSERT_EQ(merged.occurrences.size(), 2u);
  EXPECT_EQ(merged.occurrences[0].word, "a");
  EXPECT_EQ(merged.occurrences[1].word, "bc");
}

TEST(WholeWords, TheSameSpanKeepsTheRefinedLemma) {
  // 飲んだ in both answers: the refined one names 飲む.
  AnalyzeResult pieces;
  pieces.occurrences = {occ("飲んだ", 0, 3, 50, 51)};
  pieces.occurrences[0].lemma = "飲む";
  AnalyzeResult fast;
  fast.occurrences = {occ("飲んだ", 0, 3, 50)};
  pieces.state[51] = {"8", 3, 0, {}, {}};  // 飲む saved: the word-level answer never names the lemma's entry
  const AnalyzeResult merged = wholeWords(pieces, fast);
  EXPECT_EQ(merged.occurrences[0].lemma, "飲む");
  EXPECT_EQ(merged.occurrences[0].lemmaEntryId, 51u);
  ASSERT_NE(merged.stateFor(51), nullptr);  // so a saved 飲む still shows on 飲んだ
  EXPECT_EQ(merged.stateFor(51)->proficiency, 3);
}

TEST(WholeWords, TheWordLevelAnswersStateIsTheFresher) {
  // The refined answer can be a cached one: for the same entry, the word-level answer's state wins.
  AnalyzeResult pieces = refined();
  pieces.state[10] = {"5", 1, 0, {}, {}};
  AnalyzeResult fast = words();
  fast.state[10] = {"5", 3, 0, {}, {}};
  const AnalyzeResult merged = wholeWords(pieces, fast);
  ASSERT_NE(merged.stateFor(10), nullptr);
  EXPECT_EQ(merged.stateFor(10)->proficiency, 3);
}

TEST(WholeWords, AWordUnsavedSinceTheRefinedAnswerWasCachedIsNotSaved) {
  // The refined answer (possibly cached) still says 他 (entry 10) is saved; the fresher word-level answer names 他
  // and has no state for it: it isn't saved any more. The pieces' own states still come through.
  AnalyzeResult pieces = refined();
  pieces.state[10] = {"5", 2, 0, {}, {}};
  const AnalyzeResult merged = wholeWords(pieces, words());
  EXPECT_EQ(merged.stateFor(10), nullptr);
  ASSERT_NE(merged.stateFor(12), nullptr);  // 边: not one of the word-level answer's words
}
