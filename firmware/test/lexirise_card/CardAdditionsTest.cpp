// v0.2 V6, signed off 2026-09-30 (docs/v0.2/reference/v6-card-additions.html): the "also" reading on the reading
// line (C15), the ⋯ tab's sentence preview (C3), Undo ignore (C17) and the home screen's session summary (C1, C7), as
// laid out (fake metrics: Latin 8 px, CJK 16 px in the small fonts) and as the controller plays them on the bench.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "FakeMetrics.h"
#include "lexirise/LexiriseConfig.h"
#include "lexirise/card/BenchSource.h"
#include "lexirise/card/CardController.h"
#include "lexirise/card/CardLayout.h"
#include "lexirise/card/CardMetrics.h"

using namespace lexipoint::card;
namespace m = lexipoint::card::metrics;
namespace config = lexipoint::config;
using lexipoint::Language;
using lexipoint::card::test::FakeMetrics;

namespace {

const FakeMetrics kMetrics;
constexpr int kContentX = m::kCardInset + m::kCardFrame + m::kRowPadH;  // 34
constexpr int kContentW = m::kScreenWidth - 2 * kContentX;              // 412
constexpr int kReadingRoom = 226;  // the header's left column: 412 − T L F K's 175 − the 11 px gap

std::vector<const Command*> texts(const DisplayList& l) {
  std::vector<const Command*> out;
  for (const Command& c : l.commands) {
    if (c.kind == Command::Kind::Text) out.push_back(&c);
  }
  return out;
}

const Command* textCommand(const DisplayList& l, const std::string& text) {
  for (const Command& c : l.commands) {
    if (c.kind == Command::Kind::Text && c.text == text) return &c;
  }
  return nullptr;
}

std::vector<Hit> hits(const DisplayList& l, const Target t, const int index = -1) {
  std::vector<Hit> out;
  for (const Hit& h : l.hits) {
    if (h.target == t && (index < 0 || h.index == index)) out.push_back(h);
  }
  return out;
}

// 一日 as the mockup draws it: いちにち / ichinichi, and the lookup's other readings.
CardWord ichinichi() {
  CardWord w;
  w.language = Language::Japanese;
  w.reading = "いちにち";
  w.romaji = "ichinichi";
  w.word = "一日";
  w.partOfSpeech = "noun";
  w.senses = {"one day"};
  w.also = {{"ついたち", "tsuitachi"},
            {"いちじつ", "ichijitsu"},
            {"つきたち", "tsukitachi"},
            {"ひとひ", "hitohi"},
            {"いっぴ", "ippi"}};
  return w;
}

}  // namespace

// ---- C15: the "also" reading ----

TEST(AlsoReading, WholeReadingsAsFitThenAnEllipsis) {
  const CardWord w = ichinichi();
  CardState s;
  const DisplayList list = layoutCard(w, s, kMetrics);
  const Command* reading = textCommand(list, "いちにち");
  ASSERT_NE(reading, nullptr);
  EXPECT_EQ(reading->font, Font::ReaderSmall);
  // " · also " (64) + ついたち (64) + … (8) after the reading (64): 200 of 226; a second one (+ ", " and 64) wouldn't.
  const Command* also = textCommand(list, " \xC2\xB7 also ついたち\xE2\x80\xA6");
  ASSERT_NE(also, nullptr);
  EXPECT_EQ(also->font, Font::UiSmall);  // its kana in the reader's font (TextRuns), as the surface line's
  EXPECT_EQ(also->rect.x, kContentX + 64);
  // On the reading's baseline.
  EXPECT_EQ(also->rect.y + kMetrics.ascender(Font::UiSmall), reading->rect.y + kMetrics.ascender(Font::ReaderSmall));
  EXPECT_LE(64 + kMetrics.width(Font::UiSmall, also->text), kReadingRoom);
}

TEST(AlsoReading, TheTapAreaGrowsWithTheLine) {
  const CardWord w = ichinichi();
  CardState s;
  const auto tap = hits(layoutCard(w, s, kMetrics), Target::ReadingLine);
  ASSERT_EQ(tap.size(), 1u);
  EXPECT_EQ(tap[0].rect.x, kContentX - m::kReadingTapPadH);
  EXPECT_EQ(tap[0].rect.w, 64 + 136 + 2 * m::kReadingTapPadH);  // the reading, the also text, 11 each side
  CardWord plain = w;
  plain.also.clear();
  EXPECT_EQ(hits(layoutCard(plain, s, kMetrics), Target::ReadingLine)[0].rect.w, 64 + 2 * m::kReadingTapPadH);
}

TEST(AlsoReading, TheRomajiTapSwitchesTheAlternativesToo) {
  const CardWord w = ichinichi();
  CardState s;
  s.reading = ReadingMode::Romaji;
  const DisplayList list = layoutCard(w, s, kMetrics);
  ASSERT_NE(textCommand(list, "ichinichi"), nullptr);
  // ichinichi (72) + " · also " (64) + tsuitachi (72) + … (8) = 216 of 226.
  EXPECT_NE(textCommand(list, " \xC2\xB7 also tsuitachi\xE2\x80\xA6"), nullptr);
}

TEST(AlsoReading, AllThatFitAreShownWithoutAnEllipsis) {
  CardWord w = ichinichi();
  w.also.resize(1);
  CardState s;
  EXPECT_NE(textCommand(layoutCard(w, s, kMetrics), " \xC2\xB7 also ついたち"), nullptr);
  w.also = {{"ひ", "hi"}, {"か", "ka"}};  // two short ones: 64 + 64 + 16 + 16 + 16 = 176
  EXPECT_NE(textCommand(layoutCard(w, s, kMetrics), " \xC2\xB7 also ひ, か"), nullptr);
}

TEST(AlsoReading, ChinesePinyinAsGivenWithoutAToggle) {
  CardWord w;
  w.language = Language::Chinese;
  w.reading = "cháng";
  w.word = "长";
  w.also = {{"zhǎng", "zhǎng"}};
  CardState s;
  s.reading = ReadingMode::Romaji;  // a Japanese setting: no effect on Chinese
  const DisplayList list = layoutCard(w, s, kMetrics);
  EXPECT_NE(textCommand(list, " \xC2\xB7 also zhǎng"), nullptr);
  EXPECT_TRUE(hits(list, Target::ReadingLine).empty());
}

TEST(AlsoReading, NoneWhenNotEvenOneFitsOrBeforePhaseB) {
  CardWord w = ichinichi();
  w.reading = "いちにちいちにちいちにち";  // 192 px: no room for " · also " and one reading
  CardState s;
  const DisplayList list = layoutCard(w, s, kMetrics);
  ASSERT_EQ(texts(list).size(), texts(layoutCard(
                                          [&] {
                                            CardWord plain = w;
                                            plain.also.clear();
                                            return plain;
                                          }(),
                                          s, kMetrics))
                                    .size());
  for (const Command* c : texts(list)) EXPECT_EQ(c->text.find("also"), std::string::npos) << c->text;
  // Phase A: the lookup's readings aren't there yet (the word is drawn as the approved card draws it).
  CardState analyzed;
  analyzed.phase = Phase::Analyzed;
  const DisplayList phaseA = layoutCard(ichinichi(), analyzed, kMetrics);
  for (const Command* c : texts(phaseA)) {
    EXPECT_EQ(c->text.find("also"), std::string::npos) << c->text;
  }
}

TEST(AlsoReading, AWordWithNoneDrawsTheApprovedCard) {
  CardWord w = benchJapanese().words[2];
  CardState s;
  const DisplayList plain = layoutCard(w, s, kMetrics);
  w.also.clear();
  const DisplayList same = layoutCard(w, s, kMetrics);
  ASSERT_EQ(plain.commands.size(), same.commands.size());
  EXPECT_EQ(plain.hits.size(), same.hits.size());
}

// ---- C3: the sentence preview ----

namespace {

// The bench card's ⋯ tab on word `word`, and the controller that plays it.
struct Actions {
  BenchSource source;
  CardController c;
  unsigned long now = 0;
  explicit Actions(const int word = 2) : source(benchJapanese(), false), c(source, ReadingMode::Kana) {
    c.open(now);
    while (c.word() < word) c.step(+1, now);
    while (c.word() > word) c.step(-1, now);
    now += 60 * 1000;
    c.tick(now);
    const Hit rank{Target::RankRow, 0, {}};
    const Hit more{Target::Tab, tabCount(Language::Japanese) - 1, {}};
    c.tap(&rank, now);
    c.tap(&more, now);
  }
  Outcome tap(const int action) {
    const Hit h{Target::Action, action, {}};
    return c.tap(&h, ++now);
  }
  DisplayList layout() const {
    CardState s = c.state();
    return layoutCard(c.currentWord(), s, kMetrics);
  }
};

}  // namespace

TEST(SentencePreviewCard, TheRowOpensThePreviewWithTheWordUnderlined) {
  Actions a;
  EXPECT_EQ(a.tap(ActionId::SaveSentence).effect, Effect::Redraw);
  ASSERT_TRUE(a.c.state().preview);
  EXPECT_EQ(a.c.state().preview->text, "毎朝の満員電車が煩わしくて、彼はとうとう会社を辞めることにした。");
  EXPECT_EQ(a.c.state().preview->text.substr(a.c.state().preview->markStart, a.c.state().preview->markLength),
            "煩わしくて");
  EXPECT_TRUE(a.c.state().toast.empty());  // nothing saved yet
  const DisplayList list = a.layout();
  // The sentence as the Context tab's Met before: ReaderMedium, the word underlined.
  bool sentence = false;
  for (const Command* t : texts(list)) sentence = sentence || (t->font == Font::ReaderMedium);
  EXPECT_TRUE(sentence);
  EXPECT_TRUE(std::any_of(list.commands.begin(), list.commands.end(),
                          [](const Command& c) { return c.kind == Command::Kind::Line; }));
  // Shorter | Longer: two half-width rows 10 px apart, the ⋯ rows' 2 px frame, text centred, no ›.
  const auto shorter = hits(list, Target::Action, ActionId::Shorter);
  const auto longer = hits(list, Target::Action, ActionId::Longer);
  ASSERT_EQ(shorter.size(), 1u);
  ASSERT_EQ(longer.size(), 1u);
  const int half = (kContentW - m::kActionGap) / 2;
  EXPECT_EQ(shorter[0].rect.x, kContentX);
  EXPECT_EQ(shorter[0].rect.w, half);
  EXPECT_EQ(longer[0].rect.x, kContentX + half + m::kActionGap);
  EXPECT_EQ(longer[0].rect.right(), kContentX + kContentW);
  EXPECT_EQ(shorter[0].rect.y, longer[0].rect.y);
  EXPECT_EQ(shorter[0].rect.h, 2 * m::kActionFrame + 2 * m::kActionPadV + kMetrics.lineHeight(Font::UiSmall));
  for (const Rect& r : {shorter[0].rect, longer[0].rect}) {
    const auto frame = std::find_if(list.commands.begin(), list.commands.end(),
                                    [&](const Command& c) { return c.kind == Command::Kind::Frame && c.rect == r; });
    ASSERT_NE(frame, list.commands.end());
    EXPECT_EQ(frame->thickness, m::kActionFrame);
  }
  const Command* shorterText = textCommand(list, "Shorter");
  ASSERT_NE(shorterText, nullptr);
  EXPECT_EQ(shorterText->rect.x, kContentX + (half - kMetrics.width(Font::UiSmall, "Shorter")) / 2);
  EXPECT_EQ(shorterText->rect.y, shorter[0].rect.y + m::kActionFrame + m::kActionPadV);
  // Then the same row, which saves: below the pair, a row's gap apart, with its ›.
  const auto save = hits(list, Target::Action, ActionId::SaveSentenceNow);
  ASSERT_EQ(save.size(), 1u);
  EXPECT_EQ(save[0].rect.y, shorter[0].rect.bottom() + m::kActionGap);
  EXPECT_EQ(save[0].rect.w, kContentW);
  const int chevrons = static_cast<int>(std::count_if(list.commands.begin(), list.commands.end(), [](const Command& c) {
    return c.kind == Command::Kind::Shape && c.shape == Shape::Chevron;
  }));
  EXPECT_EQ(chevrons, 1);
  EXPECT_NE(textCommand(list, "Save the sentence as a card"), nullptr);
  EXPECT_TRUE(hits(list, Target::Action, ActionId::SaveSentence).empty());  // the rows are gone
}

TEST(SentencePreviewCard, ShorterAndLongerAClauseAtATime) {
  Actions a;
  a.tap(ActionId::SaveSentence);
  // The word's clause is the first: Shorter drops the last.
  EXPECT_EQ(a.tap(ActionId::Shorter).effect, Effect::Redraw);
  EXPECT_EQ(a.c.state().preview->text, "毎朝の満員電車が煩わしくて、");
  EXPECT_EQ(a.tap(ActionId::Shorter).effect, Effect::None);  // the word's own clause stays: nothing changes
  EXPECT_EQ(a.c.state().preview->text, "毎朝の満員電車が煩わしくて、");
  a.tap(ActionId::Longer);  // puts it back
  EXPECT_EQ(a.c.state().preview->text, "毎朝の満員電車が煩わしくて、彼はとうとう会社を辞めることにした。");
  a.tap(ActionId::Longer);  // none dropped: the page's next clause
  EXPECT_EQ(a.c.state().preview->text, "毎朝の満員電車が煩わしくて、彼はとうとう会社を辞めることにした。春の風が、");
  a.tap(ActionId::Longer);
  EXPECT_EQ(a.c.state().preview->text,
            "毎朝の満員電車が煩わしくて、彼はとうとう会社を辞めることにした。春の風が、少しだけ優しく感じられた。");
  EXPECT_FALSE(a.c.state().previewLonger);                                  // the page's end
  EXPECT_EQ(a.tap(ActionId::Longer).effect, Effect::None);                  // never past it
  EXPECT_TRUE(hits(a.layout(), Target::Action, ActionId::Longer).empty());  // no target with nothing to do
}

TEST(SentencePreviewCard, ShorterDropsTheFirstClauseWhileOneIsBeforeTheWord) {
  Actions a(3);  // 彼: the second clause
  a.tap(ActionId::SaveSentence);
  a.tap(ActionId::Shorter);
  EXPECT_EQ(a.c.state().preview->text, "彼はとうとう会社を辞めることにした。");
  EXPECT_EQ(a.c.state().preview->markStart, 0u);
  a.tap(ActionId::Longer);
  EXPECT_EQ(a.c.state().preview->text, "毎朝の満員電車が煩わしくて、彼はとうとう会社を辞めることにした。");
}

TEST(SentencePreviewCard, SavingGoesBackToTheRowsWithItsToast) {
  Actions a;
  a.tap(ActionId::SaveSentence);
  a.tap(ActionId::Shorter);
  const Outcome o = a.tap(ActionId::SaveSentenceNow);
  ASSERT_EQ(o.sentences.size(), 1u);
  EXPECT_FALSE(o.sentences[0].undo);
  EXPECT_EQ(o.sentences[0].text, "毎朝の満員電車が煩わしくて、");
  EXPECT_EQ(o.sentences[0].readyAtMs, a.now + config::kToastMs);  // after its Undo window, as a word's save
  EXPECT_FALSE(a.c.state().preview);
  EXPECT_EQ(a.c.state().toast, "Sentence saved as a card  \xC2\xB7  Undo");
  EXPECT_TRUE(a.c.state().toastUndo);
  EXPECT_EQ(a.c.nextDueMs(), a.now + config::kToastMs);  // 2 s
  EXPECT_FALSE(hits(a.layout(), Target::Action, ActionId::SaveSentence).empty());
  const Hit undo{Target::ToastUndo, 0, {}};
  const Outcome back = a.c.tap(&undo, ++a.now);
  ASSERT_EQ(back.sentences.size(), 1u);
  EXPECT_TRUE(back.sentences[0].undo);
  EXPECT_EQ(back.sentences[0].readyAtMs, a.now);  // an Undo is sent (or drops the waiting save) at once
}

TEST(SentencePreviewCard, AnyOtherWayOutLeavesItUnsaved) {
  const auto opened = [] {
    auto a = std::make_unique<Actions>();
    a->tap(ActionId::SaveSentence);
    EXPECT_TRUE(a->c.state().preview);
    return a;
  };
  {  // the ⋯ tab again
    auto a = opened();
    const Hit more{Target::Tab, tabCount(Language::Japanese) - 1, {}};
    EXPECT_EQ(a->c.tap(&more, ++a->now).effect, Effect::Redraw);
    EXPECT_FALSE(a->c.state().preview);
  }
  {  // another tab
    auto a = opened();
    const Hit meaning{Target::Tab, 0, {}};
    a->c.tap(&meaning, ++a->now);
    EXPECT_FALSE(a->c.state().preview);
  }
  {  // a side-button step (and back: the rows, not the preview)
    auto a = opened();
    a->c.step(+1, ++a->now);
    a->c.step(-1, ++a->now);
    EXPECT_FALSE(a->c.state().preview);
  }
  {  // back to the card view
    auto a = opened();
    a->c.home();
    EXPECT_FALSE(a->c.state().preview);
  }
  {  // a stale tap on the rows' Save sentence while it's open: only opens it (never saves)
    auto a = opened();
    a->tap(ActionId::Shorter);
    const Outcome o = a->tap(ActionId::SaveSentence);
    EXPECT_TRUE(o.sentences.empty());
    EXPECT_EQ(a->c.state().preview->text, "毎朝の満員電車が煩わしくて、");
  }
}

TEST(SentencePreviewCard, ALongSentenceIsCutAndLongerStopsBeforeItWouldBe) {
  CardState s;
  s.view = View::Expanded;
  s.tab = tabCount(Language::Japanese) - 1;
  s.level = Level::Learning;
  const std::string clause = "あいうえおかきくけこさしすせそたちつてと、";  // 21 characters: a 412 px line holds 20
  std::string longText;
  for (int i = 0; i < 20; i++) longText += clause;
  s.preview = MarkedText{longText, 0, 3};
  s.previewLonger = MarkedText{longText + clause, 0, 3};
  const DisplayList list = layoutCard(benchJapanese().words[2], s, kMetrics);
  bool cut = false;
  for (const Command* t : texts(list)) {
    if (t->font == Font::ReaderMedium && t->text.size() >= 3 && t->text.substr(t->text.size() - 3) == "\xE2\x80\xA6") {
      cut = true;
    }
  }
  EXPECT_TRUE(cut);                                                             // wrapText's line limit, with …
  EXPECT_TRUE(hits(list, Target::Action, ActionId::Longer).empty());            // Longer would only be cut too
  EXPECT_EQ(hits(list, Target::Action, ActionId::SaveSentenceNow).size(), 1u);  // the rows stay on the card
  EXPECT_LE(hits(list, Target::Action, ActionId::SaveSentenceNow)[0].rect.bottom(), list.card.bottom());
  // A short one that Longer can grow and still fit: the target is there.
  s.preview = MarkedText{clause, 0, 3};
  s.previewLonger = MarkedText{clause + clause, 0, 3};
  EXPECT_EQ(hits(layoutCard(benchJapanese().words[2], s, kMetrics), Target::Action, ActionId::Longer).size(), 1u);
}

// ---- C17: Undo ignore ----

TEST(UndoIgnore, TheRowReadsUndoIgnoreForAnIgnoredWord) {
  CardState s;
  s.view = View::Expanded;
  s.tab = tabCount(Language::Japanese) - 1;
  s.level = Level::Learning;
  const CardWord w = benchJapanese().words[2];
  EXPECT_NE(textCommand(layoutCard(w, s, kMetrics), "Ignore this word"), nullptr);
  s.ignored = true;
  const DisplayList list = layoutCard(w, s, kMetrics);
  EXPECT_EQ(textCommand(list, "Ignore this word"), nullptr);
  const Command* row = textCommand(list, "Undo ignore");
  ASSERT_NE(row, nullptr);
  const auto hit = hits(list, Target::Action, ActionId::Ignore);  // the same row, the same id
  ASSERT_EQ(hit.size(), 1u);
  EXPECT_TRUE(hit[0].rect.contains(row->rect.x, row->rect.y));
  s.level = Level::None;  // no Undo save row: still the third row's words
  EXPECT_NE(textCommand(layoutCard(w, s, kMetrics), "Undo ignore"), nullptr);
}

TEST(UndoIgnore, TheNewToastsFitWithTheirUndo) {
  const CardStrings str;
  for (const std::string& text :
       {std::string(str.noLongerIgnored) + str.undoSuffix, std::string(str.sdCardFailed), std::string(str.cantIgnore),
        std::string(str.actionDone[actionIndex(ActionId::SaveSentence)]) + str.undoSuffix}) {
    CardState s;
    s.toast = text;
    const DisplayList list = layoutCard(benchJapanese().words[0], s, kMetrics);
    EXPECT_NE(textCommand(list, text), nullptr) << text;  // whole, not cut
  }
}

// ---- C1, C7: the summary on the home screen ----

TEST(SessionSummaryBox, TwoLinesInTheToastsFrameAndType) {
  const std::vector<std::string> lines = {"3 saved \xC2\xB7 11 looked up", "1,204 words in Japanese"};
  const DisplayList list = layoutSummary(lines, kMetrics);
  const int lineH = kMetrics.lineHeight(Font::UiSmall);
  EXPECT_EQ(list.toast.y, m::kToastTop);
  EXPECT_EQ(list.toast.h, 2 * m::kToastFrame + 2 * m::kToastPadV + 2 * lineH);
  const int widest = std::max(kMetrics.width(Font::UiSmall, lines[0]), kMetrics.width(Font::UiSmall, lines[1]));
  EXPECT_EQ(list.toast.w, 2 * m::kToastFrame + 2 * m::kToastPadH + widest);
  EXPECT_EQ(list.toast.x, (480 - list.toast.w) / 2);
  const auto frame = std::find_if(list.commands.begin(), list.commands.end(), [&](const Command& c) {
    return c.kind == Command::Kind::Frame && c.rect == list.toast;
  });
  ASSERT_NE(frame, list.commands.end());
  EXPECT_EQ(frame->thickness, m::kToastFrame);
  const Command* second = textCommand(list, lines[1]);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->font, Font::UiSmall);
  EXPECT_EQ(second->rect.y, m::kToastTop + m::kToastFrame + m::kToastPadV + lineH);
  EXPECT_EQ(second->rect.x, list.toast.x + (list.toast.w - kMetrics.width(Font::UiSmall, lines[1])) / 2);  // centred
  EXPECT_TRUE(list.hits.empty());  // not a target: the home screen's next input dismisses it
  EXPECT_EQ(layoutSummary({lines[0]}, kMetrics).toast.h, 2 * m::kToastFrame + 2 * m::kToastPadV + lineH);
  EXPECT_TRUE(layoutSummary({}, kMetrics).commands.empty());
}

// ---- V6 review R1-R2 ----

TEST(SentencePreviewCard, TheSentenceIsDrawnInBracketsButSavedBare) {
  Actions a;
  a.tap(ActionId::SaveSentence);
  const DisplayList list = a.layout();
  bool opens = false;
  for (const Command* t : texts(list)) {
    if (t->font == Font::ReaderMedium && t->text.rfind("\xE3\x80\x8C", 0) == 0) opens = true;  // 「
  }
  EXPECT_TRUE(opens);
  EXPECT_EQ(a.c.state().preview->text.rfind("\xE3\x80\x8C", 0), std::string::npos);
  const Outcome o = a.tap(ActionId::SaveSentenceNow);
  ASSERT_EQ(o.sentences.size(), 1u);
  EXPECT_EQ(o.sentences[0].text.find("\xE3\x80\x8C"), std::string::npos);
}

TEST(SentencePreviewCard, ASwipeToAnotherTabOrTheRankRowTwiceClosesIt) {
  Actions a;
  a.tap(ActionId::SaveSentence);
  ASSERT_TRUE(a.c.state().preview);
  a.c.swipe(Swipe::Right);  // the tab before ⋯
  EXPECT_FALSE(a.c.state().preview);
  a.c.swipe(Swipe::Left);  // back to ⋯: the rows
  EXPECT_TRUE(isActionsTab(Language::Japanese, a.c.state().tab));
  EXPECT_FALSE(a.c.state().preview);
  a.tap(ActionId::SaveSentence);
  const Hit rank{Target::RankRow, 0, {}};
  a.c.tap(&rank, ++a.now);
  a.c.tap(&rank, ++a.now);
  EXPECT_EQ(a.c.state().view, View::Expanded);
  EXPECT_FALSE(a.c.state().preview);
}

TEST(UndoIgnore, OnTheBenchAfterTheToastExpires) {
  Actions a;
  ASSERT_EQ(a.tap(ActionId::Ignore).ignores.size(), 1u);
  a.now += config::kIgnoreToastMs;
  a.c.tick(a.now);
  ASSERT_TRUE(a.c.state().toast.empty());
  const Outcome o = a.tap(ActionId::Ignore);
  ASSERT_EQ(o.ignores.size(), 1u);
  EXPECT_FALSE(o.ignores[0].ignored);
  EXPECT_FALSE(a.c.state().ignored);
  EXPECT_EQ(a.c.state().toast, "No longer ignored  \xC2\xB7  Undo");
  EXPECT_TRUE(a.tap(ActionId::Ignore).ignores.empty());  // its Undo is up: a second tap does nothing
}

TEST(SessionSummaryBox, EveryLineIsCentred) {
  const std::vector<std::string> lines = {"1 saved \xC2\xB7 1 looked up", "1,204 words in Japanese"};
  const DisplayList list = layoutSummary(lines, kMetrics);
  for (const std::string& line : lines) {
    const Command* c = textCommand(list, line);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->rect.x, list.toast.x + (list.toast.w - kMetrics.width(Font::UiSmall, line)) / 2) << line;
  }
  EXPECT_NE(kMetrics.width(Font::UiSmall, lines[0]), kMetrics.width(Font::UiSmall, lines[1]));
}

TEST(SentencePreviewCard, LongerKeepsItsTargetWhenItFillsTheLinesExactly) {
  CardState s;
  s.view = View::Expanded;
  s.tab = tabCount(Language::Japanese) - 1;
  s.level = Level::Learning;
  const CardWord w = benchJapanese().words[2];
  // `lines` full lines once in 「」: 20 characters (20 px each) fill a 412 px line, the brackets two of them.
  const auto lines = [](const int n) {
    std::string t;
    for (int i = 0; i < n * 20 - 2; i++) t += "あ";
    return MarkedText{t, 0, 3};
  };
  // How many lines the sentence gets: a sentence far too long is cut to exactly that many.
  s.preview = lines(40);
  const DisplayList cut = layoutCard(w, s, kMetrics);
  int maxLines = 0;
  for (const Command* t : texts(cut)) maxLines += t->font == Font::ReaderMedium ? 1 : 0;
  ASSERT_GT(maxLines, 1);
  s.preview = lines(1);
  s.previewLonger = lines(maxLines);  // exactly the lines there are: fits uncut
  EXPECT_EQ(hits(layoutCard(w, s, kMetrics), Target::Action, ActionId::Longer).size(), 1u);
  s.previewLonger = lines(maxLines + 1);
  EXPECT_TRUE(hits(layoutCard(w, s, kMetrics), Target::Action, ActionId::Longer).empty());
}

TEST(AlsoReading, AReadingThatDoesntFitWithItsEllipsisIsLeftOut) {
  CardWord w = ichinichi();
  // いちにち (64) + " · also " (64) + あいうえお (80) = 208: with ", か" (+32) no; with "…" (+8) 216 of 226: yes.
  w.also = {{"あいうえお", "aiueo"}, {"か", "ka"}};
  CardState s;
  EXPECT_NE(textCommand(layoutCard(w, s, kMetrics), " \xC2\xB7 also あいうえお\xE2\x80\xA6"), nullptr);
  // One px less room than that needs: none at all.
  w.also = {{"あいうえおか", "aiueoka"}, {"か", "ka"}};  // 224 + 8 = 232 > 226
  const DisplayList list = layoutCard(w, s, kMetrics);
  for (const Command* c : texts(list)) EXPECT_EQ(c->text.find("also"), std::string::npos) << c->text;
}

TEST(SessionSummaryBox, ALineTooLongIsCutToTheScreen) {
  const std::string longLine(200, 'x');
  const DisplayList list = layoutSummary({longLine}, kMetrics);
  EXPECT_LE(list.toast.w, 480 - 2 * m::kCardInset);
  const std::vector<const Command*> t = texts(list);
  ASSERT_EQ(t.size(), 1u);
  EXPECT_EQ(t[0]->text.substr(t[0]->text.size() - 3), "\xE2\x80\xA6");
}

TEST(SessionSummaryBox, TheBoxClearsWhatsUnderIt) {
  const DisplayList list = layoutSummary({"3 saved \xC2\xB7 11 looked up"}, kMetrics);
  EXPECT_EQ(list.commands.front().kind, Command::Kind::Fill);
  EXPECT_EQ(list.commands.front().rect, list.toast);
  EXPECT_FALSE(list.commands.front().black);
}

TEST(SessionSummaryBox, TheBoxFitsItsWidestLineWhicheverItIs) {
  const std::vector<std::string> lines = {"12 saved \xC2\xB7 140 looked up", "5 words in Japanese"};
  ASSERT_GT(kMetrics.width(Font::UiSmall, lines[0]), kMetrics.width(Font::UiSmall, lines[1]));
  const DisplayList list = layoutSummary(lines, kMetrics);
  EXPECT_EQ(list.toast.w, 2 * m::kToastFrame + 2 * m::kToastPadH + kMetrics.width(Font::UiSmall, lines[0]));
}

TEST(BenchSentence, TheLaterSentencesAreKeptApart) {
  BenchBook book = benchJapanese();
  book.lines.push_back({{"雨が降る。晴れ", -1}});
  const BenchSource source(book, false);
  const auto sentence = source.sentenceForSave(0);
  ASSERT_TRUE(sentence);
  EXPECT_EQ(sentence->after, (std::vector<LaterSentence>{
                                 {"春の風が、少しだけ優しく感じられた。", ""}, {"雨が降る。", ""}, {"晴れ", ""}}));
}
