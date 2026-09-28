// v0.2 V7b: page analyses on the SD card (page/PageStore.h), over the in-memory card.

#include <gtest/gtest.h>

#include <string>

#include "Fakes.h"
#include "lexirise/page/PageStore.h"

using lexipoint::Language;
using lexipoint::fakes::FakeFiles;
using namespace lexipoint::page;
namespace config = lexipoint::config;

namespace {

PageAnalysis pageOf(const std::string& text, const Language language = Language::Japanese) {
  PageAnalysis page;
  EXPECT_EQ(parsePage(R"({"occurrences":[{"word":"猫","entryId":7,"charStart":0,"charEnd":1,"isWordLike":true}],)"
                      R"("entryMetaById":{"7":{"rank":12}},"stateByEntryId":{}})",
                      language, page),
            lexipoint::api::ParseStatus::Ok);
  page.textUnits = static_cast<uint32_t>(text.size());  // ASCII in these tests: a byte a unit
  page.textHash = textHash(text);
  return page;
}

}  // namespace

TEST(PageStore, APageIsReadBackOnlyForTheVeryTextItWasAskedFor) {
  FakeFiles files;
  PageStore store(files);
  const PageKey key{bookKey("/books/a.epub"), 3, 1200};
  ASSERT_TRUE(store.write(key, pageOf("some text")));
  EXPECT_TRUE(files.exists(pagePath(key).c_str()));
  const auto hit = store.read(key, Language::Japanese, 9, textHash("some text"));
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->occurrences.size(), 1u);
  EXPECT_FALSE(store.read(key, Language::Japanese, 9, textHash("some texT")));  // the layout moved the page's end
  EXPECT_FALSE(store.read(key, Language::Japanese, 10, textHash("some text")));
  EXPECT_FALSE(store.read(key, Language::Chinese, 9, textHash("some text")));  // asked as another language
  EXPECT_FALSE(store.read(PageKey{key.book, 3, 1201}, Language::Japanese, 9, textHash("some text")));
}

TEST(PageStore, AFileThatDoesntCheckOutIsRemoved) {
  FakeFiles files;
  PageStore store(files);
  const PageKey key{1, 0, 0};
  ASSERT_TRUE(store.write(key, pageOf("x")));
  files.files[pagePath(key)][60] ^= 0x10;
  EXPECT_FALSE(store.read(key, Language::Japanese, 1, textHash("x")));
  EXPECT_FALSE(files.exists(pagePath(key).c_str()));
}

TEST(PageStore, PastItsCapTheOldestPagesFileGoes) {
  FakeFiles files;
  PageStore store(files);
  for (uint32_t i = 0; i <= config::kPageCacheFiles; i++) ASSERT_TRUE(store.write(PageKey{1, 0, i}, pageOf("x")));
  EXPECT_EQ(store.kept(), config::kPageCacheFiles);
  EXPECT_FALSE(files.exists(pagePath(PageKey{1, 0, 0}).c_str()));
  EXPECT_TRUE(files.exists(pagePath(PageKey{1, 0, 1}).c_str()));
  EXPECT_TRUE(files.exists(pagePath(PageKey{1, 0, config::kPageCacheFiles}).c_str()));
  // A reboot reads the index back (flushed as the reader closed): the next page still pushes out the oldest.
  ASSERT_TRUE(store.flush());
  PageStore again(files);
  ASSERT_TRUE(again.write(PageKey{2, 0, 0}, pageOf("x")));
  EXPECT_FALSE(files.exists(pagePath(PageKey{1, 0, 1}).c_str()));
  EXPECT_EQ(again.kept(), config::kPageCacheFiles);
}

TEST(PageStore, ARewrittenPageMovesToTheNewestEnd) {
  FakeFiles files;
  PageStore store(files);
  ASSERT_TRUE(store.write(PageKey{1, 0, 0}, pageOf("x")));
  ASSERT_TRUE(store.write(PageKey{1, 0, 1}, pageOf("x")));
  ASSERT_TRUE(store.write(PageKey{1, 0, 0}, pageOf("y")));  // analyzed again (the layout changed)
  ASSERT_TRUE(store.flush());
  std::vector<PageKey> index;
  ASSERT_TRUE(parseIndex(files.files[config::kPageIndexPath], index));
  ASSERT_EQ(index.size(), 2u);
  EXPECT_EQ(index.back(), (PageKey{1, 0, 0}));
  const int writes = files.writes;
  ASSERT_TRUE(store.write(PageKey{1, 0, 0}, pageOf("z")));  // already the newest: only the page is written
  EXPECT_EQ(files.writes, writes + 1);
  EXPECT_TRUE(store.flush());  // nothing to save
  EXPECT_EQ(files.writes, writes + 1);
}

TEST(PageStore, AFailedWriteLeavesNoFileAndNoIndexEntry) {
  FakeFiles files;
  PageStore store(files);
  const PageKey key{1, 0, 0};
  files.failWriteOf = pagePath(key);
  EXPECT_FALSE(store.write(key, pageOf("x")));
  EXPECT_FALSE(files.exists(pagePath(key).c_str()));
  EXPECT_EQ(store.kept(), 0u);
}

TEST(PageStore, TheIndexRefusesWhatItDidntWrite) {
  std::vector<PageKey> index{{1, 2, 3}, {4, 5, 6}};
  const std::string bytes = serializeIndex(index);
  std::vector<PageKey> back;
  ASSERT_TRUE(parseIndex(bytes, back));
  EXPECT_EQ(back, index);
  std::string flipped = bytes;
  flipped[20] ^= 1;
  EXPECT_FALSE(parseIndex(flipped, back));
  EXPECT_FALSE(parseIndex(bytes.substr(0, bytes.size() - 1), back));
  FakeFiles files;
  files.files[config::kPageIndexPath] = "junk";
  PageStore store(files);
  EXPECT_EQ(store.kept(), 0u);  // a fresh cache (the folder removed: AnUnreadableIndexTakesTheWholeCacheWithIt)
}

TEST(PageStore, PathsNameTheBookSectionAndStart) {
  EXPECT_EQ(pagePath(PageKey{0xABCDEF01u, 12, 3456}), "/.lexirise/pages/abcdef01/12-3456.bin");
  EXPECT_EQ(bookDir(0x1u), "/.lexirise/pages/00000001");
  EXPECT_EQ(bookKey("/books/a.epub"), textHash("/books/a.epub"));
}

TEST(PageStore, TheIndexIsSavedEveryFewPagesAndAtFlush) {
  FakeFiles files;
  PageStore store(files);
  for (uint32_t i = 0; i + 1 < config::kPageIndexSaveEvery; i++)
    ASSERT_TRUE(store.write(PageKey{1, 0, i}, pageOf("x")));
  EXPECT_FALSE(files.exists(config::kPageIndexPath));  // not yet
  ASSERT_TRUE(store.write(PageKey{1, 0, 99}, pageOf("x")));
  std::vector<PageKey> index;
  ASSERT_TRUE(parseIndex(files.files[config::kPageIndexPath], index));
  EXPECT_EQ(index.size(), config::kPageIndexSaveEvery);
  ASSERT_TRUE(store.write(PageKey{1, 0, 100}, pageOf("x")));
  EXPECT_TRUE(store.flush());  // the reader closing
  ASSERT_TRUE(parseIndex(files.files[config::kPageIndexPath], index));
  EXPECT_EQ(index.size(), config::kPageIndexSaveEvery + 1);
  const int writes = files.writes;
  EXPECT_TRUE(store.flush());  // nothing new: nothing written
  EXPECT_EQ(files.writes, writes);
}

TEST(PageStore, AnUnreadableIndexTakesTheWholeCacheWithIt) {
  FakeFiles files;
  files.files[pagePath(PageKey{3, 1, 2})] = "an orphan";
  files.files[config::kPageIndexPath] = "junk";
  PageStore store(files);
  EXPECT_EQ(store.kept(), 0u);
  EXPECT_FALSE(files.exists(pagePath(PageKey{3, 1, 2}).c_str()));
  EXPECT_FALSE(files.exists(config::kPageIndexPath));
}

TEST(PageStore, AnIndexTooLargeToReadTakesTheCacheWithItToo) {
  FakeFiles files;
  files.files[pagePath(PageKey{3, 1, 2})] = "an orphan";
  files.files[config::kPageIndexPath] = std::string(config::kPageIndexMaxBytes + 1, 'x');
  PageStore store(files);
  EXPECT_EQ(store.kept(), 0u);
  EXPECT_FALSE(files.exists(pagePath(PageKey{3, 1, 2}).c_str()));
}
