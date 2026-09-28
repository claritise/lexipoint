// The lemma cache (C21, V7c; lookup/LookupCache.h): its files, hits, misses, staleness, caps, and what it never keeps.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/lookup/LookupCache.h"

using lexipoint::Language;
using lexipoint::SettingsFiles;
using lexipoint::api::LookupResult;
using lexipoint::api::Sense;
using lexipoint::lookup::BucketFind;
using lexipoint::lookup::bucketPath;
using lexipoint::lookup::cacheable;
using lexipoint::lookup::CachedLookup;
using lexipoint::lookup::CacheRead;
using lexipoint::lookup::findInBucket;
using lexipoint::lookup::LookupCache;
using lexipoint::lookup::parseBucket;
using lexipoint::lookup::serializeBucket;
namespace config = lexipoint::config;

namespace {

// An in-memory card that counts what's done to it.
class CountingFiles final : public SettingsFiles {
 public:
  std::map<std::string, std::string> files;
  int reads = 0;
  int writes = 0;
  int removes = 0;
  bool failWrites = false;
  std::string failWriteOf;  // this path's writes fail
  std::string failReadOf;   // this path's reads fail (an SD error)
  size_t lastMaxBytes = 0;

  ReadStatus read(const char* path, const size_t maxBytes, std::string& out) override {
    reads++;
    lastMaxBytes = maxBytes;
    if (failReadOf == path) return ReadStatus::Error;
    const auto it = files.find(path);
    if (it == files.end()) return ReadStatus::Missing;
    if (it->second.size() > maxBytes) return ReadStatus::TooLarge;
    out = it->second;
    return ReadStatus::Ok;
  }
  bool write(const char* path, const std::string_view content) override {
    writes++;
    if (failWrites || failWriteOf == path) return false;
    files[path] = std::string(content);
    return true;
  }
  bool exists(const char* path) override { return files.count(path) != 0; }
  bool remove(const char* path) override {
    removes++;
    return files.erase(path) != 0;
  }
  bool rename(const char*, const char*) override { return false; }
  bool ensureDir(const char*) override { return true; }
};

uint32_t wallNow = 0;
uint32_t wall() { return wallNow; }
constexpr uint32_t kNow = 1790000000;  // 2026-09

LookupResult entry(const std::string& word, const std::string& meaning = "a meaning") {
  LookupResult e;
  e.word = word;
  e.reading = "yomi";
  e.senses = {Sense{meaning, "noun"}};
  e.level = "JLPT-N3";
  e.rank = 1234;
  e.frequency = 0.5F;
  return e;
}

CachedLookup record(const std::string& text, const Language language = Language::Japanese, const uint32_t at = kNow) {
  return CachedLookup{language, text, entry(text), at};
}

class LookupCacheTest : public ::testing::Test {
 protected:
  void SetUp() override { wallNow = kNow; }
  CountingFiles files;
  LookupCache cache{files, wall};
};

TEST_F(LookupCacheTest, AMissIsOneBucketReadAndNothingElse) {
  const CacheRead read = cache.read(Language::Japanese, "灯台");
  EXPECT_EQ(read.outcome, CacheRead::Outcome::Miss);
  EXPECT_FALSE(read.entry.has_value());
  EXPECT_EQ(files.reads, 1);
  EXPECT_EQ(files.writes, 0);
  EXPECT_EQ(files.removes, 0);
  EXPECT_EQ(files.lastMaxBytes, config::kLookupBucketMaxBytes);  // bounded: one small file at most
}

TEST_F(LookupCacheTest, AWrittenAnswerIsAHitWithEveryFieldTheCardKeeps) {
  ASSERT_TRUE(cache.write({record("灯台")}));
  const CacheRead read = cache.read(Language::Japanese, "灯台");
  ASSERT_EQ(read.outcome, CacheRead::Outcome::Hit);
  ASSERT_TRUE(read.entry.has_value());
  const LookupResult want = entry("灯台");
  EXPECT_EQ(read.entry->word, want.word);
  EXPECT_EQ(read.entry->reading, want.reading);
  EXPECT_EQ(read.entry->level, want.level);
  EXPECT_EQ(read.entry->rank, want.rank);
  EXPECT_FLOAT_EQ(read.entry->frequency, want.frequency);
  ASSERT_EQ(read.entry->senses.size(), 1u);
  EXPECT_EQ(read.entry->senses[0].translation, "a meaning");
  EXPECT_EQ(read.entry->senses[0].partOfSpeech, "noun");
  EXPECT_FALSE(read.entry->translationPending);
  EXPECT_EQ(cache.hits(), 1u);
  EXPECT_EQ(cache.reads(), 1u);
}

TEST_F(LookupCacheTest, KeyedByLanguageAndExactText) {
  ASSERT_TRUE(cache.write({record("猫")}));
  EXPECT_EQ(cache.read(Language::Chinese, "猫").outcome, CacheRead::Outcome::Miss);
  EXPECT_EQ(cache.read(Language::Japanese, "猫 ").outcome, CacheRead::Outcome::Miss);
  EXPECT_EQ(cache.read(Language::Japanese, "猫").outcome, CacheRead::Outcome::Hit);
  EXPECT_NE(bucketPath(Language::Japanese, "猫"), bucketPath(Language::Chinese, "猫"));
  EXPECT_EQ(bucketPath(Language::Japanese, "猫").rfind("/.lexirise/lookups/ja/", 0), 0u);
}

TEST_F(LookupCacheTest, TextsInOneBucketDontCollide) {
  // Find two texts that share a bucket: each still reads its own record.
  std::string a = "w0";
  std::string b;
  for (int i = 1; i < 1000 && b.empty(); i++) {
    const std::string t = "w" + std::to_string(i);
    if (bucketPath(Language::Japanese, t) == bucketPath(Language::Japanese, a)) b = t;
  }
  ASSERT_FALSE(b.empty());
  ASSERT_TRUE(cache.write({record(a), record(b)}));
  EXPECT_EQ(files.writes, 1);  // one bucket, one write
  EXPECT_EQ(cache.read(Language::Japanese, a).entry->word, a);
  EXPECT_EQ(cache.read(Language::Japanese, b).entry->word, b);
}

TEST_F(LookupCacheTest, OlderThanThirtyDaysIsStaleAndReplacedByTheNewAnswer) {
  ASSERT_TRUE(cache.write({record("霧", Language::Japanese, kNow - config::kLookupMaxAgeS - 1)}));
  const CacheRead old = cache.read(Language::Japanese, "霧");
  EXPECT_EQ(old.outcome, CacheRead::Outcome::Stale);
  EXPECT_FALSE(old.entry.has_value());
  ASSERT_TRUE(cache.write({record("霧", Language::Japanese, kNow)}));
  EXPECT_EQ(cache.read(Language::Japanese, "霧").outcome, CacheRead::Outcome::Hit);
  std::vector<CachedLookup> kept;
  ASSERT_TRUE(parseBucket(files.files[bucketPath(Language::Japanese, "霧")], Language::Japanese, kept));
  EXPECT_EQ(kept.size(), 1u);  // replaced, not added
}

TEST_F(LookupCacheTest, ARecordStampedFarAheadIsStale) {
  // Written under a clock that was wrong: it would never age, so past kLookupMaxAgeS ahead it's asked again.
  ASSERT_TRUE(cache.write({record("雪", Language::Japanese, kNow + config::kLookupMaxAgeS + 1)}));
  EXPECT_EQ(cache.read(Language::Japanese, "雪").outcome, CacheRead::Outcome::Stale);
  ASSERT_TRUE(cache.write({record("雨", Language::Japanese, kNow + 60)}));  // a little ahead: fine
  EXPECT_EQ(cache.read(Language::Japanese, "雨").outcome, CacheRead::Outcome::Hit);
}

TEST_F(LookupCacheTest, WithTheClockNotSetAnyAgeIsTaken) {
  ASSERT_TRUE(cache.write({record("霧", Language::Japanese, kNow - 10 * config::kLookupMaxAgeS)}));
  wallNow = 0;
  EXPECT_EQ(cache.read(Language::Japanese, "霧").outcome, CacheRead::Outcome::Hit);
}

TEST_F(LookupCacheTest, AnAnswerWithNoTimeIsStampedAsWritten) {
  ASSERT_TRUE(cache.write({record("霧", Language::Japanese, 0)}));
  std::vector<CachedLookup> kept;
  ASSERT_TRUE(parseBucket(files.files[bucketPath(Language::Japanese, "霧")], Language::Japanese, kept));
  EXPECT_EQ(kept[0].fetchedS, kNow);
}

TEST_F(LookupCacheTest, ARecordOfUnknownTimeIsStaleOnceTheClockIsSet) {
  wallNow = 0;
  ASSERT_TRUE(cache.write({record("霧", Language::Japanese, 0)}));
  wallNow = kNow;
  EXPECT_EQ(cache.read(Language::Japanese, "霧").outcome, CacheRead::Outcome::Stale);
}

TEST_F(LookupCacheTest, NeverKeepsAPendingAnEmptyOrAnOversizedAnswer) {
  LookupResult pending = entry("書きこむ");
  pending.translationPending = true;
  EXPECT_FALSE(cacheable("書きこむ", pending));
  LookupResult noSense = entry("x");
  noSense.senses.clear();
  EXPECT_FALSE(cacheable("x", noSense));
  LookupResult huge = entry("y", std::string(config::kLookupRecordMaxBytes, 'a'));
  EXPECT_FALSE(cacheable("y", huge));
  EXPECT_FALSE(cacheable("", entry("")));
  EXPECT_TRUE(cacheable("猫", entry("猫")));
  ASSERT_TRUE(cache.write({CachedLookup{Language::Japanese, "書きこむ", pending, kNow}}));
  EXPECT_EQ(files.writes, 0);  // nothing to add: no bucket touched
  EXPECT_EQ(files.reads, 0);
  EXPECT_EQ(cache.read(Language::Japanese, "書きこむ").outcome, CacheRead::Outcome::Miss);
}

TEST_F(LookupCacheTest, ABucketKeepsItsNewestWithinItsCaps) {
  // Texts all in one bucket, one more than a bucket holds: the oldest goes.
  const std::string first = "t0";
  std::vector<std::string> same{first};
  for (int i = 1; same.size() < config::kLookupBucketMax + 1; i++) {
    const std::string t = "t" + std::to_string(i);
    if (bucketPath(Language::Chinese, t) == bucketPath(Language::Chinese, first)) same.push_back(t);
  }
  std::vector<CachedLookup> all;
  for (const std::string& t : same) all.push_back(record(t, Language::Chinese));
  ASSERT_TRUE(cache.write(all));
  EXPECT_EQ(cache.read(Language::Chinese, same.front()).outcome, CacheRead::Outcome::Miss);
  EXPECT_EQ(cache.read(Language::Chinese, same.back()).outcome, CacheRead::Outcome::Hit);
  const std::string& bytes = files.files[bucketPath(Language::Chinese, first)];
  EXPECT_LE(bytes.size(), config::kLookupBucketMaxBytes);
  std::vector<CachedLookup> kept;
  ASSERT_TRUE(parseBucket(bytes, Language::Chinese, kept));
  EXPECT_EQ(kept.size(), config::kLookupBucketMax);
}

TEST_F(LookupCacheTest, ABucketNeverOutgrowsItsBytes) {
  const std::string first = "b0";
  std::vector<CachedLookup> all{CachedLookup{Language::Japanese, first, entry(first, std::string(900, 'm')), kNow}};
  for (int i = 1; all.size() < 40; i++) {
    const std::string t = "b" + std::to_string(i);
    if (bucketPath(Language::Japanese, t) == bucketPath(Language::Japanese, first)) {
      all.push_back(CachedLookup{Language::Japanese, t, entry(t, std::string(900, 'm')), kNow});
    }
  }
  ASSERT_TRUE(cache.write(all));
  const std::string& bytes = files.files[bucketPath(Language::Japanese, first)];
  EXPECT_LE(bytes.size(), config::kLookupBucketMaxBytes);
  EXPECT_EQ(cache.read(Language::Japanese, all.back().text).outcome, CacheRead::Outcome::Hit);
  EXPECT_EQ(cache.read(Language::Japanese, first).outcome, CacheRead::Outcome::Miss);
}

TEST_F(LookupCacheTest, AFileThatDoesntCheckOutIsRemovedAndIsAMiss) {
  ASSERT_TRUE(cache.write({record("猫")}));
  std::string& bytes = files.files[bucketPath(Language::Japanese, "猫")];
  bytes[bytes.size() - 1] ^= 0x01;  // the CRC fails
  EXPECT_EQ(cache.read(Language::Japanese, "猫").outcome, CacheRead::Outcome::Miss);
  EXPECT_EQ(files.files.count(bucketPath(Language::Japanese, "猫")), 0u);
  files.files[bucketPath(Language::Japanese, "猫")] = std::string(config::kLookupBucketMaxBytes + 1, 'x');
  EXPECT_EQ(cache.read(Language::Japanese, "猫").outcome, CacheRead::Outcome::Miss);  // too large
  EXPECT_EQ(files.files.count(bucketPath(Language::Japanese, "猫")), 0u);
}

TEST_F(LookupCacheTest, ParsingRefusesAnotherVersionLanguageOrCutFile) {
  const std::string bytes = serializeBucket({record("猫")});
  std::vector<CachedLookup> out;
  ASSERT_TRUE(parseBucket(bytes, Language::Japanese, out));
  EXPECT_FALSE(parseBucket(bytes, Language::Chinese, out));
  std::string version = bytes;
  version[4] = 99;
  EXPECT_FALSE(parseBucket(version, Language::Japanese, out));
  for (size_t cut = 0; cut < bytes.size(); cut++)
    EXPECT_FALSE(parseBucket(bytes.substr(0, cut), Language::Japanese, out));
  ASSERT_TRUE(parseBucket(serializeBucket({}), Language::Japanese, out));
  EXPECT_TRUE(out.empty());
}

TEST_F(LookupCacheTest, AReadErrorIsAMissAndKeepsTheFile) {
  ASSERT_TRUE(cache.write({record("猫")}));
  files.failReadOf = bucketPath(Language::Japanese, "猫");
  EXPECT_EQ(cache.read(Language::Japanese, "猫").outcome, CacheRead::Outcome::Miss);
  EXPECT_EQ(files.removes, 0);
  const std::string before = files.files[files.failReadOf];
  EXPECT_FALSE(cache.write({record("猫", Language::Japanese, kNow + 1)}));  // not overwritten with less
  EXPECT_EQ(files.files[files.failReadOf], before);
  files.failReadOf.clear();
  EXPECT_EQ(cache.read(Language::Japanese, "猫").outcome, CacheRead::Outcome::Hit);
}

TEST_F(LookupCacheTest, SomeBucketsWrittenOthersNot) {
  std::string other;  // a text in another bucket than 猫's
  for (int i = 0; other.empty(); i++) {
    const std::string t = "o" + std::to_string(i);
    if (bucketPath(Language::Japanese, t) != bucketPath(Language::Japanese, "猫")) other = t;
  }
  files.failWriteOf = bucketPath(Language::Japanese, other);
  EXPECT_FALSE(cache.write({record("猫"), record(other)}));
  EXPECT_EQ(cache.read(Language::Japanese, "猫").outcome, CacheRead::Outcome::Hit);
  EXPECT_EQ(cache.read(Language::Japanese, other).outcome, CacheRead::Outcome::Miss);
}

TEST_F(LookupCacheTest, AReadDecodesOnlyTheRecordAsked) {
  const std::string bytes = serializeBucket({record("犬"), record("猫"), record("鳥")});
  CachedLookup found;
  ASSERT_EQ(findInBucket(bytes, Language::Japanese, "猫", found), BucketFind::Found);
  EXPECT_EQ(found.text, "猫");
  EXPECT_EQ(found.entry.word, "猫");
  CachedLookup none;
  EXPECT_EQ(findInBucket(bytes, Language::Japanese, "馬", none), BucketFind::Absent);
  EXPECT_TRUE(none.text.empty());
  EXPECT_EQ(findInBucket(bytes, Language::Chinese, "猫", none), BucketFind::Malformed);
  EXPECT_EQ(findInBucket(bytes.substr(0, bytes.size() - 1), Language::Japanese, "猫", none), BucketFind::Malformed);
}

TEST_F(LookupCacheTest, AFailedWriteSaysSo) {
  files.failWrites = true;
  EXPECT_FALSE(cache.write({record("猫")}));
}

}  // namespace
