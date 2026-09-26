// C2: the book tag on each save, and each tag's book title (book-tags.ini).

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Fakes.h"
#include "lexirise/settings/BookTags.h"

using lexipoint::addBookTagIn;
using lexipoint::BookTagList;
using lexipoint::BookTagStore;
using lexipoint::parseBookTags;
using lexipoint::serializeBookTags;
using lexipoint::Settings;
using Tags = std::vector<std::string>;
namespace config = lexipoint::config;

TEST(BookTags, ParsesItsLinesAndSkipsTheRest) {
  const auto list = parseBookTags(
      "norwegian-wood=Norwegian Wood\r\n"
      "h98593b64=活着\n"
      "Bad Slug=x\n"  // not a slug
      "empty=\n"      // no title
      "garbage\n"
      "1q84=1Q84 = a=b\n"  // only the first `=` separates
      "\n"
      "norwegian-wood=Noruwei no Mori");  // listed again: the last line wins
  EXPECT_EQ(list, (BookTagList{{"h98593b64", "活着"}, {"1q84", "1Q84 = a=b"}, {"norwegian-wood", "Noruwei no Mori"}}));
}

TEST(BookTags, RoundTrips) {
  const BookTagList list = {{"kokoro", "Kokoro"}, {"h1aae0462", "変身"}};
  EXPECT_EQ(serializeBookTags(list), "kokoro=Kokoro\nh1aae0462=変身\n");
  EXPECT_EQ(parseBookTags(serializeBookTags(list)), list);
}

TEST(BookTags, AListedSlugKeepsItsFirstTitle) {
  BookTagList list = {{"kokoro", "Kokoro"}};
  EXPECT_FALSE(addBookTagIn(list, "kokoro", "KOKORO!"));
  EXPECT_TRUE(addBookTagIn(list, "sanshiro", "Sanshirō"));
  EXPECT_EQ(list, (BookTagList{{"kokoro", "Kokoro"}, {"sanshiro", "Sanshirō"}}));
  EXPECT_EQ(lexipoint::bookTitleIn(list, "sanshiro"), "Sanshirō");
  EXPECT_EQ(lexipoint::bookTitleIn(list, "other"), std::nullopt);
}

TEST(BookTags, ATitleIsKeptOnOneLineAndCutAtACharacter) {
  BookTagList list;
  EXPECT_FALSE(addBookTagIn(list, "a=b", "x"));       // not a slug
  EXPECT_FALSE(addBookTagIn(list, "blank", " \n "));  // no title
  EXPECT_TRUE(addBookTagIn(list, "two-lines", "  Two\r\nLines\t "));
  EXPECT_EQ(list.back().title, "Two Lines");
  std::string long_;
  for (int i = 0; i < 100; i++) long_ += "字";  // 3 bytes each
  EXPECT_TRUE(addBookTagIn(list, "h00000000", long_));
  EXPECT_EQ(list.back().title, long_.substr(0, config::kBookTagTitleMaxBytes / 3 * 3));
}

TEST(BookTags, TheOldestAreForgottenPastTheLimits) {
  BookTagList list;
  for (size_t i = 0; i <= config::kBookTagsMax; i++) addBookTagIn(list, "b" + std::to_string(i), "Book");
  ASSERT_EQ(list.size(), config::kBookTagsMax);
  EXPECT_EQ(list.front().slug, "b1");  // b0, the oldest, went

  BookTagList longTitles;
  const std::string title(config::kBookTagTitleMaxBytes, 'x');
  for (size_t i = 0; i < config::kBookTagsMax; i++) addBookTagIn(longTitles, "b" + std::to_string(i), title);
  EXPECT_LE(serializeBookTags(longTitles).size(), config::kBookTagsMaxBytes);
  EXPECT_EQ(longTitles.back().slug, "b" + std::to_string(config::kBookTagsMax - 1));  // the newest always stays
}

TEST(SaveTags, TheUsersTagsThenTheBooks) {
  Settings s;
  s.tags = "xteink,novels";
  EXPECT_EQ(lexipoint::saveTags(s, "book:kokoro"), (Tags{"xteink", "novels", "book:kokoro"}));
  s.tagBook = false;
  EXPECT_EQ(lexipoint::saveTags(s, "book:kokoro"), (Tags{"xteink", "novels"}));
}

TEST(SaveTags, ABookTagTheUserTypedIsNotAddedTwice) {
  Settings s;
  s.tags = "book:kokoro,xteink";
  EXPECT_EQ(lexipoint::saveTags(s, "book:kokoro"), (Tags{"book:kokoro", "xteink"}));
  s.tags = "";
  EXPECT_EQ(lexipoint::saveTags(s, "book:kokoro"), (Tags{"book:kokoro"}));
  EXPECT_EQ(lexipoint::saveTags(s, ""), Tags{});
}

TEST(BookSaveTags, RecordsANewBooksTitleOnceAndTagsTheSave) {
  lexipoint::fakes::FakeFiles files;
  BookTagStore store(files);
  Settings s;
  EXPECT_EQ(lexipoint::bookSaveTags(s, "活着", "/Books/活着.epub", store), (Tags{"xteink", "book:h98593b64"}));
  EXPECT_EQ(files.files[config::kBookTagsPath], "h98593b64=活着\n");
  const int writes = files.writes;
  lexipoint::bookSaveTags(s, "活着", "/Books/活着.epub", store);      // the next card in the book: no write
  lexipoint::bookSaveTags(s, "活着", "/Other/活着 (2).epub", store);  // the same title elsewhere: one tag
  EXPECT_EQ(files.writes, writes);
  EXPECT_EQ(BookTagStore(files).title("h98593b64"), "活着");  // as a reboot reads it
}

TEST(BookSaveTags, AnUntitledBookIsRecordedByItsFileName) {
  lexipoint::fakes::FakeFiles files;
  BookTagStore store(files);
  EXPECT_EQ(lexipoint::bookSaveTags(Settings{}, "", "/Books/untitled.epub", store), (Tags{"xteink", "book:h74959a8c"}));
  EXPECT_EQ(store.title("h74959a8c"), "untitled");
}

TEST(BookSaveTags, AControlCharactersOnlyTitleIsUntitledForTheTagAndTheRecord) {
  lexipoint::fakes::FakeFiles files;
  BookTagStore store(files);
  EXPECT_EQ(lexipoint::bookSaveTags(Settings{}, "\x01\t\r\n", "/Books/untitled.epub", store),
            (Tags{"xteink", "book:h74959a8c"}));
  EXPECT_EQ(store.title("h74959a8c"), "untitled");
}

TEST(BookSaveTags, OffMeansNoTagAndNoRecord) {
  lexipoint::fakes::FakeFiles files;
  BookTagStore store(files);
  Settings s;
  s.tagBook = false;
  EXPECT_EQ(lexipoint::bookSaveTags(s, "Kokoro", "/k.epub", store), Tags{"xteink"});
  EXPECT_EQ(files.writes, 0);
  EXPECT_EQ(files.files.count(config::kBookTagsPath), 0u);
}

TEST(BookSaveTags, AFailedRecordStillTagsTheSave) {
  lexipoint::fakes::FakeFiles files;
  BookTagStore store(files);
  files.failWriteOf = config::kBookTagsTmpPath;
  EXPECT_EQ(lexipoint::bookSaveTags(Settings{}, "Kokoro", "/k.epub", store), (Tags{"xteink", "book:kokoro"}));
  EXPECT_EQ(store.title("kokoro"), std::nullopt);  // not recorded: the next card tries again
  EXPECT_EQ(files.files.count(config::kBookTagsPath), 0u);
}

TEST(BookTagStore, LoadsOnFirstUseAndSavesANewSlug) {
  lexipoint::fakes::FakeFiles files;
  files.files[config::kBookTagsPath] = "kokoro=Kokoro\n";
  BookTagStore store(files);
  EXPECT_EQ(store.title("kokoro"), "Kokoro");
  EXPECT_TRUE(store.remember("kokoro", "Something else"));  // listed: nothing written
  EXPECT_EQ(files.writes, 0);
  EXPECT_TRUE(store.remember("sanshiro", "Sanshiro"));
  EXPECT_EQ(files.files[config::kBookTagsPath], "kokoro=Kokoro\nsanshiro=Sanshiro\n");
}

TEST(BookTagStore, AFailedWriteIsRetriedByTheNextRemember) {
  lexipoint::fakes::FakeFiles files;
  BookTagStore store(files);
  files.failWriteOf = config::kBookTagsTmpPath;
  EXPECT_FALSE(store.remember("kokoro", "Kokoro"));
  EXPECT_EQ(store.title("kokoro"), std::nullopt);
  EXPECT_TRUE(store.remember("kokoro", "Kokoro"));
  EXPECT_EQ(files.files[config::kBookTagsPath], "kokoro=Kokoro\n");
  EXPECT_EQ(BookTagStore(files).title("kokoro"), "Kokoro");
}

TEST(BookTagStore, AnInterruptedSaveIsRecoveredAndAnUnreadableFileSetAside) {
  lexipoint::fakes::FakeFiles files;
  files.files[config::kBookTagsBackupPath] = "kokoro=Kokoro\n";  // stopped between "final → .bak" and "tmp → final"
  files.files[config::kBookTagsTmpPath] = "kokoro=Kokoro\nsan";
  EXPECT_EQ(BookTagStore(files).title("kokoro"), "Kokoro");
  EXPECT_EQ(files.files.count(config::kBookTagsTmpPath), 0u);

  lexipoint::fakes::FakeFiles big;
  big.files[config::kBookTagsPath] = std::string(config::kBookTagsMaxBytes + 1, 'x');
  BookTagStore store(big);
  EXPECT_EQ(store.title("kokoro"), std::nullopt);
  EXPECT_EQ(big.files.count(config::kBookTagsBadPath), 1u);  // the next save can't overwrite it
  EXPECT_TRUE(store.remember("kokoro", "Kokoro"));
  EXPECT_EQ(big.files[config::kBookTagsPath], "kokoro=Kokoro\n");
}
