// v0.2 V7b R5: the home menu's index <-> item mapping (home/HomeMenuIndex.h), with and without the Sync Vocabulary
// row; and File Transfer's choices (network/NetworkModes.h).

#include <gtest/gtest.h>

#include <iterator>
#include <string>
#include <vector>

#include "activities/home/HomeMenuIndex.h"
#include "activities/network/NetworkModes.h"

TEST(HomeMenuIndex, EveryCombinationRoundTripsInTheMenusOrder) {
  for (const bool vocab : {false, true}) {
    SCOPED_TRACE(std::string("vocab ") + (vocab ? "on" : "off"));
    std::vector<HomeMenuItem> rows = {HomeMenuItem::FILE_BROWSER, HomeMenuItem::LIBRARY, HomeMenuItem::FILE_TRANSFER};
    if (vocab) rows.push_back(HomeMenuItem::VOCAB_SYNC);  // just above Settings
    rows.push_back(HomeMenuItem::SETTINGS_MENU);
    for (int i = 0; i < static_cast<int>(rows.size()); i++) {
      EXPECT_EQ(homeMenuItemAt(i, vocab), rows[static_cast<size_t>(i)]) << i;
      EXPECT_EQ(homeMenuIndexOf(rows[static_cast<size_t>(i)], vocab), i) << i;
    }
    EXPECT_EQ(homeMenuItemAt(static_cast<int>(rows.size()), vocab), HomeMenuItem::NONE);
    if (!vocab) EXPECT_EQ(homeMenuIndexOf(HomeMenuItem::VOCAB_SYNC, vocab), 0);  // absent: the first row
  }
}

// V8 R10: File Transfer's choices, in the list's order (activities/network/NetworkModes.h; the activity's rows
// follow it, a static_assert there).
TEST(NetworkModes, JoinHotspotThenUsbDrive) {
  const std::vector<NetworkMode> modes(std::begin(kNetworkModes), std::end(kNetworkModes));
  EXPECT_EQ(modes,
            (std::vector<NetworkMode>{NetworkMode::JOIN_NETWORK, NetworkMode::CREATE_HOTSPOT, NetworkMode::USB_DRIVE}));
  EXPECT_EQ(kNetworkModeCount, 3u);
}
