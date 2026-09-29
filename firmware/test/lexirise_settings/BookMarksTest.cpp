// V9a: each book's "Page marks" row in the reader menu (settings/BookMarks.h, marks-off.ini).

#include <gtest/gtest.h>

#include <string>

#include "Fakes.h"
#include "lexirise/settings/BookMarks.h"

using lexipoint::BookMarksOffList;
using lexipoint::BookMarksRow;
using lexipoint::BookMarksStore;
using lexipoint::marksOnIn;
using lexipoint::parseBookMarksOff;
using lexipoint::serializeBookMarksOff;
using lexipoint::setMarksIn;
namespace config = lexipoint::config;

TEST(BookMarks, ABookShowsMarksUnlessItsTurnedOff) {
  const BookMarksOffList list = parseBookMarksOff("/Books/三体.epub\r\n\n/Books/a.epub\n/Books/三体.epub\n");
  EXPECT_EQ(list, (BookMarksOffList{"/Books/a.epub", "/Books/三体.epub"}));  // listed twice: once, where it's newest
  EXPECT_FALSE(marksOnIn(list, "/Books/三体.epub"));
  EXPECT_TRUE(marksOnIn(list, "/Books/other.epub"));  // the default: on
  EXPECT_EQ(serializeBookMarksOff(list), "/Books/a.epub\n/Books/三体.epub\n");
  EXPECT_EQ(parseBookMarksOff(serializeBookMarksOff(list)), list);
}

TEST(BookMarks, OnTakesTheLineAwayAndOffPutsItLast) {
  BookMarksOffList list = {"/a.epub", "/b.epub"};
  EXPECT_TRUE(setMarksIn(list, "/a.epub", false));
  EXPECT_EQ(list, (BookMarksOffList{"/b.epub", "/a.epub"}));
  EXPECT_TRUE(setMarksIn(list, "/b.epub", true));
  EXPECT_EQ(list, (BookMarksOffList{"/a.epub"}));
  EXPECT_FALSE(setMarksIn(list, "", false));
  EXPECT_FALSE(setMarksIn(list, "/x\n.epub", false));
  EXPECT_EQ(list, (BookMarksOffList{"/a.epub"}));
}

TEST(BookMarks, TheOldestAreForgottenPastTheLimits) {
  BookMarksOffList list;
  for (size_t i = 0; i <= config::kBookMarksOffMax; i++) setMarksIn(list, "/" + std::to_string(i) + ".epub", false);
  ASSERT_EQ(list.size(), config::kBookMarksOffMax);
  EXPECT_TRUE(marksOnIn(list, "/0.epub"));  // forgotten: its marks come back
  BookMarksOffList longPaths;
  const std::string tail(1000, 'x');
  for (int i = 0; i < 40; i++) setMarksIn(longPaths, "/" + std::to_string(i) + tail, false);
  EXPECT_LE(serializeBookMarksOff(longPaths).size(), config::kBookMarksOffMaxBytes);
  EXPECT_EQ(longPaths.back(), "/39" + tail);
}

TEST(BookMarksRow, ReadsTheBookAndSavesEachTap) {
  lexipoint::fakes::FakeFiles files;
  BookMarksStore store(files);
  BookMarksRow row(store);
  row.open("/Books/三体.epub");
  EXPECT_TRUE(row.on());
  EXPECT_TRUE(row.toggle());
  EXPECT_FALSE(row.on());
  EXPECT_EQ(files.files[config::kBookMarksOffPath], "/Books/三体.epub\n");
  EXPECT_FALSE(BookMarksStore(files).on("/Books/三体.epub"));  // as a reboot reads it
  files.failWriteOf = config::kBookMarksOffTmpPath;
  EXPECT_FALSE(row.toggle());
  EXPECT_FALSE(row.on());  // as saved
  files.failWriteOf.clear();
  EXPECT_TRUE(row.toggle());
  EXPECT_EQ(files.files[config::kBookMarksOffPath], "");
  row.open("/Books/other.epub");
  EXPECT_TRUE(row.on());
}
