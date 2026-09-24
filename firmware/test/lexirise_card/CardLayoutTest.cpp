// popup-ui.md §1.1 (binding): the card's boxes, in device pixels, from the layout. Fixed fake metrics
// (FakeMetrics.h), so font-dependent heights are known; the §1.1 numbers are checked exactly.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "FakeMetrics.h"
#include "RefreshStrength.h"
#include "lexirise/card/BenchPage.h"
#include "lexirise/card/CardController.h"
#include "lexirise/card/CardFrame.h"
#include "lexirise/card/CardLayout.h"
#include "lexirise/card/CardMetrics.h"
#include "lexirise/card/ShapeGeometry.h"
#include "lexirise/card/ShownTargets.h"
#include "lexirise/card/TextRuns.h"
#include "lexirise/text/Utf8Prefix.h"

using namespace lexipoint::card;
namespace m = lexipoint::card::metrics;
using lexipoint::Language;
using lexipoint::card::test::FakeMetrics;

namespace {

const FakeMetrics kMetrics;

struct Built {
  CardWord word;
  CardState state;
  DisplayList list;
};

Built build(const BenchBook& book, const int word, const View view = View::Card, const bool low = false,
            const int tab = 0) {
  const auto scene = bench::layoutPage(book, word, low, true, kMetrics);
  Built b{book.words[word], bench::benchState(book, word, scene), {}};
  b.state.view = view;
  b.state.tab = tab;
  b.list = layoutCard(b.word, b.state, kMetrics);
  return b;
}

bool has(const DisplayList& l, const Command::Kind kind, const Rect& r) {
  return std::any_of(l.commands.begin(), l.commands.end(),
                     [&](const Command& c) { return c.kind == kind && c.rect == r; });
}

std::vector<Hit> hits(const DisplayList& l, const Target t) {
  std::vector<Hit> out;
  for (const Hit& h : l.hits) {
    if (h.target == t) out.push_back(h);
  }
  return out;
}

const Command* textCommand(const DisplayList& l, const std::string& text) {
  for (const Command& c : l.commands) {
    if (c.kind == Command::Kind::Text && c.text == text) return &c;
  }
  return nullptr;
}

}  // namespace

TEST(CardLayout, TheFrameIsBottomAnchoredAndInset) {
  const auto b = build(benchJapanese(), 2);
  EXPECT_EQ(b.list.card.x, m::kCardInset);
  EXPECT_EQ(b.list.card.w, 480 - 2 * m::kCardInset);
  EXPECT_EQ(b.list.card.bottom(), 800 - m::kCardInset);
  EXPECT_TRUE(has(b.list, Command::Kind::Frame, b.list.card));
  const auto frame = std::find_if(b.list.commands.begin(), b.list.commands.end(), [&](const Command& c) {
    return c.kind == Command::Kind::Frame && c.rect == b.list.card;
  });
  EXPECT_EQ(frame->thickness, 3);
}

TEST(CardLayout, CardViewRowsJapanese) {
  const auto b = build(benchJapanese(), 2);  // 煩わしい: saved as learning
  // Header: 11 + max(reading 24 + word 51, levels (2+7+23+7+2 = 41) + 6 + 23) + 3 + surface 25 + 11 = 125.
  // Meaning: 11 + 2 × 24 + 11 = 70 (both senses need 3 lines at the fake width: the first alone, 2 lines).
  // Rank: 1 + 14 + 25 + 14 = 54. Card: 3 + 125 + 1 + 70 + 54 + 3 = 256.
  EXPECT_EQ(b.list.card.h, 256);
  const int top = b.list.card.y;
  // T L F K: 175 wide (2 + 4 × 42 + 3 + 2), right-aligned at the content edge (446), 11 below the frame.
  EXPECT_TRUE(has(b.list, Command::Kind::Frame, {446 - 175, top + 3 + 11, 175, 41}));
  const auto levels = hits(b.list, Target::Level);
  ASSERT_EQ(levels.size(), 4u);
  for (int i = 0; i < 4; i++) {
    EXPECT_EQ(levels[i].rect.w, 42);
    EXPECT_EQ(levels[i].rect.x, 446 - 175 + 2 + i * 43);
  }
  EXPECT_TRUE(has(b.list, Command::Kind::Fill, levels[1].rect));  // L is filled (learning)
  ASSERT_NE(textCommand(b.list, "learning"), nullptr);
  // ✕ cell 62 wide at the inner right edge, its divider 1 px left of it.
  const auto close = hits(b.list, Target::Close);
  ASSERT_EQ(close.size(), 1u);
  EXPECT_EQ(close[0].rect, (Rect{463 - 62, 786 - 3 - 53, 62, 53}));
  EXPECT_TRUE(has(b.list, Command::Kind::Fill, {463 - 62 - 1, 786 - 3 - 53, 1, 53}));
  // The rank row's tap area: from the inner left edge to the divider.
  EXPECT_EQ(hits(b.list, Target::RankRow)[0].rect, (Rect{17, 786 - 3 - 53, 400 - 17, 53}));
}

TEST(CardLayout, BadgePillAndBars) {
  const auto b = build(benchJapanese(), 2);
  // The badge: frame 3, 7 px padding, line box 25; 11 px after the word.
  const Command* word = textCommand(b.list, "煩わしい");
  ASSERT_NE(word, nullptr);
  const int badgeX = 34 + 4 * 38 + 11;
  const int badgeW = 2 * 3 + 2 * 7 + 16;  // "N1": two half-em letters
  bool badge = false;
  for (const Command& c : b.list.commands) {
    if (c.kind == Command::Kind::Frame && c.rect.x == badgeX && c.rect.w == badgeW && c.rect.h == 31) badge = true;
  }
  EXPECT_TRUE(badge);
  // The POS pill: rounded, frame 1, radius 4, 7 px padding.
  bool pill = false;
  for (const Command& c : b.list.commands) {
    if (c.kind == Command::Kind::RoundedFrame) {
      pill = true;
      EXPECT_EQ(c.thickness, 1);
      EXPECT_EQ(c.radius, 4);
      EXPECT_EQ(c.rect.w, 2 + 14 + FakeMetrics().width(Font::UiSmall, "adjective"));
    }
  }
  EXPECT_TRUE(pill);
  // Five bars 6 wide, 3 apart, heights 8 13 17 21 25, bottom-aligned; ⌈0.181 × 5⌉ = 1 filled.
  std::vector<Command> bars;
  for (const Command& c : b.list.commands) {
    if ((c.kind == Command::Kind::Fill || c.kind == Command::Kind::Frame) && c.rect.w == 6 && c.rect.h >= 8 &&
        c.rect.h <= 25 && c.rect.x >= 34 && c.rect.x < 34 + 45) {
      bars.push_back(c);
    }
  }
  ASSERT_EQ(bars.size(), 5u);
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(bars[i].rect.x, 34 + 9 * i);
    EXPECT_EQ(bars[i].rect.h, m::kBarHeights[i]);
    EXPECT_EQ(bars[i].rect.bottom(), bars[0].rect.bottom());
    EXPECT_EQ(bars[i].kind, i == 0 ? Command::Kind::Fill : Command::Kind::Frame);
  }
  EXPECT_NE(textCommand(b.list, "#29,774 rare"), nullptr);
}

TEST(CardLayout, ChineseHasNoSurfaceLineOrReadingToggle) {
  const auto b = build(benchChinese(), 5);                 // 选择
  EXPECT_TRUE(hits(b.list, Target::ReadingLine).empty());  // pinyin doesn't toggle
  EXPECT_NE(textCommand(b.list, "xuǎn zé"), nullptr);
  EXPECT_NE(textCommand(b.list, "HSK 4"), nullptr);
  EXPECT_NE(textCommand(b.list, "fresh"), nullptr);
  const auto ja = build(benchJapanese(), 2);
  ASSERT_EQ(hits(ja.list, Target::ReadingLine).size(), 1u);
  const Rect rd = hits(ja.list, Target::ReadingLine)[0].rect;
  EXPECT_EQ(rd.x, 34 - 11);
  EXPECT_EQ(rd.y, ja.list.card.y + 3 + 11 - 8);
}

TEST(CardLayout, ExpandedView) {
  const auto b = build(benchJapanese(), 2, View::Expanded);
  EXPECT_EQ(b.list.card.h, m::kExpandedCardHeight);  // bottom-anchored; the strip takes the rest (80)
  EXPECT_EQ(b.list.card.y, 800 - 14 - 706);
  EXPECT_EQ(b.list.card.bottom(), 786);
  // Tabs: Meaning Examples Context Kanji Form, then ⋯ (42 wide, a divider left of it).
  const auto tabs = hits(b.list, Target::Tab);
  ASSERT_EQ(tabs.size(), 6u);
  EXPECT_EQ(tabs[0].rect.x, 17);
  EXPECT_EQ(tabs[4].rect.right(), 463 - 42 - 1);
  EXPECT_EQ(tabs[5].rect, (Rect{463 - 42, tabs[0].rect.y, 42, tabs[0].rect.h}));
  EXPECT_EQ(tabs[0].rect.h, 11 + 23 + 11);
  EXPECT_TRUE(has(b.list, Command::Kind::Fill, tabs[0].rect));  // the active tab is filled
  // The rank row is the same as in card view, now pointing up.
  bool up = false;
  for (const Command& c : b.list.commands) up = up || (c.kind == Command::Kind::Shape && c.shape == Shape::TriangleUp);
  EXPECT_TRUE(up);
  EXPECT_EQ(hits(b.list, Target::Close)[0].rect.bottom(), 786 - 3);
  // The strip's marker: "line 2/5", 11 from the right edge, 6 from the top.
  const Command* marker = textCommand(b.list, "line 2/5");
  ASSERT_NE(marker, nullptr);
  EXPECT_EQ(marker->rect.y, 6);
  EXPECT_EQ(marker->rect.x + FakeMetrics().width(Font::UiSmall, "line 2/5"), 480 - 11);
  // Meaning tab: numbered senses.
  EXPECT_NE(textCommand(b.list, "1."), nullptr);
  EXPECT_NE(textCommand(b.list, "2."), nullptr);
}

TEST(CardLayout, EveryTabRendersSomething) {
  for (const BenchBook* book : {&benchJapanese(), &benchChinese()}) {
    for (int tab = 0; tab < tabCount(book->language); tab++) {
      const auto b = build(*book, book->start, View::Expanded, false, tab);
      const int body = std::count_if(b.list.commands.begin(), b.list.commands.end(), [&](const Command& c) {
        return c.rect.y > b.list.card.y + 130 && c.rect.y < 786 - 100;
      });
      EXPECT_GT(body, 0) << "tab " << tab;
    }
  }
  // ⋯ with a saved word: Undo first; unsaved: no Undo, the other actions keep their ids.
  const auto saved = build(benchJapanese(), 2, View::Expanded, false, 5);
  const auto actions = hits(saved.list, Target::Action);
  ASSERT_EQ(actions.size(), 4u);
  EXPECT_EQ(actions[0].index, 0);
  const auto unsaved = build(benchJapanese(), 0, View::Expanded, false, 5);
  ASSERT_EQ(hits(unsaved.list, Target::Action).size(), 3u);
  EXPECT_EQ(hits(unsaved.list, Target::Action)[0].index, 1);
  // Form tab of a word with no forms: the note.
  EXPECT_NE(textCommand(build(benchJapanese(), 0, View::Expanded, false, 4).list,
                        "Not inflected here: this is the dictionary form."),
            nullptr);
}

TEST(CardLayout, CardViewStripOnlyWhenTheWordIsCovered) {
  // D17: the strip row appears when the word's box on the page ends below the card's top.
  const auto base = build(benchJapanese(), 2);
  const int cardTop = base.list.card.y;
  CardState s = base.state;
  s.wordOnPage = Rect{100, cardTop - 36, 60, 36};  // ends exactly at the card's top: visible
  const auto clear = layoutCard(base.word, s, kMetrics);
  EXPECT_EQ(clear.card, base.list.card);
  s.wordOnPage = Rect{100, cardTop - 35, 60, 36};  // one pixel under it: covered
  const auto covered = layoutCard(base.word, s, kMetrics);
  EXPECT_EQ(covered.card.h - base.list.card.h, m::kCardStripHeight + 1);  // the strip row and its divider
  EXPECT_EQ(covered.card.bottom(), 786);
  const std::string mark = "line 2/5";
  const Command* marker = textCommand(covered, mark);
  ASSERT_NE(marker, nullptr);
  EXPECT_EQ(marker->rect.y, covered.card.y + 3 + m::kCardStripMarkerTop);
  EXPECT_EQ(marker->rect.x + FakeMetrics().width(Font::UiSmall, mark), 463 - 8);
  EXPECT_TRUE(has(covered, Command::Kind::Fill, {17, covered.card.y + 3 + m::kCardStripHeight, 446, 1}));
  EXPECT_EQ(textCommand(clear, mark), nullptr);
}

TEST(CardLayout, StripScrollsTheActiveWordIntoView) {
  CardWord w = benchJapanese().words[0];
  CardState s;
  s.view = View::Expanded;
  s.strip.tokens = {{"一二三四五六七八九十一二三四五", 0, 390}, {"語", 390, 26}};
  s.strip.activeFirst = s.strip.activeLast = 1;
  const auto list = layoutCard(w, s, kMetrics);
  const Command* word = textCommand(list, "語");
  ASSERT_NE(word, nullptr);
  const int clipRight = 480 - 28 - 56;
  EXPECT_EQ(word->rect.x + 26, clipRight - 11);                             // ends 11 inside the clip
  EXPECT_EQ(textCommand(list, "一二三四五六七八九十一二三四五"), nullptr);  // pushed past the left edge
}

TEST(CardLayout, Phases) {
  CardWord w = benchJapanese().words[2];
  CardState s;
  s.phase = Phase::Pending;
  s.pendingText = "煩";
  auto list = layoutCard(w, s, kMetrics);
  EXPECT_NE(textCommand(list, "煩…"), nullptr);
  EXPECT_TRUE(hits(list, Target::Level).empty());
  EXPECT_TRUE(hits(list, Target::RankRow).empty());
  EXPECT_EQ(hits(list, Target::Close).size(), 1u);  // closing always works
  s.phase = Phase::Analyzed;
  list = layoutCard(w, s, kMetrics);
  EXPECT_EQ(hits(list, Target::Level).size(), 4u);
  EXPECT_EQ(textCommand(list, "N1"), nullptr);  // the badge comes with phase B
  EXPECT_EQ(textCommand(list, "#29,774 rare"), nullptr);
  s.phase = Phase::TranslationPending;
  list = layoutCard(w, s, kMetrics);
  EXPECT_NE(textCommand(list, "translation pending"), nullptr);
}

TEST(CardLayout, Toast) {
  CardState s;
  s.toast = "Saved as learning  ·  Undo";
  const auto list = layoutCard(benchJapanese().words[0], s, kMetrics);
  EXPECT_EQ(list.toast.y, m::kToastTop);
  EXPECT_EQ(list.toast.h, 2 * 3 + 2 * 6 + 23);
  EXPECT_EQ(list.toast.x, (480 - list.toast.w) / 2);
}

TEST(CardLayout, HitsArePrioritisedOverTheCard) {
  const auto b = build(benchJapanese(), 2);
  const Rect close = hits(b.list, Target::Close)[0].rect;
  EXPECT_EQ(b.list.hitAt(close.x + 5, close.y + 5)->target, Target::Close);
  EXPECT_EQ(b.list.hitAt(b.list.card.x + 5, b.list.card.y + 5)->target, Target::Card);
  EXPECT_EQ(b.list.hitAt(5, 5), nullptr);  // the page: outside the card
}

TEST(CardText, WrapAndEllipsis) {
  const auto lines = wrapText(kMetrics, Font::Ui, "aa bb cc dd", 50, 0);  // 10 px per letter
  EXPECT_EQ(lines, (std::vector<std::string>{"aa bb", "cc dd"}));
  const auto cut = wrapText(kMetrics, Font::Ui, "aa bb cc dd ee ff", 50, 2);
  ASSERT_EQ(cut.size(), 2u);
  EXPECT_EQ(cut[1], "cc d…");  // cut by characters, as far as fits
  EXPECT_EQ(wrapText(kMetrics, Font::Ui, "満員電車", 45, 0), (std::vector<std::string>{"満員", "電車"}));
  EXPECT_EQ(meaningText({"he, him", "boyfriend, lover"}, kMetrics, 412), "he, him; boyfriend, lover");
  std::string twoLines;  // 28 × "ab": exactly two lines at 412 px (14 × 30 − 10 = 410 each)
  for (int i = 0; i < 28; i++) twoLines += i ? " ab" : "ab";
  EXPECT_EQ(meaningText({twoLines, "y"}, kMetrics, 412), twoLines);  // the second sense would need a third
}

TEST(CardText, RankAndBars) {
  EXPECT_EQ(formatRank(29774), "#29,774");
  EXPECT_EQ(formatRank(182), "#182");
  EXPECT_EQ(formatRank(1234567), "#1,234,567");
  EXPECT_EQ(rankBand(182), 0);
  EXPECT_EQ(rankBand(1238), 1);
  EXPECT_EQ(rankBand(6864), 2);
  EXPECT_EQ(rankBand(22811), 3);
  EXPECT_EQ(filledBars(0.0f), 1);
  EXPECT_EQ(filledBars(0.181f), 1);
  EXPECT_EQ(filledBars(0.586f), 3);
  EXPECT_EQ(filledBars(1.0f), 5);
  EXPECT_EQ(tabCount(Language::Japanese), 6);
  EXPECT_EQ(tabCount(Language::Chinese), 5);
}

TEST(BenchFixtures, AreTheReferencesWords) {
  const BenchBook& ja = benchJapanese();
  ASSERT_EQ(ja.words.size(), 6u);
  EXPECT_EQ(ja.words[2].word, "煩わしい");
  EXPECT_EQ(ja.saved[2], Level::Learning);
  ASSERT_TRUE(ja.words[2].metBefore);
  const auto& met = *ja.words[2].metBefore;
  EXPECT_EQ(met.text.substr(met.markStart, met.markLength), "煩わしい");
  const BenchBook& zh = benchChinese();
  EXPECT_EQ(zh.start, 5);
  EXPECT_EQ(zh.saved[5], Level::Fresh);
  EXPECT_TRUE(zh.words[5].examplesOnlyTraditional);
  const auto scene = bench::layoutPage(ja, 2, false, true, kMetrics);
  EXPECT_EQ(scene.sentence.text.substr(scene.sentence.markStart, scene.sentence.markLength), "煩わしくて");
  EXPECT_EQ(scene.strip.lineNumber, 2);
}

TEST(CardText, KinsokuKeepsPunctuationWithItsWord) {
  // 20 px per CJK character, 3 characters a line: 。」 can't start a line and 「 can't end one.
  const auto lines = wrapText(kMetrics, Font::Ui, "ああ。」「いい", 60, 0);
  ASSERT_GE(lines.size(), 2u);
  for (const std::string& line : lines) {
    EXPECT_NE(line.rfind("。", 0), 0u) << line;
    EXPECT_NE(line.rfind("」", 0), 0u) << line;
    EXPECT_FALSE(line.size() >= 3 && line.compare(line.size() - 3, 3, "「") == 0) << line;
  }
}

TEST(CardText, UiTextRunsSetCjkInTheReaderFamily) {
  const auto runs = textRuns(Font::UiSmall, "Met before · ノルウェイの森");
  ASSERT_EQ(runs.size(), 2u);
  EXPECT_EQ(runs[0].font, Font::UiSmall);
  EXPECT_EQ(runs[1].font, Font::ReaderSmall);
  EXPECT_EQ(runs[1].text, "ノルウェイの森");
  EXPECT_EQ(textRuns(Font::Ui, "漢").front().font, Font::ReaderMedium);
  EXPECT_EQ(textRuns(Font::Page, "abc漢").size(), 1u);  // reader fonts have both
  // Placed runs share the command font's baseline.
  const auto placed = placeRuns(kMetrics, Font::UiSmall, 10, 100, "ab漢",
                                [](const Font f, const std::string& t) { return FakeMetrics().width(f, t); });
  ASSERT_EQ(placed.size(), 2u);
  EXPECT_EQ(placed[1].x, 10 + 16);
  EXPECT_EQ(placed[0].y + kMetrics.ascender(Font::UiSmall), placed[1].y + kMetrics.ascender(Font::ReaderSmall));
}

TEST(CardShapes, InkStaysInsideItsBox) {
  for (const Shape s : {Shape::Cross, Shape::TriangleDown, Shape::TriangleUp, Shape::Ellipsis, Shape::Chevron}) {
    const Rect box{100, 200, 21, 21};
    const ShapeInk ink = shapeInk(s, box);
    const auto inside = [&](const Point& p) { return box.contains(p.x, p.y); };
    for (const auto& poly : ink.polygons) {
      for (const Point& p : poly) EXPECT_TRUE(inside(p));
    }
    for (const auto& st : ink.strokes) EXPECT_TRUE(inside(st.from) && inside(st.to));
    for (const Rect& d : ink.dots) EXPECT_TRUE(inside({d.x, d.y}) && inside({d.right() - 1, d.bottom() - 1}));
    EXPECT_FALSE(ink.polygons.empty() && ink.strokes.empty() && ink.dots.empty());
  }
  EXPECT_EQ(shapeInk(Shape::Ellipsis, {0, 0, 20, 20}).dots.size(), 3u);
}

TEST(CardLayout, LongWordsNeverRunIntoTheLevels) {
  CardWord w = benchJapanese().words[2];
  w.word = "国際連合安全保障理事会";
  w.reading = std::string(60, 'k');
  w.surface = "国際連合安全保障理事会の決議によって";
  w.conjugation = "a-very-long-conjugation-label";
  CardState s;
  const auto list = layoutCard(w, s, kMetrics);
  const Rect levels = hits(list, Target::Level)[0].rect;
  for (const Command& c : list.commands) {
    if (c.kind != Command::Kind::Text || c.rect.y >= levels.y + 70) continue;  // the reading and word rows
    if (c.rect.x >= levels.x - 2) continue;                                    // T L F K's own labels, the state
    EXPECT_LE(c.rect.x + kMetrics.width(c.font, c.text), levels.x - m::kHeaderColumnGap) << c.text;
  }
  EXPECT_EQ(textCommand(list, "N1"), nullptr);  // no room for the badge: dropped
  for (const Command& c : list.commands) {
    if (c.kind == Command::Kind::Text) EXPECT_LE(c.rect.x + kMetrics.width(c.font, c.text), 446) << c.text;
  }
}

TEST(CardLayout, BodyContentStaysInTheBody) {
  CardWord w = benchJapanese().words[2];
  w.chars[0].gloss = std::string(400, 'g');  // wraps far past the body
  for (int i = 0; i < 12; i++) w.chars.push_back(w.chars[0]);
  CardState s;
  s.view = View::Expanded;
  s.tab = 3;
  const auto list = layoutCard(w, s, kMetrics);
  const int tabsTop = hits(list, Target::Tab)[0].rect.y;
  for (const Command& c : list.commands) {
    if (c.kind == Command::Kind::Text && c.rect.y > list.card.y + 130 && c.rect.y < tabsTop) {
      EXPECT_LE(c.rect.y + kMetrics.lineHeight(c.font), tabsTop) << c.text;
    }
  }
  // A paragraph cut by the body ends with …
  s.tab = 2;
  s.contextSentence.text = std::string(3000, 'x');
  const auto ctx = layoutCard(w, s, kMetrics);
  bool ellipsis = false;
  for (const Command& c : ctx.commands) {
    ellipsis = ellipsis || (c.kind == Command::Kind::Text && c.text.size() > 3 &&
                            c.text.compare(c.text.size() - 3, 3, "\xE2\x80\xA6") == 0);
  }
  EXPECT_TRUE(ellipsis);
}

TEST(CardLayout, MissingData) {
  CardWord w = benchJapanese().words[0];
  w.senses.clear();
  w.rank = 0;
  CardState s;
  s.level = Level::Fresh;
  const auto list = layoutCard(w, s, kMetrics);
  EXPECT_EQ(textCommand(list, "#6,864 uncommon"), nullptr);
  int fresh = 0;
  for (const Command& c : list.commands) fresh += c.kind == Command::Kind::Text && c.text == "fresh";
  EXPECT_EQ(fresh, 2);  // under T L F K, and alone in the rank row (no rank)
  EXPECT_EQ(filledBars(std::nanf("")), 1);
}

TEST(CardLayout, ToastUndoIsFrontMost) {
  CardState s;
  s.view = View::Expanded;
  s.toast = "Saved as learning  \xC2\xB7  Undo";
  s.toastUndo = true;
  const auto list = layoutCard(benchJapanese().words[2], s, kMetrics);
  ASSERT_FALSE(list.hits.empty());
  EXPECT_EQ(list.hits[0].target, Target::ToastUndo);
  EXPECT_EQ(list.hitAt(list.toast.x + 2, list.toast.y + 2)->target, Target::ToastUndo);
  s.toastUndo = false;  // "Readings: romaji": no target, taps go to what's under it
  EXPECT_NE(layoutCard(benchJapanese().words[2], s, kMetrics).hits[0].target, Target::ToastUndo);
}

TEST(CardLayout, MeaningNumbersAreBoldAndWrapToTheEdge) {
  CardWord w = benchJapanese().words[2];
  std::string sense;
  for (int i = 0; i < 30; i++) sense += i ? " ww" : "ww";
  w.senses = {sense};
  CardState s;
  s.view = View::Expanded;
  const auto list = layoutCard(w, s, kMetrics);
  const Command* number = textCommand(list, "1.");
  ASSERT_NE(number, nullptr);
  EXPECT_EQ(number->font, Font::UiBold);
  int lines = 0;
  for (const Command& c : list.commands) {
    if (c.kind == Command::Kind::Text && c.font == Font::Ui && c.text.find('w') != std::string::npos) {
      lines++;
      EXPECT_LE(c.rect.x + kMetrics.width(c.font, c.text), 446);
    }
  }
  EXPECT_GE(lines, 2);
}

TEST(CardFrame, ComposesThePageOnlyInCardView) {
  CardController c(benchJapanese(), ReadingMode::Kana, false);
  c.open(0);
  c.tick(60000);
  Frame f = composeFrame(c, kMetrics);
  EXPECT_TRUE(f.pageShown);
  EXPECT_FALSE(f.scene.page.commands.empty());
  EXPECT_EQ(f.card.card.bottom(), 786);
  const Hit* rank = nullptr;
  for (const Hit& h : f.card.hits) {
    if (h.target == Target::RankRow) rank = &h;
  }
  ASSERT_NE(rank, nullptr);
  c.tap(rank, 60000);
  f = composeFrame(c, kMetrics);
  EXPECT_FALSE(f.pageShown);
  EXPECT_EQ(f.card.card.h, m::kExpandedCardHeight);
}

TEST(CardLayout, AMarkedWordNeverCoversTheEllipsis) {
  // Context tab, a sentence the body cuts, the word just past the last line that fits.
  CardWord w = benchJapanese().words[2];
  w.metBefore.reset();
  CardState s;
  s.view = View::Expanded;
  s.tab = 2;
  for (int i = 0; i < 400; i++) s.contextSentence.text += "あ";
  s.contextSentence.text += "煩";
  s.contextSentence.markStart = s.contextSentence.text.size() - 3;
  s.contextSentence.markLength = 3;
  for (int i = 0; i < 400; i++) s.contextSentence.text += "い";
  const auto list = layoutCard(w, s, kMetrics);
  const Command* cut = nullptr;
  for (const Command& c : list.commands) {
    if (c.kind == Command::Kind::Text && c.font == Font::ReaderMedium && c.text.size() >= 3 &&
        c.text.compare(c.text.size() - 3, 3, "\xE2\x80\xA6") == 0) {
      cut = &c;
    }
  }
  ASSERT_NE(cut, nullptr);
  for (const Command& c : list.commands) {
    if (c.kind == Command::Kind::Fill && c.rect.y == cut->rect.y) {
      EXPECT_LE(c.rect.right(),
                cut->rect.x + kMetrics.width(cut->font, cut->text) - kMetrics.width(cut->font, "\xE2\x80\xA6") + 1);
    }
  }
}

namespace {
bool endsWithEllipsis(const std::string& t) { return t.size() >= 3 && t.compare(t.size() - 3, 3, "\xE2\x80\xA6") == 0; }
}  // namespace

TEST(CardLayout, ALongSenseIsCutWithEllipsisWhereTheBodyEnds) {
  CardWord w = benchJapanese().words[2];
  std::string sense;
  for (int i = 0; i < 400; i++) sense += i ? " ww" : "ww";
  w.senses = {sense, "second"};
  CardState s;
  s.view = View::Expanded;
  const auto list = layoutCard(w, s, kMetrics);
  const int tabsTop = hits(list, Target::Tab)[0].rect.y;
  bool cut = false;
  for (const Command& c : list.commands) {
    if (c.kind != Command::Kind::Text || c.font != Font::Ui || c.text.find('w') == std::string::npos) continue;
    EXPECT_LE(c.rect.x + kMetrics.width(c.font, c.text), 446) << c.text;
    EXPECT_LE(c.rect.y + kMetrics.lineHeight(c.font), tabsTop) << c.text;
    cut = cut || endsWithEllipsis(c.text);
  }
  EXPECT_TRUE(cut);
  EXPECT_EQ(textCommand(list, "2."), nullptr);  // no room left for the second sense
}

TEST(CardLayout, ALongFormLabelFitsTheCard) {
  CardWord w = benchJapanese().words[2];
  w.forms = {{"煩わしくて", std::string(200, 'l')}};
  CardState s;
  s.view = View::Expanded;
  s.tab = 4;
  const auto list = layoutCard(w, s, kMetrics);
  bool found = false;
  for (const Command& c : list.commands) {
    if (c.kind == Command::Kind::Text && c.font == Font::Ui && c.text.find('l') != std::string::npos) {
      found = true;
      EXPECT_LE(c.rect.x + kMetrics.width(c.font, c.text), 446) << c.text;
    }
  }
  EXPECT_TRUE(found);
}

TEST(CardLayout, ALongToastStaysOnScreen) {
  CardState s;
  s.toast = std::string(200, 't');
  const auto list = layoutCard(benchJapanese().words[0], s, kMetrics);
  EXPECT_GE(list.toast.x, m::kCardInset);
  EXPECT_LE(list.toast.right(), m::kScreenWidth - m::kCardInset);
}

TEST(Refresh, APromotionNeverWeakensTheRefreshAskedFor) {
  enum class Mode { Fast, Half, Full };
  const auto stronger = [](Mode a, Mode b) { return strongerRefresh(a, b, Mode::Full, Mode::Half); };
  EXPECT_EQ(stronger(Mode::Full, Mode::Half), Mode::Full);  // the reader's due full refresh stays full
  EXPECT_EQ(stronger(Mode::Fast, Mode::Half), Mode::Half);  // the card's half refresh on dismiss
  EXPECT_EQ(stronger(Mode::Half, Mode::Full), Mode::Full);
  EXPECT_EQ(stronger(Mode::Fast, Mode::Fast), Mode::Fast);
}

TEST(CardText, FirstCharsNeverSplitACharacter) {
  using lexipoint::text::utf8FirstChars;
  EXPECT_EQ(utf8FirstChars("煩わしい", 2), "煩わ");
  EXPECT_EQ(utf8FirstChars("煩わしい", 0), "");
  EXPECT_EQ(utf8FirstChars("ab", 5), "ab");
  EXPECT_EQ(utf8FirstChars("", 1), "");
}

TEST(CardText, AWordWiderThanTheLineIsCutWhereverItFalls) {
  std::vector<size_t> source;
  const auto lines = wrapText(kMetrics, Font::Ui, "aa bbbbbbbbbb cc", 50, 0, 0, &source);  // 10 px per letter
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "aa");
  EXPECT_LE(kMetrics.width(Font::Ui, lines[1]), 50);
  EXPECT_EQ(lines[1].compare(lines[1].size() - 3, 3, "\xE2\x80\xA6"), 0);
  EXPECT_EQ(lines[2], "cc");
  EXPECT_EQ(source, (std::vector<size_t>{2, 10, 2}));  // the bytes each line covers, before the cut
}

TEST(CardLayout, ALongLatinWordNeverCrossesTheFrame) {
  CardWord w = benchJapanese().words[2];
  w.senses = {"pneumonoultramicroscopicsilicovolcanoconiosisxxxxxxxxxxxx; lung disease"};
  CardState s;
  s.view = View::Expanded;
  const auto list = layoutCard(w, s, kMetrics);
  for (const Command& c : list.commands) {
    if (c.kind == Command::Kind::Text && c.rect.y >= list.card.y) {  // the card, not the strip above it
      EXPECT_LE(c.rect.x + kMetrics.width(c.font, c.text), 446) << c.text;
    }
  }
}

TEST(CardLayout, ALongFormIsCutAndItsLabelDropped) {
  CardWord w = benchJapanese().words[2];
  w.forms = {{std::string(60, 'f'), "the label"}};
  CardState s;
  s.view = View::Expanded;
  s.tab = 4;
  const auto list = layoutCard(w, s, kMetrics);
  for (const Command& c : list.commands) {
    if (c.kind == Command::Kind::Text && c.rect.y >= list.card.y) {
      EXPECT_LE(c.rect.x + kMetrics.width(c.font, c.text), 446) << c.text;
    }
  }
  EXPECT_EQ(textCommand(list, "the label"), nullptr);
}

TEST(ShownTargets, ATapMatchesTheFrameOnScreenWhenItWasMade) {
  const Hit before{Target::Level, 1, {0, 0, 10, 10}};
  const Hit after{Target::Tab, 2, {0, 0, 10, 10}};
  ShownTargets t;
  EXPECT_EQ(t.at(100), nullptr);  // nothing shown yet: the tap is dropped
  t.drawing({before}, 0);
  EXPECT_EQ(t.at(100), nullptr);  // the first frame is still refreshing
  t.shown(200);
  ASSERT_NE(t.at(250), nullptr);
  EXPECT_EQ(t.at(250)->hits.at(0).target, Target::Level);
  EXPECT_EQ(t.at(150), nullptr);  // before the first frame showed
  t.drawing({after}, 0);          // phase B: laid out, refreshing
  EXPECT_EQ(t.at(300)->hits.at(0).target, Target::Level);
  t.shown(500);
  EXPECT_EQ(t.at(400)->hits.at(0).target, Target::Level);  // made during the refresh: the frame the user saw
  EXPECT_EQ(t.at(500)->hits.at(0).target, Target::Tab);
}

TEST(ShownTargets, MillisWrapping) {
  ShownTargets t;
  t.drawing({Hit{Target::Close, 0, {}}}, 0);
  t.shown(0xFFFFFF00UL);
  t.drawing({Hit{Target::Tab, 0, {}}}, 0);
  t.shown(0x10UL);  // after the wrap
  EXPECT_EQ(t.at(0x20UL)->hits.at(0).target, Target::Tab);
  EXPECT_EQ(t.at(0xFFFFFFF0UL)->hits.at(0).target, Target::Close);
}
