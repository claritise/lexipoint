#include <gtest/gtest.h>

#include "lexirise/web/HiddenPath.h"

using lexipoint::web::isHiddenWebPath;

TEST(HiddenPath, AnyDotSegmentIsHidden) {
  for (const char* hidden : {"/.lexirise", "/.lexirise/config.ini", "/.crosspoint/wifi.json", "/books/.cache/x",
                             "/a/b/.hidden", "/..", "//.x", ".lexirise/config.ini"}) {
    EXPECT_TRUE(isHiddenWebPath(hidden)) << hidden;
  }
}

TEST(HiddenPath, OrdinaryPathsAreNot) {
  for (const char* visible : {"/", "", "/books", "/books/a.epub", "/a.b/c", "/x/y.z/", "/日本語/本.epub"}) {
    EXPECT_FALSE(isHiddenWebPath(visible)) << visible;
  }
}
