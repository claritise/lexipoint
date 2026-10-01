// v0.2 V9a: the page marks (page/PageMarks.h; page-annotations.md §2 "V9a decisions"). The bench first: a static page
// (the signed-off mockup's first paragraph, laid out one CJK break unit per token) and a recorded, synthetic analysis
// (bench/v9a-ja-analysis.json, made up), through the same span → glyph walk as the card's highlight, into the marks'
// rectangles, pinned in bench/v9a-ja-marks.golden (LEXIPOINT_UPDATE_V9A_GOLDEN=1 rewrites it: only for a change
// claritise signed off). Then the rules one by one.

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../lexirise_card/FakeMetrics.h"
#include "Fakes.h"
#include "lexirise/page/CardMarks.h"
#include "lexirise/page/MarkGate.h"
#include "lexirise/page/MarkKeeper.h"
#include "lexirise/page/MarkSlots.h"
#include "lexirise/page/MarkVisibility.h"
#include "lexirise/page/PageMarks.h"
#include "lexirise/page/Prefetch.h"
#include "lexirise/text/SentenceBuilder.h"
#include "lexirise/vocab/VocabMirror.h"

using lexipoint::IgnoredKey;
using lexipoint::Language;
using lexipoint::card::ReaderLine;
using lexipoint::card::ReaderPage;
using lexipoint::card::Rect;
using lexipoint::card::test::FakeMetrics;
using namespace lexipoint::page;

namespace {

const FakeMetrics kMetrics;  // a CJK character 26 px wide, the page's line 36 px, its ascender 28
constexpr int kLeft = 12;
constexpr int kTop = 14;

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

// Splits UTF-8 into its characters.
std::vector<std::string> chars(const std::string& text) {
  std::vector<std::string> out;
  for (size_t i = 0; i < text.size();) {
    size_t n = 1;
    const auto c = static_cast<unsigned char>(text[i]);
    if (c >= 0xF0)
      n = 4;
    else if (c >= 0xE0)
      n = 3;
    else if (c >= 0xC0)
      n = 2;
    out.push_back(text.substr(i, n));
    i += n;
  }
  return out;
}

// The bench page: the mockup's first paragraph in the reader's lines, each line's break units one token each (a CJK
// character, with a closing mark glued to the character before it, as ParsedText cuts them), drawn from kLeft
// without justification, a line every 36 px from kTop.
struct Bench {
  lexipoint::text::PageModel model;
  ReaderPage page;
  Bench() {
    const std::vector<std::string> lines = {"　灯台守の祖父は、毎朝五時に起", "きて海を眺めていた。天気が荒れ",
                                            "そうな日には、窓辺に座ったま", "ま、じっと雲の動きを追ってい", "た。"};
    for (size_t li = 0; li < lines.size(); li++) {
      lexipoint::text::TextLine line;
      line.startsParagraph = li == 0;
      ReaderLine row;
      row.y = kTop + 36 * static_cast<int>(li);
      int x = kLeft;
      for (const std::string& c : chars(lines[li])) {
        const bool closing = c == "、" || c == "。";
        if (closing && !line.tokens.empty()) {
          line.tokens.back() += c;
          row.tokens.back().text += c;
          row.tokens.back().width += FakeMetrics::em(lexipoint::card::Font::Page);
        } else {
          line.tokens.push_back(c);
          row.tokens.push_back({c, x, FakeMetrics::em(lexipoint::card::Font::Page)});
        }
        x += FakeMetrics::em(lexipoint::card::Font::Page);
      }
      model.lines.push_back(line);
      page.lines.push_back(row);
    }
  }
};

PageAnalysis benchAnalysis(std::string* text = nullptr) {
  const std::string body = readFile(std::string(V9A_BENCH_DIR) + "/v9a-ja-analysis.json");
  PageAnalysis page;
  EXPECT_EQ(parsePage(body, Language::Japanese, page), lexipoint::api::ParseStatus::Ok);
  if (text) {
    const size_t key = body.find("\"text\"");
    const size_t at = body.find('"', body.find(':', key)) + 1;
    *text = body.substr(at, body.find('"', at) - at);
  }
  return page;
}

std::string serialize(const std::vector<Rect>& fills) {
  std::string out;
  for (const Rect& r : fills) {
    out +=
        std::to_string(r.x) + " " + std::to_string(r.y) + " " + std::to_string(r.w) + " " + std::to_string(r.h) + "\n";
  }
  return out;
}

}  // namespace

// --- the bench ---

TEST(PageMarksBench, TheBenchPagesMarksArePinned) {
  const Bench bench;
  std::string recordedText;
  const PageAnalysis page = benchAnalysis(&recordedText);
  const auto built = lexipoint::text::buildPageText(bench.model, lexipoint::text::Script::Japanese);
  ASSERT_TRUE(built);
  EXPECT_EQ(built->text, recordedText);  // the analysis is of this very page text

  const std::vector<SpanMark> marks = pageMarks(page, MarkSources{});
  const std::vector<MarkRun> runs = markRuns(bench.page, *built, marks, kMetrics);
  std::vector<Rect> fills;
  const int below = markBelowBaseline(FakeMetrics::em(lexipoint::card::Font::Page));
  for (const MarkRun& run : runs) markFills(run, kMetrics.ascender(lexipoint::card::Font::Page), below, fills);

  const std::string golden = std::string(V9A_BENCH_DIR) + "/v9a-ja-marks.golden";
  if (std::getenv("LEXIPOINT_UPDATE_V9A_GOLDEN")) {
    std::ofstream(golden, std::ios::binary) << serialize(fills);
    GTEST_SKIP() << "rewrote " << golden;
  }
  EXPECT_EQ(serialize(fills), readFile(golden));
}

TEST(PageMarksBench, EveryUnsavedWordIsMarkedParticlesIncluded) {
  const PageAnalysis page = benchAnalysis();
  const std::vector<SpanMark> marks = pageMarks(page, MarkSources{});
  // The words and their marks, in order (the bench's states: 祖父 and じっと level 2, 荒れる 1, まま 3, 起きる 座る 雲
  // 追う 4; the rest never saved).
  std::string shown;
  for (const SpanMark& m : marks) {
    const Occ* occ = nullptr;
    for (const Occ& o : page.occurrences) {
      if (o.start == m.start && o.end == m.end) occ = &o;
    }
    ASSERT_NE(occ, nullptr);
    shown += std::string(page.str(occ->word)) + (m.mark == Mark::New ? "_" : ".");
  }
  EXPECT_EQ(shown,
            "灯台守_の_祖父.は_毎朝_五_時_に_海_を_眺めて_いた_天気_が_荒れ.そう_な_日_に_は_窓辺_に_じっと.の_動き_を_"
            "いた_");
}

// --- the rules ---

TEST(PageMarks, TheLevelsDecideTheMark) {
  EXPECT_EQ(markFor({}), Mark::New);                            // never saved
  EXPECT_EQ(markFor({true, 0, false, false}), Mark::New);       // saved at level 0
  EXPECT_EQ(markFor({true, 1, false, false}), Mark::Learning);  // tracked
  EXPECT_EQ(markFor({true, 2, false, false}), Mark::Learning);  // learning
  EXPECT_EQ(markFor({true, 3, false, false}), Mark::None);      // fresh
  EXPECT_EQ(markFor({true, 4, false, false}), Mark::None);      // known
  EXPECT_EQ(markFor({true, 2, true, false}), Mark::None);       // suspended in Lexirise
  EXPECT_EQ(markFor({false, 0, false, true}), Mark::None);      // ignored on the reader
}

TEST(PageMarks, TheLemmasStateComesFirstThenTheWordsOwn) {
  PageAnalysis page = benchAnalysis();
  // 起きて: its lemma 起きる (110) is known. Its own entry (109) saved as tracked changes nothing.
  const Occ* okite = nullptr;
  for (const Occ& o : page.occurrences) {
    if (page.str(o.word) == "起きて") okite = &o;
  }
  ASSERT_NE(okite, nullptr);
  EXPECT_EQ(markFor(wordStateOf(page, *okite, MarkSources{})), Mark::None);
  // With the lemma unsaved (the mirror says so), the word's own entry answers.
  struct Sources final : MarkSources {
    MirrorSays mirror(const uint32_t id) const override {
      MirrorSays says;
      if (id == 110) says.verdict = MirrorSays::Verdict::Unsaved;
      if (id == 109) {
        says.verdict = MirrorSays::Verdict::Saved;
        says.state.savedExpressionId = "1";
        says.state.proficiency = 1;
      }
      return says;
    }
  } sources;
  EXPECT_EQ(markFor(wordStateOf(page, *okite, sources)), Mark::Learning);
}

TEST(PageMarks, TheMirrorOutranksTheSnapshotWhenItSays) {
  const PageAnalysis page = benchAnalysis();
  struct Sources final : MarkSources {
    MirrorSays mirror(const uint32_t id) const override {
      MirrorSays says;
      if (id == 103) {  // 祖父, learning on the page: known since
        says.verdict = MirrorSays::Verdict::Saved;
        says.state.savedExpressionId = "5103";
        says.state.proficiency = 4;
      }
      if (id == 129) {  // じっと: suspended in Lexirise
        says.verdict = MirrorSays::Verdict::Saved;
        says.state.proficiency = 2;
        says.suspended = true;
      }
      if (id == 130) says.verdict = MirrorSays::Verdict::Unsaved;  // 雲: removed since
      if (id == 101) {                                             // 灯台守: saved since, at level 0
        says.verdict = MirrorSays::Verdict::Saved;
        says.state.proficiency = 0;
      }
      return says;
    }
  } sources;
  const auto markOf = [&](const std::string& word) {
    for (const Occ& o : page.occurrences) {
      if (page.str(o.word) == word) return markFor(wordStateOf(page, o, sources));
    }
    return Mark::None;
  };
  EXPECT_EQ(markOf("祖父"), Mark::None);
  EXPECT_EQ(markOf("じっと"), Mark::None);
  EXPECT_EQ(markOf("雲"), Mark::New);
  EXPECT_EQ(markOf("灯台守"), Mark::New);
  EXPECT_EQ(markOf("荒れ"), Mark::Learning);  // the mirror doesn't speak: the snapshot stands
}

TEST(PageMarks, AnIgnoredWordIsNotMarked) {
  const PageAnalysis page = benchAnalysis();
  struct Sources final : MarkSources {
    mutable std::vector<IgnoredKey> asked;
    bool ignored(const IgnoredKey& key) const override {
      asked.push_back(key);
      return key.entryId == 120 || key.entryId == 102;  // 荒れる (by its lemma's entry) and の
    }
  } sources;
  const std::vector<IgnoredKey>& asked = sources.asked;
  const std::vector<SpanMark> marks = pageMarks(page, sources);
  for (const SpanMark& m : marks) {
    for (const Occ& o : page.occurrences) {
      if (o.start == m.start) EXPECT_TRUE(page.str(o.word) != "荒れ" && page.str(o.word) != "の");
    }
  }
  EXPECT_FALSE(asked.empty());
  for (const IgnoredKey& key : asked) EXPECT_EQ(key.language, Language::Japanese);
}

TEST(PageMarks, PunctuationAndEmptySpansNeverMark) {
  PageAnalysis page = benchAnalysis();
  for (const SpanMark& m : pageMarks(page, MarkSources{})) {
    for (const Occ& o : page.occurrences) {
      if (o.start == m.start) EXPECT_TRUE(o.wordLike);
    }
  }
}

TEST(PageMarks, AWordAcrossALineEndIsMarkedOnBothLines) {
  const Bench bench;
  const auto built = lexipoint::text::buildPageText(bench.model, lexipoint::text::Script::Japanese);
  ASSERT_TRUE(built);
  // まま: the last character of line 3 and the first of line 4.
  const uint32_t at = lexipoint::text::utf16Length(built->text.substr(0, built->text.find("まま")));
  const std::vector<MarkRun> runs = markRuns(bench.page, *built, {{at, at + 2, Mark::New}}, kMetrics);
  ASSERT_EQ(runs.size(), 2u);
  EXPECT_EQ(runs[0].box, (Rect{kLeft + 13 * 26, kTop + 72, 26, 36}));
  EXPECT_EQ(runs[1].box, (Rect{kLeft, kTop + 108, 26, 36}));
}

TEST(PageMarks, TheFillsSitUnderTheBaselinePulledInAtEachEnd) {
  std::vector<Rect> fills;
  markFills({{100, 50, 52, 36}, Mark::New}, 28, 6, fills);
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0], (Rect{102, 50 + 28 + 6, 48, 2}));
  fills.clear();
  markFills({{100, 50, 26, 36}, Mark::Learning}, 28, 6, fills);  // 102..124: squares at 102 106 ... 122
  ASSERT_EQ(fills.size(), 6u);
  EXPECT_EQ(fills.front(), (Rect{102, 84, 2, 2}));
  EXPECT_EQ(fills.back(), (Rect{122, 84, 2, 2}));
  fills.clear();
  markFills({{100, 50, 4, 36}, Mark::New}, 28, 6, fills);  // too narrow once pulled in
  markFills({{100, 50, 5, 36}, Mark::Learning}, 28, 6, fills);
  markFills({{100, 50, 52, 36}, Mark::None}, 28, 6, fills);
  EXPECT_TRUE(fills.empty());
}

// The mirror's say for the marks is the card's (MirrorView over the saved-state rule): one real VocabStore.
TEST(PageMarks, MirrorViewIsTheCardsRule) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::vocab::VocabStore store(files);
  // Not loaded: only the reader's own pending writes speak.
  lexipoint::vocab::LiveState own;
  own.language = Language::Japanese;
  own.entryId = 103;
  own.saved = true;
  own.savedId = 7;
  own.proficiency = 4;
  own.own = true;
  store.record({own});
  const MirrorView view(Language::Japanese, 0, store);
  EXPECT_EQ(view.says(103).verdict, MirrorSays::Verdict::Saved);
  EXPECT_EQ(view.says(103).state.proficiency, 4);
  EXPECT_EQ(view.says(104).verdict, MirrorSays::Verdict::Snapshot);
  EXPECT_EQ(view.says(0).verdict, MirrorSays::Verdict::Snapshot);
}

// fix-marks: the device's sources put the open card's unsent level over the mirror and the page's snapshot (its newest
// choice), keep a suspension the mirror knows, and leave every other entry to MirrorView.
TEST(PageMarks, TheCardsUnsentLevelComesFirst) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::vocab::VocabStore store(files);
  lexipoint::IgnoredWordStore ignored(files);
  store.load(Language::Japanese);
  lexipoint::vocab::LiveState saved;  // the mirror: 104 saved at level 1
  saved.language = Language::Japanese;
  saved.entryId = 104;
  saved.saved = true;
  saved.savedId = 8;
  saved.proficiency = 1;
  saved.own = true;
  store.record({saved});
  const StoreSources before(Language::Japanese, 0, store, ignored);
  EXPECT_EQ(before.mirror(104).state.proficiency, 1);
  EXPECT_EQ(before.mirror(105).verdict, MirrorSays::Verdict::Snapshot);
  lexipoint::vocab::LiveState k = saved;
  k.savedId = 0;
  k.proficiency = 4;
  lexipoint::vocab::LiveState t = k;
  t.entryId = 105;
  t.proficiency = 1;
  lexipoint::vocab::LiveState removed = t;
  removed.entryId = 106;
  removed.saved = false;
  store.setUnsent({k, t, removed});
  const StoreSources sources(Language::Japanese, 0, store, ignored);
  EXPECT_EQ(sources.mirror(104).verdict, MirrorSays::Verdict::Saved);
  EXPECT_EQ(sources.mirror(104).state.proficiency, 4);  // over the mirror
  EXPECT_EQ(sources.mirror(105).verdict, MirrorSays::Verdict::Saved);
  EXPECT_EQ(sources.mirror(105).state.proficiency, 1);  // over the page's snapshot
  EXPECT_EQ(sources.mirror(106).verdict, MirrorSays::Verdict::Unsaved);
  EXPECT_EQ(sources.mirror(107).verdict, MirrorSays::Verdict::Snapshot);  // not the card's: MirrorView says
  EXPECT_EQ(sources.mirror(0).verdict, MirrorSays::Verdict::Snapshot);
  EXPECT_FALSE(sources.mirror(104).suspended);
  // A suspension the mirror knows stays under the card's level (as the mirror keeps it once the write lands).
  lexipoint::fakes::FakeFiles files2;
  lexipoint::vocab::VocabStore suspendedStore(files2);
  lexipoint::vocab::Mirror mirror;
  mirror.put({104, 8, 0, 1, /*suspended=*/true, 0, false, 1, true});
  files2.files[lexipoint::vocab::mirrorFile(Language::Japanese).path] =
      lexipoint::vocab::serializeMirror(mirror, Language::Japanese);
  suspendedStore.load(Language::Japanese);
  suspendedStore.setUnsent({k});
  const StoreSources held(Language::Japanese, 0, suspendedStore, ignored);
  EXPECT_EQ(held.mirror(104).state.proficiency, 4);
  EXPECT_TRUE(held.mirror(104).suspended);
}

// A word's characters are tokens of their own on the device (ParsedText's CJK breaks), so its pieces on one line
// merge into one underline, the justification's gaps included (the signed-off mockup's one segment per word per line).
TEST(PageMarks, AMultiCharacterWordIsOneUnderlinePerLine) {
  const Bench bench;
  const auto built = lexipoint::text::buildPageText(bench.model, lexipoint::text::Script::Japanese);
  ASSERT_TRUE(built);
  const uint32_t at = lexipoint::text::utf16Length(built->text.substr(0, built->text.find("灯台守")));
  const std::vector<MarkRun> runs = markRuns(bench.page, *built, {{at, at + 3, Mark::New}}, kMetrics);
  ASSERT_EQ(runs.size(), 1u);
  EXPECT_EQ(runs[0].box, (Rect{kLeft + 26, kTop, 3 * 26, 36}));
  std::vector<Rect> fills;
  markFills(runs[0], 28, 6, fills);
  ASSERT_EQ(fills.size(), 1u);
  EXPECT_EQ(fills[0], (Rect{kLeft + 26 + 2, kTop + 28 + 6, 3 * 26 - 4, 2}));
}

TEST(PageMarks, AJustifiedLinesGapsAreInsideTheWordsUnderline) {
  // 灯 台 守 spread by 3 px gaps (a justified line): one run from 灯's x to 守's advance end.
  lexipoint::text::PageModel model;
  lexipoint::text::TextLine line;
  line.tokens = {"灯", "台", "守"};
  line.startsParagraph = true;
  model.lines = {line};
  ReaderPage page;
  page.lines = {{kTop, {{"灯", 12, 26}, {"台", 41, 26}, {"守", 70, 26}}}};
  const auto built = lexipoint::text::buildPageText(model, lexipoint::text::Script::Japanese);
  ASSERT_TRUE(built);
  const std::vector<MarkRun> runs = markRuns(page, *built, {{0, 3, Mark::Learning}}, kMetrics);
  ASSERT_EQ(runs.size(), 1u);
  EXPECT_EQ(runs[0].box, (Rect{12, kTop, 96 - 12, 36}));
}

TEST(PageMarks, TheUnderlineSitsUnderTheInkForTheFontsEm) {
  EXPECT_EQ(markBelowBaseline(29), 6);  // NotoSerifCJK 14 pt, the reader's default (the signed-off mockup's)
  EXPECT_EQ(markBelowBaseline(37), 7);  // 18 pt
  EXPECT_EQ(markBelowBaseline(25), 5);  // 12 pt
}

TEST(PageMarks, CardAndPageShareOneLevelRule) {
  EXPECT_EQ(markForLevel(false, 0), Mark::New);
  EXPECT_EQ(markForLevel(true, 0), Mark::New);
  EXPECT_EQ(markForLevel(true, 1), Mark::Learning);
  EXPECT_EQ(markForLevel(true, 2), Mark::Learning);
  EXPECT_EQ(markForLevel(true, 3), Mark::None);
  EXPECT_EQ(markForLevel(true, 4), Mark::None);
  EXPECT_EQ(markForLevel(true, 7), Mark::None);  // past 4 (never sent): no mark, as the card reads it
}

// A word without a lemma (the server leaves it out when it's the word) is ignored by its own form, as the card's
// headword() gives it.
TEST(PageMarks, AWordWithoutALemmaIsKeyedByItsOwnForm) {
  PageAnalysis page;
  ASSERT_EQ(parsePage(R"({"occurrences":[{"entryId":0,"word":"ほげ","isWordLike":true,"charStart":0,"charEnd":2}]})",
                      Language::Japanese, page),
            lexipoint::api::ParseStatus::Ok);
  struct Sources final : MarkSources {
    mutable std::vector<IgnoredKey> asked;
    bool ignored(const IgnoredKey& key) const override {
      asked.push_back(key);
      return true;
    }
  } sources;
  EXPECT_TRUE(pageMarks(page, sources).empty());
  ASSERT_EQ(sources.asked.size(), 1u);
  EXPECT_EQ(sources.asked[0].entryId, 0u);
  EXPECT_EQ(sources.asked[0].text, "ほげ");
}

// --- the reader's kept pages (ReaderMarks.h: MarkGate, and who shows marks) ---

TEST(MarkGate, APageOnScreenMakesEveryPageDueInOrderOnce) {
  MarkGate gate;
  EXPECT_EQ(gate.due(), -1);  // nothing drawn yet
  gate.drawn(1, 3, 10, 0);
  EXPECT_EQ(gate.due(), 0);  // the page on screen first
  gate.done(0);
  EXPECT_EQ(gate.due(), 1);  // the next
  gate.done(1);
  EXPECT_EQ(gate.due(), 2);  // the one before
  gate.done(2);
  EXPECT_EQ(gate.due(), -1);
  gate.drawn(1, 3, 10, 0);  // the same page drawn again (a card closed over it, every loop pass): nothing to read
  EXPECT_EQ(gate.due(), -1);
}

TEST(MarkGate, ATurnBackAnotherBookAndAReload) {
  MarkGate gate;
  gate.drawn(1, 3, 10, 0);
  for (int i = 0; i < 3; i++) gate.done(i);
  gate.drawn(1, 3, 9, 0);  // turned back: every page due again
  EXPECT_EQ(gate.due(), 0);
  for (int i = 0; i < 3; i++) gate.done(i);
  gate.drawn(2, 3, 9, 0);  // another book, same page
  EXPECT_EQ(gate.due(), 0);
  for (int i = 0; i < 3; i++) gate.done(i);
  gate.reload(1);  // the prefetcher wrote the next page again
  EXPECT_EQ(gate.due(), 1);
  gate.done(7);  // out of range: ignored
  gate.reload(-1);
  EXPECT_EQ(gate.due(), 1);
}

TEST(MarkGate, TheKeptPagesAreThisTheNextAndTheOneBefore) {
  const std::array<int, lexipoint::config::kMarkPages> offsets{0, 1, -1};
  const std::array<std::string, lexipoint::config::kMarkPages> names{"this", "next", "previous"};
  for (size_t i = 0; i < offsets.size(); i++) {
    EXPECT_EQ(kMarkPageSlots[i].offset, offsets[i]);
    EXPECT_EQ(kMarkPageSlots[i].name, names[i]);
  }
}

TEST(ReaderMarksRules, WhoShowsMarksAndWhoSteps) {
  for (int bits = 0; bits < 8; bits++) {
    const bool usable = bits & 1, markWords = bits & 2, bookOn = bits & 4;
    EXPECT_EQ(bookShowsMarks(settingsShowMarks(usable, markWords), bookOn), usable && markWords && bookOn) << bits;
  }
  EXPECT_TRUE(stepsMarked(true, true));
  EXPECT_FALSE(stepsMarked(true, false));  // "Every word"
  EXPECT_FALSE(stepsMarked(false, true));  // no marks shown: every word
  EXPECT_FALSE(stepsMarked(false, false));
}

// --- The kept pages (MarkSlots) ---

namespace {

// A reader turning pages over a section whose every page was analyzed: the loop's pass per page around the one on
// screen (MarkGate's order), counting the analysis files read.
struct Reader {
  MarkSlots slots;
  std::vector<int> reads;
  static PageId id(const int page, const Language language = Language::Japanese) {
    return {PageKey{1, 2, static_cast<uint32_t>(page) * 1000}, language, 100, static_cast<uint32_t>(page)};
  }
  void show(const int page, const Language language = Language::Japanese) {
    slots.onScreen(2, page);
    for (const MarkPage& slot : kMarkPageSlots) {
      const int p = page + slot.offset;
      if (p < 0 || slots.have(id(p, language))) continue;
      reads.push_back(p);
      auto analysis = std::make_unique<PageAnalysis>();
      analysis->language = language;
      slots.put(id(p, language), 2, p, std::move(analysis));
    }
  }
  bool marks(const int page) { return slots.find(id(page).key, 100, static_cast<uint32_t>(page)) != nullptr; }
};

}  // namespace

TEST(MarkSlots, ATurnReadsOneFileEitherWay) {
  Reader r;
  r.show(10);
  EXPECT_EQ(r.reads, (std::vector<int>{10, 11, 9}));
  for (const int page : {11, 12, 13}) {
    r.reads.clear();
    r.show(page);
    EXPECT_EQ(r.reads, std::vector<int>{page + 1}) << page;  // the page just left stays kept
    EXPECT_TRUE(r.marks(page - 1));
  }
  for (const int page : {12, 11, 10}) {
    r.reads.clear();
    r.show(page);
    EXPECT_EQ(r.reads, std::vector<int>{page - 1}) << page;
    EXPECT_TRUE(r.marks(page + 1));
  }
}

TEST(MarkSlots, AReloadOrAnotherLanguageReadsThePageAgain) {
  Reader r;
  r.show(10);
  r.reads.clear();
  r.slots.reload(Reader::id(11).key);  // the prefetcher wrote it again
  r.show(10);
  EXPECT_EQ(r.reads, std::vector<int>{11});
  r.reads.clear();
  r.show(10, Language::Chinese);  // the book's lookup language changed: every page read again, in it
  EXPECT_EQ(r.reads, (std::vector<int>{10, 11, 9}));
  EXPECT_EQ(r.slots.find(Reader::id(10).key, 100, 10)->language, Language::Chinese);
}

TEST(MarkSlots, APageNotAnalyzedIsKnownSoAndNotReadAgain) {
  MarkSlots slots;
  slots.onScreen(2, 5);
  const PageId id = Reader::id(5);
  slots.put(id, 2, 5, nullptr);
  EXPECT_TRUE(slots.have(id));
  EXPECT_EQ(slots.find(id.key, id.units, id.hash), nullptr);
  EXPECT_EQ(slots.kept(), 0u);
}

TEST(MarkSlots, ADrawnPageIsReadOnceUntilAnotherIsDrawnOrFound) {
  MarkSlots slots;
  const PageKey x{1, 2, 1000};
  const PageKey y{1, 2, 2000};
  EXPECT_TRUE(slots.peekDue(x, 1));   // the book's first page: read as drawn
  EXPECT_FALSE(slots.peekDue(x, 1));  // drawn again (a card closed over it): not read again
  EXPECT_TRUE(slots.peekDue(y, 1));   // a jump
  EXPECT_TRUE(slots.peekDue(x, 1));   // back to it (not kept any more): read again
  // A page found kept forgets the last read, so the one read before is read again if it comes back.
  slots.peeked(Reader::id(3), 2, 3, std::make_unique<PageAnalysis>());
  ASSERT_NE(slots.find(Reader::id(3).key, 100, 3), nullptr);
  EXPECT_TRUE(slots.peekDue(x, 1));
}

TEST(MarkSlots, ClearForgetsEverything) {
  Reader r;
  r.show(10);
  EXPECT_EQ(r.slots.kept(), 3u);
  EXPECT_FALSE(r.slots.peekDue(PageKey{9, 9, 9}, 1) && r.slots.peekDue(PageKey{9, 9, 9}, 1));
  r.slots.clear();
  EXPECT_EQ(r.slots.kept(), 0u);
  EXPECT_TRUE(r.slots.peekDue(PageKey{9, 9, 9}, 1));
}

// What a book's first page reads is loaded as the book opens: which languages' mirrors.
TEST(MarkSlots, ABookLoadsItsOwnLanguagesMirrorOrEveryOneOnWhenItsTextDecides) {
  lexipoint::Settings settings;
  using lexipoint::text::BookLanguage;
  EXPECT_EQ(marksLanguages(BookLanguage("ja", std::nullopt), settings), std::vector<Language>{Language::Japanese});
  EXPECT_EQ(marksLanguages(BookLanguage("zh-CN", std::nullopt), settings), std::vector<Language>{Language::Chinese});
  EXPECT_EQ(marksLanguages(BookLanguage("en", Language::Chinese), settings),
            std::vector<Language>{Language::Chinese});  // the reader's own choice for the book
  EXPECT_EQ(marksLanguages(BookLanguage("", std::nullopt), settings),
            (std::vector<Language>{Language::Japanese, Language::Chinese}));  // the text decides: both on
  settings.chinese.enabled = false;
  EXPECT_EQ(marksLanguages(BookLanguage("", std::nullopt), settings), std::vector<Language>{Language::Japanese});
  EXPECT_TRUE(marksLanguages(BookLanguage("zh", std::nullopt), settings).empty());  // switched off: nothing
}

// Pages read as drawn on fast turns fill every slot asked; a new one then goes in place of the farthest from it,
// and the loop, once it catches up, reads one file.
TEST(MarkSlots, FastTurnsKeepThePagesNearestAndTheLoopReadsOneFile) {
  Reader r;
  r.show(10);  // 10, 11, 9 kept
  r.show(10);  // the loop looks again (a redraw): found kept, each still asked for by this page
  const auto drawFast = [&r](const int page) {
    if (r.marks(page) || !r.slots.peekDue(Reader::id(page).key, Reader::id(page).hash)) return;
    r.reads.push_back(page);
    r.slots.peeked(Reader::id(page), 2, page, std::make_unique<PageAnalysis>());
  };
  r.reads.clear();
  for (const int page : {11, 12, 13}) drawFast(page);
  EXPECT_EQ(r.reads, (std::vector<int>{12, 13}));
  EXPECT_TRUE(r.marks(11));
  EXPECT_TRUE(r.marks(12));
  EXPECT_TRUE(r.marks(13));
  EXPECT_FALSE(r.marks(9));  // the farthest went first
  r.reads.clear();
  r.show(13);
  EXPECT_EQ(r.reads, std::vector<int>{14});  // 12 (the one before) stays kept
}

// A page read as drawn and found not analyzed is known so in the book's language, whichever the loop asks.
TEST(MarkSlots, APageNotAnalyzedMatchesInAnyLanguage) {
  MarkSlots slots;
  slots.peeked(PageId{Reader::id(5).key, Language::Japanese, 100, 5}, 2, 5, nullptr);
  slots.onScreen(2, 5);
  EXPECT_TRUE(slots.have(Reader::id(5, Language::Chinese)));
  EXPECT_TRUE(slots.have(Reader::id(5)));
}

// The loop resets what's kept when the book stops or starts showing marks, whichever of the two flags changed.
TEST(MarkVisibility, TheBooksRowAndTheSettingsTogether) {
  MarkVisibility v;
  EXPECT_TRUE(v.update(true, true));   // a book opens showing marks
  EXPECT_FALSE(v.update(true, true));  // every later pass: nothing
  EXPECT_TRUE(v.update(true, false));  // Page marks Off in the menu
  EXPECT_FALSE(v.shown());
  EXPECT_FALSE(v.update(false, false));  // Mark words off as well: still hidden, nothing to reset
  EXPECT_FALSE(v.update(true, false));
  EXPECT_TRUE(v.update(true, true));  // Page marks On: read afresh
  EXPECT_TRUE(v.shown());
}

// The prefetcher names the page it wrote; it's reloaded wherever it's kept now.
TEST(MarkGate, AWrittenPageIsFoundByItsKey) {
  std::array<std::optional<PageKey>, lexipoint::config::kMarkPages> wanted{};
  wanted[0] = PageKey{1, 2, 3000};
  wanted[1] = PageKey{1, 2, 4000};
  EXPECT_EQ(slotOfKey(wanted, PageKey{1, 2, 4000}), std::optional<size_t>(1));
  EXPECT_EQ(slotOfKey(wanted, PageKey{1, 2, 9000}), std::nullopt);  // not around the page on screen any more
}

// The page under a card is worked out again when the mirror or the ignore list changes, not on every frame.
TEST(CardMarks, WorkedOutAgainOnlyWhenWhatDecidesThemChanges) {
  CardMarks cache;
  int page = 0;
  const CardMarks::Key k{PageKey{1, 2, 3000}, &page, 7, 1};
  EXPECT_FALSE(cache.holds(k));
  cache.keep(k, {{1, 2, 3, 4}});
  EXPECT_TRUE(cache.holds(k));  // the card's next frames
  CardMarks::Key saved = k;
  saved.vocab = 8;  // a save on the card went into the mirror
  EXPECT_FALSE(cache.holds(saved));
  CardMarks::Key ignored = k;
  ignored.ignored = 2;  // ⋯ Ignore
  EXPECT_FALSE(cache.holds(ignored));
  int other = 0;
  CardMarks::Key another = k;
  another.laidOut = &other;  // a new card's word select page
  EXPECT_FALSE(cache.holds(another));
  cache.drop();
  EXPECT_FALSE(cache.holds(k));
}

// The book's Lookup language changed from the reader menu is worked out again on the next pass, without a new page.
TEST(ReaderMarksRules, UsabilityIsWorkedOutAgainWhenTheLookupLanguageChanges) {
  EXPECT_FALSE(usableStale(false, false, false, false));  // every other pass: nothing read
  EXPECT_TRUE(usableStale(true, false, false, false));    // the settings
  EXPECT_TRUE(usableStale(false, true, false, false));    // another book
  EXPECT_TRUE(usableStale(false, false, true, false));    // another page
  EXPECT_TRUE(usableStale(false, false, false, true));    // the book's Lookup language (a menu over the same page)
}

// --- The real keeper (MarkKeeper) over a fake book and PageStore on FakeFiles ---

namespace {

// A section of kPages pages, each page's text "p<page> v<layout>" (a reflow changes every text at the same keys), a
// page's analysis written to the store in a language (one file per key: the last written wins).
struct Book {
  static constexpr int kPages = 24;
  lexipoint::fakes::FakeFiles files;
  PageStore store{files};
  MarkKeeper keeper{store};
  int page = 5;
  int layout = 0;
  Language language = Language::Japanese;
  unsigned long drawnMs = 1;

  static PageKey key(const int p) { return PageKey{1, 2, static_cast<uint32_t>(p) * 1000}; }
  std::string text(const int p) const { return "p" + std::to_string(p) + " v" + std::to_string(layout); }
  uint32_t units(const int p) const { return lexipoint::text::utf16Length(text(p)); }
  uint32_t hash(const int p) const { return textHash(text(p)); }
  void write(const int p) {  // the prefetcher analyzed page p's text now, in the book's language
    PageAnalysis a;
    a.language = language;
    a.textUnits = units(p);
    a.textHash = hash(p);
    EXPECT_TRUE(store.write(key(p), a));
  }
  bool analyzed(const int p) {  // the store holds this very text, in the book's language
    return store.peek(key(p), units(p), hash(p)).has_value() &&
           store.peek(key(p), units(p), hash(p))->language == language;
  }
  struct Texts final : PageTexts {
    Book& b;
    explicit Texts(Book& book) : b(book) {}
    std::optional<PageText> textOf(const int which) override {
      const int p = b.page + which;
      if (p < 0 || p >= kPages) return std::nullopt;
      return PageText{key(p), b.language, b.text(p), b.units(p)};
    }
    int pageIndex() const override { return b.page; }
  };
  void open() { keeper.open(true, true, {language}); }
  // One loop pass; returns the analysis files it read.
  int loopPass() {
    Texts texts(*this);
    const int before = files.reads;
    if (keeper.due(1, 2, page, drawnMs, true)) keeper.read(texts);
    return files.reads - before;
  }
  void settle() {
    for (int i = 0; i < 8; i++) loopPass();
  }
  // The page on screen drawn; whether it's marked. `read` (when given): whether its file was read as it's drawn (on
  // the render task, the latency the loop's read-ahead saves).
  bool draw(bool* read = nullptr) {
    drawnMs++;
    const auto lock = keeper.hold();
    return keeper.analysisLocked(key(page), units(page), hash(page), 2, page, read) != nullptr;
  }
};

}  // namespace

TEST(MarkKeeper, ASteadyTurnReadsOneAnalysisFileEitherWay) {
  Book b;
  for (int p = 0; p < Book::kPages; p++) b.write(p);
  b.open();
  bool read = false;
  EXPECT_TRUE(b.draw(&read));  // the book's first page, read as drawn
  EXPECT_TRUE(read);
  b.settle();
  for (int turn = 0; turn < 6; turn++) {
    b.page++;
    EXPECT_TRUE(b.draw(&read));
    EXPECT_FALSE(read) << "forward to " << b.page << ": kept by the loop, not read as drawn";
    int reads = 0;
    for (int i = 0; i < 8; i++) reads += b.loopPass();
    EXPECT_LE(reads, 1) << "forward to " << b.page;
  }
  for (int turn = 0; turn < 6; turn++) {
    b.page--;
    EXPECT_TRUE(b.draw(&read));
    EXPECT_FALSE(read) << "back to " << b.page;
    int reads = 0;
    for (int i = 0; i < 8; i++) reads += b.loopPass();
    EXPECT_LE(reads, 1) << "back to " << b.page;
  }
}

TEST(MarkKeeper, AReflowOnTheSameIndexIsReadAgainAndTheNextPageAhead) {
  Book b;
  for (int p = 0; p < Book::kPages; p++) b.write(p);
  b.open();
  b.draw();
  b.settle();
  b.layout = 1;  // Rotate screen from the reader menu: the same index, other texts
  for (int p = 0; p < Book::kPages; p++) b.write(p);
  EXPECT_TRUE(b.draw());  // read as drawn: another text, so read again
  b.settle();
  b.page++;
  const auto lock = b.keeper.hold();
  EXPECT_NE(b.keeper.analysisLocked(Book::key(b.page), b.units(b.page), b.hash(b.page), 2, -1), nullptr)
      << "the next page was read ahead for the new layout";
}

TEST(MarkKeeper, TheBooksRowAndItsLanguageDecide) {
  Book b;
  for (int p = 0; p < Book::kPages; p++) b.write(p);
  b.keeper.open(true, false, {Language::Japanese});  // Page marks Off
  EXPECT_FALSE(b.keeper.drawn());
  EXPECT_EQ(b.loopPass(), 0);  // nothing read
  b.keeper.setBookOn(true);    // turned on in the menu: the page drawn right after follows it
  EXPECT_TRUE(b.keeper.drawn());
  EXPECT_TRUE(b.draw());
  b.keeper.open(true, true, {Language::Chinese});  // its Lookup language changed: a Japanese file isn't its
  b.language = Language::Chinese;
  EXPECT_FALSE(b.draw());
  b.write(b.page);  // analyzed again in Chinese
  b.keeper.reload(Book::key(b.page));
  b.settle();
  EXPECT_TRUE(b.draw());
}

// Random navigation (fixed seeds) through the real keeper: an analyzed page is always drawn marked, even right
// after the prefetcher rewrote the page on screen, before the loop's next look at it.
TEST(MarkKeeper, AnAnalyzedPageIsAlwaysDrawnMarked) {
  for (unsigned seed = 1; seed <= 40; seed++) {
    std::mt19937 rng(seed);
    Book b;
    for (int p = 0; p < Book::kPages; p++) {
      if (p % 7 != 3) b.write(p);  // a few pages never analyzed
    }
    b.open();
    int draws = 0;
    for (int step = 0; step < 400; step++) {
      bool drawn = true;
      switch (rng() % 9) {
        case 0:  // forward
        case 1:  // a fast turn (no loop pass after it, below)
          if (b.page + 1 < Book::kPages) b.page++;
          break;
        case 2:  // back
          if (b.page > 0) b.page--;
          break;
        case 3:  // a jump
          b.page = static_cast<int>(rng() % Book::kPages);
          break;
        case 4:  // drawn again (a card or a menu closed over it)
          break;
        case 5: {  // the prefetcher wrote this page or the next
          const int p = std::min(b.page + static_cast<int>(rng() % 2), Book::kPages - 1);
          if (p % 7 != 3) b.write(p);
          b.keeper.reload(Book::key(p));
          drawn = false;
          break;
        }
        case 6:  // a reflow on the same index: every page's text changes; half are cached for it already
          b.layout++;
          for (int p = 0; p < Book::kPages; p++) {
            if (p % 2 == 0 && p % 7 != 3) b.write(p);
          }
          break;
        case 7:  // the book's Lookup language changed, and the page on screen analyzed in it since
          b.language = b.language == Language::Japanese ? Language::Chinese : Language::Japanese;
          b.keeper.open(true, true, {b.language});
          b.write(b.page);
          break;
        default:
          drawn = false;
          break;
      }
      if (drawn) {
        const bool marked = b.draw();
        draws++;
        if (b.analyzed(b.page)) {
          EXPECT_TRUE(marked) << "seed " << seed << " step " << step << " page " << b.page;
        }
      }
      const int passes = (rng() % 9 == 1) ? 0 : static_cast<int>(rng() % 4);
      for (int pass = 0; pass < passes; pass++) b.loopPass();
    }
    EXPECT_GT(draws, 200);
  }
}

TEST(MarkSlots, APageWhoseTextChangedAtTheSameKeyIsReadAgain) {
  MarkSlots slots;
  slots.onScreen(2, 5);
  const PageId before = Reader::id(5);
  slots.put(before, 2, 5, std::make_unique<PageAnalysis>());
  PageId after = before;
  after.hash = 99;  // a reflow: the same first character, another text
  EXPECT_FALSE(slots.have(after));
  EXPECT_EQ(slots.find(after.key, after.units, after.hash), nullptr);
  EXPECT_TRUE(slots.peekDue(after.key, after.hash));  // read as drawn, for the new text
  slots.put(after, 2, 5, std::make_unique<PageAnalysis>());
  EXPECT_NE(slots.find(after.key, after.units, after.hash), nullptr);
  EXPECT_EQ(slots.kept(), 1u);  // in the old one's place
}

TEST(MarkGate, TheSamePageDrawnAgainLooksAtItsOwnTextOnly) {
  MarkGate gate;
  gate.drawn(1, 3, 10, 100);
  for (int i = 0; i < 3; i++) gate.done(i);
  gate.drawn(1, 3, 10, 100);  // the same drawing: nothing
  EXPECT_EQ(gate.due(), -1);
  gate.drawn(1, 3, 10, 200);  // drawn again: the page on screen looked at (a reflow would show there)
  EXPECT_EQ(gate.due(), 0);
  gate.done(0);
  EXPECT_EQ(gate.due(), -1);
  gate.rearm();  // its text changed: all anew
  EXPECT_EQ(gate.due(), 0);
}

// A page read as drawn before the prefetcher wrote it (not analyzed then) is read again when it's drawn next, even
// before the loop looks at it.
TEST(MarkSlots, AReloadReadsAPageReadAsDrawnAgain) {
  MarkSlots slots;
  const PageKey x{1, 2, 1000};
  const PageKey y{1, 2, 2000};
  EXPECT_TRUE(slots.peekDue(x, 1));
  EXPECT_FALSE(slots.peekDue(x, 1));
  slots.reload(y);  // another page written: this one's read stands
  EXPECT_FALSE(slots.peekDue(x, 1));
  slots.reload(x);
  EXPECT_TRUE(slots.peekDue(x, 1));
}

TEST(MarkKeeper, APageWrittenAfterItWasDrawnIsMarkedOnItsNextDrawing) {
  Book b;
  b.open();
  EXPECT_FALSE(b.draw());  // not analyzed yet: read as drawn, known so
  EXPECT_FALSE(b.draw());  // drawn again: not read again
  b.write(b.page);         // the prefetcher analyzed it (a card closed over it is drawn right after)
  b.keeper.reload(Book::key(b.page));
  EXPECT_TRUE(b.draw());
}

// Turned off, nothing is kept; turned on again, the pages around are read afresh (their files may have changed
// meanwhile), not taken from before.
TEST(MarkKeeper, TurnedOffAndOnThePagesAreReadAfresh) {
  Book b;
  for (int p = 0; p < Book::kPages; p++) {
    if (p != b.page) b.write(p);
  }
  b.open();
  b.draw();
  b.settle();
  b.keeper.setBookOn(false);  // Page marks Off in the menu
  EXPECT_EQ(b.loopPass(), 0);
  b.write(b.page);  // the prefetcher analyzed the page on screen meanwhile
  b.keeper.reload(Book::key(b.page));
  b.keeper.setBookOn(true);
  int reads = 0;
  for (int i = 0; i < 8; i++) reads += b.loopPass();
  EXPECT_EQ(reads, 3);  // this, the next and the previous: every one afresh
  EXPECT_TRUE(b.draw());
}

// A book's sources (the ignore list and its languages' mirrors) are loaded as it opens showing marks, and when its
// row is turned on; never for a book that shows none.
TEST(MarkKeeper, ABooksSourcesAreLoadedWhenItShowsMarks) {
  struct Loads final : MarkKeeper::Loader {
    std::vector<Language> loaded;
    void load(const Language language) override { loaded.push_back(language); }
  };
  lexipoint::fakes::FakeFiles files;
  PageStore store{files};
  Loads loads;
  MarkKeeper keeper(store, &loads);
  keeper.open(true, true, {Language::Japanese, Language::Chinese});
  EXPECT_EQ(loads.loaded, (std::vector<Language>{Language::Japanese, Language::Chinese}));
  loads.loaded.clear();
  keeper.open(true, false, {Language::Chinese});  // Page marks Off for this book
  EXPECT_TRUE(loads.loaded.empty());
  keeper.setBookOn(true);  // turned on in the menu
  EXPECT_EQ(loads.loaded, std::vector<Language>{Language::Chinese});
  loads.loaded.clear();
  keeper.setBookOn(false);
  EXPECT_TRUE(loads.loaded.empty());
  keeper.open(false, false, {Language::Japanese});  // Mark words on the page off
  keeper.setBookOn(true);
  EXPECT_TRUE(loads.loaded.empty());
  // Turned on with the book open (the loop's next pass sees the setting): loaded then, before any page is read.
  EXPECT_TRUE(keeper.due(1, 2, 5, 1, true));
  EXPECT_EQ(loads.loaded, std::vector<Language>{Language::Japanese});
  loads.loaded.clear();
  keeper.due(1, 2, 5, 2, true);  // every later pass: nothing
  EXPECT_TRUE(loads.loaded.empty());
}

// A kept page at the very place of the one being kept, with another key, is an old layout's (a reflow moved every
// page's first character): it goes first, before a page of this layout near the one on screen.
TEST(MarkSlots, AnOldLayoutsPageAtTheSamePlaceGoesFirst) {
  MarkSlots slots;
  slots.onScreen(2, 5);
  slots.put(Reader::id(4), 2, 4, std::make_unique<PageAnalysis>());
  slots.put(Reader::id(5), 2, 5, std::make_unique<PageAnalysis>());
  slots.put(Reader::id(6), 2, 6, std::make_unique<PageAnalysis>());
  slots.onScreen(2, 4);  // back a page, before the reflow ends: nothing asked yet
  const PageId reflowed{PageKey{1, 2, 5500}, Language::Japanese, 100, 55};  // page 5 of the new layout
  slots.put(reflowed, 2, 5, std::make_unique<PageAnalysis>());
  EXPECT_EQ(slots.find(Reader::id(5).key, 100, 5), nullptr);  // the old page 5 went
  EXPECT_NE(slots.find(Reader::id(4).key, 100, 4), nullptr);
  EXPECT_NE(slots.find(Reader::id(6).key, 100, 6), nullptr);
  EXPECT_NE(slots.find(reflowed.key, 100, 55), nullptr);
}

// The normal online path: the next page is kept as not analyzed, the prefetcher writes it and names it, and the
// loop reads it again ahead of the turn (MarkGate::reload), so the turn draws it from memory, not read as drawn.
TEST(MarkKeeper, APageWrittenAheadIsReadByTheLoopBeforeTheTurn) {
  Book b;
  for (int p = 0; p < Book::kPages; p++) {
    if (p != 6) b.write(p);
  }
  b.open();
  b.draw();
  b.settle();  // 6 kept as not analyzed
  b.write(6);
  b.keeper.reload(Book::key(6));
  int reads = 0;
  for (int i = 0; i < 8; i++) reads += b.loopPass();
  EXPECT_EQ(reads, 1);
  b.page++;
  bool read = true;
  EXPECT_TRUE(b.draw(&read));
  EXPECT_FALSE(read);
}

// A new page on screen forgets which kept pages the last drawing asked for; this drawing's own are never the ones
// that go, even when a page left behind by a jump is as far from the page being kept.
TEST(MarkSlots, TheDrawingsOwnPagesStayWhenAnotherIsAsFarAway) {
  MarkSlots slots;
  slots.onScreen(2, 7);
  slots.put(Reader::id(11), 2, 11, std::make_unique<PageAnalysis>());
  slots.put(Reader::id(10), 2, 10, std::make_unique<PageAnalysis>());
  slots.put(Reader::id(7), 2, 7, std::make_unique<PageAnalysis>());
  slots.onScreen(2, 10);  // a jump to 10: this, the next, then the previous
  EXPECT_TRUE(slots.have(Reader::id(10)));
  EXPECT_TRUE(slots.have(Reader::id(11)));
  slots.put(Reader::id(9), 2, 9, std::make_unique<PageAnalysis>());  // 11 and 7 both two pages from 9
  EXPECT_NE(slots.find(Reader::id(11).key, 100, 11), nullptr);
  EXPECT_NE(slots.find(Reader::id(10).key, 100, 10), nullptr);
  EXPECT_EQ(slots.find(Reader::id(7).key, 100, 7), nullptr);
}

// A page the loop knows is not analyzed isn't read again as it's drawn: offline with nothing analyzed, each turn
// draws from what the loop kept (no file read on the render task).
TEST(MarkKeeper, APageKnownNotAnalyzedIsntReadAsItsDrawn) {
  Book b;
  b.open();
  bool read = false;
  EXPECT_FALSE(b.draw(&read));
  EXPECT_TRUE(read);  // the book's first page: read as drawn, once
  b.settle();
  for (int turn = 0; turn < 6; turn++) {
    b.page++;
    EXPECT_FALSE(b.draw(&read));
    EXPECT_FALSE(read) << "turn to " << b.page;
    b.settle();
  }
}

// After a Lookup language change, a page analyzed in the other language is known not analyzed in this one: its
// file (13-15 KB) isn't read and parsed as each page is drawn.
TEST(MarkKeeper, AnotherLanguagesFileIsntReadAsEachPageIsDrawn) {
  Book b;
  for (int p = 0; p < Book::kPages; p++) b.write(p);  // Japanese
  b.language = Language::Chinese;
  b.open();  // the book's Lookup language is Chinese now
  bool read = false;
  EXPECT_FALSE(b.draw(&read));
  EXPECT_TRUE(read);
  b.settle();
  for (int turn = 0; turn < 6; turn++) {
    b.page++;
    EXPECT_FALSE(b.draw(&read));
    EXPECT_FALSE(read) << "turn to " << b.page;
    b.settle();
  }
}

// The loop tells the kept pages which page is on screen: after jumps, the pages this drawing asks for stay and the one
// left behind goes, so the next turn draws from memory.
TEST(MarkKeeper, AfterJumpsThePagesAroundTheOneOnScreenStay) {
  Book b;
  for (int p = 0; p < Book::kPages; p++) b.write(p);
  b.open();
  for (const int p : {11, 10, 7}) {  // jumps, each read as drawn before the loop looks
    b.page = p;
    b.draw();
  }
  b.page = 10;  // back to 10: kept already
  b.settle();   // 10 and 11 asked for; 9 read in place of 7 (as far from 9 as 11 is)
  b.page = 11;
  bool read = true;
  EXPECT_TRUE(b.draw(&read));
  EXPECT_FALSE(read);
}

// Known not analyzed: a page kept with no analysis, for this very text only.
TEST(MarkSlots, KnownNotAnalyzedIsThisTextKeptWithNoAnalysis) {
  MarkSlots slots;
  slots.onScreen(2, 5);
  slots.put(Reader::id(5), 2, 5, nullptr);
  slots.put(Reader::id(6), 2, 6, std::make_unique<PageAnalysis>());
  EXPECT_TRUE(slots.knownNotAnalyzed(Reader::id(5).key, 100, 5));
  EXPECT_FALSE(slots.knownNotAnalyzed(Reader::id(5).key, 100, 99));  // another text at the same key
  EXPECT_FALSE(slots.knownNotAnalyzed(Reader::id(6).key, 100, 6));   // analyzed
  EXPECT_FALSE(slots.knownNotAnalyzed(Reader::id(7).key, 100, 7));   // not kept
  slots.reload(Reader::id(5).key);
  EXPECT_FALSE(slots.knownNotAnalyzed(Reader::id(5).key, 100, 5));  // written since
}
