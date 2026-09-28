// v0.2 V7b R1: the touch line's idle level, learned (input/TouchLine.h), a page's text against sentences cut with
// another script (text::pageOffsetOf), the drawn page's packing. A page sentence's saved states: SavedStateTest.cpp.

#include <gtest/gtest.h>

#include <algorithm>

#include "FakeApi.h"
#include "Fakes.h"
#include "VocabFixtures.h"
#include "lexirise/input/TouchLine.h"
#include "lexirise/page/PageSentences.h"
#include "lexirise/page/ReaderPages.h"
#include "lexirise/text/SentenceBuilder.h"
#include "lexirise/vocab/VocabMirror.h"

using lexipoint::Language;
using lexipoint::input::TouchLine;
namespace config = lexipoint::config;

TEST(TouchLine, NothingIsATouchBeforeTheIdleLevelIsKnown) {
  TouchLine line;
  EXPECT_FALSE(line.active(0));
  EXPECT_FALSE(line.active(1));
  line.idle(-1, 0);  // no line on this board
  EXPECT_FALSE(line.learned());
}

TEST(TouchLine, EitherPolarityIsLearnedFromTheIdleLevel) {
  for (const int idle : {0, 1}) {
    TouchLine line;
    line.idle(idle, 100);
    EXPECT_TRUE(line.takeLearned());
    EXPECT_FALSE(line.takeLearned());  // said once
    EXPECT_EQ(line.idleLevel(), idle);
    EXPECT_FALSE(line.active(idle));
    EXPECT_TRUE(line.active(1 - idle));
    EXPECT_FALSE(line.active(-1));
  }
}

TEST(TouchLine, ALineThatChangesWithNoFingerDownIsGivenUpOn) {
  TouchLine line;
  unsigned long t = 1000;
  line.idle(0, t);
  for (unsigned i = 1; i < config::kTouchLineFlipsMax; i++) line.idle(static_cast<int>(i % 2), t += 1000);
  EXPECT_FALSE(line.ignored());
  line.idle(static_cast<int>(config::kTouchLineFlipsMax % 2), t += 1000);
  EXPECT_TRUE(line.ignored());
  EXPECT_TRUE(line.takeIgnored());
  EXPECT_FALSE(line.active(0));  // never trusted again: no call gives up for it
  EXPECT_FALSE(line.active(1));
}

TEST(TouchLine, ARareChangeIsRelearnedNotGivenUpOn) {
  TouchLine line;
  unsigned long t = 0;
  line.idle(0, t);
  for (int i = 0; i < 20; i++) line.idle(i % 2 ? 0 : 1, t += config::kTouchLineFlipWindowMs + 1);  // one per window
  EXPECT_FALSE(line.ignored());
  EXPECT_TRUE(line.learned());
}

// --- the page text against sentences cut with another script ---

TEST(PageOffsets, ChineseSentencesAreSlicesOfThePageTextJoinedWithJapaneseRules) {
  using namespace lexipoint::text;
  PageModel page;
  page.lines.push_back(TextLine{{"他说", "：", "“好", "。”", "“走", "吧", "。”"}, true});
  page.lines.push_back(TextLine{{"我", "用", "iPhone", "看", "了", "3.5", "小时", "。"}, false});
  page.lines.push_back(TextLine{{"His", "name", "is", "Tom.", "他", "笑", "了", "。"}, true});
  const auto text = buildPageText(page, Script::Japanese);
  ASSERT_TRUE(text);
  for (size_t l = 0; l < page.lines.size(); l++) {
    for (size_t t = 0; t < page.lines[l].tokens.size(); t++) {
      const auto sentence = buildSentence(page, {l, t}, Script::Chinese);
      if (!sentence) continue;
      const auto at = pageOffsetOf(*text, *sentence);
      ASSERT_TRUE(at) << sentence->text;
      // The sentence's characters sit at the same offsets from its start as in the page text.
      for (const SentenceChar& c : sentence->chars) {
        const auto it = std::find_if(text->chars.begin(), text->chars.end(), [&](const SentenceChar& p) {
          return p.token.line == c.token.line && p.token.token == c.token.token && p.codepoint == c.codepoint;
        });
        ASSERT_NE(it, text->chars.end());
        EXPECT_EQ(it->start - *at, c.start) << sentence->text;
      }
    }
  }
}

TEST(DrawnPage, PacksAndUnpacks) {
  using lexipoint::page::packDrawn;
  using lexipoint::page::unpackDrawn;
  const auto d = unpackDrawn(packDrawn(12, 345, 0xFFFFFFF0UL));
  EXPECT_EQ(d.spine, 12);
  EXPECT_EQ(d.page, 345);
  EXPECT_EQ(d.ms, 0xFFFFFFF0UL);
  EXPECT_EQ(unpackDrawn(0).spine, -1);
  EXPECT_EQ(unpackDrawn(packDrawn(70000, 1, 5)).spine, -1);  // out of range: not drawn
}
