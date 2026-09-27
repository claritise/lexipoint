// C17 (V5): the words the reader ignored (ignored.ini), on the reader only.

#include <gtest/gtest.h>

#include <string>
#include <thread>

#include "Fakes.h"
#include "lexirise/settings/IgnoredWords.h"

using lexipoint::IgnoredKey;
using lexipoint::ignoredKeyFor;
using lexipoint::IgnoredWords;
using lexipoint::IgnoredWordStore;
using lexipoint::Language;
using lexipoint::parseIgnored;
using lexipoint::serializeIgnored;
namespace config = lexipoint::config;
using Write = lexipoint::IgnoredWordStore::Write;

namespace {

IgnoredKey ja(const uint32_t id) { return {Language::Japanese, id, {}}; }
IgnoredKey jaText(const std::string& text) { return {Language::Japanese, 0, text}; }

}  // namespace

TEST(IgnoredWords, AKeyIsTheEntryIdElseTheDictionaryForm) {
  EXPECT_EQ(ignoredKeyFor(Language::Chinese, 42, "选择"), (IgnoredKey{Language::Chinese, 42, {}}));
  EXPECT_EQ(ignoredKeyFor(Language::Japanese, 0, "和子"), jaText("和子"));
  EXPECT_FALSE(ignoredKeyFor(Language::Japanese, 0, ""));
  EXPECT_FALSE(ignoredKeyFor(Language::Japanese, 0, "a\nb"));  // one line only
  EXPECT_TRUE(ignoredKeyFor(Language::Japanese, 0, std::string(config::kIgnoredTextMaxBytes, 'x')));
  EXPECT_FALSE(ignoredKeyFor(Language::Japanese, 0, std::string(config::kIgnoredTextMaxBytes + 1, 'x')));  // never cut
}

TEST(IgnoredWords, ParsesItsLinesAndSkipsTheRest) {
  const IgnoredWords list = parseIgnored(
      "ja:123\r\n"
      "zh:123\n"  // the same id in the other language: another word
      "ja:~和子\n"
      "ko:5\n"              // an unknown language
      "ja:0\n"              // no entry
      "ja:12x\n"            // not a number
      "ja:-4\n"             // not a number
      "ja:~\n"              // no form
      "ja\n"                // no colon
      "ja:123\n"            // again: counted once
      "ja:99999999999\n");  // past uint32
  EXPECT_EQ(list.size(), 3u);
  EXPECT_TRUE(list.contains(ja(123)));
  EXPECT_TRUE(list.contains({Language::Chinese, 123, {}}));
  EXPECT_TRUE(list.contains(jaText("和子")));
  EXPECT_FALSE(list.contains(ja(5)));
  EXPECT_FALSE(list.contains({Language::Chinese, 0, "和子"}));
}

TEST(IgnoredWords, RoundTripsNewestLast) {
  IgnoredWords list;
  EXPECT_TRUE(list.add(ja(7)));
  EXPECT_TRUE(list.add(jaText("和子")));
  EXPECT_TRUE(list.add({Language::Chinese, 4294967295U, {}}));
  EXPECT_FALSE(list.add(ja(7)));  // already there
  const std::string text = serializeIgnored(list);
  EXPECT_EQ(text, "ja:7\nzh:4294967295\nja:~和子\n");
  EXPECT_EQ(parseIgnored(text), list);
  EXPECT_TRUE(list.remove(ja(7)));
  EXPECT_FALSE(list.remove(ja(7)));
  EXPECT_EQ(serializeIgnored(list), "zh:4294967295\nja:~和子\n");
}

TEST(IgnoredWords, PastTheCapsTheOldestIsForgotten) {
  IgnoredWords list;
  for (uint32_t id = 1; id <= config::kIgnoredIdsMax + 1; id++) list.add(ja(id));
  EXPECT_EQ(list.size(), config::kIgnoredIdsMax);
  EXPECT_FALSE(list.contains(ja(1)));
  EXPECT_TRUE(list.contains(ja(2)));
  for (size_t i = 0; i <= config::kIgnoredTextsMax; i++) list.add(jaText("w" + std::to_string(i)));
  EXPECT_EQ(list.size(), config::kIgnoredIdsMax + config::kIgnoredTextsMax);
  EXPECT_FALSE(list.contains(jaText("w0")));
  // Full, with the longest lines: still under the file's cap, and read back whole.
  IgnoredWords longest;
  for (uint32_t i = 0; i < config::kIgnoredIdsMax; i++) longest.add(ja(4294967295U - i));
  for (size_t i = 0; i < config::kIgnoredTextsMax; i++) {
    std::string form(config::kIgnoredTextMaxBytes, 'x');
    form.replace(0, std::to_string(i).size(), std::to_string(i));
    longest.add(jaText(form));
  }
  const std::string text = serializeIgnored(longest);
  EXPECT_LE(text.size(), config::kIgnoredMaxBytes);
  EXPECT_EQ(parseIgnored(text), longest);
}

TEST(IgnoredWordStore, LoadsOnFirstUseAndWritesOnlyAChange) {
  lexipoint::fakes::FakeFiles files;
  files.files[config::kIgnoredPath] = "ja:123\n";
  IgnoredWordStore store(files);
  EXPECT_TRUE(store.contains(ja(123)));
  EXPECT_NE(Write::Failed, store.write(ja(123), true));  // already: nothing written
  EXPECT_NE(Write::Failed, store.write(ja(5), false));
  EXPECT_EQ(files.writes, 0);
  EXPECT_NE(Write::Failed, store.write(jaText("和子"), true));
  EXPECT_EQ(files.files[config::kIgnoredPath], "ja:123\nja:~和子\n");
  EXPECT_NE(Write::Failed, store.write(ja(123), false));
  EXPECT_EQ(files.files[config::kIgnoredPath], "ja:~和子\n");
  EXPECT_TRUE(IgnoredWordStore(files).contains(jaText("和子")));  // read back, as after a reboot
  EXPECT_EQ(Write::Failed, store.write(jaText(""), true));        // not a key
}

TEST(IgnoredWordStore, AListIsACopy) {
  lexipoint::fakes::FakeFiles files;
  IgnoredWordStore store(files);
  const IgnoredWords copy = store.list();
  EXPECT_NE(Write::Failed, store.write(ja(1), true));
  EXPECT_EQ(copy.size(), 0u);
  EXPECT_EQ(store.list().size(), 1u);
}

TEST(IgnoredWordStore, AFailedWriteChangesNothing) {
  lexipoint::fakes::FakeFiles files;
  IgnoredWordStore store(files);
  files.failWriteOf = config::kIgnoredTmpPath;
  EXPECT_EQ(Write::Failed, store.write(ja(1), true));
  EXPECT_FALSE(store.contains(ja(1)));
  EXPECT_NE(Write::Failed, store.write(ja(1), true));
  EXPECT_EQ(files.files[config::kIgnoredPath], "ja:1\n");
}

TEST(IgnoredWordStore, AnInterruptedSaveIsRecoveredAndAnUnreadableFileSetAside) {
  lexipoint::fakes::FakeFiles files;
  files.files[config::kIgnoredBackupPath] = "ja:1\n";  // stopped between "final → .bak" and "tmp → final"
  files.files[config::kIgnoredTmpPath] = "ja:1\nja:2";
  EXPECT_TRUE(IgnoredWordStore(files).contains(ja(1)));
  EXPECT_EQ(files.files.count(config::kIgnoredTmpPath), 0u);

  lexipoint::fakes::FakeFiles big;
  big.files[config::kIgnoredPath] = std::string(config::kIgnoredMaxBytes + 1, 'x');
  IgnoredWordStore store(big);
  EXPECT_FALSE(store.contains(ja(1)));
  EXPECT_EQ(big.files.count(config::kIgnoredBadPath), 1u);  // the next save can't overwrite it
  EXPECT_NE(Write::Failed, store.write(ja(1), true));
  EXPECT_EQ(big.files[config::kIgnoredPath], "ja:1\n");

  // Readable, with lines that aren't keys: they're skipped (and dropped when the file is next written).
  lexipoint::fakes::FakeFiles garbled;
  garbled.files[config::kIgnoredPath] = "\x01\x02 not a key\nko:5\nja:9\n";
  IgnoredWordStore kept(garbled);
  EXPECT_TRUE(kept.contains(ja(9)));
  EXPECT_NE(Write::Failed, kept.write(ja(10), true));
  EXPECT_EQ(garbled.files[config::kIgnoredPath], "ja:9\nja:10\n");
}

TEST(IgnoredWordStore, ReadsNeverTearUnderConcurrentWrites) {
  lexipoint::fakes::FakeFiles files;
  IgnoredWordStore store(files);
  constexpr uint32_t kWrites = 200;
  std::thread writer([&] {
    for (uint32_t i = 1; i <= kWrites; i++) {
      store.write(ja(i), true);
      store.write(jaText("w" + std::to_string(i)), true);
    }
  });
  for (uint32_t i = 0; i < kWrites; i++) {
    const IgnoredWords list = store.list();
    // Each set is whole: the forms never run ahead of the ids.
    for (uint32_t id = 1; id <= kWrites; id++) {
      if (list.contains(jaText("w" + std::to_string(id)))) {
        EXPECT_TRUE(list.contains(ja(id)));
      }
    }
  }
  writer.join();
  EXPECT_EQ(parseIgnored(files.files[config::kIgnoredPath]), store.list());
  EXPECT_EQ(store.list().size(), kWrites + config::kIgnoredTextsMax);
}

TEST(IgnoredWordStore, AFailedWriteOnAFullListPutsItBackExactly) {
  lexipoint::fakes::FakeFiles files;
  IgnoredWords full;
  for (uint32_t id = 1; id <= config::kIgnoredIdsMax; id++) full.add(ja(id));
  full.add(jaText("和子"));
  files.files[config::kIgnoredPath] = serializeIgnored(full);
  IgnoredWordStore store(files);
  store.load();
  files.failWriteOf = config::kIgnoredTmpPath;
  EXPECT_EQ(Write::Failed, store.write(ja(99999), true));  // would forget ja:1
  EXPECT_EQ(store.list(), full);                           // ja:1 back, as oldest
  files.failWriteOf = config::kIgnoredTmpPath;             // the fake fails one write at a time
  EXPECT_EQ(Write::Failed, store.write(ja(500), false));
  EXPECT_EQ(store.list(), full);  // back in its place
  files.failWriteOf = config::kIgnoredTmpPath;
  EXPECT_EQ(Write::Failed, store.write(jaText("和子"), false));
  EXPECT_EQ(store.list(), full);
}

TEST(IgnoredWordStore, AFailedWriteOnAFullFormsListPutsTheOldestFormBack) {
  lexipoint::fakes::FakeFiles files;
  IgnoredWords full;
  for (size_t i = 0; i < config::kIgnoredTextsMax; i++) full.add(jaText("w" + std::to_string(i)));
  files.files[config::kIgnoredPath] = serializeIgnored(full);
  IgnoredWordStore store(files);
  store.load();
  files.failWriteOf = config::kIgnoredTmpPath;
  EXPECT_EQ(Write::Failed, store.write(jaText("new"), true));  // would forget w0
  EXPECT_EQ(store.list(), full);                               // w0 back, as oldest
  EXPECT_TRUE(store.contains(jaText("w0")));
}

TEST(IgnoredWords, AKeysTextIsItsLine) {
  EXPECT_EQ(lexipoint::ignoredKeyText(ja(4294967295U)), "ja:4294967295");
  EXPECT_EQ(lexipoint::ignoredKeyText({Language::Chinese, 0, "选择"}), "zh:~选择");
}

TEST(IgnoredWordStore, AWriteSaysWhetherItWroteOrHadNothingToDo) {
  lexipoint::fakes::FakeFiles files;
  IgnoredWordStore store(files);
  EXPECT_EQ(store.write(ja(1), true), Write::Written);
  EXPECT_EQ(store.write(ja(1), true), Write::Unchanged);
  EXPECT_EQ(store.write(ja(2), false), Write::Unchanged);
  EXPECT_EQ(store.write(jaText(""), true), Write::Failed);  // not a key
  files.failWriteOf = config::kIgnoredTmpPath;
  EXPECT_EQ(store.write(ja(1), false), Write::Failed);
  EXPECT_TRUE(store.contains(ja(1)));
}

TEST(IgnoredWordStore, UndoingAnIgnoreAtTheCapBringsBackTheOldest) {
  lexipoint::fakes::FakeFiles files;
  IgnoredWords full;
  for (uint32_t id = 1; id <= config::kIgnoredIdsMax; id++) full.add(ja(id));
  files.files[config::kIgnoredPath] = serializeIgnored(full);
  IgnoredWordStore store(files);
  store.load();
  std::optional<IgnoredKey> evicted;
  EXPECT_EQ(store.write(ja(99999), true, &evicted), Write::Written);  // pushes out ja:1
  EXPECT_EQ(evicted, ja(1));
  EXPECT_FALSE(store.contains(ja(1)));
  files.failWriteOf = config::kIgnoredTmpPath;
  EXPECT_EQ(store.write(ja(99999), false, nullptr, evicted), Write::Failed);  // its Undo's write fails: no change
  EXPECT_TRUE(store.contains(ja(99999)));
  EXPECT_FALSE(store.contains(ja(1)));
  EXPECT_EQ(store.write(ja(99999), false, nullptr, evicted), Write::Written);  // and goes through
  EXPECT_EQ(store.list(), full);                                               // ja:1 back, as oldest
  EXPECT_EQ(parseIgnored(files.files[config::kIgnoredPath]), full);
}

TEST(IgnoredWords, AtTheCapTheOldestGoesFirst) {
  // Never one past the cap, even for a moment (no reallocation past it on the device).
  IgnoredWords list;
  list.reserve(config::kIgnoredIdsMax, 0);
  for (uint32_t id = 1; id <= config::kIgnoredIdsMax; id++) list.add(ja(id));
  std::optional<IgnoredKey> evicted;
  EXPECT_TRUE(list.add(ja(5000), &evicted));
  EXPECT_EQ(evicted, ja(1));
  EXPECT_EQ(list.size(), config::kIgnoredIdsMax);
}

TEST(IgnoredWordStore, ARestoreNeverPutsAPartPastItsCap) {
  // A restore of the other kind (a form for an id's Undo) can't overfill a full forms part.
  lexipoint::fakes::FakeFiles files;
  IgnoredWordStore store(files);
  for (size_t i = 0; i < config::kIgnoredTextsMax; i++) store.write(jaText("w" + std::to_string(i)), true);
  store.write(ja(1), true);
  EXPECT_EQ(store.write(ja(1), false, nullptr, jaText("other")), Write::Written);
  EXPECT_FALSE(store.contains(jaText("other")));  // no room: not put back
  EXPECT_EQ(store.list().size(), config::kIgnoredTextsMax);
}
