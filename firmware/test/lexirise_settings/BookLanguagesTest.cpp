// languages.md §1, step 3: each book's lookup language (books.ini).

#include <gtest/gtest.h>

#include <iterator>
#include <string>
#include <vector>

#include "Fakes.h"
#include "lexirise/settings/BookLanguages.h"

using lexipoint::BookLanguageList;
using lexipoint::BookLanguageStore;
using lexipoint::Language;
using lexipoint::nextBookLanguage;
using lexipoint::parseBookLanguages;
using lexipoint::serializeBookLanguages;
using lexipoint::setBookLanguageIn;
namespace config = lexipoint::config;

TEST(BookLanguages, ParsesItsLinesAndSkipsTheRest) {
  const auto list = parseBookLanguages(
      "zh=/Books/三体.epub\r\n"
      "ja=/Books/a=b.epub\n"  // only the first `=` separates
      "en=/Books/x.epub\n"    // not a lookup language
      "zh=\n"                 // no path
      "garbage\n"
      "ZH=/Books/upper.epub\n"  // any case, as config.ini
      "\n"
      "ja=/Books/三体.epub");  // listed again: the last line wins
  EXPECT_EQ(list, (BookLanguageList{{"/Books/a=b.epub", Language::Japanese},
                                    {"/Books/upper.epub", Language::Chinese},
                                    {"/Books/三体.epub", Language::Japanese}}));
}

TEST(BookLanguages, RoundTrips) {
  const BookLanguageList list = {{"/a.epub", Language::Chinese}, {"/b c.epub", Language::Japanese}};
  EXPECT_EQ(serializeBookLanguages(list), "zh=/a.epub\nja=/b c.epub\n");
  EXPECT_EQ(parseBookLanguages(serializeBookLanguages(list)), list);
}

TEST(BookLanguages, SetMovesTheBookToTheEndAndAutoRemovesIt) {
  BookLanguageList list = {{"/a.epub", Language::Chinese}, {"/b.epub", Language::Japanese}};
  EXPECT_TRUE(setBookLanguageIn(list, "/a.epub", Language::Japanese));
  EXPECT_EQ(list, (BookLanguageList{{"/b.epub", Language::Japanese}, {"/a.epub", Language::Japanese}}));
  EXPECT_TRUE(setBookLanguageIn(list, "/b.epub", std::nullopt));
  EXPECT_EQ(list, (BookLanguageList{{"/a.epub", Language::Japanese}}));
  EXPECT_EQ(lexipoint::bookLanguageIn(list, "/a.epub"), Language::Japanese);
  EXPECT_EQ(lexipoint::bookLanguageIn(list, "/b.epub"), std::nullopt);
}

TEST(BookLanguages, APathThatCannotBeALineIsRefused) {
  BookLanguageList list = {{"/a.epub", Language::Chinese}};
  EXPECT_FALSE(setBookLanguageIn(list, "", Language::Chinese));
  EXPECT_FALSE(setBookLanguageIn(list, "/x\nja=/y.epub", Language::Chinese));
  EXPECT_EQ(list.size(), 1u);
}

TEST(BookLanguages, TheOldestAreForgottenPastTheLimits) {
  BookLanguageList list;
  for (size_t i = 0; i <= config::kBookLanguagesMax; i++) {
    setBookLanguageIn(list, "/" + std::to_string(i) + ".epub", Language::Chinese);
  }
  ASSERT_EQ(list.size(), config::kBookLanguagesMax);
  EXPECT_EQ(list.front().path, "/1.epub");  // /0.epub, the oldest, went

  BookLanguageList longPaths;
  const std::string tail(1000, 'x');
  for (int i = 0; i < 40; i++) setBookLanguageIn(longPaths, "/" + std::to_string(i) + tail, Language::Japanese);
  EXPECT_LE(serializeBookLanguages(longPaths).size(), config::kBookLanguagesMaxBytes);
  EXPECT_EQ(longPaths.back().path, "/39" + tail);  // the newest always stays
}

TEST(BookLanguages, TheMenuCyclesAutoJapaneseChinese) {
  // Auto, then kLanguages in settings order: a new language joins the cycle by being added there.
  std::optional<Language> choice;
  std::vector<std::optional<Language>> seen;
  for (size_t i = 0; i <= std::size(lexipoint::kLanguages); i++) seen.push_back(choice = nextBookLanguage(choice));
  EXPECT_EQ(seen, (std::vector<std::optional<Language>>{Language::Japanese, Language::Chinese, std::nullopt}));
}

TEST(BookLanguageRow, ReadsTheBooksChoiceAndSavesEachTap) {
  lexipoint::fakes::FakeFiles files;
  files.files[config::kBookLanguagesPath] = "zh=/Books/三体.epub\n";
  BookLanguageStore store(files);
  lexipoint::BookLanguageRow row(store);
  row.open("/Books/三体.epub");
  EXPECT_EQ(row.language(), Language::Chinese);
  EXPECT_TRUE(row.cycle());
  EXPECT_EQ(row.language(), std::nullopt);
  EXPECT_EQ(files.files[config::kBookLanguagesPath], "");  // Auto: the line goes
  EXPECT_TRUE(row.cycle());
  EXPECT_EQ(store.get("/Books/三体.epub"), Language::Japanese);
  files.failWriteOf = config::kBookLanguagesTmpPath;
  EXPECT_FALSE(row.cycle());
  EXPECT_EQ(row.language(), Language::Japanese);  // as saved
  row.open("/Books/other.epub");
  EXPECT_EQ(row.language(), std::nullopt);
}

TEST(BookLanguages, CodesRoundTrip) {
  for (const Language language : lexipoint::kLanguages) {
    EXPECT_EQ(lexipoint::languageFromCode(lexipoint::languageCode(language)), language);
  }
  EXPECT_EQ(lexipoint::languageFromCode("ZH"), Language::Chinese);  // a hand-edited file, as config.ini reads it
  EXPECT_EQ(lexipoint::languageFromCode("en"), std::nullopt);
}

TEST(BookLanguageStore, LoadsOnFirstUseAndSavesEachChange) {
  lexipoint::fakes::FakeFiles files;
  files.files[config::kBookLanguagesPath] = "zh=/Books/三体.epub\n";
  BookLanguageStore store(files);
  EXPECT_EQ(store.get("/Books/三体.epub"), Language::Chinese);
  EXPECT_EQ(store.get("/Books/other.epub"), std::nullopt);
  EXPECT_TRUE(store.set("/Books/other.epub", Language::Japanese));
  EXPECT_EQ(files.files[config::kBookLanguagesPath], "zh=/Books/三体.epub\nja=/Books/other.epub\n");
  EXPECT_TRUE(store.set("/Books/三体.epub", std::nullopt));
  EXPECT_EQ(files.files[config::kBookLanguagesPath], "ja=/Books/other.epub\n");
  EXPECT_EQ(BookLanguageStore(files).get("/Books/other.epub"), Language::Japanese);  // as a reboot reads it
}

TEST(BookLanguageStore, AFailedSaveChangesNothing) {
  lexipoint::fakes::FakeFiles files;
  BookLanguageStore store(files);
  files.failWriteOf = config::kBookLanguagesTmpPath;
  EXPECT_FALSE(store.set("/a.epub", Language::Chinese));
  EXPECT_EQ(store.get("/a.epub"), std::nullopt);
  EXPECT_EQ(files.files.count(config::kBookLanguagesPath), 0u);
}

TEST(BookLanguageStore, AnInterruptedSaveIsRecoveredAndAnUnreadableFileSetAside) {
  lexipoint::fakes::FakeFiles files;
  files.files[config::kBookLanguagesBackupPath] = "zh=/a.epub\n";  // stopped between "final → .bak" and "tmp → final"
  files.files[config::kBookLanguagesTmpPath] = "zh=/a.epub\nja=/b";
  EXPECT_EQ(BookLanguageStore(files).get("/a.epub"), Language::Chinese);
  EXPECT_EQ(files.files.count(config::kBookLanguagesTmpPath), 0u);

  lexipoint::fakes::FakeFiles big;
  big.files[config::kBookLanguagesPath] = std::string(config::kBookLanguagesMaxBytes + 1, 'x');
  BookLanguageStore store(big);
  EXPECT_EQ(store.get("/a.epub"), std::nullopt);
  EXPECT_EQ(big.files.count(config::kBookLanguagesBadPath), 1u);  // the next save can't overwrite it
  EXPECT_TRUE(store.set("/a.epub", Language::Chinese));
  EXPECT_EQ(big.files[config::kBookLanguagesPath], "zh=/a.epub\n");
}
