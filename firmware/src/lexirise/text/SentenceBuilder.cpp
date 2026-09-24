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

// A Latin space the layout kept as a token of its own (&nbsp;): it only means "a space here".
bool isLatinSpace(const uint32_t cp) { return cp == ' ' || cp == 0x00A0 || cp == 0x202F; }

bool isLatinHyphen(const uint32_t cp) { return cp == '-' || cp == 0x2010; }

// Punctuation, symbols and spaces: a tap on a token picks its first piece with a character that isn't
// one of these.
bool isPunctuationLike(const uint32_t cp) {
  if (cp < 0x80) return !((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'));
  return (cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x3004) || (cp >= 0x3008 && cp <= 0x3011) ||
         (cp >= 0x3014 && cp <= 0x301F) || cp == 0x30FB || (cp >= 0xFF01 && cp <= 0xFF0F) ||
         (cp >= 0xFF1A && cp <= 0xFF20) || (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65) ||
         cp == 0x00A0 || cp == 0x00AB || cp == 0x00BB;
}

std::vector<uint32_t> decode(const std::string& utf8) {
  std::vector<uint32_t> cps;
  const auto* p = reinterpret_cast<const unsigned char*>(utf8.c_str());
  while (uint32_t cp = utf8NextCodepoint(&p)) cps.push_back(cp);
  return cps;
}

// One piece of text in reading order. Usually one laid-out token; a token that holds a sentence break
// inside it (Chinese “好。”“走 is a single token, since CrossPoint never splits two non-CJK characters)
// becomes several pieces.
struct Item {
  std::vector<uint32_t> cps;
  size_t token = 0;        // the laid-out token it came from (flattened index)
  bool lineStart = false;  // first piece of the first token of a line
  bool paragraphStart = false;
  bool spaceBefore = false;  // an explicit space token preceded it
};

class Builder {
 public:
  Builder(const PageModel& page, const Script script) : script_(script) {
    size_t token = 0;
    bool pendingSpace = false;
    for (const TextLine& line : page.lines) {
      bool first = true;
      for (const std::string& text : line.tokens) {
        std::vector<uint32_t> cps;
        bool onlySpaces = true;
        for (const uint32_t cp : decode(text)) {
          if (isInvisible(cp)) continue;
          onlySpaces = onlySpaces && isLatinSpace(cp);
          cps.push_back(cp);
        }
        if (!cps.empty() && onlySpaces) {
          cps.clear();  // &nbsp;: no text of its own, just a space before what follows
          pendingSpace = true;
        }
        const bool hasText = !cps.empty();
        addToken(std::move(cps), token, first, first && line.startsParagraph, pendingSpace && hasText);
        if (hasText) pendingSpace = false;
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
        if (!isPunctuationLike(cp)) return static_cast<long>(i);
      }
    }
    return first;
  }

  std::optional<BuiltSentence> build(const size_t tap) const {
    if (items_[tap].cps.empty()) return std::nullopt;
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
  // --- splitting a token into pieces ---

  // Where a sentence (or a line of dialogue) ends inside a token. CrossPoint glues punctuation onto the
  // character before it, so a token can be 好。”“走 (two sentences) but also か！？」と, 3.50 or
  // example.com (one). So: a run of terminators and closers stays together; after it, a closed run
  // (。” / ！？」) ends the sentence unless a Japanese quotative follows, and a bare run (。 / .) only when
  // an opener or a CJK character follows it (never a digit or a letter: 3.50, e.g., example.com). A
  // closer followed by an opener (”“ / 」「) is a break on its own.
  std::vector<size_t> internalCuts(const std::vector<uint32_t>& cps) const {
    std::vector<size_t> cuts;
    for (size_t k = 1; k < cps.size(); k++) {
      const uint32_t after = cps[k];
      // Never inside a run: closers stay with what they close, terminators with each other (！？, ...).
      if (Punctuation::isCloser(after, script_) || Punctuation::isTerminator(after, script_) ||
          Punctuation::isEllipsis(after)) {
        continue;
      }
      bool cut = Punctuation::isCloser(cps[k - 1], script_) && Punctuation::isOpener(after, script_);
      if (!cut) {
        size_t j = k;  // back over closers, to the character they follow
        while (j > 0 && Punctuation::isCloser(cps[j - 1], script_)) j--;
        const bool closed = j < k;
        if (j > 0) {
          const uint32_t end = cps[j - 1];
          const bool terminated = Punctuation::isTerminator(end, script_) || (closed && Punctuation::isEllipsis(end));
          const bool nextStartsASentence = Punctuation::isOpener(after, script_) || utf8IsCjkCodepoint(after);
          cut = terminated && (closed || nextStartsASentence);
        }
        if (cut && (closed || Punctuation::isQuestionOrExclamation(cps[j - 1])) &&
            Punctuation::continuesQuote(cps.data() + k, cps.size() - k, script_)) {
          cut = false;
        }
      }
      if (cut) cuts.push_back(k);
    }
    return cuts;
  }

  void addToken(std::vector<uint32_t> cps, const size_t token, const bool lineStart, const bool paragraphStart,
                const bool spaceBefore) {
    std::vector<size_t> cuts = internalCuts(cps);
    cuts.push_back(cps.size());
    size_t from = 0;
    bool first = true;
    for (const size_t to : cuts) {
      Item item;
      item.cps.assign(cps.begin() + static_cast<long>(from), cps.begin() + static_cast<long>(to));
      item.token = token;
      item.lineStart = first && lineStart;
      item.paragraphStart = first && paragraphStart;
      item.spaceBefore = first && spaceBefore;
      items_.push_back(std::move(item));
      first = false;
      from = to;
    }
  }

  // --- sentence boundaries between pieces ---

  bool isEmpty(const size_t i) const { return items_[i].cps.empty(); }

  // The index of the last codepoint that isn't a closer, or -1.
  long lastNonCloser(const Item& item) const {
    long i = static_cast<long>(item.cps.size()) - 1;
    while (i >= 0 && Punctuation::isCloser(item.cps[static_cast<size_t>(i)], script_)) i--;
    return i;
  }

  bool isOnlyClosers(const size_t i) const { return !isEmpty(i) && lastNonCloser(items_[i]) < 0; }

  // Does the sentence end after piece i (with any closers in it)?
  bool endsSentence(const size_t i) const {
    if (isEmpty(i)) return false;
    const Item& item = items_[i];
    const long last = lastNonCloser(item);
    if (last < 0) return false;
    const uint32_t cp = item.cps[static_cast<size_t>(last)];
    // Latin "..." is an ellipsis, not three full stops.
    const bool dots = cp == '.' && last > 0 && item.cps[static_cast<size_t>(last) - 1] == '.';
    if (Punctuation::isTerminator(cp, script_) && !dots) return true;
    if (Punctuation::isEllipsis(cp) || dots) {
      // "…" ends a sentence when a closer follows, in this piece or the next, or the paragraph ends.
      if (last + 1 < static_cast<long>(item.cps.size())) return true;
      if (i + 1 >= items_.size()) return false;  // the page ends: unknown, so not a sentence end
      return items_[i + 1].paragraphStart || isOnlyClosers(i + 1);
    }
    return false;
  }

  // Does a sentence end just before piece i, counting the closers after a terminator (。」) as part of
  // the sentence they close?
  bool sentenceEndsBefore(size_t i) const {
    while (i > 0 && isOnlyClosers(i - 1)) i--;
    return i > 0 && endsSentence(i - 1);
  }

  // Does a quote before piece i run on into the sentence (Japanese 」と, ！？と, 」って)?
  bool quoteContinues(const size_t i) const {
    const Item& prev = items_[i - 1];
    if (prev.cps.empty()) return false;
    const uint32_t last = prev.cps.back();
    if (!Punctuation::isCloser(last, script_) && !Punctuation::isQuestionOrExclamation(last)) return false;
    const Item& next = items_[i];
    return Punctuation::continuesQuote(next.cps.data(), next.cps.size(), script_);
  }

  // Does a new sentence start at piece i (i > 0)?
  bool cutBefore(const size_t i) const {
    if (items_[i].paragraphStart) return true;
    if (!isOnlyClosers(i) && sentenceEndsBefore(i) && !quoteContinues(i)) return true;
    // Dialogue: 」「 is a break even without a terminator.
    const Item& prev = items_[i - 1];
    return !prev.cps.empty() && !items_[i].cps.empty() && Punctuation::isCloser(prev.cps.back(), script_) &&
           Punctuation::isOpener(items_[i].cps.front(), script_);
  }

  // --- joining ---

  // Whether a space goes between two neighbouring pieces. The layout doesn't keep that bit, so it is
  // decided by script: CJK text never has spaces between characters, Latin words always do (an explicit
  // &nbsp; token always does). A Latin word hyphenated across a line break rejoins without one.
  static bool needsSpace(const Item& prev, const Item& next) {
    if (next.spaceBefore) return true;
    if (next.token == prev.token) return false;  // pieces of one token were never apart
    const uint32_t left = prev.cps.back();
    const uint32_t right = next.cps.front();
    if (utf8IsCjkCodepoint(left) || utf8IsCjkCodepoint(right)) return false;
    if (next.lineStart && isLatinHyphen(left)) return false;
    return true;
  }

  // Calls fn(index, spaceBefore) for each non-empty piece in [begin, end), exactly as they are joined.
  template <class Fn>
  void forEachJoined(const size_t begin, const size_t end, Fn&& fn) const {
    const Item* prev = nullptr;
    for (size_t i = begin; i < end; i++) {
      if (isEmpty(i)) continue;
      fn(i, prev != nullptr && needsSpace(*prev, items_[i]));
      prev = &items_[i];
    }
  }

  static uint32_t units(const uint32_t cp) { return cp > 0xFFFF ? 2 : 1; }

  // The joined length (UTF-16 units): the cap and the offsets share one measure.
  size_t length(const size_t begin, const size_t end) const {
    size_t total = 0;
    forEachJoined(begin, end, [&](const size_t i, const bool space) {
      total += space ? 1 : 0;
      for (const uint32_t cp : items_[i].cps) total += units(cp);
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
    forEachJoined(begin, end, [&](const size_t i, const bool space) {
      if (space) {
        out.text += ' ';
        at++;
      }
      if (i == tap) out.tapOffset = at;
      for (const uint32_t cp : items_[i].cps) {
        utf8AppendCodepoint(cp, out.text);
        at += units(cp);
      }
      if (i == tap) out.tapLength = at - out.tapOffset;
    });
    return out;
  }

  Script script_;
  std::vector<Item> items_;
};

}  // namespace

uint32_t utf16Length(const std::string& utf8) {
  uint32_t total = 0;
  for (const uint32_t cp : decode(utf8)) total += cp > 0xFFFF ? 2 : 1;
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
