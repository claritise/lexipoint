#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "FakeApi.h"
#include "Fakes.h"
#include "VocabFixtures.h"
#include "lexirise/vocab/VocabMirror.h"

using lexipoint::Language;
using lexipoint::api::ApiError;
using lexipoint::fakes::apiFailure;
using lexipoint::fakes::apiOk;
using lexipoint::fakes::FakeApi;
using lexipoint::fakes::FakeFiles;
using lexipoint::fakes::FakeVocabAccount;
using lexipoint::fakes::FakeVocabItem;
using lexipoint::fakes::kSept2026Ms;
using namespace lexipoint::vocab;
namespace config = lexipoint::config;

namespace {

constexpr uint32_t kEpochS = static_cast<uint32_t>(kSept2026Ms / 1000) + 86400;  // a day after the items
constexpr unsigned long kStartMs = 1000;
const char* const kJaPath = config::kVocabPathJa;

FakeVocabItem item(const uint32_t n, const int proficiency = 2, const std::string& unit = "word") {
  FakeVocabItem i;
  i.savedId = 100000 + n;
  i.entryId = 500000 + n;
  i.proficiency = proficiency;
  i.updatedMs = kSept2026Ms + n * 1000;
  i.unitType = unit;
  return i;
}

std::vector<FakeVocabItem> account(const uint32_t n) {
  std::vector<FakeVocabItem> items;
  for (uint32_t i = 1; i <= n; i++)
    items.push_back(item(i, static_cast<int>(i % 5), i % 10 == 0 ? "sentence" : "word"));
  return items;
}

// Runs the store's due pages, as idle cards would, until none is due or `max` ran; returns how many.
int sync(VocabStore& store, FakeApi& api, const unsigned long nowMs, const int max = 100,
         const uint32_t epochS = kEpochS) {
  int pages = 0;
  while (pages < max) {
    const auto plan = store.next(Language::Japanese, nowMs, epochS);
    if (!plan) break;
    store.apply(sendPage(api, *plan), nowMs, epochS);
    pages++;
  }
  return pages;
}

struct Fixture : ::testing::Test {
  FakeFiles files;
  FakeApi api;
  FakeVocabAccount lexirise;
  void SetUp() override { lexirise.serve(api); }
  std::unique_ptr<VocabStore> loaded() {
    auto store = std::make_unique<VocabStore>(files);
    store->load(Language::Japanese);
    return store;
  }
};

}  // namespace

// --- The file ---

TEST(VocabMirrorFile, RoundTripsEntriesAndProgress) {
  Mirror m;
  m.sync.synced = true;
  m.sync.cursorMs = 123456789012ULL;
  m.sync.full.running = true;
  m.sync.full.offset = 96;
  m.sync.full.newestMs = 99999999999ULL;
  m.sync.fullDoneS = 1790000000;
  m.sync.generation = 7;
  m.sync.full.lastCount = 345;   // flag 4, offset 40
  m.sync.full.unbounded = true;  // flag 8
  m.sync.full.slack = 1;         // flag bits 4-5: one short of the overlap
  m.sync.inc.running = true;     // the incremental pass under way (version 2)
  m.sync.inc.offset = 144;
  m.sync.inc.newestMs = 88888888888ULL;
  m.sync.inc.lastCount = 300;
  m.sync.inc.slack = 0;
  m.sync.inc.unbounded = true;
  m.put(Entry{30, 3, 0, 4, false, 7, false});
  m.put(Entry{10, 1, 1790300000, 0, true, 6, true});  // a live answer's
  m.put(Entry{20, 2, 0, 2, false, 7, false});
  const std::string bytes = serializeMirror(m, Language::Japanese);
  EXPECT_EQ(bytes.size(), config::kVocabHeaderBytes + 3 * config::kVocabRecordBytes);
  Mirror back;
  ASSERT_TRUE(parseMirror(bytes, Language::Japanese, back));
  EXPECT_EQ(back, m);
  EXPECT_EQ(back.entries().front().entryId, 10u);             // sorted
  EXPECT_FALSE(parseMirror(bytes, Language::Chinese, back));  // another language's file
}

TEST(VocabMirrorFile, AnythingButWhatItWroteIsRefused) {
  Mirror m;
  m.put(Entry{10, 1, 0, 1, false, 0, false});
  m.put(Entry{20, 2, 0, 2, false, 0, false});
  const std::string good = serializeMirror(m, Language::Japanese);
  Mirror out;
  std::string flipped = good;
  flipped[config::kVocabHeaderBytes + 5] ^= 0x01;  // a record's bit: the CRC catches it
  EXPECT_FALSE(parseMirror(flipped, Language::Japanese, out));
  EXPECT_FALSE(parseMirror(good.substr(0, good.size() - 1), Language::Japanese, out));  // cut
  EXPECT_FALSE(parseMirror(good + "x", Language::Japanese, out));
  EXPECT_FALSE(parseMirror("", Language::Japanese, out));
  std::string magic = good;
  magic[0] = 'X';
  EXPECT_FALSE(parseMirror(magic, Language::Japanese, out));
  EXPECT_TRUE(out.entries().empty());  // left alone
}

TEST(VocabMirrorFile, IsBoundedAndCompact) {
  Mirror m;
  for (uint32_t i = 1; i <= config::kVocabMirrorMax; i++)
    ASSERT_EQ(m.put(Entry{i, i, 0, 1, false, 0, false}), Mirror::Put::Added);
  EXPECT_EQ(m.put(Entry{config::kVocabMirrorMax + 1, 1, 0, 1, false, 0, false}), Mirror::Put::Full);  // no room
  EXPECT_EQ(m.put(Entry{5, 5, 0, 3, false, 0, false}), Mirror::Put::Changed);  // an update still is
  const std::string bytes = serializeMirror(m, Language::Chinese);
  EXPECT_EQ(bytes.size(), config::kVocabMaxBytes);
  Mirror back;
  EXPECT_TRUE(parseMirror(bytes, Language::Chinese, back));
  EXPECT_EQ(back.size(), config::kVocabMirrorMax);
}

TEST_F(Fixture, ACorruptFileIsSetAsideAndSyncedAgain) {
  files.files[kJaPath] = "LXVM garbage";
  auto store = loaded();
  EXPECT_EQ(store->size(Language::Japanese), 0u);
  EXPECT_EQ(files.files.count(kJaPath), 0u);
  EXPECT_EQ(files.files[config::kVocabBadPathJa], "LXVM garbage");  // the user's copy kept, out of the way
  lexirise.items = account(3);
  EXPECT_EQ(sync(*store, api, kStartMs), 2);  // a full pass (one page), then an incremental one
  EXPECT_EQ(store->size(Language::Japanese), 3u);
}

TEST_F(Fixture, AnInterruptedSaveIsRecoveredFromItsBackup) {
  lexirise.items = account(3);
  {
    auto store = loaded();
    sync(*store, api, kStartMs);
  }
  // Power lost between "file → .bak" and ".tmp → file".
  files.files[config::kVocabBackupPathJa] = files.files[kJaPath];
  files.files.erase(kJaPath);
  files.files[config::kVocabTmpPathJa] = "half";
  auto store = loaded();
  EXPECT_EQ(store->size(Language::Japanese), 3u);
  EXPECT_TRUE(store->syncState(Language::Japanese).synced);
  EXPECT_EQ(files.files.count(config::kVocabTmpPathJa), 0u);
}

// --- Sync ---

TEST_F(Fixture, AFullSyncPagesThroughEverythingAndKeepsOnlyWords) {
  lexirise.items = account(120);
  auto store = loaded();
  const auto first = store->next(Language::Japanese, kStartMs, kEpochS);
  ASSERT_TRUE(first);
  EXPECT_EQ(first->pass, Pass::Full);
  sync(*store, api, kStartMs);
  // Pages of kVocabPageItems, each the next offset less the overlap; then an incremental pass from the top.
  const uint32_t step = config::kVocabPageItems - config::kVocabPageOverlap;
  EXPECT_EQ(lexirise.offsets, (std::vector<uint32_t>{0, step, 2 * step, 0}));
  EXPECT_NE(api.vocabRequests[0].path.find("sortId=updated_at&sortDesc=true"), std::string::npos);
  EXPECT_NE(api.vocabRequests[0].path.find("language=ja"), std::string::npos);
  EXPECT_EQ(store->size(Language::Japanese), 108u);  // 120, less the sentence cards
  const auto kept = store->find(Language::Japanese, 500007);
  ASSERT_TRUE(kept);
  EXPECT_EQ(kept->savedId, 100007u);
  EXPECT_EQ(kept->proficiency, 2);
  EXPECT_FALSE(store->find(Language::Japanese, 500010));  // a sentence card
  const SyncState s = store->syncState(Language::Japanese);
  EXPECT_TRUE(s.synced);
  EXPECT_FALSE(s.full.running);
  EXPECT_EQ(s.cursorMs, kSept2026Ms + 120 * 1000);  // the newest change
  EXPECT_EQ(s.fullDoneS, kEpochS);
  // Nothing due until the interval has passed.
  EXPECT_FALSE(store->next(Language::Japanese, kStartMs + 1000, kEpochS));
  EXPECT_TRUE(store->next(Language::Japanese, kStartMs + config::kVocabSyncIntervalMs, kEpochS));
}

TEST_F(Fixture, AnIncrementalSyncStopsAtTheCursor) {
  lexirise.items = account(120);
  auto store = loaded();
  sync(*store, api, kStartMs);
  lexirise.offsets.clear();
  // A save in the Lexirise app, and a level changed there.
  FakeVocabItem added = item(500, 3);
  added.updatedMs = kSept2026Ms + 900000;
  lexirise.items.push_back(added);
  lexirise.items[4].proficiency = 4;
  lexirise.items[4].updatedMs = kSept2026Ms + 900001;
  const unsigned long later = kStartMs + config::kVocabSyncIntervalMs;
  EXPECT_EQ(sync(*store, api, later), 1);  // one page: the third item is older than the cursor
  EXPECT_EQ(lexirise.offsets, (std::vector<uint32_t>{0}));
  ASSERT_TRUE(store->find(Language::Japanese, 500500));
  EXPECT_EQ(store->find(Language::Japanese, 500005)->proficiency, 4);
  EXPECT_EQ(store->syncState(Language::Japanese).cursorMs, kSept2026Ms + 900001);
}

TEST_F(Fixture, AnIncrementalSyncLongerThanAPageGoesOnUntilTheCursor) {
  lexirise.items = account(10);
  auto store = loaded();
  sync(*store, api, kStartMs);
  for (uint32_t n = 1; n <= 70; n++) {
    FakeVocabItem i = item(1000 + n);
    i.updatedMs = kSept2026Ms + 5000000 + n;
    lexirise.items.push_back(i);
  }
  lexirise.offsets.clear();
  EXPECT_EQ(sync(*store, api, kStartMs + config::kVocabSyncIntervalMs), 3);  // the probe, then two whole pages
  EXPECT_EQ(store->size(Language::Japanese), 9u + 70u);
  EXPECT_EQ(store->syncState(Language::Japanese).cursorMs, kSept2026Ms + 5000070);
}

TEST_F(Fixture, AFullSyncResumesAfterAnInterruptionFromItsSavedOffset) {
  lexirise.items = account(120);
  {
    auto store = loaded();
    EXPECT_EQ(sync(*store, api, kStartMs, 1), 1);  // the card closed after one page, then a reboot
    EXPECT_TRUE(store->syncState(Language::Japanese).full.running);
  }
  auto store = loaded();
  const auto plan = store->next(Language::Japanese, kStartMs, kEpochS);
  ASSERT_TRUE(plan);
  EXPECT_EQ(plan->pass, Pass::Full);
  EXPECT_EQ(plan->offset, config::kVocabPageItems - config::kVocabPageOverlap);
  sync(*store, api, kStartMs);
  EXPECT_EQ(store->size(Language::Japanese), 108u);
  EXPECT_TRUE(store->syncState(Language::Japanese).synced);
}

TEST_F(Fixture, ARateLimitRefusalWaitsItsRetryTime) {
  lexirise.items = account(3);
  auto store = loaded();
  api.vocabServer = nullptr;
  auto limited = apiFailure(ApiError::RateLimited);
  limited.retryAfterS = 30;
  api.vocabReplies = {limited};
  EXPECT_EQ(sync(*store, api, kStartMs), 1);
  EXPECT_FALSE(store->next(Language::Japanese, kStartMs + 29000, kEpochS));
  lexirise.serve(api);
  EXPECT_TRUE(store->next(Language::Japanese, kStartMs + 30000, kEpochS));
  EXPECT_EQ(sync(*store, api, kStartMs + 30000), 2);
  EXPECT_EQ(store->size(Language::Japanese), 3u);
}

TEST_F(Fixture, AFailedOrUnreadablePageWaitsAndChangesNothing) {
  lexirise.items = account(3);
  auto store = loaded();
  api.vocabServer = nullptr;
  api.vocabReplies = {apiOk("{\"items\":[{\"id\":1,")};  // cut short
  EXPECT_EQ(sync(*store, api, kStartMs), 1);
  EXPECT_EQ(store->size(Language::Japanese), 0u);
  EXPECT_FALSE(store->syncState(Language::Japanese).full.running);
  EXPECT_FALSE(store->next(Language::Japanese, kStartMs + config::kVocabFailureWaitMs - 1, kEpochS));
  api.vocabReplies = {apiFailure(ApiError::Network)};
  EXPECT_EQ(sync(*store, api, kStartMs + config::kVocabFailureWaitMs), 1);
  EXPECT_FALSE(store->next(Language::Japanese, kStartMs + config::kVocabFailureWaitMs + 1, kEpochS));
}

TEST_F(Fixture, PagesAreBudgetedPerHour) {
  auto store = loaded();
  // An account that never ends: every page has another after it.
  api.vocabServer = [](const lexipoint::net::Request& request) {
    const uint32_t offset = FakeVocabAccount::param(request.path, "offset");
    return apiOk(lexipoint::fakes::vocabPageJson({item(offset + 1)}, offset + config::kVocabPageItems, 1000000, 8));
  };
  int fetched = 0;
  for (unsigned long t = kStartMs; t < kStartMs + 1800UL * 1000UL; t += 1000) fetched += sync(*store, api, t, 1);
  EXPECT_EQ(fetched, static_cast<int>(config::kVocabPagesPerHour));
  EXPECT_EQ(sync(*store, api, kStartMs + 3600UL * 1000UL, 1), 1);  // an hour after the first, one more
}

TEST_F(Fixture, DeletionsAreFoundByTheNextFullPass) {
  lexirise.items = account(20);
  auto store = loaded();
  sync(*store, api, kStartMs);
  ASSERT_TRUE(store->find(Language::Japanese, 500003));
  lexirise.items.erase(lexirise.items.begin() + 2);  // gone from the account: no updated_at shows it
  sync(*store, api, kStartMs + config::kVocabSyncIntervalMs);
  EXPECT_TRUE(store->find(Language::Japanese, 500003));  // an incremental pass can't see it
  const uint32_t weekLater = kEpochS + config::kVocabResyncS;
  const auto plan = store->next(Language::Japanese, kStartMs + 2 * config::kVocabSyncIntervalMs, weekLater);
  ASSERT_TRUE(plan);
  EXPECT_EQ(plan->pass, Pass::Full);
  sync(*store, api, kStartMs + 2 * config::kVocabSyncIntervalMs, 100, weekLater);
  EXPECT_FALSE(store->find(Language::Japanese, 500003));
  EXPECT_EQ(store->size(Language::Japanese), 17u);
}

TEST_F(Fixture, AWordRemovedInLexiriseComesBackAtLevelZeroByTheIncrementalPass) {
  lexirise.items = account(5);
  auto store = loaded();
  sync(*store, api, kStartMs);
  // A dictionary word's DELETE keeps the item at level 0 with a new updated_at (lexirise-api-notes.md, Suspended).
  lexirise.items[1].proficiency = 0;
  lexirise.items[1].updatedMs = kSept2026Ms + 99000;
  sync(*store, api, kStartMs + config::kVocabSyncIntervalMs);
  EXPECT_EQ(store->find(Language::Japanese, 500002)->proficiency, 0);
}

TEST_F(Fixture, APageCallGivesThePageOrWhyNot) {
  lexirise.items = account(3);
  auto store = loaded();
  const auto plan = store->next(Language::Japanese, kStartMs, kEpochS);
  const PageCall call = sendPage(api, *plan);
  EXPECT_EQ(call.error, ApiError::None);
  EXPECT_EQ(call.page.items.size(), 3u);
  // Offline (no WiFi up): nothing happens but the wait.
  api.vocabServer = nullptr;
  const PageCall offline = sendPage(api, *plan);
  EXPECT_EQ(offline.error, ApiError::Network);
}

// --- Live answers and local writes ---

TEST_F(Fixture, LiveAnswersAndWritesUpdateTheMirrorAndTheLiveAnswerWins) {
  lexirise.items = account(5);
  auto store = loaded();
  sync(*store, api, kStartMs);
  ASSERT_EQ(store->find(Language::Japanese, 500002)->proficiency, 2);
  // The card's analysis says 500002 is at level 4 (changed in the app since), 500003 isn't saved (deleted), and 42 was
  // just saved on the card.
  store->record({LiveState{Language::Japanese, 500002, true, 100002, 4}, LiveState{Language::Japanese, 500003},
                 LiveState{Language::Japanese, 42, true, 777, 1}});
  EXPECT_EQ(store->find(Language::Japanese, 500002)->proficiency, 4);
  EXPECT_EQ(store->find(Language::Japanese, 500003)->savedId, 0u);  // kept as a removal (V7b R1)
  EXPECT_EQ(store->find(Language::Japanese, 42)->savedId, 777u);
  const int writesBefore = files.writes;
  EXPECT_TRUE(store->flush());
  EXPECT_GT(files.writes, writesBefore);
  EXPECT_TRUE(store->flush());  // nothing new: nothing written
  // After a reboot, from the file.
  auto again = loaded();
  EXPECT_EQ(again->find(Language::Japanese, 500002)->proficiency, 4);
  EXPECT_EQ(again->find(Language::Japanese, 42)->savedId, 777u);
}

TEST_F(Fixture, TheOfflineFallbackAnswersFromTheMirror) {
  lexirise.items = account(5);
  lexirise.items[0].suspended = true;
  {
    auto store = loaded();
    sync(*store, api, kStartMs);
  }
  api.vocabServer = nullptr;  // offline from here
  auto store = loaded();
  const auto saved = store->savedState(Language::Japanese, 500004);
  ASSERT_TRUE(saved);
  EXPECT_EQ(saved->savedExpressionId, "100004");
  EXPECT_EQ(saved->proficiency, 4);
  EXPECT_TRUE(store->find(Language::Japanese, 500001)->suspended);
  EXPECT_FALSE(store->savedState(Language::Japanese, 1));      // never saved
  EXPECT_FALSE(store->savedState(Language::Chinese, 500004));  // another language's mirror
}

TEST_F(Fixture, AFailedWriteKeepsTheChangeForTheNextWrite) {
  auto store = loaded();
  store->record({LiveState{Language::Japanese, 9, true, 90, 2}});
  files.failWriteOf = config::kVocabTmpPathJa;
  EXPECT_FALSE(store->flush());
  EXPECT_EQ(store->find(Language::Japanese, 9)->savedId, 90u);  // memory keeps it
  EXPECT_TRUE(store->flush());
  auto again = loaded();
  EXPECT_EQ(again->find(Language::Japanese, 9)->savedId, 90u);
}

TEST(VocabLiveState, IdsThatCantBeKeptAreLeftAlone) {
  lexipoint::api::EntryState saved;
  saved.savedExpressionId = "2644872";
  saved.proficiency = 3;
  const auto live = liveStateOf(Language::Chinese, 12, saved);
  ASSERT_TRUE(live);
  EXPECT_TRUE(live->saved);
  EXPECT_EQ(live->savedId, 2644872u);
  saved.savedExpressionId = "abc-1";
  EXPECT_FALSE(liveStateOf(Language::Chinese, 12, saved));  // not a whole number: the mirror can't hold it
  EXPECT_FALSE(liveStateOf(Language::Chinese, 0, std::nullopt));
  const auto unsaved = liveStateOf(Language::Chinese, 12, std::nullopt);
  ASSERT_TRUE(unsaved);
  EXPECT_FALSE(unsaved->saved);
}

TEST(VocabLiveState, ALocalChangeDuringAFullPassSurvivesItsSweep) {
  Mirror m;
  m.sync.full.running = true;
  m.sync.generation = 3;
  ASSERT_EQ(applyLive(m, LiveState{Language::Japanese, 5, true, 50, 1}), LiveApplied::Changed);
  EXPECT_EQ(m.find(5)->mark, 3);
  EXPECT_EQ(m.sweep(3), 0u);
  EXPECT_EQ(applyLive(m, LiveState{Language::Japanese, 5, true, 50, 1}), LiveApplied::None);  // the same: nothing
}

// --- Review round 1 ---

namespace {
int cancelAfter = 0;  // the cancel predicate says "input" once asked this many times
bool cancelSoon() { return --cancelAfter < 0; }
}  // namespace

TEST_F(Fixture, APageGivenUpForInputChangesNothingAndWaitsNothing) {
  lexirise.items = account(120);
  auto store = loaded();
  const auto plan = store->next(Language::Japanese, kStartMs, kEpochS);
  ASSERT_TRUE(plan);
  cancelAfter = 3;
  const PageCall call = sendPage(api, *plan, cancelSoon);
  EXPECT_TRUE(call.cancelled);
  EXPECT_TRUE(call.page.items.empty());
  const SyncState before = store->syncState(Language::Japanese);
  const int writes = files.writes;
  store->apply(call, kStartMs, kEpochS);
  EXPECT_EQ(store->syncState(Language::Japanese), before);
  EXPECT_EQ(store->size(Language::Japanese), 0u);
  EXPECT_EQ(files.writes, writes);
  const auto again = store->next(Language::Japanese, kStartMs + 1, kEpochS);  // no failure wait
  ASSERT_TRUE(again);
  EXPECT_EQ(again->offset, plan->offset);
  EXPECT_EQ(sync(*store, api, kStartMs + 1), 4);  // then the whole pass as usual
  EXPECT_EQ(store->size(Language::Japanese), 108u);
}

TEST_F(Fixture, AFullPassKeepsEveryWordThoughItemsAreDeletedBetweenItsPages) {
  lexirise.items = account(120);
  auto store = loaded();
  ASSERT_EQ(sync(*store, api, kStartMs, 1), 1);  // the first page
  // Five items deleted from the top part already read: everything after moves up five, past the overlap.
  for (int i = 0; i < 5; i++) lexirise.items.pop_back();  // the newest (the list is newest first)
  sync(*store, api, kStartMs);
  EXPECT_TRUE(store->syncState(Language::Japanese).synced);
  // Every word still in the account is kept (the five deleted ones may stay until the next full pass).
  for (const FakeVocabItem& i : lexirise.items) {
    if (i.unitType == "word") EXPECT_TRUE(store->find(Language::Japanese, i.entryId)) << i.entryId;
  }
}

TEST_F(Fixture, AFullPassWithoutACountDropsNothing) {
  lexirise.items = account(20);
  auto store = loaded();
  sync(*store, api, kStartMs);
  store->record({LiveState{Language::Japanese, 42, true, 7, 1}});  // not in the account
  api.vocabServer = nullptr;
  // Two pages, neither with languageCount (a first page alone needs none: nothing was read before it).
  api.vocabReplies = {apiOk("{\"items\":[" + lexipoint::fakes::vocabItemJson(item(1), 8) + "],\"nextOffset\":1}"),
                      apiOk("{\"items\":[" + lexipoint::fakes::vocabItemJson(item(2), 8) + "],\"nextOffset\":null}")};
  const uint32_t weekLater = kEpochS + config::kVocabResyncS;
  sync(*store, api, kStartMs + config::kVocabSyncIntervalMs, 2, weekLater);
  ASSERT_TRUE(store->syncState(Language::Japanese).synced);
  ASSERT_FALSE(store->syncState(Language::Japanese).full.running);
  EXPECT_TRUE(store->find(Language::Japanese, 42));  // can't tell a deletion from a shift: no sweep
  EXPECT_TRUE(store->find(Language::Japanese, 500001));
}

TEST_F(Fixture, AChangeInTheCursorsOwnMillisecondIsTaken) {
  lexirise.items = account(5);
  auto store = loaded();
  sync(*store, api, kStartMs);
  const uint64_t cursor = store->syncState(Language::Japanese).cursorMs;
  lexirise.items[4].proficiency = 4;  // the newest, changed again within the same millisecond
  ASSERT_EQ(lexirise.items[4].updatedMs, cursor);
  EXPECT_EQ(sync(*store, api, kStartMs + config::kVocabSyncIntervalMs), 1);
  EXPECT_EQ(store->find(Language::Japanese, 500005)->proficiency, 4);
}

TEST_F(Fixture, AFullPassEndedWithoutAClockGetsItsTimeFromTheFirstPageWithOne) {
  lexirise.items = account(3);
  auto store = loaded();
  sync(*store, api, kStartMs, 100, /*epochS=*/0);  // the clock not set yet
  ASSERT_TRUE(store->syncState(Language::Japanese).synced);
  EXPECT_EQ(store->syncState(Language::Japanese).fullDoneS, 0u);
  sync(*store, api, kStartMs + config::kVocabSyncIntervalMs, 100, kEpochS);
  EXPECT_EQ(store->syncState(Language::Japanese).fullDoneS, kEpochS);
  // So the weekly pass comes a week after that.
  EXPECT_EQ(
      store->next(Language::Japanese, kStartMs + 3 * config::kVocabSyncIntervalMs, kEpochS + config::kVocabResyncS)
          ->pass,
      Pass::Full);
}

TEST_F(Fixture, APageCutByATimeoutGivesNoPageAndChangesNothing) {
  lexirise.items = account(3);
  auto store = loaded();
  api.vocabServer = nullptr;
  auto cut = apiFailure(ApiError::Timeout);
  cut.body =
      lexipoint::fakes::vocabPageJson({item(1), item(2)}, std::nullopt, 2).substr(0, 900);  // bytes, then silence
  cut.sent = true;
  api.vocabReplies = {cut};
  const auto plan = store->next(Language::Japanese, kStartMs, kEpochS);
  const PageCall call = sendPage(api, *plan);
  EXPECT_EQ(call.error, ApiError::Timeout);
  EXPECT_TRUE(call.page.items.empty());
  store->apply(call, kStartMs, kEpochS);
  EXPECT_EQ(store->size(Language::Japanese), 0u);
  EXPECT_FALSE(store->syncState(Language::Japanese).full.running);
  EXPECT_FALSE(store->next(Language::Japanese, kStartMs + 1, kEpochS));  // the failure's wait
}

TEST_F(Fixture, AnswersForALanguageNotLoadedYetWaitForItsLoad) {
  lexirise.items = account(3);
  {
    auto store = loaded();
    sync(*store, api, kStartMs);
  }
  VocabStore store(files);  // a new boot: nothing read yet
  store.record({LiveState{Language::Japanese, 500001, true, 100001, 4}, LiveState{Language::Japanese, 500002}});
  EXPECT_FALSE(store.loaded(Language::Japanese));
  store.load(Language::Japanese);
  EXPECT_EQ(store.find(Language::Japanese, 500001)->proficiency, 4);
  EXPECT_EQ(store.find(Language::Japanese, 500002)->savedId, 0u);  // a removal (V7b R1)
  EXPECT_TRUE(store.dirty());
}

// --- Review round 2 ---

namespace {

// One random account and full pass: between its pages, items are deleted (sentence cards too), added and changed, as
// the user might in the Lexirise app meanwhile. Returns the account's words the mirror is missing once the pass and
// the incremental pass after it are done.
std::vector<uint32_t> missingAfterAChurningPass(const uint32_t seed) {
  std::mt19937 rng(seed);
  FakeFiles files;
  FakeApi api;
  FakeVocabAccount lexirise;
  lexirise.serve(api);
  const uint32_t n = 200 + rng() % 151;
  uint32_t nextId = 1;
  uint64_t clock = kSept2026Ms;
  const auto newItem = [&](const uint64_t updatedMs) {
    FakeVocabItem i = item(nextId++, static_cast<int>(rng() % 5), rng() % 8 == 0 ? "sentence" : "word");
    i.updatedMs = updatedMs;
    return i;
  };
  for (uint32_t i = 0; i < n; i++) {
    if (rng() % 4 != 0) clock += 1000;  // some ties
    lexirise.items.push_back(newItem(clock));
  }
  VocabStore store(files);
  store.load(Language::Japanese);
  int pages = 0;
  while (pages < 400) {
    const auto plan = store.next(Language::Japanese, kStartMs, kEpochS);
    if (!plan) break;
    if (plan->pass == Pass::Full && pages > 0) {
      for (uint32_t d = rng() % 6; d > 0 && !lexirise.items.empty(); d--) {
        lexirise.items.erase(lexirise.items.begin() + static_cast<std::ptrdiff_t>(rng() % lexirise.items.size()));
      }
      for (uint32_t a = rng() % 6; a > 0; a--) lexirise.items.push_back(newItem(clock += 1000));
      for (uint32_t c = rng() % 4; c > 0 && !lexirise.items.empty(); c--) {
        FakeVocabItem& changed = lexirise.items[rng() % lexirise.items.size()];
        changed.proficiency = static_cast<int>(rng() % 5);
        changed.updatedMs = clock += 1000;
      }
    }
    store.apply(sendPage(api, *plan), kStartMs, kEpochS);
    pages++;
  }
  std::vector<uint32_t> missing;
  for (const FakeVocabItem& i : lexirise.items) {
    if (i.unitType == "word" && !store.find(Language::Japanese, i.entryId)) missing.push_back(i.entryId);
  }
  return missing;
}

}  // namespace

TEST(VocabSync, NoLiveWordIsLostWhateverChangesBetweenAFullPassesPages) {
  for (uint32_t seed = 1; seed <= 120; seed++) {
    EXPECT_EQ(missingAfterAChurningPass(seed), std::vector<uint32_t>{}) << "seed " << seed;
  }
}

TEST_F(Fixture, AGroupTiedAtTheCursorIsntReadAgainEachPass) {
  for (uint32_t n = 1; n <= 120; n++) {
    FakeVocabItem i = item(n);
    i.updatedMs = kSept2026Ms;  // one bulk import: every item the same updated_at
    lexirise.items.push_back(i);
  }
  auto store = loaded();
  sync(*store, api, kStartMs);
  ASSERT_EQ(store->size(Language::Japanese), 120u);
  lexirise.offsets.clear();
  EXPECT_EQ(sync(*store, api, kStartMs + config::kVocabSyncIntervalMs), 1);  // not three pages of ties
  EXPECT_EQ(lexirise.offsets, (std::vector<uint32_t>{0}));
}

TEST_F(Fixture, ALiveAnswerDuringAFullPassDoesntHideTheItemsOwnFields) {
  lexirise.items = account(120);
  lexirise.items[0].suspended = true;  // the oldest: on the pass's last page
  lexirise.items[0].nextReviewMs = kSept2026Ms + 86400000ULL;
  auto store = loaded();
  ASSERT_EQ(sync(*store, api, kStartMs, 1), 1);                             // the pass's first page
  store->record({LiveState{Language::Japanese, 500001, true, 100001, 1}});  // a card's answer for it
  sync(*store, api, kStartMs);
  const auto e = store->find(Language::Japanese, 500001);
  ASSERT_TRUE(e);
  EXPECT_TRUE(e->suspended);  // the page's item, reached after the live answer
  EXPECT_EQ(e->nextReviewS, static_cast<uint32_t>((kSept2026Ms + 86400000ULL) / 1000));
  EXPECT_FALSE(e->live);
}

TEST_F(Fixture, ARereadInsideAnIncrementalPassKeepsEveryNewWord) {
  lexirise.items = account(10);
  auto store = loaded();
  sync(*store, api, kStartMs);
  for (uint32_t n = 1; n <= 150; n++) {
    FakeVocabItem i = item(1000 + n);
    i.updatedMs = kSept2026Ms + 5000000 + n;
    lexirise.items.push_back(i);
  }
  const unsigned long later = kStartMs + config::kVocabSyncIntervalMs;
  ASSERT_EQ(sync(*store, api, later, 1), 1);              // the incremental pass's first page
  for (int i = 0; i < 6; i++) lexirise.items.pop_back();  // six of the newest, read already, deleted
  sync(*store, api, later);
  for (uint32_t n = 1; n <= 144; n++) EXPECT_TRUE(store->find(Language::Japanese, 501000 + n)) << n;
}

TEST(VocabStorePending, PastItsCapTheNewestAreKeptInOrder) {
  FakeFiles files;
  VocabStore store(files);
  std::vector<LiveState> states;
  for (uint32_t i = 1; i <= config::kVocabPendingMax + 5; i++)
    states.push_back(LiveState{Language::Japanese, i, true, i, 1});
  states.push_back(
      LiveState{Language::Japanese, config::kVocabPendingMax, true, config::kVocabPendingMax, 3});  // later
  store.record(states);
  store.load(Language::Japanese);
  for (uint32_t i = 1; i <= 6; i++) EXPECT_FALSE(store.find(Language::Japanese, i)) << i;  // the oldest dropped
  EXPECT_TRUE(store.find(Language::Japanese, 7));
  EXPECT_EQ(store.find(Language::Japanese, config::kVocabPendingMax)->proficiency, 3);  // the later answer wins
}

TEST(VocabStorePending, AnotherLanguagesAnswersWaitForItsOwnLoad) {
  FakeFiles files;
  VocabStore store(files);
  store.load(Language::Japanese);
  store.record({LiveState{Language::Chinese, 9, true, 90, 2}, LiveState{Language::Japanese, 8, true, 80, 1}});
  EXPECT_TRUE(store.find(Language::Japanese, 8));
  EXPECT_FALSE(store.find(Language::Japanese, 9));
  EXPECT_FALSE(store.find(Language::Chinese, 9));
  store.load(Language::Chinese);
  EXPECT_EQ(store.find(Language::Chinese, 9)->savedId, 90u);
}

// --- Review round 3 ---

namespace {

// A list as GET /v1/vocabulary gives it (newest change first, ties in a stable order), paged `limit` at a time straight
// into the pure sync.
struct Listed {
  uint32_t id;
  int proficiency;
  uint64_t updatedMs;
  bool word;
};

void sortListed(std::vector<Listed>& list) {
  std::stable_sort(list.begin(), list.end(), [](const Listed& a, const Listed& b) {
    return a.updatedMs != b.updatedMs ? a.updatedMs > b.updatedMs : a.id < b.id;
  });
}

PageCall pageOf(std::vector<Listed>& list, const PagePlan& plan, const uint32_t limit, const bool counted = true) {
  sortListed(list);
  PageCall call;
  call.plan = plan;
  call.sent = true;
  for (uint32_t i = plan.offset; i < list.size() && i < plan.offset + limit; i++) {
    lexipoint::api::VocabItem v;
    v.savedId = list[i].id;
    v.entryId = list[i].id + 1000;
    v.proficiency = list[i].proficiency;
    v.updatedMs = list[i].updatedMs;
    v.word = list[i].word;
    call.page.items.push_back(v);
    call.page.newestMs = std::max(call.page.newestMs, v.updatedMs);
  }
  if (plan.offset + limit < list.size()) call.page.nextOffset = plan.offset + limit;
  if (counted) call.page.languageCount = static_cast<uint32_t>(list.size());
  return call;
}

Listed& byId(std::vector<Listed>& list, const uint32_t id) {
  return *std::find_if(list.begin(), list.end(), [id](const Listed& l) { return l.id == id; });
}

// Pages until the pass under way ends.
void syncPass(Mirror& m, RunState& run, std::vector<Listed>& list, const unsigned long nowMs, const uint32_t limit) {
  for (int i = 0; i < 1000; i++) {
    const auto plan = nextPage(m, run, Language::Japanese, nowMs, kEpochS);
    if (!plan) return;
    applyPage(m, run, pageOf(list, *plan, std::min(limit, plan->limit)), nowMs, kEpochS);  // as much as asked
    if (!m.sync.full.running && !m.sync.inc.running) return;
  }
}

}  // namespace

TEST(VocabSync, AnIncrementalPagesRereadWinsEvenWhenItReachesTheCursor) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 10; i++) list.push_back({i, 1, kSept2026Ms + i * 1000, true});
  Mirror m;
  RunState run;
  unsigned long now = kStartMs;
  syncPass(m, run, list, now, 3);  // the full pass
  const uint64_t cursor = m.sync.cursorMs;
  // Word 4 changes in the app (newer than the cursor), and five sentence cards are added at the top.
  byId(list, 4).updatedMs = cursor + 40;
  byId(list, 4).proficiency = 3;
  for (uint32_t k = 0; k < 5; k++) list.push_back({101 + k, 0, cursor + 200 + k, false});
  now += config::kVocabSyncIntervalMs + 1;
  auto plan = nextPage(m, run, Language::Japanese, now, kEpochS);
  ASSERT_TRUE(plan);
  ASSERT_EQ(plan->pass, Pass::Incremental);
  applyPage(m, run, pageOf(list, *plan, 5), now, kEpochS);  // the first page: the sentence cards only
  // They're deleted before the next page: word 4 moves up past it, and that page also reaches the cursor.
  list.erase(std::remove_if(list.begin(), list.end(), [](const Listed& l) { return !l.word; }), list.end());
  syncPass(m, run, list, now, 5);
  EXPECT_EQ(m.find(1004)->proficiency, 3);
  EXPECT_GE(m.sync.cursorMs, cursor + 40);
}

TEST(VocabSync, ALaterWordOfAGroupTiedAtTheCursorChangedInItsMillisecondWaitsForTheFullPass) {
  // A known limit (page-annotations.md §1.2): only if updated_at isn't in commit order.
  std::vector<Listed> list = {{1, 1, kSept2026Ms - 5000, true}, {2, 1, kSept2026Ms, true}, {3, 1, kSept2026Ms, true}};
  Mirror m;
  RunState run;
  unsigned long now = kStartMs;
  syncPass(m, run, list, now, 50);
  now += config::kVocabSyncIntervalMs + 1;
  syncPass(m, run, list, now, 50);
  ASSERT_EQ(m.sync.cursorMs, kSept2026Ms);
  byId(list, 3).proficiency = 4;  // word 3, second in the tie order, changed in the cursor's own millisecond
  now += config::kVocabSyncIntervalMs + 1;
  syncPass(m, run, list, now, 50);
  EXPECT_EQ(m.find(1003)->proficiency, 1);  // the incremental pass stopped at word 2, unchanged
  const auto full = nextPage(m, run, Language::Japanese, now, kEpochS + config::kVocabResyncS);
  ASSERT_TRUE(full);
  ASSERT_EQ(full->pass, Pass::Full);
  applyPage(m, run, pageOf(list, *full, 50), now, kEpochS + config::kVocabResyncS);
  EXPECT_EQ(m.find(1003)->proficiency, 4);  // the weekly full pass
}

// --- Review round 4 ---

TEST(VocabSync, AShortPageLeavesLessSlackSoOneDeletionIsReadAgain) {
  // Pages of one item: the next starts right after (no overlap to spare), so a single deletion above it moves the next
  // word into the page already read.
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 8; i++) list.push_back({i, 1, kSept2026Ms + i * 1000, true});
  Mirror m;
  RunState run;
  const unsigned long now = kStartMs;
  for (int page = 0; page < 3; page++) {
    const auto plan = nextPage(m, run, Language::Japanese, now, kEpochS);
    ASSERT_TRUE(plan);
    applyPage(m, run, pageOf(list, *plan, 1), now, kEpochS);
  }
  list.erase(list.begin());  // the newest, read already
  syncPass(m, run, list, now, 1);
  ASSERT_TRUE(m.sync.synced);
  for (const Listed& l : list) EXPECT_TRUE(m.find(l.id + 1000)) << l.id;
}

TEST(VocabSync, AFullPassWithoutACountBringsTheNextOneSoon) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 6; i++) list.push_back({i, 1, kSept2026Ms + i * 1000, true});
  Mirror m;
  RunState run;
  for (int page = 0; page < 3 && !m.sync.synced; page++) {  // pages of four: two, neither with a count
    const auto plan = nextPage(m, run, Language::Japanese, kStartMs, kEpochS);
    applyPage(m, run, pageOf(list, *plan, 4, /*counted=*/false), kStartMs, kEpochS);
  }
  ASSERT_TRUE(m.sync.synced);
  const uint32_t soonS = static_cast<uint32_t>(config::kVocabSyncIntervalMs / 1000);
  const unsigned long later = kStartMs + 2 * config::kVocabSyncIntervalMs;
  run.incDone = true;
  run.incDoneMs = later;
  EXPECT_FALSE(nextPage(m, run, Language::Japanese, later, kEpochS + soonS - 1));
  const auto again = nextPage(m, run, Language::Japanese, later, kEpochS + soonS);
  ASSERT_TRUE(again);
  EXPECT_EQ(again->pass, Pass::Full);
}

TEST(VocabSync, AnIncrementalPassWithoutACountBringsAFullOneSoon) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 6; i++) list.push_back({i, 1, kSept2026Ms + i * 1000, true});
  Mirror m;
  RunState run;
  unsigned long now = kStartMs;
  syncPass(m, run, list, now, 50);
  now += config::kVocabSyncIntervalMs + 1;
  syncPass(m, run, list, now, 50);  // the incremental pass after it, counted
  for (uint32_t i = 7; i <= 12; i++) list.push_back({i, 2, kSept2026Ms + 90000 + i, true});  // two pages of four
  now += config::kVocabSyncIntervalMs + 1;
  for (int page = 0; page < 3; page++) {
    const auto inc = nextPage(m, run, Language::Japanese, now, kEpochS);
    ASSERT_EQ(inc->pass, Pass::Incremental);
    applyPage(m, run, pageOf(list, *inc, 4, /*counted=*/false), now, kEpochS);
    if (!m.sync.inc.running) break;
  }
  ASSERT_FALSE(m.sync.inc.running);
  const uint32_t soonS = static_cast<uint32_t>(config::kVocabSyncIntervalMs / 1000);
  const auto full = nextPage(m, run, Language::Japanese, now + config::kVocabSyncIntervalMs, kEpochS + soonS);
  ASSERT_TRUE(full);
  EXPECT_EQ(full->pass, Pass::Full);  // not a week on
}

// --- Review rounds 5 and 6 ---

namespace {

// CRC-32 (IEEE), bit by bit: to forge a file the mirror must refuse though its check holds.
uint32_t crcOf(const std::string& a, const std::string& b) {
  uint32_t crc = 0xFFFFFFFF;
  for (const std::string* part : {&a, &b}) {
    for (const char c : *part) {
      crc ^= static_cast<uint8_t>(c);
      for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

}  // namespace

TEST(VocabSync, ASinglePageWithoutACountIsWholeAndSweeps) {
  std::vector<Listed> list = {{1, 1, kSept2026Ms + 1000, true}, {2, 1, kSept2026Ms + 2000, true}};
  Mirror m;
  RunState run;
  applyLive(m, LiveState{Language::Japanese, 9999, true, 1, 1});  // not in the account
  const auto plan = nextPage(m, run, Language::Japanese, kStartMs, kEpochS);
  applyPage(m, run, pageOf(list, *plan, 50, /*counted=*/false), kStartMs, kEpochS);
  ASSERT_TRUE(m.sync.synced);
  EXPECT_FALSE(m.find(9999));  // swept: the one page is the whole list
  EXPECT_FALSE(m.sync.resyncSoon);
  EXPECT_EQ(m.sync.fullDoneS, kEpochS);  // the weekly rule
}

TEST(VocabSync, APassBroughtForwardThatStillHasNoCountFallsBackToWeekly) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 6; i++) list.push_back({i, 1, kSept2026Ms + i * 1000, true});
  Mirror m;
  RunState run;
  uint32_t epoch = kEpochS;
  const auto uncountedPass = [&](const unsigned long nowMs) {
    for (int page = 0; page < 3; page++) {
      const auto plan = nextPage(m, run, Language::Japanese, nowMs, epoch);
      if (!plan) return;
      applyPage(m, run, pageOf(list, *plan, 4, /*counted=*/false), nowMs, epoch);
      if (!m.sync.full.running) return;
    }
  };
  uncountedPass(kStartMs);
  ASSERT_TRUE(m.sync.resyncSoon);
  const uint32_t soonS = static_cast<uint32_t>(config::kVocabSyncIntervalMs / 1000);
  epoch += soonS;
  run.incDone = true;
  run.incDoneMs = kStartMs + 1;
  ASSERT_EQ(nextPage(m, run, Language::Japanese, kStartMs + 2, epoch)->pass, Pass::Full);  // brought forward once
  uncountedPass(kStartMs + 2);
  EXPECT_EQ(m.sync.fullDoneS, epoch);  // no count again: the weekly rule, not another soon pass
  const auto next = nextPage(m, run, Language::Japanese, kStartMs + 3, epoch + soonS);
  EXPECT_TRUE(!next || next->pass != Pass::Full);
  // A counted pass later clears it.
  epoch += config::kVocabResyncS;
  for (int page = 0; page < 3; page++) {
    const auto plan = nextPage(m, run, Language::Japanese, kStartMs + 4, epoch);
    if (!plan) break;
    applyPage(m, run, pageOf(list, *plan, 4), kStartMs + 4, epoch);
    if (!m.sync.full.running) break;
  }
  EXPECT_FALSE(m.sync.resyncSoon);
}

TEST(VocabSync, AClockBehindTheLastPassOrFarAheadMakesAFullPassDue) {
  std::vector<Listed> list = {{1, 1, kSept2026Ms + 1000, true}};
  Mirror m;
  RunState run;
  syncPass(m, run, list, kStartMs, 50);
  run.incDone = true;
  run.incDoneMs = kStartMs;
  ASSERT_FALSE(nextPage(m, run, Language::Japanese, kStartMs + 1, kEpochS + 10));
  EXPECT_EQ(nextPage(m, run, Language::Japanese, kStartMs + 1, kEpochS - 10)->pass, Pass::Full);  // moved back
  EXPECT_EQ(nextPage(m, run, Language::Japanese, kStartMs + 1, kEpochS + config::kVocabResyncS + 1)->pass,
            Pass::Full);  // far ahead
}

TEST(VocabMirrorFile, TheSlackRoundTripsAndOneOutOfRangeIsRefused) {
  for (const uint8_t slack : {uint8_t{0}, uint8_t{1}, static_cast<uint8_t>(config::kVocabPageOverlap)}) {
    Mirror m;
    m.sync.full.running = true;
    m.sync.full.slack = slack;
    Mirror back;
    ASSERT_TRUE(parseMirror(serializeMirror(m, Language::Japanese), Language::Japanese, back));
    EXPECT_EQ(back.sync.full.slack, slack);
  }
  // Bits 4-5 saying 3 short of a 2-item overlap: not a file serializeMirror writes, though its CRC holds.
  std::string bytes = serializeMirror(Mirror(), Language::Japanese);
  bytes[10] = static_cast<char>((static_cast<uint8_t>(bytes[10]) & ~0x30) | 0x30);
  const size_t crcAt = config::kVocabHeaderBytes - 4;
  const uint32_t crc = crcOf(bytes.substr(0, crcAt), bytes.substr(config::kVocabHeaderBytes));
  for (size_t i = 0; i < 4; i++) bytes[crcAt + i] = static_cast<char>((crc >> (8 * i)) & 0xFF);
  Mirror out;
  EXPECT_FALSE(parseMirror(bytes, Language::Japanese, out));
  // An over-large slack in memory is written as the whole overlap.
  Mirror big;
  big.sync.full.slack = 3;
  Mirror clamped;
  ASSERT_TRUE(parseMirror(serializeMirror(big, Language::Japanese), Language::Japanese, clamped));
  EXPECT_EQ(clamped.sync.full.slack, config::kVocabPageOverlap);
}

TEST_F(Fixture, AnAnswerThatAgreesWithTheMirrorWritesNothing) {
  lexirise.items = account(5);
  auto store = loaded();
  sync(*store, api, kStartMs);
  ASSERT_FALSE(store->dirty());
  const int writes = files.writes;
  // The card's analysis says what the mirror has: 500002 saved as 100002 at level 2, 42 not saved.
  store->record({LiveState{Language::Japanese, 500002, true, 100002, 2}, LiveState{Language::Japanese, 42}});
  EXPECT_FALSE(store->dirty());
  EXPECT_TRUE(store->flush());
  EXPECT_EQ(files.writes, writes);
  EXPECT_FALSE(store->find(Language::Japanese, 500002)->live);  // still a page's entry
}

// --- Review round 7 ---

TEST_F(Fixture, APageGivenUpDoesntRetryAWriteThatFailed) {
  lexirise.items = account(3);
  auto store = loaded();
  sync(*store, api, kStartMs);
  store->record({LiveState{Language::Japanese, 42, true, 7, 1}});
  files.failWriteOf = config::kVocabTmpPathJa;
  ASSERT_FALSE(store->flush());  // the write failed: the slot stays dirty
  ASSERT_TRUE(store->dirty());
  files.failWriteOf.clear();
  const int writes = files.writes;
  const auto plan = store->next(Language::Japanese, kStartMs + config::kVocabSyncIntervalMs, kEpochS);
  ASSERT_TRUE(plan);
  PageCall given;
  given.plan = *plan;
  given.cancelled = true;
  store->apply(given, kStartMs + config::kVocabSyncIntervalMs, kEpochS);
  EXPECT_EQ(files.writes, writes);  // the reader has input: no file write now
  EXPECT_TRUE(store->dirty());
}

// --- Review round 8 ---

TEST_F(Fixture, AnIncrementalPassLongerThanOneWakeCarriesOnAcrossRestarts) {
  lexirise.items = account(54);
  {
    auto store = loaded();
    sync(*store, api, kStartMs);
  }
  const uint64_t cursor = lexirise.items.back().updatedMs;
  for (uint32_t n = 1; n <= 200; n++) {  // changed in the app since: five pages of them
    FakeVocabItem i = item(1000 + n);
    i.updatedMs = cursor + 1000 + n;
    lexirise.items.push_back(i);
  }
  lexirise.offsets.clear();
  unsigned long now = kStartMs + config::kVocabSyncIntervalMs;
  for (int boot = 0; boot < 20; boot++, now += config::kVocabSyncIntervalMs) {  // every sleep is a restart
    auto store = loaded();
    sync(*store, api, now, 2);  // two pages a wake
    if (!store->syncState(Language::Japanese).inc.running) break;
  }
  auto store = loaded();
  EXPECT_FALSE(store->syncState(Language::Japanese).inc.running);
  EXPECT_EQ(store->syncState(Language::Japanese).cursorMs, cursor + 1200);  // the pass ended: the cursor moved
  for (uint32_t n = 1; n <= 200; n++) EXPECT_TRUE(store->find(Language::Japanese, 501000 + n)) << n;
  // Each page once, in order: never the top again after a restart.
  // The probe at the top, then whole pages from its next offset less the overlap.
  const uint32_t first = config::kVocabProbeItems - config::kVocabPageOverlap;
  const uint32_t step = config::kVocabPageItems - config::kVocabPageOverlap;
  EXPECT_EQ(lexirise.offsets,
            (std::vector<uint32_t>{0, first, first + step, first + 2 * step, first + 3 * step, first + 4 * step}));
}

// --- Review round 9 ---

TEST_F(Fixture, AQuietIncrementalPassWritesNothing) {
  lexirise.items = account(120);
  auto store = loaded();
  sync(*store, api, kStartMs);
  const uint64_t cursor = store->syncState(Language::Japanese).cursorMs;
  const int writes = files.writes;
  lexirise.offsets.clear();
  EXPECT_EQ(sync(*store, api, kStartMs + config::kVocabSyncIntervalMs), 1);  // one page: nothing new
  EXPECT_EQ(lexirise.offsets, (std::vector<uint32_t>{0}));
  EXPECT_EQ(store->syncState(Language::Japanese).cursorMs, cursor);
  EXPECT_FALSE(store->syncState(Language::Japanese).inc.running);
  EXPECT_EQ(files.writes, writes);  // nothing to write
}

TEST(VocabSync, AFullPassSupersedesALeftOverIncrementalPass) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 6; i++) list.push_back({i, 1, kSept2026Ms + i * 1000, true});
  Mirror m;
  RunState run;
  syncPass(m, run, list, kStartMs, 3);  // synced
  m.sync.inc.running = true;            // an incremental pass left under way (in the file, before a restart)
  m.sync.inc.offset = 48;
  const uint32_t weekLater = kEpochS + config::kVocabResyncS;
  const auto full = nextPage(m, run, Language::Japanese, kStartMs + 1, weekLater);
  ASSERT_TRUE(full);
  EXPECT_EQ(full->pass, Pass::Full);
  EXPECT_EQ(full->offset, 0u);
  applyPage(m, run, pageOf(list, *full, 3), kStartMs + 1, weekLater);
  EXPECT_FALSE(m.sync.inc.running);
  EXPECT_TRUE(m.sync.full.running);
  for (int page = 0; page < 5 && m.sync.full.running; page++) {
    const auto plan = nextPage(m, run, Language::Japanese, kStartMs + 1, weekLater);
    applyPage(m, run, pageOf(list, *plan, 3), kStartMs + 1, weekLater);
  }
  ASSERT_FALSE(m.sync.full.running);
  const auto after = nextPage(m, run, Language::Japanese, kStartMs + 2, weekLater);
  ASSERT_TRUE(after);
  EXPECT_EQ(after->pass, Pass::Incremental);
  EXPECT_EQ(after->offset, 0u);
}

TEST_F(Fixture, APassLeftUnderWayIsWrittenThoughItsPagesChangedNoEntry) {
  lexirise.items = account(120);
  {
    auto store = loaded();
    sync(*store, api, kStartMs);
  }
  // 100 items touched in the app without a change the mirror keeps (their updated_at moves, their fields don't).
  const uint64_t cursor = lexirise.items.back().updatedMs;
  for (uint32_t n = 0; n < 100; n++) lexirise.items[n].updatedMs = cursor + 1000 + n;
  lexirise.offsets.clear();
  unsigned long now = kStartMs + config::kVocabSyncIntervalMs;
  for (int boot = 0; boot < 10; boot++, now += config::kVocabSyncIntervalMs) {  // a restart after every page
    auto store = loaded();
    sync(*store, api, now, 1);
    if (!store->syncState(Language::Japanese).inc.running) break;
  }
  const uint32_t first = config::kVocabProbeItems - config::kVocabPageOverlap;
  const uint32_t step = config::kVocabPageItems - config::kVocabPageOverlap;
  EXPECT_EQ(lexirise.offsets, (std::vector<uint32_t>{0, first, first + step, first + 2 * step}));  // never the top
  EXPECT_EQ(loaded()->syncState(Language::Japanese).cursorMs, cursor + 1099);
}

// --- Review round 10 ---

TEST(VocabMirrorFile, TheVersion3FileIsPinned) {
  // Version 3 (V7b R5): 20-byte records with each state's time and the own flag. The format mustn't move.
  Mirror m;
  m.sync.synced = true;
  m.sync.cursorMs = 123456789012ULL;
  m.sync.fullDoneS = 1790000000;
  m.sync.generation = 7;
  m.sync.resyncSoon = true;
  m.sync.lastSyncS = 1790000100;
  m.sync.full = PassProgress{true, 96, 99999999999ULL, 345, 1, true};
  m.sync.inc = PassProgress{true, 144, 88888888888ULL, 300, 0, true};
  m.put(Entry{30, 3, 0, 4, false, 7, false, 1790000200, false});
  m.put(Entry{10, 1, 1790300000, 0, true, 6, true, 1790000300, true});
  std::string hex;
  for (const char c : serializeMirror(m, Language::Japanese)) {
    char b[3];
    std::snprintf(b, sizeof(b), "%02x", static_cast<unsigned>(static_cast<uint8_t>(c)));
    hex += b;
  }
  EXPECT_EQ(hex,
            "4c58564d030014006a615f0702000000141a99be1c000000ffe776481700000060000000803bb16a5901000090000000"
            "38ce30b2140000002c01000027e43bb16a00000015f9abaa0a0000000100000060cfb56aac3cb16a000706001e000000"
            "0300000000000000483cb16a04000700");
}

TEST_F(Fixture, AQuietIncrementalPassAsksForAProbeOnly) {
  lexirise.items = account(120);
  auto store = loaded();
  sync(*store, api, kStartMs);
  api.vocabRequests.clear();
  EXPECT_EQ(sync(*store, api, kStartMs + config::kVocabSyncIntervalMs), 1);
  ASSERT_EQ(api.vocabRequests.size(), 1u);
  EXPECT_NE(api.vocabRequests[0].path.find("&limit=" + std::to_string(config::kVocabProbeItems) + "&offset=0&"),
            std::string::npos);
}

TEST_F(Fixture, AnAgreeingAnswerDuringAFullPassWritesNothing) {
  lexirise.items = account(120);
  auto store = loaded();
  sync(*store, api, kStartMs);
  ASSERT_EQ(sync(*store, api, kStartMs + 1, 1, kEpochS + config::kVocabResyncS), 1);  // the weekly pass's first page
  ASSERT_TRUE(store->syncState(Language::Japanese).full.running);
  const int writes = files.writes;
  // A card agrees with words the pass hasn't reached (older marks): no write, not dirty.
  store->record(
      {LiveState{Language::Japanese, 500001, true, 100001, 1}, LiveState{Language::Japanese, 500002, true, 100002, 2}});
  EXPECT_FALSE(store->dirty());
  EXPECT_TRUE(store->flush());
  EXPECT_EQ(files.writes, writes);
  // The pass still ends with every word, and sweeps nothing it shouldn't.
  sync(*store, api, kStartMs + 2, 100, kEpochS + config::kVocabResyncS);
  EXPECT_FALSE(store->syncState(Language::Japanese).full.running);
  EXPECT_EQ(store->size(Language::Japanese), 108u);
}

TEST(VocabSync, AnIncrementalReReadSentBackToTheTopThatEndsQuietlyClearsTheFile) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 6; i++) list.push_back({i, 1, kSept2026Ms + i * 1000, true});
  Mirror m;
  RunState run;
  syncPass(m, run, list, kStartMs, 50);
  run.incDone = true;
  run.incDoneMs = kStartMs;
  m.sync.inc.running = true;  // in the file: a pass under way, sent back to the top by a re-read
  m.sync.inc.offset = 0;
  m.sync.inc.slack = 0;
  const auto plan = nextPage(m, run, Language::Japanese, kStartMs + 1, kEpochS);
  ASSERT_TRUE(plan);
  ASSERT_EQ(plan->pass, Pass::Incremental);
  EXPECT_TRUE(applyPage(m, run, pageOf(list, *plan, 50), kStartMs + 1, kEpochS));  // must be written: it's cleared
  EXPECT_FALSE(m.sync.inc.running);
}

// --- Review round 11 ---

TEST(VocabSync, APagesRepeatedItemsDontEndThePassInsideACursorTie) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 4; i++) list.push_back({i, 1, kSept2026Ms + i, true});
  for (uint32_t i = 11; i <= 18; i++) list.push_back({i, 1, kSept2026Ms + 1000, true});  // eight tied: the cursor
  Mirror m;
  RunState run;
  unsigned long now = kStartMs;
  syncPass(m, run, list, now, 50);
  ASSERT_EQ(m.sync.cursorMs, kSept2026Ms + 1000);
  for (uint32_t i = 11; i <= 18; i++) {  // cards said another level for each: live, and not what the account has
    ASSERT_EQ(applyLive(m, LiveState{Language::Japanese, i + 1000, true, i, 3}), LiveApplied::Changed);
  }
  now += config::kVocabSyncIntervalMs + 1;
  syncPass(m, run, list, now, 50);  // the probe (5), then the rest from its next offset less the overlap
  for (uint32_t i = 11; i <= 18; i++) {
    EXPECT_EQ(m.find(i + 1000)->proficiency, 1) << i;  // every one refreshed from the page
    EXPECT_FALSE(m.find(i + 1000)->live) << i;
  }
}

namespace {

// A synced mirror of ten words, then twenty newer ones: an incremental pass's probe (offset 0), the next page planned
// at the probe's next offset less the overlap (3, slack 2).
struct AfterAProbe {
  std::vector<Listed> list;
  Mirror m;
  RunState run;
  unsigned long now = kStartMs;
  std::vector<uint32_t> limits;  // each page's limit after the probe
  AfterAProbe() {
    for (uint32_t i = 1; i <= 10; i++) list.push_back({i, 1, kSept2026Ms + i * 1000, true});
    syncPass(m, run, list, now, 50);
    for (uint32_t i = 101; i <= 120; i++) list.push_back({i, 2, kSept2026Ms + 50000 + i, true});
    now += config::kVocabSyncIntervalMs + 1;
    const auto probe = nextPage(m, run, Language::Japanese, now, kEpochS);
    EXPECT_EQ(probe->limit, config::kVocabProbeItems);
    applyPage(m, run, pageOf(list, *probe, probe->limit), now, kEpochS);
    EXPECT_EQ(m.sync.inc.offset, config::kVocabProbeItems - config::kVocabPageOverlap);
    EXPECT_EQ(m.sync.inc.slack, config::kVocabPageOverlap);
  }
  // Deletes `n` of the words the probe read (the newest), then pages until the pass ends; the plans' offsets.
  std::vector<uint32_t> deleteThenFinish(const uint32_t n) {
    sortListed(list);
    list.erase(list.begin(), list.begin() + n);
    std::vector<uint32_t> offsets;
    for (int i = 0; i < 50 && m.sync.inc.running; i++) {
      const auto plan = nextPage(m, run, Language::Japanese, now, kEpochS);
      offsets.push_back(plan->offset);
      limits.push_back(plan->limit);
      applyPage(m, run, pageOf(list, *plan, plan->limit), now, kEpochS);
    }
    return offsets;
  }
  bool hasEveryWord() const {
    for (const Listed& l : list) {
      if (!m.find(l.id + 1000)) return false;
    }
    return true;
  }
};

}  // namespace

TEST(VocabSync, ADropOfThreeOrFourAfterTheProbeReReadsFromOffsetOneOrTwo) {
  for (const uint32_t drop : {3u, 4u}) {
    AfterAProbe p;
    const std::vector<uint32_t> offsets = p.deleteThenFinish(drop);
    ASSERT_GE(offsets.size(), 2u);
    EXPECT_EQ(offsets[0], 3u);                // the page after the probe: it finds the drop
    EXPECT_EQ(offsets[1], 3u - (drop - 2u));  // read again from 2 (a drop of 3) or 1 (of 4)
    EXPECT_TRUE(p.hasEveryWord()) << drop;
  }
}

TEST(VocabSync, ADropOfFiveOrMoreAfterTheProbeSendsThePassBackToAProbe) {
  for (const uint32_t drop : {5u, 8u}) {
    AfterAProbe p;
    const std::vector<uint32_t> offsets = p.deleteThenFinish(drop);
    ASSERT_GE(offsets.size(), 2u);
    EXPECT_EQ(offsets[1], 0u);  // back to the top, as a probe again
    EXPECT_EQ(p.limits[1], config::kVocabProbeItems);
    EXPECT_TRUE(p.hasEveryWord()) << drop;
  }
}

// --- V7b (carried from V7a's review) ---

TEST(VocabSync, AReReadDoesntEndThePassInsideACursorTie) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 4; i++) list.push_back({i, 1, kSept2026Ms + i, true});
  for (uint32_t i = 11; i <= 20; i++) list.push_back({i, 1, kSept2026Ms + 1000, true});  // ten tied: the cursor
  Mirror m;
  RunState run;
  unsigned long now = kStartMs;
  syncPass(m, run, list, now, 50);
  ASSERT_EQ(m.sync.cursorMs, kSept2026Ms + 1000);
  for (uint32_t i = 11; i <= 20; i++) {  // cards said another level for each: live, and not what the account has
    ASSERT_EQ(applyLive(m, LiveState{Language::Japanese, i + 1000, true, i, 3}), LiveApplied::Changed);
  }
  now += config::kVocabSyncIntervalMs + 1;
  auto plan = nextPage(m, run, Language::Japanese, now, kEpochS);
  ASSERT_EQ(plan->limit, config::kVocabProbeItems);
  applyPage(m, run, pageOf(list, *plan, plan->limit), now, kEpochS);  // the probe: 11-15 refreshed
  // Three older words deleted: the next page sees the drop and reads again from before it, over items it refreshed.
  list.erase(std::remove_if(list.begin(), list.end(), [](const Listed& l) { return l.id <= 3; }), list.end());
  for (int i = 0; i < 20 && m.sync.inc.running; i++) {
    plan = nextPage(m, run, Language::Japanese, now, kEpochS);
    applyPage(m, run, pageOf(list, *plan, 5), now, kEpochS);
    if (m.sync.inc.running && m.sync.inc.slack == 0) EXPECT_TRUE(m.sync.inc.reread);
  }
  for (uint32_t i = 11; i <= 20; i++) {
    EXPECT_EQ(m.find(i + 1000)->proficiency, 1) << i;  // every one refreshed, the ones after the re-read too
  }
}

TEST(VocabSync, AReReadSentBackToTheTopIsStillAReRead) {
  Mirror m;
  RunState run;
  m.sync.synced = true;
  m.sync.cursorMs = kSept2026Ms + 1000;
  m.sync.inc.running = true;  // a pass under way, sent back to the top by a re-read
  m.sync.inc.offset = 0;
  m.sync.inc.slack = 0;
  m.sync.inc.reread = true;
  std::vector<Listed> list;
  for (uint32_t i = 11; i <= 20; i++) {
    list.push_back({i, 1, kSept2026Ms + 1000, true});
    m.put(Entry{i + 1000, i, 0, 1, false, 0, false});  // as the pass already read them: unchanged, at the cursor
  }
  const auto plan = nextPage(m, run, Language::Japanese, kStartMs, kEpochS);
  ASSERT_TRUE(plan);
  applyPage(m, run, pageOf(list, *plan, plan->limit), kStartMs, kEpochS);
  EXPECT_TRUE(m.sync.inc.running);  // no tie stop: on to the rest
  EXPECT_TRUE(m.sync.inc.reread);
}

TEST(VocabMirrorFile, TheReReadBitRoundTrips) {
  Mirror m;
  m.sync.synced = true;
  m.sync.inc.start(kSept2026Ms);
  m.sync.inc.offset = 7;
  m.sync.inc.slack = 0;
  m.sync.inc.reread = true;
  Mirror back;
  ASSERT_TRUE(parseMirror(serializeMirror(m, Language::Japanese), Language::Japanese, back));
  EXPECT_EQ(back.sync.inc, m.sync.inc);
  m.sync.inc.reread = false;
  ASSERT_TRUE(parseMirror(serializeMirror(m, Language::Japanese), Language::Japanese, back));
  EXPECT_FALSE(back.sync.inc.reread);
}

// --- V7b R1: removals ---

TEST(VocabRemoval, ALiveRemovalIsKeptOnlyForAnEntryTheMirrorHadAndOnlyALaterIncrementalPassDropsIt) {
  Mirror m;
  m.sync.synced = true;
  EXPECT_EQ(applyLive(m, LiveState{Language::Japanese, 9, false, 0, 0}), LiveApplied::None);  // not had: nothing
  EXPECT_EQ(m.size(), 0u);
  ASSERT_EQ(applyLive(m, LiveState{Language::Japanese, 9, true, 90, 2}), LiveApplied::Changed);
  EXPECT_EQ(applyLive(m, LiveState{Language::Japanese, 9, false, 0, 0}), LiveApplied::Changed);
  ASSERT_TRUE(m.find(9));
  EXPECT_EQ(m.find(9)->savedId, 0u);
  EXPECT_TRUE(m.find(9)->live);
  EXPECT_FALSE(savedStateOf(*m.find(9)));
  EXPECT_EQ(applyLive(m, LiveState{Language::Japanese, 9, false, 0, 0}), LiveApplied::None);  // already
  Mirror back;
  ASSERT_TRUE(parseMirror(serializeMirror(m, Language::Japanese), Language::Japanese, back));
  EXPECT_EQ(back.find(9)->savedId, 0u);
  EXPECT_EQ(m.sweep(m.sync.generation), 0u);                            // made during this pass: kept (R3)
  EXPECT_EQ(m.sweep(static_cast<uint8_t>(m.sync.generation + 1)), 0u);  // never by a sweep (R4)
  EXPECT_EQ(m.dropRemovalsUpTo(0), 1u);  // a removal of unknown time goes once the mirror is complete again (R5)
  EXPECT_EQ(m.size(), 0u);
}

TEST(VocabRemoval, APagesItemReplacesARemoval) {
  std::vector<Listed> list{{9, 2, kSept2026Ms + 5000, true}};
  Mirror m;
  RunState run;
  syncPass(m, run, list, kStartMs, 50);
  ASSERT_EQ(applyLive(m, LiveState{Language::Japanese, 1009, false, 0, 0}), LiveApplied::Changed);
  byId(list, 9).updatedMs = kSept2026Ms + 9000;  // saved again in the app
  syncPass(m, run, list, kStartMs + config::kVocabSyncIntervalMs + 1, 50);
  EXPECT_EQ(m.find(1009)->savedId, 9u);
}

TEST(VocabMirrorFile, ARecordWithoutASavedIdMustBeARemoval) {
  Mirror m;
  m.put(Entry{5, 0, 0, 0, false, 0, true});
  std::string bytes = serializeMirror(m, Language::Japanese);
  Mirror back;
  ASSERT_TRUE(parseMirror(bytes, Language::Japanese, back));
  m = Mirror();
  m.put(Entry{5, 0, 0, 0, false, 0, false});  // not live: not a file serializeMirror writes for a removal
  EXPECT_FALSE(parseMirror(serializeMirror(m, Language::Japanese), Language::Japanese, back));
}

TEST(VocabSync, TheMirrorIsCompleteAsOfTheStartOfAnIncrementalPassFromTheTop) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 12; i++) list.push_back({i, 1, kSept2026Ms + i * 1000, true});
  Mirror m;
  RunState run;
  // A full pass over two pages at E and E+200 s; a word is saved in the app at E+50 s, after its first page (it goes
  // to the top, already read): the full pass doesn't have it, so it can't say the mirror is complete as of its end.
  const PagePlan first{Language::Japanese, Pass::Full, 0, 8};
  applyPage(m, run, pageOf(list, first, 8), kStartMs, kEpochS);
  list.push_back({99, 2, kSept2026Ms + 500000, true});
  const PagePlan second{Language::Japanese, Pass::Full, m.sync.full.offset, 8};
  applyPage(m, run, pageOf(list, second, 8), kStartMs, kEpochS + 200);
  ASSERT_TRUE(m.sync.synced);
  EXPECT_FALSE(m.find(1099));
  EXPECT_EQ(m.sync.lastSyncS, 0u);  // a page analyzed at E+100 keeps its snapshot (the word saved)
  // The incremental pass after it, from the top at E+300 over two pages: complete as of E+300 once it ends.
  const PagePlan probe{Language::Japanese, Pass::Incremental, 0, config::kVocabProbeItems};
  applyPage(m, run, pageOf(list, probe, 1), kStartMs, kEpochS + 300);  // cut short: the pass goes on
  ASSERT_TRUE(m.sync.inc.running);
  EXPECT_EQ(m.sync.lastSyncS, 0u);
  const PagePlan rest{Language::Japanese, Pass::Incremental, m.sync.inc.offset, config::kVocabPageItems};
  applyPage(m, run, pageOf(list, rest, 50), kStartMs, kEpochS + 400);
  ASSERT_FALSE(m.sync.inc.running);
  EXPECT_TRUE(m.find(1099));
  EXPECT_EQ(m.sync.lastSyncS, kEpochS + 300);  // its start, not its end
  Mirror back;
  ASSERT_TRUE(parseMirror(serializeMirror(m, Language::Japanese), Language::Japanese, back));
  EXPECT_EQ(back.sync.lastSyncS, kEpochS + 300);
}

TEST(VocabSync, APassResumedAfterARestartLeavesTheTimeAlone) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 12; i++) list.push_back({i, 1, kSept2026Ms + i * 1000, true});
  Mirror m;
  RunState run;
  syncPass(m, run, list, kStartMs, 50);
  const PagePlan probe{Language::Japanese, Pass::Incremental, 0, config::kVocabProbeItems};
  for (uint32_t i = 20; i <= 30; i++) list.push_back({i, 1, kSept2026Ms + 900000 + i, true});
  applyPage(m, run, pageOf(list, probe, config::kVocabProbeItems), kStartMs, kEpochS + 300);
  ASSERT_TRUE(m.sync.inc.running);
  const uint32_t before = m.sync.lastSyncS;
  RunState restarted;  // every sleep is a restart: the pass carries on from the file, its start unknown
  const PagePlan rest{Language::Japanese, Pass::Incremental, m.sync.inc.offset, config::kVocabPageItems};
  applyPage(m, restarted, pageOf(list, rest, 50), kStartMs, kEpochS + 900);
  ASSERT_FALSE(m.sync.inc.running);
  EXPECT_EQ(m.sync.lastSyncS, before);
}

// --- V7b R2: what counts as a change for the reader ---

TEST(VocabSync, ANewFullPassOverAnUnchangedAccountChangesNoEntryForTheReader) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 12; i++) list.push_back({i, 2, kSept2026Ms + i * 1000, true});
  Mirror m;
  RunState run;
  syncPass(m, run, list, kStartMs, 5);
  // A full pass again (a week later): each page re-marks the entries, and writes them (the marks are the file's).
  const PagePlan plan{Language::Japanese, Pass::Full, 0, 5};
  std::vector<uint32_t> changed;
  EXPECT_TRUE(applyPage(m, run, pageOf(list, plan, 5), kStartMs, kEpochS + config::kVocabResyncS, &changed));
  EXPECT_TRUE(changed.empty());
  EXPECT_EQ(m.find(1012)->mark, m.sync.generation);
  // A level changed in the app is one.
  byId(list, 5).proficiency = 4;  // on the next page
  const PagePlan next{Language::Japanese, Pass::Full, m.sync.full.offset, 5};
  applyPage(m, run, pageOf(list, next, 5), kStartMs, kEpochS + config::kVocabResyncS, &changed);
  EXPECT_EQ(changed, std::vector<uint32_t>{1005});
}

TEST(VocabStorePending, APendingAnswerIsReadBeforeTheLoad) {
  FakeFiles files;
  VocabStore store(files);
  store.record({LiveState{Language::Japanese, 7, true, 70, 2}, LiveState{Language::Japanese, 7, true, 70, 3}});
  const auto pending = store.pendingState(Language::Japanese, 7);
  ASSERT_TRUE(pending);
  EXPECT_EQ(pending->proficiency, 3);  // the newest
  EXPECT_FALSE(store.pendingState(Language::Chinese, 7));
  store.load(Language::Japanese);
  EXPECT_FALSE(store.pendingState(Language::Japanese, 7));  // loaded: find() says
  EXPECT_EQ(store.find(Language::Japanese, 7)->proficiency, 3);
}

TEST(VocabRemoval, ARemovalMadeDuringTheIncrementalPassOutlivesItsEnd) {
  std::vector<Listed> list;
  for (uint32_t i = 1; i <= 12; i++) list.push_back({i, 1, kSept2026Ms + i * 1000, true});
  Mirror m;
  RunState run;
  syncPass(m, run, list, kStartMs, 50);  // the full pass
  // An incremental pass begins from the top; a live answer then reports word 3 removed (this generation's mark).
  for (uint32_t i = 20; i <= 30; i++) list.push_back({i, 1, kSept2026Ms + 900000 + i, true});
  const PagePlan probe{Language::Japanese, Pass::Incremental, 0, config::kVocabProbeItems};
  applyPage(m, run, pageOf(list, probe, config::kVocabProbeItems), kStartMs, kEpochS + 300);
  ASSERT_TRUE(m.sync.inc.running);
  // Known at E+350, after the pass's start (E+300): a page analyzed between them may still say saved.
  ASSERT_EQ(applyLive(m, LiveState{Language::Japanese, 1003, false, 0, 0, kEpochS + 350}), LiveApplied::Changed);
  list.erase(std::remove_if(list.begin(), list.end(), [](const Listed& l) { return l.id == 3; }), list.end());
  const PagePlan rest{Language::Japanese, Pass::Incremental, m.sync.inc.offset, config::kVocabPageItems};
  applyPage(m, run, pageOf(list, rest, 50), kStartMs, kEpochS + 400);
  ASSERT_FALSE(m.sync.inc.running);
  EXPECT_EQ(m.sync.lastSyncS, kEpochS + 300);
  ASSERT_TRUE(m.find(1003));  // a page analyzed after this pass's start may still say saved
  EXPECT_EQ(m.find(1003)->savedId, 0u);
}

TEST(VocabRemoval, TheReadersOwnRemovalOfUnknownTimeStaysUntilAFullPassEnds) {
  Mirror m;
  m.put(Entry{5, 0, 0, 0, false, 0, true, 0, true});   // the reader's, no clock
  m.put(Entry{6, 0, 0, 0, false, 0, true, 0, false});  // an answer's, no clock
  EXPECT_EQ(m.dropRemovalsUpTo(1790000000), 1u);       // the answer's goes
  EXPECT_TRUE(m.find(5));
  EXPECT_EQ(m.dropOwnUnknownRemovals(), 1u);  // the full pass's end
  EXPECT_FALSE(m.find(5));
}
