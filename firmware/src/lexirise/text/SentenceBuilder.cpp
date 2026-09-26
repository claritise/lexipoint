#if LEXIRISE

#include "SentenceBuilder.h"

#include <Utf8.h>

#include <algorithm>

#include "CharClass.h"
#include "Utf8Prefix.h"
#include "Utf8Units.h"
#include "lexirise/LexiriseConfig.h"

namespace lexipoint::text {
namespace {

std::vector<uint32_t> decode(const std::string& utf8) {
  std::vector<uint32_t> cps;
  const auto* p = reinterpret_cast<const unsigned char*>(utf8.c_str());
  while (uint32_t cp = utf8NextCodepoint(&p)) cps.push_back(cp);
  return cps;
}

// What a token was that has no text of its own.
enum class Spacing { None, Latin, Ideographic };

// One piece of text in reading order. Usually one laid-out token; a token that holds a sentence break
// inside it (Chinese “好。”“走 is a single token, since CrossPoint never splits two non-CJK characters)
// becomes several pieces. A token that is only spacing (&nbsp;, the full-width 　) becomes an empty
// piece that keeps the line/paragraph flags and hands its spacing to the next piece.
struct Item {
  std::vector<uint32_t> cps;
  std::vector<uint32_t> tokenCps;  // each of cps' index among the token's codepoints (invisible ones counted)
  TokenRef ref;                    // the laid-out token it came from
  size_t token = 0;                // the same, flattened
  bool lineStart = false;
  bool paragraphStart = false;
  Spacing spacingBefore = Spacing::None;
};

class Builder {
 public:
  Builder(const PageModel& page, const Script script) : script_(script) {
    size_t token = 0;
    Spacing pending = Spacing::None;
    for (size_t l = 0; l < page.lines.size(); l++) {
      const TextLine& line = page.lines[l];
      bool first = true;
      for (size_t t = 0; t < line.tokens.size(); t++) {
        std::vector<uint32_t> cps;
        std::vector<uint32_t> tokenCps;
        bool latinSpaces = true;
        bool ideographicSpaces = true;
        uint32_t index = 0;
        for (const uint32_t cp : decode(line.tokens[t])) {
          const uint32_t at = index++;
          if (chars::isInvisible(cp)) continue;
          latinSpaces = latinSpaces && chars::isLatinSpace(cp);
          ideographicSpaces = ideographicSpaces && cp == chars::kIdeographicSpace;
          cps.push_back(cp);
          tokenCps.push_back(at);
        }
        if (!cps.empty() && (latinSpaces || ideographicSpaces)) {
          pending = latinSpaces ? Spacing::Latin : Spacing::Ideographic;
          cps.clear();
          tokenCps.clear();
        }
        const bool hasText = !cps.empty();
        addToken(std::move(cps), std::move(tokenCps), {l, t}, token, first, first && line.startsParagraph,
                 hasText ? pending : Spacing::None);
        if (hasText) pending = Spacing::None;
        first = false;
        token++;
      }
    }
  }

  // The piece to centre on for a tapped token: its first piece with a letter in it (else its first).
  long pieceFor(const PageModel& page, const TokenRef tap) const {
    if (tap.line >= page.lines.size() || tap.token >= page.lines[tap.line].tokens.size()) return -1;
    size_t token = tap.token;
    for (size_t l = 0; l < tap.line; l++) token += page.lines[l].tokens.size();
    long first = -1;
    for (size_t i = 0; i < items_.size(); i++) {
      if (items_[i].token != token) continue;
      if (first < 0) first = static_cast<long>(i);
      for (const uint32_t cp : items_[i].cps) {
        if (!chars::isPunctuationLike(cp)) return static_cast<long>(i);
      }
    }
    return first;
  }

  // The piece holding a character (its token and codepoint), or -1.
  long pieceWith(const SentenceChar& c) const {
    for (size_t i = 0; i < items_.size(); i++) {
      const Item& item = items_[i];
      if (item.ref.line == c.token.line && item.ref.token == c.token.token &&
          std::find(item.tokenCps.begin(), item.tokenCps.end(), c.codepoint) != item.tokenCps.end()) {
        return static_cast<long>(i);
      }
    }
    return -1;
  }

  // The first piece with text after the one holding `last` (a sentence's last character): where the next
  // sentence starts. -1 when there's none on the page.
  long pieceAfter(const SentenceChar& last) const {
    const long i = pieceWith(last);
    if (i < 0) return -1;
    const size_t next = textAfter(static_cast<size_t>(i));
    return next < items_.size() ? static_cast<long>(next) : -1;
  }

  // The sentence from `start` on, for the one after a sentence (buildSentenceAfter). Usually a sentence starts
  // there; after a long one the cap cut, it doesn't, and building around `start` would reach back over what
  // was just shown: then it starts at `start` (cut on the left) and runs right up to the cap.
  std::optional<BuiltSentence> buildFrom(const size_t start) const {
    if (cutBefore(start)) return build(start);
    const size_t n = items_.size();
    size_t end = start + 1;
    while (end < n && !cutBefore(end)) end++;
    bool truncatedRight = end == n && !sentenceEndsBefore(n);
    if (length(start, end) > config::kMaxSentenceUnits) {
      // As build() does: a clause end (Chinese "；") is a better cut than the middle of a word.
      for (size_t i = start; i + 1 < end; i++) {
        if (endsWithFallbackCut(i)) {
          end = i + 1;
          truncatedRight = true;
          break;
        }
      }
    }
    size_t hi = start + 1;
    while (hi < end && length(start, hi + 1) <= config::kMaxSentenceUnits) hi++;
    if (hi < end) truncatedRight = true;
    return join(start, hi, start, /*left=*/true, truncatedRight);
  }

  std::optional<BuiltSentence> build(const size_t tap) const {
    if (isEmpty(tap)) return std::nullopt;
    const size_t n = items_.size();

    // The sentence: from the last cut at or before the tap to the first cut after it.
    size_t begin = tap;
    while (begin > 0 && !cutBefore(begin)) begin--;
    size_t end = tap + 1;
    while (end < n && !cutBefore(end)) end++;
    bool truncatedLeft = begin == 0 && !items_[0].paragraphStart;
    bool truncatedRight = end == n && !sentenceEndsBefore(n);

    if (length(begin, end) > config::kMaxSentenceUnits) {
      // Chinese "；" first (a clause end is a better cut than a word in the middle), then centre on the tap.
      applyFallbackCuts(tap, begin, end, truncatedLeft, truncatedRight);
      if (length(begin, end) > config::kMaxSentenceUnits) {
        centre(tap, begin, end, truncatedLeft, truncatedRight);
      }
    }
    return join(begin, end, tap, truncatedLeft, truncatedRight);
  }

 private:
  bool isTerminator(const uint32_t cp) const { return Punctuation::isTerminator(cp, script_); }
  bool isCloser(const uint32_t cp) const { return Punctuation::isCloser(cp, script_); }

  // --- splitting a token into pieces ---

  // Where a sentence (or a line of dialogue) ends inside a token. CrossPoint glues punctuation onto the
  // character before it, so a token can be 好。”“走 (two sentences) but also か！？」と, 3.50 or
  // example.com (one). So: a run of terminators and closers stays together; after it, a closed run
  // (。” / ！？」) ends the sentence unless a Japanese quotative follows, and a bare run (。 / .) only when
  // an opener or a CJK character follows it (never a digit or a letter after a dot: 3.50, ３．５,
  // example.com). A closing quote followed by an opening one (”“ / 」「) is a break on its own.
  std::vector<size_t> internalCuts(const std::vector<uint32_t>& cps) const {
    std::vector<size_t> cuts;
    for (size_t k = 1; k < cps.size(); k++) {
      const uint32_t after = cps[k];
      // Never inside a run: closers stay with what they close, terminators with each other (！？, ...).
      if (isCloser(after) || isTerminator(after) || Punctuation::isEllipsis(after)) continue;
      bool cut = Punctuation::isQuoteCloser(cps[k - 1], script_) && Punctuation::isQuoteOpener(after, script_);
      if (!cut) {
        size_t j = k;  // back over closers, to the character they follow
        while (j > 0 && isCloser(cps[j - 1])) j--;
        const bool closed = j < k;
        if (j > 0) {
          const uint32_t end = cps[j - 1];
          const bool terminated = isTerminator(end) || (closed && Punctuation::isEllipsis(end));
          const bool dotInWord = !closed && Punctuation::isDot(end) && chars::isAlnum(after);
          const bool nextStarts = Punctuation::isOpener(after, script_) || utf8IsCjkCodepoint(after);
          cut = terminated && !dotInWord && (closed || nextStarts);
          if (cut && (closed || Punctuation::isQuestionOrExclamation(end)) &&
              Punctuation::continuesQuote(cps.data() + k, cps.size() - k, script_)) {
            cut = false;
          }
        }
      }
      if (cut) cuts.push_back(k);
    }
    return cuts;
  }

  void addToken(std::vector<uint32_t> cps, std::vector<uint32_t> tokenCps, const TokenRef ref, const size_t token,
                const bool lineStart, const bool paragraphStart, const Spacing spacingBefore) {
    std::vector<size_t> cuts = internalCuts(cps);
    cuts.push_back(cps.size());
    size_t from = 0;
    bool first = true;
    for (const size_t to : cuts) {
      Item item;
      item.cps.assign(cps.begin() + static_cast<long>(from), cps.begin() + static_cast<long>(to));
      item.tokenCps.assign(tokenCps.begin() + static_cast<long>(from), tokenCps.begin() + static_cast<long>(to));
      item.ref = ref;
      item.token = token;
      item.lineStart = first && lineStart;
      item.paragraphStart = first && paragraphStart;
      item.spacingBefore = first ? spacingBefore : Spacing::None;
      items_.push_back(std::move(item));
      first = false;
      from = to;
    }
  }

  // --- sentence boundaries between pieces (empty pieces are looked past) ---

  bool isEmpty(const size_t i) const { return items_[i].cps.empty(); }

  // The nearest piece with text before i (or -1), and after i (or items_.size()).
  long textBefore(size_t i) const {
    while (i > 0) {
      if (!isEmpty(--i)) return static_cast<long>(i);
    }
    return -1;
  }
  size_t textAfter(size_t i) const {
    while (++i < items_.size()) {
      if (!isEmpty(i)) return i;
    }
    return items_.size();
  }

  bool paragraphStartsIn(const size_t from, const size_t to) const {
    for (size_t i = from; i <= to && i < items_.size(); i++) {
      if (items_[i].paragraphStart) return true;
    }
    return false;
  }

  // The index of the last codepoint that isn't a closer, or -1.
  long lastNonCloser(const Item& item) const {
    long i = static_cast<long>(item.cps.size()) - 1;
    while (i >= 0 && isCloser(item.cps[static_cast<size_t>(i)])) i--;
    return i;
  }

  bool isOnlyClosers(const size_t i) const { return !isEmpty(i) && lastNonCloser(items_[i]) < 0; }

  // Does the sentence end after piece i (with any closers in it)?
  bool endsSentence(const size_t i) const {
    if (isEmpty(i)) return false;
    const Item& item = items_[i];
    const long last = lastNonCloser(item);
    if (last < 0) return false;
    const auto at = static_cast<size_t>(last);
    const uint32_t cp = item.cps[at];
    const bool closed = at + 1 < item.cps.size();
    const size_t next = textAfter(i);
    const bool hasNext = next < items_.size();
    // Latin "..." is an ellipsis, not three full stops.
    const bool dots = cp == '.' && at > 0 && item.cps[at - 1] == '.';
    if (isTerminator(cp) && !dots) return !(Punctuation::isDot(cp) && !closed && dotInWord(i, next));
    if (Punctuation::isEllipsis(cp) || dots) {
      // "…" ends a sentence when a closer follows, in this piece or the next, or the paragraph ends.
      if (closed) return true;
      if (!hasNext) return false;  // the page ends: unknown, so not a sentence end
      return paragraphStartsIn(i + 1, next) || isOnlyClosers(next);
    }
    return false;
  }

  // A dot that belongs to a number or an abbreviation rather than ending a sentence: straight before a
  // digit or letter with no space between (３．｜５), or after a single letter that follows another
  // letter-dot pair (Ｕ．Ｓ．Ａ．｜に). Across Latin words there was a space, so "late. The" still ends.
  bool dotInWord(const size_t i, const size_t next) const {
    const Item& item = items_[i];
    if (next < items_.size() && between(item, items_[next]) == Spacing::None &&
        chars::isAlnum(items_[next].cps.front())) {
      return true;
    }
    const auto isLetterDot = [](const Item& piece) {
      return piece.cps.size() == 2 && chars::isAlnum(piece.cps[0]) && Punctuation::isDot(piece.cps[1]);
    };
    const long prev = textBefore(i);
    const bool spacedAfter = next < items_.size() && between(item, items_[next]) != Spacing::None;
    return isLetterDot(item) && !spacedAfter && prev >= 0 && isLetterDot(items_[static_cast<size_t>(prev)]) &&
           between(items_[static_cast<size_t>(prev)], item) == Spacing::None;
  }

  // Does a sentence end just before piece i, counting the closers after a terminator (。」) as part of
  // the sentence they close?
  bool sentenceEndsBefore(const size_t i) const {
    long j = textBefore(i);
    while (j >= 0 && isOnlyClosers(static_cast<size_t>(j))) j = textBefore(static_cast<size_t>(j));
    return j >= 0 && endsSentence(static_cast<size_t>(j));
  }

  // Does a quote before piece i run on into the sentence (Japanese 」と, ！？と, 」って)?
  bool quoteContinues(const size_t i) const {
    const long prev = textBefore(i);
    if (prev < 0 || isEmpty(i)) return false;
    const uint32_t last = items_[static_cast<size_t>(prev)].cps.back();
    if (!isCloser(last) && !Punctuation::isQuestionOrExclamation(last)) return false;
    const Item& next = items_[i];
    return Punctuation::continuesQuote(next.cps.data(), next.cps.size(), script_);
  }

  // Does a new sentence start at piece i (i > 0)? An empty piece only starts one at a paragraph.
  bool cutBefore(const size_t i) const {
    if (items_[i].paragraphStart) return true;
    if (isEmpty(i)) return false;
    if (!isOnlyClosers(i) && sentenceEndsBefore(i) && !quoteContinues(i)) return true;
    // Dialogue: 」「 / ”“ is a break even without a terminator (not 』（ or 】【).
    const long prev = textBefore(i);
    return prev >= 0 && Punctuation::isQuoteCloser(items_[static_cast<size_t>(prev)].cps.back(), script_) &&
           Punctuation::isQuoteOpener(items_[i].cps.front(), script_);
  }

  // --- joining ---

  // What goes between two neighbouring pieces. The layout doesn't keep spaces, so they're decided by
  // script: CJK text never has spaces between characters, Latin words always do (not before , . ! ? ) ”
  // or after ( “); a Latin word hyphenated across a line break rejoins without one. A spacing token
  // (&nbsp;, 　) always shows as what it was.
  static Spacing between(const Item& prev, const Item& next) {
    if (next.spacingBefore != Spacing::None) return next.spacingBefore;
    if (next.token == prev.token) return Spacing::None;  // pieces of one token were never apart
    const uint32_t left = prev.cps.back();
    const uint32_t right = next.cps.front();
    if (utf8IsCjkCodepoint(left) || utf8IsCjkCodepoint(right)) return Spacing::None;
    if (next.lineStart && chars::isLatinHyphen(left)) return Spacing::None;
    // ’ closes a quote (go’) and ends a word (Dickens’s, don’t), but also starts one (’n’, ’tis): a piece
    // starting with ’ attaches unless it is a word of its own.
    const bool apostropheWord = right == chars::kRightSingleQuote && next.cps.size() > 1 &&
                                !chars::isPunctuationLike(next.cps[1]) &&
                                !(chars::isAlnum(left) && chars::isContractionSuffix(next.cps.data(), next.cps.size()));
    if ((chars::attachesLeft(right) && !apostropheWord) || chars::attachesRight(left)) return Spacing::None;
    return Spacing::Latin;
  }

  // Calls fn(index, spacing) for each piece with text in [begin, end), exactly as they are joined (the
  // first gets no spacing: a sentence never starts with a space).
  template <class Fn>
  void forEachJoined(const size_t begin, const size_t end, Fn&& fn) const {
    const Item* prev = nullptr;
    for (size_t i = begin; i < end; i++) {
      if (isEmpty(i)) continue;
      fn(i, prev != nullptr ? between(*prev, items_[i]) : Spacing::None);
      prev = &items_[i];
    }
  }

  // The joined length (UTF-16 units): the cap and the offsets share one measure.
  size_t length(const size_t begin, const size_t end) const {
    size_t total = 0;
    forEachJoined(begin, end, [&](const size_t i, const Spacing spacing) {
      total += spacing == Spacing::None ? 0 : 1;
      for (const uint32_t cp : items_[i].cps) total += utf16Units(cp);
    });
    return total;
  }

  bool endsWithFallbackCut(const size_t i) const {
    return !isEmpty(i) && Punctuation::isFallbackCut(items_[i].cps.back(), script_);
  }

  void applyFallbackCuts(const size_t tap, size_t& begin, size_t& end, bool& left, bool& right) const {
    for (size_t i = tap; i > begin; i--) {
      if (endsWithFallbackCut(i - 1)) {
        begin = i;
        left = true;
        break;
      }
    }
    for (size_t i = tap; i + 1 < end; i++) {
      if (endsWithFallbackCut(i)) {
        end = i + 1;
        right = true;
        break;
      }
    }
  }

  // Grows a window around the tap one piece at a time, alternating sides, while it fits the cap.
  void centre(const size_t tap, size_t& begin, size_t& end, bool& left, bool& right) const {
    size_t lo = tap;
    size_t hi = tap + 1;
    bool growLeft = true;
    while (true) {
      const bool canLeft = lo > begin && length(lo - 1, hi) <= config::kMaxSentenceUnits;
      const bool canRight = hi < end && length(lo, hi + 1) <= config::kMaxSentenceUnits;
      if (!canLeft && !canRight) break;
      if ((growLeft && canLeft) || !canRight) {
        lo--;
      } else {
        hi++;
      }
      growLeft = !growLeft;
    }
    if (lo > begin) left = true;
    if (hi < end) right = true;
    begin = lo;
    end = hi;
  }

  BuiltSentence join(const size_t begin, const size_t end, const size_t tap, const bool left, const bool right) const {
    BuiltSentence out;
    out.truncatedLeft = left;
    out.truncatedRight = right;
    uint32_t at = 0;
    forEachJoined(begin, end, [&](const size_t i, const Spacing spacing) {
      if (spacing != Spacing::None) {
        utf8AppendCodepoint(spacing == Spacing::Ideographic ? chars::kIdeographicSpace : ' ', out.text);
        at++;
      }
      if (i == tap) out.tapOffset = at;
      const Item& item = items_[i];
      for (size_t k = 0; k < item.cps.size(); k++) {
        const uint32_t cp = item.cps[k];
        utf8AppendCodepoint(cp, out.text);
        out.chars.push_back({at, static_cast<uint8_t>(utf16Units(cp)), item.ref, item.tokenCps[k]});
        at += utf16Units(cp);
      }
      if (i == tap) out.tapLength = at - out.tapOffset;
    });
    return out;
  }

  Script script_;
  std::vector<Item> items_;
};

}  // namespace

std::optional<BuiltSentence> buildSentenceAfter(const PageModel& page, const BuiltSentence& current,
                                                const Script script) {
  if (current.chars.empty()) return std::nullopt;
  const Builder builder(page, script);
  const long piece = builder.pieceAfter(current.chars.back());
  if (piece < 0) return std::nullopt;
  return builder.buildFrom(static_cast<size_t>(piece));
}

std::optional<BuiltSentence> buildSentenceFrom(const PageModel& page, const SentenceChar& first, const Script script) {
  const Builder builder(page, script);
  const long piece = builder.pieceWith(first);
  if (piece < 0) return std::nullopt;
  return builder.buildFrom(static_cast<size_t>(piece));
}

// Every character of the view counts (NUL too: its length decides, not a terminator); a run of stray continuation
// bytes, a broken character, counts as one unit.
uint32_t utf16Length(std::string_view utf8) {
  uint32_t total = 0;
  while (!utf8.empty()) {
    const std::string_view character = utf8FirstChars(utf8, 1);
    total += utf16Units(utf8FirstCodepoint(character));
    utf8.remove_prefix(character.size());
  }
  return total;
}

std::optional<BuiltSentence> buildSentence(const PageModel& page, const TokenRef tap, const Script script) {
  const Builder builder(page, script);
  const long piece = builder.pieceFor(page, tap);
  if (piece < 0) return std::nullopt;
  return builder.build(static_cast<size_t>(piece));
}

}  // namespace lexipoint::text

#endif  // LEXIRISE
