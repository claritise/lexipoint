// V8 R7: the asset the device's updater looks for is the one publish_release.py uploads (test_publish_release.py
// test_the_asset_is_what_the_updater_looks_for pins the same name for the script).

#include <gtest/gtest.h>

#include <string>

#include "lexirise/ota/ReleaseAsset.h"

using lexipoint::ota::releaseAssetName;

TEST(ReleaseAsset, TheNameIsWhatPublishReleaseUploads) {
  char name[lexipoint::config::kReleaseAssetNameBytes] = {};
  ASSERT_TRUE(releaseAssetName("1.6.5-lexi.2", name, sizeof(name)));
  EXPECT_STREQ(name, "lexipoint-1.6.5-lexi.2-x4pro.bin");
}

TEST(ReleaseAsset, TheLongestTagFitsAndOneMoreDoesnt) {
  const size_t longest = lexipoint::config::kReleaseAssetNameBytes - std::string("lexipoint--x4pro.bin").size() - 1;
  char name[lexipoint::config::kReleaseAssetNameBytes] = {};
  EXPECT_TRUE(releaseAssetName(std::string(longest, '9').c_str(), name, sizeof(name)));
  EXPECT_FALSE(releaseAssetName(std::string(longest + 1, '9').c_str(), name, sizeof(name)));
}
