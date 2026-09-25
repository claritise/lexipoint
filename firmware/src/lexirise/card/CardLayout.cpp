#if LEXIRISE

#include "CardLayout.h"

#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

#include "CardMetrics.h"
#include "lexirise/LexiriseConfig.h"
#include "lexirise/text/Utf8Units.h"

namespace lexipoint::card {

namespace m = metrics;

// ---- model helpers ------------------------------------------------------------------------------

int tabCount(const Language language) {
  const CardStrings s;
  return static_cast<int>(language == Language::Japanese ? std::size(s.tabsJa) : std::size(s.tabsZh)) + 1;  // + ⋯
}

bool isActionsTab(const Language language, const int tab) { return tab == tabCount(language) - 1; }

int rankBand(const uint32_t rank, const Language language) {
  int band = 0;
  for (const uint32_t limit : language == Language::Chinese ? config::kRankBandLimitsZh : config::kRankBandLimitsJa) {
    if (rank < limit) return band;
    band++;
  }
  return band;
}

int filledBars(const float frequency) {
  if (!(frequency > 0)) return 1;  // missing, zero or NaN
  const int n = static_cast<int>(std::ceil(frequency * m::kBarCount));
  return std::clamp(n, 1, m::kBarCount);
}

std::string formatRank(const uint32_t rank) {
  const std::string digits = std::to_string(rank);
  std::string out;
  for (size_t i = 0; i < digits.size(); i++) {
    if (i != 0 && (digits.size() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return "#" + out;
}

// ---- text ---------------------------------------------------------------------------------------

namespace {

constexpr const char* kEllipsis = "\xE2\x80\xA6";    // …
constexpr const char* kOpenQuote = "\xE3\x80\x8C";   // 「 : around the Examples tab's sentences
constexpr const char* kCloseQuote = "\xE3\x80\x8D";  // 」

// Japanese/Chinese line-breaking (kinsoku): closing punctuation never starts a line, opening brackets
// never end one.
bool noLineStart(const uint32_t cp) {
  switch (cp) {
    case 0x3001:
    case 0x3002:
    case 0xFF0C:
    case 0xFF0E:
    case 0x300D:
    case 0x300F:
    case 0xFF09:
    case 0x3015:
    case 0x3011:
    case 0x3009:
    case 0x300B:
    case 0xFF01:
    case 0xFF1F:
    case 0xFF1A:
    case 0xFF1B:
    case 0x30FC:
    case 0x30FB:
    case 0x2026:
    case 0x3005:
      return true;
    default:
      return false;
  }
}
bool noLineEnd(const uint32_t cp) {
  return cp == 0x300C || cp == 0x300E || cp == 0xFF08 || cp == 0x3014 || cp == 0x3010 || cp == 0x3008 || cp == 0x300A;
}

// Break units: a Latin word with its trailing spaces, or one CJK character (kinsoku: a closing mark
// joins the unit before it, an opening bracket the unit after it).
std::vector<std::string> breakUnits(const std::string& text) {
  std::vector<std::string> units;
  std::string current;
  std::string opening;  // opening brackets waiting for the next character
  bool inSpaces = false;
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    std::string ch;
    utf8AppendCodepoint(cp, ch);
    if (utf8IsCjkCodepoint(cp)) {
      if (noLineStart(cp) && opening.empty()) {
        if (!current.empty()) {
          current += ch;
        } else if (!units.empty()) {
          units.back() += ch;
        } else {
          units.push_back(std::move(ch));
        }
        continue;
      }
      if (!current.empty()) units.push_back(std::move(current));
      current.clear();
      inSpaces = false;
      if (noLineEnd(cp)) {
        opening += ch;
        continue;
      }
      units.push_back(opening + ch);
      opening.clear();
      continue;
    }
    if (!opening.empty()) {
      current = std::move(opening);
      opening.clear();
    }
    if (cp == ' ') {
      current += ch;
      inSpaces = true;
      continue;
    }
    if (inSpaces) {
      units.push_back(std::move(current));
      current.clear();
      inSpaces = false;
    }
    current += ch;
  }
  if (!current.empty()) units.push_back(std::move(current));
  if (!opening.empty()) units.push_back(std::move(opening));
  return units;
}

std::string trimRight(std::string s) {
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}

// Drops whole codepoints from the end until `line…` fits.
std::string withEllipsis(const TextMetrics& metrics, const Font font, std::string line, const int width) {
  line = trimRight(std::move(line));
  while (!line.empty() && metrics.width(font, line + kEllipsis) > width) {
    size_t end = line.size() - 1;
    while (end > 0 && (static_cast<unsigned char>(line[end]) & 0xC0) == 0x80) end--;
    line = trimRight(line.substr(0, end));
  }
  return line + kEllipsis;
}

// Without the paragraph indent (U+3000) at either end.
std::string trimIdeographicSpace(std::string s) {
  static constexpr const char kSpace[] = "\xE3\x80\x80";
  while (s.rfind(kSpace, 0) == 0) s.erase(0, 3);
  while (s.size() >= 3 && s.compare(s.size() - 3, 3, kSpace) == 0) s.erase(s.size() - 3);
  return s;
}

// The text as it is when it fits in `width`, else cut with …
std::string fitText(const TextMetrics& metrics, const Font font, std::string text, const int width) {
  if (text.empty() || metrics.width(font, text) <= width) return text;
  return withEllipsis(metrics, font, std::move(text), width);
}

int centred(const int boxTop, const int boxHeight, const int contentHeight) {
  return boxTop + (boxHeight - contentHeight) / 2;
}

}  // namespace

std::vector<std::string> wrapText(const TextMetrics& metrics, const Font font, const std::string& text, const int width,
                                  const int maxLines, const int firstIndent, std::vector<size_t>* sourceBytes) {
  std::vector<std::string> lines;
  if (sourceBytes) sourceBytes->clear();
  std::string line;
  const std::vector<std::string> units = breakUnits(text);
  const auto room = [&] { return lines.empty() ? width - firstIndent : width; };
  const auto push = [&](const std::string& source) {
    const int w = room();
    const std::string trimmed = trimRight(source);
    if (sourceBytes) sourceBytes->push_back(trimmed.size());
    lines.push_back(metrics.width(font, trimmed) > w ? withEllipsis(metrics, font, trimmed, w) : trimmed);
  };
  for (size_t i = 0; i < units.size(); i++) {
    const std::string candidate = line + units[i];
    if (line.empty() || metrics.width(font, trimRight(candidate)) <= room()) {
      line = candidate;
      continue;
    }
    push(line);
    line = units[i];
    if (maxLines > 0 && static_cast<int>(lines.size()) == maxLines) {
      lines.back() = withEllipsis(metrics, font, lines.back(), lines.size() == 1 ? width - firstIndent : width);
      return lines;
    }
  }
  if (!line.empty()) push(line);
  return lines;
}

std::string meaningText(const std::vector<std::string>& senses, const TextMetrics& metrics, const int width) {
  if (senses.empty()) return {};
  if (senses.size() >= 2) {
    const std::string both = senses[0] + "; " + senses[1];
    const auto lines = wrapText(metrics, Font::Ui, both, width, 0);
    if (static_cast<int>(lines.size()) <= m::kMeaningMaxLines) return both;
  }
  return senses[0];
}

// ---- layout -------------------------------------------------------------------------------------

namespace {

class Layout {
 public:
  Layout(const CardWord& word, const CardState& state, const TextMetrics& metrics, const CardStrings& strings)
      : w_(word), s_(state), m_(metrics), str_(strings), ja_(word.language == Language::Japanese) {}

  DisplayList run() {
    if (s_.view == View::Expanded) {
      expanded();
    } else {
      cardView();
    }
    if (!s_.toast.empty()) toast();
    return std::move(out_);
  }

 private:
  // The card's inner box (inside the frame) and its padded content columns.
  static constexpr int kInnerX = m::kCardInset + m::kCardFrame;
  static constexpr int kInnerW = m::kScreenWidth - 2 * (m::kCardInset + m::kCardFrame);
  static constexpr int kInnerRight = kInnerX + kInnerW;
  static constexpr int kContentX = kInnerX + m::kRowPadH;
  static constexpr int kContentRight = kInnerRight - m::kRowPadH;
  static constexpr int kContentW = kContentRight - kContentX;
  static constexpr int kCardBottom = m::kScreenHeight - m::kCardInset;

  int lh(const Font f) const { return m_.lineHeight(f); }
  int tw(const Font f, const std::string& t) const { return m_.width(f, t); }
  bool pending() const { return s_.phase == Phase::Pending; }
  // Phase B has run (the rank row and badge are final), whatever it found.
  bool translated() const {
    return s_.phase == Phase::Complete || s_.phase == Phase::TranslationPending || s_.phase == Phase::Unanswered;
  }
  Font readingFont() const { return ja_ && s_.reading == ReadingMode::Kana ? Font::ReaderSmall : Font::UiSmall; }
  std::string readingText() const {
    if (pending()) return {};
    return ja_ && s_.reading == ReadingMode::Romaji ? w_.romaji : w_.reading;
  }

  // ---- header ----

  int levelBoxWidth() const {
    return 2 * m::kLevelFrame + m::kLevelCells * m::kLevelCellWidth + (m::kLevelCells - 1) * m::kLevelDivider;
  }
  int levelBoxHeight() const { return 2 * m::kLevelFrame + 2 * m::kLevelPadV + lh(Font::UiSmall); }
  int headerLeftHeight() const { return lh(readingFont()) + m::kWordLineBox; }
  int headerRightHeight() const { return levelBoxHeight() + m::kStateGap + lh(Font::UiSmall); }
  int surfaceLineHeight() const {
    const int pill = lh(Font::UiSmall) + 2 * m::kPillFrame;
    return std::max({ja_ ? lh(Font::ReaderSmall) : 0, lh(Font::UiSmall), pill});
  }
  int headerHeight() const {
    return m::kRowPadV + std::max(headerLeftHeight(), headerRightHeight()) + m::kSurfaceGap + surfaceLineHeight() +
           m::kRowPadV;
  }

  // The header's left column (reading, word, badge) ends before T L F K.
  int headerLeftLimit() const { return kContentRight - levelBoxWidth() - m::kHeaderColumnGap; }

  void header(const int rowTop) {
    const int top = rowTop + m::kRowPadV;
    const int limit = headerLeftLimit();
    // Reading (and, in Japanese, its tap area: kana ⇄ romaji).
    const Font rf = readingFont();
    const std::string reading = fitText(m_, rf, readingText(), limit - kContentX);
    if (!reading.empty()) {
      out_.text(rf, kContentX, top, reading);
      if (ja_) {
        out_.hit(Target::ReadingLine, {kContentX - m::kReadingTapPadH, top - m::kReadingTapPadTop,
                                       tw(rf, reading) + 2 * m::kReadingTapPadH, lh(rf) + m::kReadingTapPadTop});
      }
    }
    // The word (phase 0: the tapped character and …), then the level badge when there's room for it.
    const int wordBox = top + lh(rf);
    const std::string word =
        fitText(m_, Font::ReaderLarge, pending() ? s_.pendingText + kEllipsis : w_.word, limit - kContentX);
    out_.text(Font::ReaderLarge, kContentX, centred(wordBox, m::kWordLineBox, lh(Font::ReaderLarge)), word);
    if (translated() && !w_.badge.empty()) {
      const int bw = 2 * m::kBadgeFrame + 2 * m::kBadgePadH + tw(Font::UiSmall, w_.badge);
      const int bh = 2 * m::kBadgeFrame + m::kBadgeLineBox;
      const Rect badge{kContentX + tw(Font::ReaderLarge, word) + m::kBadgeGap, centred(wordBox, m::kWordLineBox, bh),
                       bw, bh};
      if (badge.right() <= limit) {
        out_.frame(badge, m::kBadgeFrame);
        out_.text(Font::UiSmall, badge.x + m::kBadgeFrame + m::kBadgePadH, centred(badge.y, badge.h, lh(Font::UiSmall)),
                  w_.badge);
      }
    }
    // T L F K and the state under it.
    if (!pending()) levels(top);
    // Surface form + conjugation (Japanese, when it isn't the lemma) and the POS pill, each only if it fits.
    const int lineTop = top + std::max(headerLeftHeight(), headerRightHeight()) + m::kSurfaceGap;
    const int lineH = surfaceLineHeight();
    int x = kContentX;
    if (!pending() && ja_ && !w_.surface.empty()) {
      const std::string surface = fitText(m_, Font::ReaderSmall, w_.surface, kContentRight - x);
      out_.text(Font::ReaderSmall, x, centred(lineTop, lineH, lh(Font::ReaderSmall)), surface);
      x += tw(Font::ReaderSmall, surface) + tw(Font::UiSmall, " ");
      if (!w_.conjugation.empty() && x + tw(Font::UiSmall, w_.conjugation) <= kContentRight) {
        out_.text(Font::UiSmall, x, centred(lineTop, lineH, lh(Font::UiSmall)), w_.conjugation);
        x += tw(Font::UiSmall, w_.conjugation) + tw(Font::UiSmall, " ");
      }
    }
    if (!pending() && !w_.partOfSpeech.empty()) {
      const int ph = lh(Font::UiSmall) + 2 * m::kPillFrame;
      const int textW = kContentRight - x - 2 * m::kPillFrame - 2 * m::kPillPadH;
      const std::string pos = fitText(m_, Font::UiSmall, w_.partOfSpeech, textW);
      if (textW >= tw(Font::UiSmall, kEllipsis)) {  // at least "…" fits: else no pill
        const Rect pill{x, centred(lineTop, lineH, ph), 2 * m::kPillFrame + 2 * m::kPillPadH + tw(Font::UiSmall, pos),
                        ph};
        out_.roundedFrame(pill, m::kPillFrame, m::kPillRadius);
        out_.text(Font::UiSmall, pill.x + m::kPillFrame + m::kPillPadH, pill.y + m::kPillFrame, pos);
      }
    }
  }

  void levels(const int top) {
    const Rect box{kContentRight - levelBoxWidth(), top, levelBoxWidth(), levelBoxHeight()};
    out_.frame(box, m::kLevelFrame);
    const int current = static_cast<int>(s_.level);
    for (int i = 0; i < m::kLevelCells; i++) {
      const int cx = box.x + m::kLevelFrame + i * (m::kLevelCellWidth + m::kLevelDivider);
      const Rect cell{cx, box.y + m::kLevelFrame, m::kLevelCellWidth, box.h - 2 * m::kLevelFrame};
      if (i > 0) out_.fill({cx - m::kLevelDivider, cell.y, m::kLevelDivider, cell.h});
      const bool on = i == current;
      if (on) out_.fill(cell);
      const std::string label = str_.levels[i];
      out_.text(Font::UiSmall, cx + (m::kLevelCellWidth - tw(Font::UiSmall, label)) / 2, cell.y + m::kLevelPadV, label,
                !on);
      out_.hit(Target::Level, cell, i);
    }
    const std::string state = current < 0 ? str_.notSaved : str_.levelNames[current];
    out_.text(Font::UiSmall, kContentRight - tw(Font::UiSmall, state), box.bottom() + m::kStateGap, state);
  }

  // ---- meaning (card view) ----

  const char* noMeaningText() const {
    switch (w_.noMeaning) {
      case NoMeaning::Offline:
        return str_.offline;
      case NoMeaning::KeyRejected:
        return str_.keyRejected;
      case NoMeaning::RateLimited:
        return str_.rateLimited;
      case NoMeaning::Unavailable:
        break;
    }
    return str_.meaningUnavailable;
  }

  const std::vector<std::string>& meaningLines() const {
    if (!meaningLines_.empty()) return meaningLines_;
    std::string text;
    if (s_.phase == Phase::Complete) {
      text = meaningText(w_.senses, m_, kContentW);
    } else if (s_.phase == Phase::TranslationPending) {
      text = str_.translationPending;  // grey in the reference: black on the device (deviation 1)
    } else if (s_.phase == Phase::Unanswered) {
      text = noMeaningText();  // why phase B brought none (offline-and-errors.md §1)
    } else {
      text = kEllipsis;
    }
    meaningLines_ = wrapText(m_, Font::Ui, text, kContentW, m::kMeaningMaxLines);
    if (meaningLines_.empty()) meaningLines_.emplace_back();
    return meaningLines_;
  }
  int meaningHeight() const { return 2 * m::kRowPadV + static_cast<int>(meaningLines().size()) * lh(Font::Ui); }
  void meaning(const int rowTop) {
    int y = rowTop + m::kRowPadV;
    for (const std::string& line : meaningLines()) {
      out_.text(Font::Ui, kContentX, y, line);
      y += lh(Font::Ui);
    }
  }

  // ---- rank row ----

  int rankContentHeight() const { return std::max(m::kBarHeights[m::kBarCount - 1], lh(Font::UiSmall)); }
  int rankHeight() const { return m::kDivider + 2 * m::kRankPadV + rankContentHeight(); }

  void rank(const int rowTop, const bool up) {
    out_.fill({kInnerX, rowTop, kInnerW, m::kDivider});
    const int top = rowTop + m::kDivider;
    const int h = rankHeight() - m::kDivider;
    const int closeX = kInnerRight - m::kCloseCellWidth;
    const Rect row{kInnerX, top, closeX - m::kDivider - kInnerX, h};
    out_.fill({closeX - m::kDivider, top, m::kDivider, h});
    const int ct = top + m::kRankPadV;
    const int ch = rankContentHeight();
    if (translated()) {
      int x = kContentX;
      std::string text;
      if (w_.rank > 0) {
        const int filled = filledBars(w_.frequency);
        for (int i = 0; i < m::kBarCount; i++) {
          const Rect bar{x, ct + ch - m::kBarHeights[i], m::kBarWidth, m::kBarHeights[i]};
          if (i < filled) {
            out_.fill(bar);
          } else {
            out_.frame(bar, m::kBarFrame);
          }
          x += m::kBarWidth + m::kBarGap;
        }
        x += tw(Font::UiSmall, " ");
        text = formatRank(w_.rank) + " " + str_.bands[rankBand(w_.rank, w_.language)];
      } else {  // no rank: the row shows only the state
        text = s_.level == Level::None ? str_.notSaved : str_.levelNames[static_cast<int>(s_.level)];
      }
      out_.text(Font::UiSmall, x, centred(ct, ch, lh(Font::UiSmall)), text);
    }
    if (!pending()) {
      out_.shape(up ? Shape::TriangleUp : Shape::TriangleDown,
                 {row.right() - m::kRankPadH - m::kArrowGlyph, centred(ct, ch, m::kArrowGlyph), m::kArrowGlyph,
                  m::kArrowGlyph});
      out_.hit(Target::RankRow, row);
    }
    const Rect close{closeX, top, m::kCloseCellWidth, h};
    out_.shape(Shape::Cross, {close.x + (close.w - m::kCloseGlyph) / 2, centred(top, h, m::kCloseGlyph), m::kCloseGlyph,
                              m::kCloseGlyph});
    out_.hit(Target::Close, close);
  }

  // ---- strips ----

  // One page line, clipped before `clipRight`, the active word inverted and scrolled into view.
  void stripLine(const int originX, const int clipRight, const int textTop) {
    const StripLine& line = s_.strip;
    int scroll = 0;
    if (line.activeLast >= 0 && line.activeLast < static_cast<int>(line.tokens.size())) {
      const StripToken& last = line.tokens[line.activeLast];
      const int activeRight = originX + last.x + last.width;
      if (activeRight > clipRight) scroll = activeRight - (clipRight - m::kStripScrollInset);
    }
    const int h = lh(Font::Page);
    const bool hasActive = line.activeFirst >= 0 && line.activeLast >= line.activeFirst &&
                           line.activeLast < static_cast<int>(line.tokens.size());
    // The word's own codepoints in token i, [from, to); none outside the word.
    const auto activeRange = [&line, hasActive](const int i) -> std::optional<std::pair<uint32_t, uint32_t>> {
      if (!hasActive || i < line.activeFirst || i > line.activeLast) return std::nullopt;
      return std::pair<uint32_t, uint32_t>{i == line.activeFirst ? line.activeStartCp : 0,
                                           i == line.activeLast ? line.activeEndCp : StripLine::kToTokenEnd};
    };
    if (hasActive) {
      const StripToken& first = line.tokens[line.activeFirst];
      const StripToken& last = line.tokens[line.activeLast];
      const int x0 = originX + first.x + tw(Font::Page, text::utf8Codepoints(first.text, 0, line.activeStartCp)) -
                     scroll - m::kHighlightPadH;
      const int x1 = originX + last.x + tw(Font::Page, text::utf8Codepoints(last.text, 0, line.activeEndCp)) - scroll +
                     m::kHighlightPadH;
      out_.fill({x0, textTop, x1 - x0, h});
    }
    for (int i = 0; i < static_cast<int>(line.tokens.size()); i++) {
      const StripToken& t = line.tokens[i];
      const int x = originX + t.x - scroll;
      if (x < originX - m::kHighlightPadH || x + t.width > clipRight) continue;  // whole tokens only
      const auto range = activeRange(i);
      if (!range) {
        out_.text(Font::Page, x, textTop, t.text);
        continue;
      }
      const auto [from, to] = *range;
      // The word's part inverted, anything glued before or after it (a full stop, a quote) as normal text.
      const std::string before = text::utf8Codepoints(t.text, 0, from);
      const std::string word = text::utf8Codepoints(t.text, from, to);
      const std::string after = text::utf8Codepoints(t.text, to, StripLine::kToTokenEnd);
      const int wordX = x + tw(Font::Page, before);
      if (!before.empty()) out_.text(Font::Page, x, textTop, before);
      out_.text(Font::Page, wordX, textTop, word, false);
      if (!after.empty()) out_.text(Font::Page, wordX + tw(Font::Page, word), textTop, after);
    }
  }
  // The detail view's strip (claritise, 2026-09-25): the active word at the left edge, inverted, then as much
  // of its sentence after it as fits. Stepping moves the text along; nothing before the word is shown, and
  // nothing scrolls. Before the sentence is known (no mark), the page line as in card view.
  void wordStrip(const int originX, const int clipRight, const int textTop) {
    const MarkedText& s = s_.contextSentence;
    if (s.markLength == 0 || s.markStart + s.markLength > s.text.size()) {
      stripLine(originX, clipRight, textTop);
      return;
    }
    const std::string word = fitText(m_, Font::Page, s.text.substr(s.markStart, s.markLength), clipRight - originX);
    const int wordW = tw(Font::Page, word);
    out_.fill({originX - m::kHighlightPadH, textTop, wordW + 2 * m::kHighlightPadH, lh(Font::Page)});
    out_.text(Font::Page, originX, textTop, word, false);
    const int restX = originX + wordW + m::kHighlightPadH;
    const std::string rest = fitText(m_, Font::Page, s.text.substr(s.markStart + s.markLength), clipRight - restX);
    if (!rest.empty() && restX + tw(Font::Page, rest) <= clipRight) out_.text(Font::Page, restX, textTop, rest);
  }
  std::string marker() const {
    return std::string(str_.line) + " " + std::to_string(s_.strip.lineNumber) + "/" +
           std::to_string(s_.strip.lineCount);
  }

  bool cardStripShown(const int cardTop) const {
    return s_.wordOnPage && s_.wordOnPage->bottom() > cardTop && !s_.strip.tokens.empty();
  }
  void cardStrip(const int rowTop) {
    const int right = kContentRight - m::kStripClip;
    stripLine(kInnerX + m::kCardStripPadH, right, centred(rowTop, m::kCardStripHeight, lh(Font::Page)));
    const std::string mk = marker();
    out_.text(Font::UiSmall, kInnerRight - m::kCardStripMarkerRight - tw(Font::UiSmall, mk),
              rowTop + m::kCardStripMarkerTop, mk);
    out_.fill({kInnerX, rowTop + m::kCardStripHeight, kInnerW, m::kDivider});
  }

  // ---- views ----

  void cardFrame(const int top) {
    out_.card = {m::kCardInset, top, m::kScreenWidth - 2 * m::kCardInset, kCardBottom - top};
    out_.fill(out_.card, false);  // the card is opaque over the page
    out_.frame(out_.card, m::kCardFrame);
  }

  void cardView() {
    const int core = headerHeight() + m::kDivider + meaningHeight() + rankHeight();
    int top = kCardBottom - m::kCardFrame - core - m::kCardFrame;
    const bool strip = cardStripShown(top);
    if (strip) top -= m::kCardStripHeight + m::kDivider;
    // Touch targets are front-most first: the card's own come before its catch-all.
    cardFrame(top);
    int y = top + m::kCardFrame;
    if (strip) {
      cardStrip(y);
      y += m::kCardStripHeight + m::kDivider;
    }
    header(y);
    y += headerHeight();
    out_.fill({kInnerX, y, kInnerW, m::kDivider});
    y += m::kDivider;
    meaning(y);
    y += meaningHeight();
    rank(y, false);
    out_.hit(Target::Card, out_.card);
  }

  void expanded() {
    // The strip: the active word and its sentence after it, in the screen above the card.
    const int cardTop = kCardBottom - m::kExpandedCardHeight;
    out_.fill({0, 0, m::kScreenWidth, cardTop}, false);
    wordStrip(m::kStripPadH, m::kScreenWidth - m::kStripPadH - m::kStripClip, centred(0, cardTop, lh(Font::Page)));
    const std::string mk = marker();
    out_.text(Font::UiSmall, m::kScreenWidth - m::kStripMarkerRight - tw(Font::UiSmall, mk), m::kStripMarkerTop, mk);

    cardFrame(cardTop);
    int y = cardTop + m::kCardFrame;
    header(y);
    y += headerHeight();
    out_.fill({kInnerX, y, kInnerW, m::kDivider});
    y += m::kDivider;
    const int tabsH = m::kDivider + 2 * m::kTabPadV + lh(Font::UiSmall);
    const int bodyBottom = kCardBottom - m::kCardFrame - rankHeight() - tabsH;
    body({kInnerX, y, kInnerW, bodyBottom - y});
    tabs(bodyBottom, tabsH);
    rank(bodyBottom + tabsH, true);
    out_.hit(Target::Card, out_.card);
  }

  void tabs(const int rowTop, const int h) {
    out_.fill({kInnerX, rowTop, kInnerW, m::kDivider});
    const int top = rowTop + m::kDivider;
    const int cellH = h - m::kDivider;
    const int count = tabCount(w_.language) - 1;  // the word tabs; ⋯ is last
    const char* const* names = ja_ ? str_.tabsJa : str_.tabsZh;
    const int moreX = kInnerRight - m::kMoreTabWidth;
    const int available = moreX - m::kDivider - kInnerX;
    int used = 0;
    for (int i = 0; i < count; i++) used += tw(Font::UiSmall, names[i]);
    const int free = std::max(0, available - used);
    int x = kInnerX;
    for (int i = 0; i < count; i++) {
      // flex: 1 1 auto: each tab its label's width plus an equal share of the rest (remainder to the first).
      const int share = free / count + (i < free % count ? 1 : 0);
      const int width = tw(Font::UiSmall, names[i]) + share;
      const Rect cell{x, top, width, cellH};
      const bool on = s_.tab == i;
      if (on) out_.fill(cell);
      out_.text(Font::UiSmall, x + share / 2, top + m::kTabPadV, names[i], !on);
      out_.hit(Target::Tab, cell, i);
      x += width;
    }
    out_.fill({moreX - m::kDivider, top, m::kDivider, cellH});
    const Rect more{moreX, top, m::kMoreTabWidth, cellH};
    const bool on = isActionsTab(w_.language, s_.tab);
    if (on) out_.fill(more);
    out_.shape(
        Shape::Ellipsis,
        {more.x + (more.w - m::kMoreGlyph) / 2, centred(top, cellH, m::kMoreGlyph), m::kMoreGlyph, m::kMoreGlyph}, !on);
    out_.hit(Target::Tab, more, count);
  }

  // ---- tab content ----

  class Flow {
   public:
    Flow(Layout& l, const Rect& body)
        : l_(l), x_(kContentX), y_(body.y + m::kBodyPadTop), bottom_(body.bottom() - m::kRowPadV) {}
    int y() const { return y_; }
    void gap(const int g) { y_ += g; }
    bool room(const int h) const { return y_ + h <= bottom_; }
    // Wrapped text, each line in a line box of `lineBox` (0: the font's own), text centred in it.
    // firstIndent: the first line starts that far in (the Meaning tab's "1.").
    void paragraph(const Font font, const std::string& text, const int lineBox = 0, const MarkedText* marked = nullptr,
                   const bool invertMark = false, const int firstIndent = 0) {
      const int box = lineBox > 0 ? lineBox : l_.lh(font);
      std::vector<size_t> sourceBytes;
      const auto lines = wrapText(l_.m_, font, text, kContentW, 0, firstIndent, &sourceBytes);
      size_t offset = 0;
      for (size_t i = 0; i < lines.size(); i++) {
        if (!room(box)) return;
        // The last line that fits, when more follow: cut with … (the body clips, popup-ui.md §1).
        const bool cut = i + 1 < lines.size() && !room(2 * box);
        const int indent = i == 0 ? firstIndent : 0;
        const std::string line = cut ? withEllipsis(l_.m_, font, lines[i], kContentW - indent) : lines[i];
        const int top = centred(y_, box, l_.lh(font));
        l_.out_.text(font, x_ + indent, top, line);
        // Marked only as far as the line is drawn: never over the … of a cut line (either cut).
        if (marked) {
          const bool whole = text.compare(offset, sourceBytes[i], line) == 0;
          mark(font, whole ? line : line.substr(0, line.size() - std::strlen(kEllipsis)), offset, top, *marked,
               invertMark);
        }
        y_ += box;  // before a cut's return too: nothing after it may draw over the cut line
        if (cut) return;
        offset += sourceBytes[i];
        while (offset < text.size() && text[offset] == ' ') offset++;
      }
    }

   private:
    // The marked span's part on this line: underlined, or inverted (the word in this book's sentence).
    void mark(const Font font, const std::string& line, const size_t offset, const int top, const MarkedText& mt,
              const bool invert) {
      const size_t start = std::max(mt.markStart, offset);
      const size_t end = std::min(mt.markStart + mt.markLength, offset + line.size());
      if (start >= end) return;
      const int x0 = x_ + l_.tw(font, line.substr(0, start - offset));
      const std::string span = line.substr(start - offset, end - start);
      const int w = l_.tw(font, span);
      if (invert) {
        l_.out_.fill({x0 - m::kHighlightPadH, top, w + 2 * m::kHighlightPadH, l_.lh(font)});
        l_.out_.text(font, x0, top, span, false);
      } else {
        l_.out_.line(x0, top + l_.lh(font) - m::kUnderlineRaise, w, m::kUnderlineThickness);
      }
    }

    Layout& l_;
    int x_;
    int y_;
    int bottom_;
  };

  static int pct(const int px, const int percent) { return m::percentOf(px, percent); }

  void body(const Rect& area) {
    Flow f(*this, area);
    const int tab = s_.tab;
    if (isActionsTab(w_.language, tab)) return actions(f);
    switch (tab) {
      case 0:  // Meaning: "1. sense", wrapping back to the left edge (the number is bold)
        if (s_.phase == Phase::Unanswered) return f.paragraph(Font::UiSmall, noMeaningText());
        for (size_t i = 0; i < w_.senses.size() && translated(); i++) {
          const int box = pct(m::kSenseText, m::kSenseLineHeightPct);
          const std::string number = std::to_string(i + 1) + ".";
          const int indent = tw(Font::UiBold, number) + tw(Font::Ui, " ");
          if (!f.room(box)) return;
          out_.text(Font::UiBold, kContentX, centred(f.y(), box, lh(Font::Ui)), number);
          f.paragraph(Font::Ui, w_.senses[i], box, nullptr, false, indent);  // cut with … where the body ends
        }
        return;
      case 1: {  // Examples: the reader's own sentences when Lexirise has none
        std::string note = str_.noExamples;
        if (w_.examplesOnlyTraditional) {
          note = str_.onlyTraditional;
          if (!w_.traditionalForm.empty()) note += " (" + w_.traditionalForm + ")";
          note += str_.hiddenInSimplified;
        }
        note += str_.fromYourReading;
        f.paragraph(Font::UiSmall, note);  // grey in the reference: black (deviation 1)
        f.gap(m::kParagraphGap);
        const int box = pct(m::kSentenceText, m::kSentenceLineHeightPct);
        f.paragraph(Font::ReaderMedium, kOpenQuote + trimIdeographicSpace(s_.contextSentence.text) + kCloseQuote, box);
        f.gap(m::kParagraphGap);
        if (w_.metBefore) {
          MarkedText met = *w_.metBefore;
          met.markStart += std::strlen(kOpenQuote);
          f.paragraph(Font::ReaderMedium, kOpenQuote + w_.metBefore->text + kCloseQuote, box, &met);
        }
        return;
      }
      case 2: {  // Context: this book's sentence (the word inverted), then where it was met before
        const int box = pct(m::kSentenceText, m::kSentenceLineHeightPct);
        std::string label = str_.thisBook;
        if (s_.pageNumber > 0) label += std::string(str_.separator) + str_.page + " " + std::to_string(s_.pageNumber);
        f.paragraph(Font::UiSmall, label);
        f.gap(m::kLabelGap);
        f.paragraph(Font::ReaderMedium, s_.contextSentence.text, box, &s_.contextSentence, true);
        f.gap(m::kContextGap);
        if (w_.metBefore) {
          f.paragraph(Font::UiSmall, std::string(str_.metBefore) + str_.separator + w_.metBeforeBook);
          f.gap(m::kLabelGap);
          f.paragraph(Font::ReaderMedium, w_.metBefore->text, box, &*w_.metBefore);
        } else {
          f.paragraph(Font::UiSmall, str_.firstTime);
        }
        return;
      }
      case 3:  // Kanji / Chars
        return characters(f);
      case 4:  // Form (Japanese)
        return forms(f);
      default:
        return;
    }
  }

  void characters(Flow& f) {
    const bool kana = !ja_ || s_.reading == ReadingMode::Kana;
    const Font rf = ja_ && kana ? Font::ReaderSmall : Font::UiSmall;
    const int charBox = pct(m::kCharText, m::kCharLineHeightPct);
    for (const CharInfo& c : w_.chars) {
      const std::string reading = kana ? c.reading : c.romaji;
      const int column = std::max({m::kCharColumnMin, tw(rf, reading), tw(Font::ReaderLarge, c.character)});
      const int gx = kContentX + column + m::kCharGap;
      // The row is the taller of the character column and the gloss; the gloss is cut to the body.
      auto gloss = wrapText(m_, Font::Ui, c.gloss, kContentRight - gx, 0);
      const int rowH = lh(rf) + charBox;
      while (!gloss.empty() &&
             !f.room(std::max(rowH, m::kCharGlossPadTop + static_cast<int>(gloss.size()) * lh(Font::Ui)))) {
        gloss.pop_back();
        if (!gloss.empty()) gloss.back() = withEllipsis(m_, Font::Ui, gloss.back(), kContentRight - gx);
      }
      if (!f.room(rowH)) return;
      const int top = f.y();
      out_.text(rf, kContentX + (column - tw(rf, reading)) / 2, top, reading);
      out_.text(Font::ReaderLarge, kContentX + (column - tw(Font::ReaderLarge, c.character)) / 2,
                centred(top + lh(rf), charBox, lh(Font::ReaderLarge)), c.character);
      int gy = top + m::kCharGlossPadTop;
      for (const std::string& line : gloss) {
        out_.text(Font::Ui, gx, gy, line);
        gy += lh(Font::Ui);
      }
      f.gap(std::max(rowH, gy - top) + m::kCharRowGap);
    }
  }

  void forms(Flow& f) {
    if (w_.forms.empty()) {
      f.paragraph(Font::UiSmall, str_.notInflected);  // grey in the reference: black (deviation 1)
      return;
    }
    const int box = pct(m::kFormLabelText, m::kFormLineHeightPct);
    for (const FormInfo& form : w_.forms) {
      if (!f.room(box)) return;
      const std::string shown = fitText(m_, Font::ReaderMedium, form.form, kContentW);
      out_.text(Font::ReaderMedium, kContentX, centred(f.y(), box, lh(Font::ReaderMedium)), shown);
      const int x = kContentX + tw(Font::ReaderMedium, shown) + tw(Font::Ui, " ");
      if (x < kContentRight) {  // no room left for the label beside a form cut to the full width
        out_.text(Font::Ui, x, centred(f.y(), box, lh(Font::Ui)), fitText(m_, Font::Ui, form.label, kContentRight - x));
      }
      f.gap(box);
    }
  }

  void actions(Flow& f) {
    std::vector<std::string> rows;
    if (s_.level != Level::None) rows.emplace_back(str_.actionUndo);
    for (const char* a : str_.actions) rows.emplace_back(a);
    const int h = 2 * m::kActionFrame + 2 * m::kActionPadV + lh(Font::UiSmall);
    for (size_t i = 0; i < rows.size(); i++) {
      if (!f.room(h)) return;
      const Rect r{kContentX, f.y(), kContentW, h};
      out_.frame(r, m::kActionFrame);
      const int ty = r.y + m::kActionFrame + m::kActionPadV;
      out_.text(Font::UiSmall, r.x + m::kActionFrame + m::kActionPadH, ty, rows[i]);
      const int cw = pct(lh(Font::UiSmall), m::kChevronBoxPct);
      out_.shape(Shape::Chevron, {r.right() - m::kActionFrame - m::kActionPadH - cw, ty, cw, lh(Font::UiSmall)});
      // Undo is action 0 only when there's a save to undo; the others keep their ids.
      const int id = s_.level != Level::None ? static_cast<int>(i) : static_cast<int>(i) + 1;
      out_.hit(Target::Action, r, id);
      f.gap(h + m::kActionGap);
    }
  }

  // ---- toast ----

  void toast() {
    const int chrome = 2 * m::kToastFrame + 2 * m::kToastPadH;
    const std::string text = fitText(m_, Font::UiSmall, s_.toast, m::kScreenWidth - 2 * m::kCardInset - chrome);
    const int w = chrome + tw(Font::UiSmall, text);
    const int h = 2 * m::kToastFrame + 2 * m::kToastPadV + lh(Font::UiSmall);
    const Rect r{(m::kScreenWidth - w) / 2, m::kToastTop, w, h};
    out_.fill(r, false);
    out_.frame(r, m::kToastFrame);
    out_.text(Font::UiSmall, r.x + m::kToastFrame + m::kToastPadH, r.y + m::kToastFrame + m::kToastPadV, text);
    out_.toast = r;
    // Front-most: it lies over the card's own targets (T L F K in the expanded view). Without Undo it
    // just swallows the tap, as the reference does.
    out_.hits.insert(out_.hits.begin(), Hit{s_.toastUndo ? Target::ToastUndo : Target::Card, 0, r});
  }

  const CardWord& w_;
  const CardState& s_;
  const TextMetrics& m_;
  const CardStrings& str_;
  const bool ja_;
  DisplayList out_;
  mutable std::vector<std::string> meaningLines_;  // computed once
};

}  // namespace

DisplayList layoutCard(const CardWord& word, const CardState& state, const TextMetrics& metrics,
                       const CardStrings& strings) {
  return Layout(word, state, metrics, strings).run();
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
