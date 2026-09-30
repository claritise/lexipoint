#pragma once

// The ⋯ tab's sentence preview (C3, v0.2 V6; docs/v0.2/00-overview.md "V6 design", "As built (V6)"): the sentence a
// sentence card is saved from, made shorter or longer a clause at a time before it's saved. Pure; tests:
// test/lexirise_card/SentencePreviewTest.cpp.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "CardModel.h"
#include "lexirise/settings/Settings.h"

namespace lexipoint::card {

// `text` split into clauses, each ending after a clause mark or at the text's end; the clauses joined are `text`. An
// empty text has none. The marks: ，；(U+FF0C U+FF1B), and in Japanese 、(U+3001) too (in Chinese it's the list comma,
// 苹果、香蕉); and a sentence's end, 。！？ (U+3002 U+FF01 U+FF1F), so the page's next sentence comes a clause at a
// time. Further marks and closing quotes or brackets (text::Punctuation::isCloser) after a mark stay in its clause.
std::vector<std::string_view> splitClauses(std::string_view text, Language language);

// A sentence after the word's on the page, and the page's own separator before it (text::separatorBetween: "" inside
// one token, as Chinese “好。”“走, " " where the page had a space or an nbsp, the page's 　 after ？ or ！).
struct LaterSentence {
  std::string text;
  std::string separator;
  bool operator==(const LaterSentence&) const = default;
};

// A word's sentence on the page, for a sentence card (CardSource::sentenceForSave).
struct SentenceForSave {
  Language language = Language::Japanese;
  std::string text;      // the sentence as the page has it
  size_t markStart = 0;  // the word in it, bytes
  size_t markLength = 0;
  // The page's sentences after it, to its end, each apart (its end is a clause's end, marked or not): what Longer adds.
  std::vector<LaterSentence> after;
};

// The clauses of the word's sentence and of the page after it; a window of them is shown and saved. Shorter drops the
// first clause while there's one before the word's, then the last; the word's own clause always stays. Longer puts
// back the last clause dropped, or with none dropped adds the next clause on the page (never past its end). Each
// returns false when there's nothing to do (and changes nothing).
class SentencePreview {
 public:
  explicit SentencePreview(const SentenceForSave& sentence);

  bool shorter();
  bool longer();
  // What's shown and saved: the clauses joined, without the spaces at either end, the word marked. Between sentences
  // the page's own separator (LaterSentence::separator): 彼は言った。He left. She stayed., 何だって！　もう一度言え。
  MarkedText shown() const;
  // What Longer would show; none when it has nothing to do.
  std::optional<MarkedText> longerShown() const;

 private:
  enum class Drop : uint8_t { First, Last };
  MarkedText window(size_t first, size_t last) const;  // clauses [first, last] joined and trimmed, the word marked
  std::vector<std::string> clauses_;                   // the sentence's, then the page's after it
  std::vector<std::string> separators_;  // per clause: before it, the page's separator (only a later sentence's first)
  size_t wordClause_ = 0;
  size_t wordAt_ = 0;  // the word's bytes into its clause
  size_t wordLength_ = 0;
  size_t first_ = 0;  // the window [first_, last_]
  size_t last_ = 0;
  std::vector<Drop> drops_;  // newest last: what Longer puts back
};

}  // namespace lexipoint::card
