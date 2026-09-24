#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>

#include "lexirise/web/HiddenPath.h"

using lexipoint::web::isHiddenPath;
using lexipoint::web::NameLookup;
using lexipoint::web::NameLookupResult;
using lexipoint::web::readEntryName;
using Kind = NameLookupResult::Kind;

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
// A card with /.lexirise (short name LEXIRI~1), a long "~" book, and one entry whose name can't be read.
NameLookup card() {
  static const std::map<std::string, NameLookupResult> entries = {
      {"/LEXIRI~1", {Kind::Found, ".lexirise"}},
      {"/lexiri~1", {Kind::Found, ".lexirise"}},
      {"/MYNOTE~1", {Kind::Found, "My notes.txt"}},
      {"/Books/Tolkien ~ The Hobbit.epub", {Kind::Found, "Tolkien ~ The Hobbit.epub"}},
      {"/BROKEN~1", {Kind::Unreadable, ""}},
  };
  return [](std::string_view prefix) {
    const auto it = entries.find(std::string(prefix));
    return it == entries.end() ? NameLookupResult{} : it->second;
  };
}
}  // namespace

TEST(HiddenPath, ShortNameAliasesOfDotFoldersAreHidden) {
  const NameLookup lookup = card();
  EXPECT_TRUE(isHiddenPath("/LEXIRI~1/config.ini", lookup));
  EXPECT_TRUE(isHiddenPath("/lexiri~1", lookup));      // FAT names are case-insensitive
  EXPECT_TRUE(isHiddenPath("/BROKEN~1/x", lookup));    // exists, name unreadable: refused
  EXPECT_FALSE(isHiddenPath("/LEXIRI~1/config.ini"));  // without a lookup only the typed names count
}

TEST(HiddenPath, OrdinaryTildeNamesStayUsable) {
  const NameLookup lookup = card();
  EXPECT_FALSE(isHiddenPath("/MYNOTE~1", lookup));                         // alias of an ordinary file
  EXPECT_FALSE(isHiddenPath("/Books/Tolkien ~ The Hobbit.epub", lookup));  // long name with a tilde
  EXPECT_FALSE(isHiddenPath("/books/new~file.epub", lookup));              // doesn't exist yet: can be created
}

TEST(HiddenPath, SegmentsWithoutTildeNeverTouchTheCard) {
  int lookups = 0;
  const NameLookup counting = [&lookups](std::string_view) {
    lookups++;
    return NameLookupResult{Kind::Found, "x"};
  };
  EXPECT_FALSE(isHiddenPath("/books/series/a.epub", counting));
  EXPECT_EQ(lookups, 0);
  EXPECT_FALSE(isHiddenPath("/books/A~1/b~2", counting));
  EXPECT_EQ(lookups, 2);
}

TEST(HiddenPath, ReadEntryNameFitsTheLongestName) {
  // SdFat's getName: 0 and nothing written when the buffer is too small.
  const std::string longest(lexipoint::config::kMaxFatNameBytes, 'x');
  const auto sdfat = [&longest](char* out, size_t size) -> size_t {
    if (longest.size() + 1 > size) return 0;
    std::memcpy(out, longest.c_str(), longest.size() + 1);
    return longest.size();
  };
  const NameLookupResult r = readEntryName(sdfat);
  EXPECT_EQ(r.kind, Kind::Found);
  EXPECT_EQ(r.name, longest);
  EXPECT_EQ(readEntryName([](char*, size_t) -> size_t { return 0; }).kind, Kind::Unreadable);
}
