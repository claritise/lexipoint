#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "VocabFixtures.h"
#include "lexirise/api/VocabPage.h"

using lexipoint::api::parseIsoTimeMs;
using lexipoint::api::ParseStatus;
using lexipoint::api::parseVocabPage;
using lexipoint::api::VocabPage;
using lexipoint::api::VocabPageReader;
using lexipoint::fakes::FakeVocabItem;
using lexipoint::fakes::isoTime;
using lexipoint::fakes::kSept2026Ms;
using lexipoint::fakes::vocabPageJson;

namespace {

std::vector<FakeVocabItem> manyItems(const size_t n) {
  std::vector<FakeVocabItem> items;
  for (size_t i = 0; i < n; i++) {
    FakeVocabItem item;
    item.savedId = static_cast<uint32_t>(2600000 + i);
    item.entryId = static_cast<uint32_t>(16000000 + i * 7);
    item.proficiency = static_cast<int>(i % 5);
    item.suspended = i % 11 == 0;
    item.updatedMs = kSept2026Ms + (n - i) * 1000;  // newest first
    if (i % 3 == 0) item.nextReviewMs = kSept2026Ms + 86400000ULL;
    if (i % 17 == 5) item.unitType = "sentence";
    items.push_back(item);
  }
  return items;
}

// The body streamed in `chunk`-byte pieces, as the client feeds it.
ParseStatus streamed(const std::string& body, const size_t chunk, VocabPage& out) {
  VocabPageReader reader;
  for (size_t at = 0; at < body.size(); at += chunk) {
    if (!reader.onBody(body.data() + at, std::min(chunk, body.size() - at))) break;
  }
  return reader.finish(out);
}

}  // namespace

TEST(VocabPage, IsoTimes) {
  EXPECT_EQ(parseIsoTimeMs("2026-09-24T06:11:34.058Z"), 1790208000000ULL + (6 * 3600 + 11 * 60 + 34) * 1000ULL + 58);
  EXPECT_EQ(parseIsoTimeMs("1970-01-01T00:00:00Z"), 0u);
  EXPECT_EQ(parseIsoTimeMs("2026-09-24T09:00:00+09:00"), 1790208000000ULL);
  EXPECT_EQ(parseIsoTimeMs("2026-09-24T00:00:00.5Z"), 1790208000500ULL);
  EXPECT_EQ(parseIsoTimeMs("2026-09-24T00:00:00.123456Z"), 1790208000123ULL);
  EXPECT_EQ(parseIsoTimeMs(isoTime(kSept2026Ms + 123456789)), kSept2026Ms + 123456789);
  for (const char* bad :
       {"", "2026-09-24", "2026-13-01T00:00:00Z", "2026-09-24T24:00:00Z", "2026-09-24T00:00:00",
        "2026-09-24T00:00:00.Z", "2026-09-24 00:00:00Z", "2026-09-24T00:00:00Zx", "garbage-garbage-gar"}) {
    EXPECT_FALSE(parseIsoTimeMs(bad).has_value()) << bad;
  }
}

TEST(VocabPage, ReadsARealisticTwoHundredItemPageAsItStreamsKeepingOnlyTheItemsOwnFields) {
  const std::vector<FakeVocabItem> items = manyItems(200);
  const std::string body = vocabPageJson(items, 200, 1234, 6000);  // ~6.8 KB an item, as measured
  ASSERT_GT(body.size(), 1200000u);
  for (const size_t chunk : {size_t{1024}, size_t{13}}) {
    VocabPage page;
    ASSERT_EQ(streamed(body, chunk, page), ParseStatus::Ok);
    ASSERT_EQ(page.items.size(), 200u);
    EXPECT_EQ(page.nextOffset, 200u);
    EXPECT_EQ(page.totalCount, 1234u);
    EXPECT_EQ(page.newestMs, items[0].updatedMs);
    for (size_t i = 0; i < items.size(); i++) {
      const auto& got = page.items[i];
      const FakeVocabItem& want = items[i];
      EXPECT_EQ(got.savedId, want.savedId);
      EXPECT_EQ(got.entryId, want.entryId);          // `dictionary_id`, never dictionary_entry.id or .dictionary_id
      EXPECT_EQ(got.proficiency, want.proficiency);  // never dictionary_entry.proficiency (4)
      EXPECT_EQ(got.suspended, want.suspended);      // never dictionary_entry.suspended (true)
      EXPECT_EQ(got.updatedMs, want.updatedMs);      // never dictionary_entry.updated_at (2099)
      EXPECT_EQ(got.word, want.unitType == "word");
      EXPECT_EQ(got.nextReviewS, want.nextReviewMs ? static_cast<uint32_t>(*want.nextReviewMs / 1000) : 0u);
      EXPECT_EQ(got.usable(), want.unitType == "word");
    }
  }
}

TEST(VocabPage, TheLastPageHasNoNextOffset) {
  VocabPage page;
  ASSERT_EQ(parseVocabPage(vocabPageJson(manyItems(3), std::nullopt, 3), page), ParseStatus::Ok);
  EXPECT_FALSE(page.nextOffset.has_value());
  EXPECT_EQ(page.items.size(), 3u);
  VocabPage empty;
  ASSERT_EQ(parseVocabPage(R"({"items":[],"totalCount":0,"nextOffset":null})", empty), ParseStatus::Ok);
  EXPECT_TRUE(empty.items.empty());
}

TEST(VocabPage, ItemsItCantKeepAreListedButNotUsable) {
  VocabPage page;
  ASSERT_EQ(parseVocabPage(R"({"items":[
      {"id":1,"dictionary_id":2,"proficiency":7,"updated_at":"2026-09-24T00:00:00Z"},
      {"id":"12","dictionary_id":"34","proficiency":2,"updated_at":"not a time","unit_type":null},
      {"dictionary_id":5,"proficiency":1},
      {"id":1.5,"dictionary_id":5,"proficiency":1},
      {"id":6,"dictionary_id":null,"proficiency":1},
      "stray", [1]
    ],"nextOffset":null})",
                           page),
            ParseStatus::Ok);
  ASSERT_EQ(page.items.size(), 7u);
  EXPECT_FALSE(page.items[0].usable());  // a level out of range
  EXPECT_EQ(page.items[0].updatedMs, 1790208000000ULL);
  EXPECT_TRUE(page.items[1].usable());  // ids as strings of digits; unit_type null: not said
  EXPECT_EQ(page.items[1].savedId, 12u);
  EXPECT_EQ(page.items[1].updatedMs, 0u);  // unreadable
  EXPECT_FALSE(page.items[2].usable());    // no id
  EXPECT_FALSE(page.items[3].usable());    // not a whole number
  EXPECT_FALSE(page.items[4].usable());    // no entry
  EXPECT_FALSE(page.items[5].usable());
  EXPECT_FALSE(page.items[6].usable());
}

TEST(VocabPage, MalformedTruncatedAndOverLongBodiesAreRefused) {
  const std::string good = vocabPageJson(manyItems(5), 5, 10);
  VocabPage page;
  EXPECT_EQ(streamed(good.substr(0, good.size() / 2), 64, page), ParseStatus::Malformed);  // cut
  EXPECT_EQ(streamed(good.substr(0, good.size() - 1), 64, page), ParseStatus::Malformed);  // the last brace
  for (const char* bad :
       {"", "[]", "{\"items\":{}}", "{\"data\":[]}", "{\"items\":[{]}", "{\"items\":[]} x", "<html>502</html>"}) {
    EXPECT_EQ(parseVocabPage(bad, page), ParseStatus::Malformed) << bad;
  }
  EXPECT_EQ(parseVocabPage(vocabPageJson(manyItems(201), 201, 300, 8), page), ParseStatus::OverLimit);
}

TEST(VocabPage, StopsTakingTheBodyOnceItsBad) {
  VocabPageReader reader;
  EXPECT_FALSE(reader.onBody("{\"items\":[,", 11));
  VocabPage page;
  EXPECT_EQ(reader.finish(page), ParseStatus::Malformed);
}

TEST(VocabPage, TheListsCountIsLanguageCountOnly) {
  VocabPage page;
  ASSERT_EQ(parseVocabPage(R"({"items":[],"totalCount":4,"languageCount":6})", page), ParseStatus::Ok);
  EXPECT_EQ(page.listCount(), 6u);  // sentence cards too: the list paged holds them
  ASSERT_EQ(parseVocabPage(R"({"items":[],"totalCount":4})", page), ParseStatus::Ok);
  EXPECT_EQ(page.totalCount, 4u);
  EXPECT_FALSE(page.listCount().has_value());  // words only: not a count of the list paged
  ASSERT_EQ(parseVocabPage(R"({"items":[]})", page), ParseStatus::Ok);
  EXPECT_FALSE(page.listCount().has_value());
}

TEST(VocabPage, ACancelledReaderStopsAtTheNextPiece) {
  static int asked = 0;
  asked = 0;
  VocabPageReader reader([] { return ++asked > 2; });
  EXPECT_TRUE(reader.onBody("{\"items\"", 8));
  EXPECT_TRUE(reader.onBody(":[", 2));
  EXPECT_FALSE(reader.onBody("]}", 2));
  EXPECT_TRUE(reader.cancelled());
  VocabPage page;
  EXPECT_NE(reader.finish(page), ParseStatus::Ok);
}
