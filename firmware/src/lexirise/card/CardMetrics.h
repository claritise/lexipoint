#pragma once

// The card's measurements in device pixels on the 480×800 panel: popup-ui.md §1.1, BINDING. Each value
// is the table's device column (the reference's CSS px × 480/340, rounded). Nothing else in the card
// code may hold a layout number. Change one only with claritise's sign-off, together with §1.1 and
// reference/card-reference.html.

namespace lexipoint::card::metrics {

// `percent` % of `px`, rounded: the §1.1 line heights (1.7, 1.8 …) and the drawn glyphs' proportions.
constexpr int percentOf(const int px, const int percent) { return (px * percent + 50) / 100; }

// The card: bottom-anchored, inset from the screen's left/right/bottom edges.
constexpr int kCardInset = 14;
constexpr int kCardFrame = 3;
constexpr int kDivider = 1;  // header, meaning, tabs, rank row, ✕ cell
constexpr int kRowPadV = 11;
constexpr int kRowPadH = 17;

// Header: reading (small, top), word (large), badge; T L F K and the state text on the right.
constexpr int kReadingText = 17;
constexpr int kWordText = 42;
constexpr int kWordLineBox = 51;  // line-height 1.2
constexpr int kBadgeText = 17;
constexpr int kBadgeFrame = 3;
constexpr int kBadgePadH = 7;
constexpr int kBadgeLineBox = 25;
constexpr int kBadgeGap = 11;  // after the word
constexpr int kLevelCellWidth = 42;
constexpr int kLevelPadV = 7;
constexpr int kLevelText = 18;
constexpr int kLevelFrame = 2;
constexpr int kLevelDivider = 1;
constexpr int kLevelCells = 4;  // T L F K
constexpr int kStateText = 16;
constexpr int kStateGap = 6;
constexpr int kReadingTapPadTop = 8;  // the reading line's tap area (Japanese): text + 8 top, 11 each side
constexpr int kReadingTapPadH = 11;
constexpr int kSurfaceText = 17;  // surface form + conjugation line
constexpr int kSurfaceGap = 3;    // above it (the reference's 2 px margin)
constexpr int kPillText = 16;
constexpr int kPillFrame = 1;
constexpr int kPillRadius = 4;
constexpr int kPillPadH = 7;

// Meaning (card view): ≤ 2 lines.
constexpr int kMeaningText = 20;
constexpr int kMeaningMaxLines = 2;

// Rank row: frequency bars, "#29,774 rare", ▼/▲, then the ✕ cell.
constexpr int kRankPadV = 14;
constexpr int kRankPadH = 17;
constexpr int kRankText = 17;
constexpr int kBarWidth = 6;  // incl. its 1 px frame
constexpr int kBarFrame = 1;
constexpr int kBarGap = 3;
constexpr int kBarHeights[] = {8, 13, 17, 21, 25};
constexpr int kBarCount = 5;
constexpr int kCloseCellWidth = 62;
constexpr int kCloseGlyph = 21;
constexpr int kArrowGlyph = 17;  // ▼ / ▲

// Expanded view.
// The expanded card: the reference's 500 px tall, bottom-anchored (§1.1: the panel's ~10 px of extra height
// goes to the strip, so the strip is 800 − 14 − 706 = 80, the table's 71 plus that). P12: at most 706; the strip
// is at least 80, more for a page line that needs it (CardLayout::expandedCardTop).
constexpr int kExpandedCardHeight = 706;
constexpr int kStripPadH = 28;  // the page's side padding (the reference's .strip, 20)
constexpr int kStripMarkerText = 14;
constexpr int kStripMarkerRight = 11;
constexpr int kStripMarkerTop = 6;
constexpr int kStripClip = 56;         // the strip's text ends this far before the right padding edge
constexpr int kStripScrollInset = 11;  // an active word past the clip ends this far inside it
constexpr int kBodyPadTop = 17;        // the tab content's top padding (the reference's 12)

// Card-view strip (D17): the card's first row, only when the card covers the word. It's set in the reader's
// font at the reader's size, so both strips grow with a bigger size (P12, claritise: "when font size is bigger,
// the row doesn't expand"): each is at least its reference height, and at least the page line plus
// kStripTextPadV above and below it.
constexpr int kCardStripHeight = 51;  // the minimum (the reference's)
// The least air above and below a strip's line: at the default 14 pt (NotoSerifCJK's line is 42 px), 42 + 8
// stays within the reference's 51, so the approved card doesn't move; 16 and 18 pt grow the row.
constexpr int kStripTextPadV = 4;
constexpr int kCardStripPadH = 17;
constexpr int kCardStripMarkerText = 14;
constexpr int kCardStripMarkerRight = 8;
constexpr int kCardStripMarkerTop = 3;

// Tab row.
constexpr int kTabText = 16;
constexpr int kTabPadV = 11;
constexpr int kMoreTabWidth = 42;  // ⋯
constexpr int kMoreGlyph = 20;

// Tab content.
constexpr int kSenseText = 20;
constexpr int kSenseLineHeightPct = 170;
constexpr int kSentenceText = 21;
constexpr int kSentenceLineHeightPct = 180;
constexpr int kLabelText = 16;
constexpr int kLabelGap = 6;       // under a small label (the reference's 4)
constexpr int kParagraphGap = 11;  // between sentences (the reference's 8)
constexpr int kContextGap = 14;    // after this book's sentence (the reference's 10)
constexpr int kCharReadingText = 16;
constexpr int kCharText = 40;
constexpr int kCharLineHeightPct = 110;
constexpr int kCharGlossText = 20;
constexpr int kCharGap = 20;          // between the character column and the gloss
constexpr int kCharColumnMin = 48;    // the character column's min width (the reference's 34)
constexpr int kCharGlossPadTop = 20;  // (the reference's 14)
constexpr int kCharRowGap = 14;       // under each character (the reference's 10)
constexpr int kFormText = 23;
constexpr int kFormLabelText = 20;
constexpr int kFormLineHeightPct = 200;
constexpr int kActionFrame = 2;
constexpr int kActionPadV = 11;
constexpr int kActionPadH = 14;
constexpr int kActionText = 18;
constexpr int kActionGap = 10;

// Toast ("Saved as learning · Undo").
constexpr int kToastFrame = 3;
constexpr int kToastText = 17;
constexpr int kToastPadV = 6;
constexpr int kToastPadH = 14;
constexpr int kToastTop = 85;

// The word highlight, on the page and in the strips: inverted, with this much padding each side.
constexpr int kHighlightPadH = 1;
// The underline under the word in a "met before" sentence: this far above the line box's bottom.
constexpr int kUnderlineRaise = 2;
constexpr int kUnderlineThickness = 1;
// The ⋯ rows' › : its box, as a share of the text's line height.
constexpr int kChevronBoxPct = 50;
// Not in §1.1 (the reference never overflows): the least room kept between the header's left column
// (word, badge, reading) and T L F K when a long word has to be cut.
constexpr int kHeaderColumnGap = kBadgeGap;

// The reader family's sizes the card draws with: the real .cpfont sizes nearest §1.1's (logged in the P4
// ledger note: the word 42 → 37.5 px, readings 17 → 16.7, sentences 21 → 20.8).
constexpr int kReaderSmallPt = 8;    // 16.7 px
constexpr int kReaderMediumPt = 10;  // 20.8 px
constexpr int kReaderLargePt = 18;   // 37.5 px

// The bench's page (P4 only): the reference's own .pg, scaled. Deviation 3: its text is the reader font.
constexpr int kBenchPagePadTop = 31;    // the reference's 22
constexpr int kBenchPagePadLeft = 28;   // the reference's 20
constexpr int kBenchPagePadRight = 28;  // the reference's 20: the device font is wider, so lines wrap here
constexpr int kBenchPageLineBox = 52;   // the reference's 19 px × line-height 1.95

// The glyphs no device font has (✕ ▼ ▲ ⋯ ›), drawn as shapes in their box (deviation 2): the ink's
// proportions, in % of the box. ShapeGeometry.h draws them.
namespace shape {
constexpr int kTriangleWidthPct = 65;  // of the box
constexpr int kTriangleHeightPct = 55;
constexpr int kCrossPct = 60;
constexpr int kStrokeWidth = 2;
constexpr int kDotPct = 12;      // ⋯: each dot, of the box
constexpr int kDotMin = 2;       // px: a dot never smaller than this
constexpr int kDotSpanPct = 70;  // the three dots' span
constexpr int kChevronWidthPct = 45;
constexpr int kChevronHeightPct = 70;
}  // namespace shape

// The panel the measurements are for.
constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 800;

}  // namespace lexipoint::card::metrics
