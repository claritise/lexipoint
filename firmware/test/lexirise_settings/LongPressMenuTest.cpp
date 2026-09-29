// Long-press Menu without word select's lookup mode (claritise, 2026-09-25).

#include <gtest/gtest.h>

#include <vector>

#include "lexirise/settings/LongPressMenu.h"

namespace lpm = lexipoint::long_press_menu;

TEST(LongPressMenu, DictionaryAndKoSyncAreNotOffered) {
  EXPECT_EQ(lpm::offered(true), (std::vector<uint8_t>{lpm::kDisabled, lpm::kBookmark, lpm::kReaderMenu}));
  EXPECT_EQ(lpm::offered(false), (std::vector<uint8_t>{lpm::kDisabled, lpm::kBookmark}));
}

// V8 removed KOReader Sync: a stored one does nothing now, and the file is rewritten.
TEST(LongPressMenu, AStoredKoSyncBecomesDisabled) {
  for (const bool home : {true, false}) {
    const auto loaded = lpm::load(lpm::kKoSync, home);
    EXPECT_EQ(loaded.value, lpm::kDisabled);
    EXPECT_TRUE(loaded.resave);
    EXPECT_EQ(lpm::indexOf(lpm::kKoSync, home), 0);  // shown as Disabled
  }
}

TEST(LongPressMenu, AStoredDictionaryBecomesTheReaderMenu) {
  EXPECT_EQ(lpm::migrated(lpm::kDictionary, true), lpm::kReaderMenu);
  EXPECT_EQ(lpm::migrated(lpm::kDictionary, false), lpm::kDisabled);  // no Home key: no Reader Menu choice
  EXPECT_EQ(lpm::migrated(200, true), lpm::kReaderMenu);              // a hand-edited file
  for (const uint8_t v : {lpm::kDisabled, lpm::kBookmark, lpm::kReaderMenu}) {
    EXPECT_EQ(lpm::migrated(v, true), v);
  }
}

TEST(LongPressMenu, LoadingADictionaryRewritesTheFile) {
  const auto dictionary = lpm::load(lpm::kDictionary, true);
  EXPECT_EQ(dictionary.value, lpm::kReaderMenu);
  EXPECT_TRUE(dictionary.resave);
  const auto readerMenu = lpm::load(lpm::kReaderMenu, true);  // kept as it is (the file stores 4, not a position)
  EXPECT_EQ(readerMenu.value, lpm::kReaderMenu);
  EXPECT_FALSE(readerMenu.resave);
  EXPECT_TRUE(lpm::load(lpm::kReaderMenu, false).resave);  // no Home key: Disabled
  EXPECT_FALSE(lpm::load(lpm::kBookmark, false).resave);
}

TEST(LongPressMenu, PositionsAndValuesRoundTrip) {
  for (const bool home : {true, false}) {
    const auto values = lpm::offered(home);
    for (size_t i = 0; i < values.size(); i++) {
      EXPECT_EQ(lpm::indexOf(lpm::valueAt(i, home), home), i);
    }
    EXPECT_EQ(lpm::valueAt(values.size(), home), lpm::kDisabled);
  }
  EXPECT_EQ(lpm::indexOf(lpm::kReaderMenu, true), 2);  // after Disabled, Bookmark
  EXPECT_EQ(lpm::indexOf(lpm::kDictionary, true), 2);  // shown as Reader Menu
}
