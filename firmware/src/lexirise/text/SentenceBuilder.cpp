#if LEXIRISE

#include "SentenceBuilder.h"

#include <Utf8.h>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::text {
namespace {

// Characters the layout carries that are never part of the text (sentence-extraction.md §2 rule 7).
bool isInvisible(const uint32_t cp) {
  return cp == 0x00AD || cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0x2060 || cp == 0xFEFF;
}

std::vector<uint32_t> decode(const std::string& utf8) {
  std::vector<uint32_t> cps;
  const auto* p = reinterpret_cast<const unsigned char*>(utf8.c_str());
  while (uint32_t cp = utf8NextCodepoint(&p)) cps.push_back(cp);
  return cps;
}

// One token of the flattened page.
struct Item {
  std::string text;           // normalised
  std::vector<uint32_t> cps;  // its codepoints
  bool lineStart = false;
  bool paragraphStart = false;
};

bool isLatinHyphen(const uint32_t cp) { return cp == '-' || cp == 0x2010; }

// Whether two neighbouring tokens were separated by a space. The layout doesn't keep that bit, so it
// is decided by script: CJK text never has spaces between characters, Latin words always do. A Latin
// word hyphenated across a line break rejoins without one.
bool needsSpace(const Item& prev, const Item& next) {
  const uint32_t left = prev.cps.back();
  const uint32_t right = next.cps.front();
  if (utf8IsCjkCodepoint(left) || utf8IsCjkCodepoint(right)) return false;
  if (next.lineStart && isLatinHyphen(left)) return false;
  return true;
}

class Builder {
 public:
  Builder(const PageModel& page, const Script script) : script_(script) {
    for (const TextLine& line : page.lines) {
      bool first = true;
      for (const std::string& token : line.tokens) {
        Item item;
        for (const uint32_t cp : decode(token)) {
          if (isInvisible(cp)) continue;
          item.cps.push_back(cp);
          utf8AppendCodepoint(cp, item.text);
        }
        item.lineStart = first;
        item.paragraphStart = first && line.startsParagraph;
        first = false;
        items_.push_back(std::move(item));
      }
    }
  }

  // Index of the tapped token in the flattened page, or -1.
  long indexOf(const PageModel& page, const TokenRef tap) const {
    if (tap.line >= page.lines.size() || tap.token >= page.lines[tap.line].tokens.size()) return -1;
    size_t index = 0;
    for (size_t l = 0; l < tap.line; l++) index += page.lines[l].tokens.size();
    return static_cast<long>(index + tap.token);
  }

  std::optional<BuiltSentence> build(const size_t tap) {
    if (items_[tap].cps.empty()) return std::nullopt;
    const size_t n = items_.size();

    // The sentence: from the last cut at or before the tap to the first cut after it.
    size_t begin = tap;
    while (begin > 0 && !cutBefore(begin)) begin--;
    size_t end = tap + 1;
    while (end < n && !cutBefore(end)) end++;
    bool truncatedLeft = begin == 0 && !items_[0].paragraphStart;
    bool truncatedRight = end == n && !sentenceEndsBefore(n);

    if (codepoints(begin, end) > config::kMaxSentenceCodepoints) {
      // Chinese "；" first (a clause end is a better cut than a word in the middle), then centre on the tap.
      applyFallbackCuts(tap, begin, end, truncatedLeft, truncatedRight);
      if (codepoints(begin, end) > config::kMaxSentenceCodepoints) {
        centre(tap, begin, end, truncatedLeft, truncatedRight);
      }
    }
    return join(begin, end, tap, truncatedLeft, truncatedRight);
  }

 private:
  bool isEmpty(const size_t i) const { return items_[i].cps.empty(); }

  // Strips trailing closers; returns the index of the last non-closer codepoint (or -1).
  long lastNonCloser(const Item& item) const {
    long i = static_cast<long>(item.cps.size()) - 1;
    while (i >= 0 && Punctuation::isCloser(item.cps[static_cast<size_t>(i)], script_)) i--;
    return i;
  }

  bool isOnlyClosers(const size_t i) const { return !isEmpty(i) && lastNonCloser(items_[i]) < 0; }

  // Does the sentence end after token i (with any closers glued to it)?
  bool endsSentence(const size_t i) const {
    if (isEmpty(i)) return false;
    const Item& item = items_[i];
    const long last = lastNonCloser(item);
    if (last < 0) return false;
    const uint32_t cp = item.cps[static_cast<size_t>(last)];
    if (Punctuation::isTerminator(cp, script_)) return true;
    if (Punctuation::isEllipsis(cp)) {
      // "…" ends a sentence when a closer follows, in this token or the next, or the paragraph ends.
      if (last + 1 < static_cast<long>(item.cps.size())) return true;
      if (i + 1 >= items_.size()) return false;  // the page ends: unknown, so not a sentence end
      return items_[i + 1].paragraphStart || isOnlyClosers(i + 1);
    }
    return false;
  }

  // Does a sentence end just before token i, counting the closers after a terminator (。」) as part of
  // the sentence they close?
  bool sentenceEndsBefore(size_t i) const {
    while (i > 0 && isOnlyClosers(i - 1)) i--;
    return i > 0 && endsSentence(i - 1);
  }

  // Does a closed quote before token i run on into the sentence (Japanese 」と)?
  bool quoteContinues(const size_t i) const {
    const Item& prev = items_[i - 1];
    if (prev.cps.empty() || !Punctuation::isCloser(prev.cps.back(), script_)) return false;
    const Item& next = items_[i];
    return Punctuation::continuesQuote(next.cps.data(), next.cps.size(), script_);
  }

  // Does a new sentence start at token i (i > 0)?
  bool cutBefore(const size_t i) const {
    if (items_[i].paragraphStart) return true;
    if (!isOnlyClosers(i) && sentenceEndsBefore(i) && !quoteContinues(i)) return true;
    // Dialogue: 」「 is a break even without a terminator.
    const Item& prev = items_[i - 1];
    if (!prev.cps.empty() && !items_[i].cps.empty() && Punctuation::isCloser(prev.cps.back(), script_) &&
        Punctuation::isOpener(items_[i].cps.front(), script_)) {
      return true;
    }
    return false;
  }

  size_t codepoints(const size_t begin, const size_t end) const {
    size_t total = 0;
    for (size_t i = begin; i < end; i++) {
      total += items_[i].cps.size();
      if (i > begin && !isEmpty(i) && !isEmpty(i - 1) && needsSpace(items_[i - 1], items_[i])) total++;
    }
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

  // Grows a window around the tap one token at a time, alternating sides, while it fits the cap.
  void centre(const size_t tap, size_t& begin, size_t& end, bool& left, bool& right) const {
    size_t lo = tap;
    size_t hi = tap + 1;
    bool growLeft = true;
    while (true) {
      const bool canLeft = lo > begin && codepoints(lo - 1, hi) <= config::kMaxSentenceCodepoints;
      const bool canRight = hi < end && codepoints(lo, hi + 1) <= config::kMaxSentenceCodepoints;
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
    long prev = -1;
    for (size_t i = begin; i < end; i++) {
      if (isEmpty(i)) continue;
      if (prev >= 0 && needsSpace(items_[static_cast<size_t>(prev)], items_[i])) out.text += ' ';
      if (i == tap) out.tapOffset = utf16Length(out.text);
      out.text += items_[i].text;
      if (i == tap) out.tapLength = utf16Length(items_[i].text);
      prev = static_cast<long>(i);
    }
    return out;
  }

  Script script_;
  std::vector<Item> items_;
};

}  // namespace

uint32_t utf16Length(const std::string& utf8) {
  uint32_t units = 0;
  for (const uint32_t cp : decode(utf8)) units += cp > 0xFFFF ? 2 : 1;
  return units;
}

std::optional<BuiltSentence> buildSentence(const PageModel& page, const TokenRef tap, const Script script) {
  Builder builder(page, script);
  const long index = builder.indexOf(page, tap);
  if (index < 0) return std::nullopt;
  return builder.build(static_cast<size_t>(index));
}

}  // namespace lexipoint::text

#endif  // LEXIRISE
