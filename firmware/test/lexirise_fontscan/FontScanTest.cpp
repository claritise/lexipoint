// LEXIPOINT: with LEXIRISE the prewarm scan takes every font the card draws with (popup-ui.md §1.1: the
// page, 3 UI and 3 reader sizes), so none of them falls back to loading glyphs one by one from SD.

#include <gtest/gtest.h>

#include <map>

#include "FontCacheManager.h"
#include "SdCardFont.h"

TEST(LexiriseFontScan, EveryCardFontIsPrewarmed) {
  constexpr int kCardFonts = 7;  // lexipoint::card::Font: UiSmall … Page
  SdCardFont fonts[kCardFonts + 1];
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  std::map<int, SdCardFont*> sdFonts;
  for (int i = 0; i <= kCardFonts; i++) sdFonts[100 + i] = &fonts[i];
  FontCacheManager manager(noBuiltinFonts, sdFonts);

  auto scope = manager.createPrewarmScope();
  for (int i = 0; i < kCardFonts; i++) manager.recordText("A", 100 + i, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  for (int i = 0; i < kCardFonts; i++) {
    ASSERT_EQ(1, fonts[i].prewarmCallCount) << "font " << i;
    EXPECT_STREQ("A", fonts[i].prewarmCalls[0].text);
  }
}

TEST(LexiriseFontScan, FontsPastTheLimitStillDrawWithoutCorruptingTheOthers) {
  constexpr int kFonts = 10;
  SdCardFont fonts[kFonts];
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  std::map<int, SdCardFont*> sdFonts;
  for (int i = 0; i < kFonts; i++) sdFonts[i] = &fonts[i];
  FontCacheManager manager(noBuiltinFonts, sdFonts);

  auto scope = manager.createPrewarmScope();
  for (int i = 0; i < kFonts; i++) manager.recordText(i % 2 ? "B" : "A", i, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(1, fonts[i].prewarmCallCount) << "font " << i;
    EXPECT_STREQ(i % 2 ? "B" : "A", fonts[i].prewarmCalls[0].text);
  }
  for (int i = 8; i < kFonts; i++) EXPECT_EQ(0, fonts[i].prewarmCallCount) << "font " << i;  // on demand
}
