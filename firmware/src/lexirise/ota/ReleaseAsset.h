#pragma once

// The firmware asset the OTA updater looks for in a release: <kReleaseAssetPrefix><tag><kReleaseAssetSuffix>, the name
// scripts/lexipoint/publish_release.py gives it (asset_name). Pure; tests: test/lexirise_ota, test_publish_release.py.

#include <cstddef>
#include <cstdio>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::ota {

// Writes the asset's name into `out` (`size` bytes). False: it doesn't fit (a tag too long for the updater).
inline bool releaseAssetName(const char* tag, char* out, const size_t size) {
  const int n = std::snprintf(out, size, "%s%s%s", config::kReleaseAssetPrefix, tag, config::kReleaseAssetSuffix);
  return n > 0 && static_cast<size_t>(n) < size;
}

}  // namespace lexipoint::ota
