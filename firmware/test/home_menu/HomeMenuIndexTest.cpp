// v0.2 V7b R5: the home menu's index <-> item mapping (home/HomeMenuIndex.h), over every OPDS x Sync Vocabulary row
// combination.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "activities/home/HomeMenuIndex.h"

TEST(HomeMenuIndex, EveryCombinationRoundTripsInTheMenusOrder) {
  for (const bool opds : {false, true}) {
    for (const bool vocab : {false, true}) {
      SCOPED_TRACE(std::string("opds ") + (opds ? "on" : "off") + ", vocab " + (vocab ? "on" : "off"));
      std::vector<HomeMenuItem> rows = {HomeMenuItem::FILE_BROWSER, HomeMenuItem::LIBRARY};
      if (opds) rows.push_back(HomeMenuItem::OPDS_BROWSER);
      rows.push_back(HomeMenuItem::FILE_TRANSFER);
      if (vocab) rows.push_back(HomeMenuItem::VOCAB_SYNC);  // just above Settings
      rows.push_back(HomeMenuItem::SETTINGS_MENU);
      for (int i = 0; i < static_cast<int>(rows.size()); i++) {
        EXPECT_EQ(homeMenuItemAt(i, opds, vocab), rows[static_cast<size_t>(i)]) << i;
        EXPECT_EQ(homeMenuIndexOf(rows[static_cast<size_t>(i)], opds, vocab), i) << i;
      }
      EXPECT_EQ(homeMenuItemAt(static_cast<int>(rows.size()), opds, vocab), HomeMenuItem::NONE);
      if (!opds) EXPECT_EQ(homeMenuIndexOf(HomeMenuItem::OPDS_BROWSER, opds, vocab), 0);  // absent: the first row
      if (!vocab) EXPECT_EQ(homeMenuIndexOf(HomeMenuItem::VOCAB_SYNC, opds, vocab), 0);
    }
  }
}
