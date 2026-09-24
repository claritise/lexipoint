#include <gtest/gtest.h>

#include <map>
#include <string>

#include "lexirise/web/HiddenPath.h"

using lexipoint::web::isHiddenPath;
using lexipoint::web::NameLookup;

TEST(HiddenPath, AnyDotSegmentIsHidden) {
  for (const char* hidden : {"/.lexirise", "/.lexirise/config.ini", "/.crosspoint/wifi.json", "/books/.cache/x",
                             "/a/b/.hidden", "/..", "//.x", ".lexirise/config.ini"}) {
    EXPECT_TRUE(isHiddenPath(hidden)) << hidden;
  }
}

TEST(HiddenPath, OrdinaryPathsAreNot) {
  for (const char* visible : {"/", "", "/books", "/books/a.epub", "/a.b/c", "/x/y.z/", "/日本語/本.epub"}) {
    EXPECT_FALSE(isHiddenPath(visible)) << visible;
  }
}

namespace {
// A card with /.lexirise (short name LEXIRI~1), /Books (no alias) and /MYNOTE~1 = "My notes.txt".
NameLookup card() {
  static const std::map<std::string, std::string> names = {{"/LEXIRI~1", ".lexirise"},
                                                           {"/lexiri~1", ".lexirise"},
                                                           {"/MYNOTE~1", "My notes.txt"},
                                                           {"/Books/BOOK~1.EPU", "Book one.epub"}};
  return [](std::string_view prefix) -> std::optional<std::string> {
    const auto it = names.find(std::string(prefix));
    if (it == names.end()) return std::nullopt;
    return it->second;
  };
}
}  // namespace

TEST(HiddenPath, ShortNameAliasesOfDotFoldersAreHidden) {
  const NameLookup lookup = card();
  EXPECT_TRUE(isHiddenPath("/LEXIRI~1/config.ini", lookup));
  EXPECT_TRUE(isHiddenPath("/lexiri~1", lookup));    // FAT names are case-insensitive
  EXPECT_TRUE(isHiddenPath("/NOSUCH~1/x", lookup));  // unresolvable alias: refused
  EXPECT_FALSE(isHiddenPath("/MYNOTE~1", lookup));   // an alias of an ordinary file
  EXPECT_FALSE(isHiddenPath("/Books/BOOK~1.EPU", lookup));
  EXPECT_FALSE(isHiddenPath("/LEXIRI~1/config.ini"));  // without a lookup only the typed names count
}

TEST(HiddenPath, SegmentsWithoutTildeNeverTouchTheCard) {
  int lookups = 0;
  const NameLookup counting = [&lookups](std::string_view) -> std::optional<std::string> {
    lookups++;
    return std::string("x");
  };
  EXPECT_FALSE(isHiddenPath("/books/series/a.epub", counting));
  EXPECT_EQ(lookups, 0);
  EXPECT_FALSE(isHiddenPath("/books/A~1/b~2", counting));
  EXPECT_EQ(lookups, 2);
}
