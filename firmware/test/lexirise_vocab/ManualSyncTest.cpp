// v0.2 V7b: the home screen's Sync Vocabulary (vocab/ManualSync.h), over a fake account and card.

#include <gtest/gtest.h>

#include "FakeApi.h"
#include "Fakes.h"
#include "VocabFixtures.h"
#include "lexirise/vocab/ManualSync.h"

using lexipoint::Language;
using lexipoint::Settings;
using lexipoint::api::ApiError;
using lexipoint::fakes::FakeApi;
using lexipoint::fakes::FakeFiles;
using lexipoint::fakes::FakeVocabAccount;
using lexipoint::fakes::kSept2026Ms;
using namespace lexipoint::vocab;
namespace config = lexipoint::config;

namespace {

constexpr uint32_t kEpochS = static_cast<uint32_t>(kSept2026Ms / 1000) + 86400;
int joins = 0;
ApiError joinOk() {
  joins++;
  return ApiError::None;
}
ApiError joinFails() {
  joins++;
  return ApiError::NoWifi;
}

Settings japaneseOnly() {
  Settings s;
  s.apiKey = "lx_TESTKEYtestkey0123456789";
  s.chinese.enabled = false;
  return s;
}

struct Rig {
  FakeFiles files;
  FakeApi api;
  FakeVocabAccount lexirise;
  VocabStore store{files};
  Rig() {
    joins = 0;
    for (uint32_t n = 1; n <= 120; n++) lexirise.items.push_back({100 + n, 500 + n, 2, false, kSept2026Ms + n * 1000});
    lexirise.serve(api);
  }
  ManualSync::Result run(ManualSync& sync, int maxSteps = 100) {
    ManualSync::Result r = ManualSync::Result::Running;
    for (int i = 0; i < maxSteps && r == ManualSync::Result::Running; i++) r = sync.step(1000, kEpochS);
    return r;
  }
};

}  // namespace

TEST(HomeSyncRow, ShownOnlyWithLexiriseOnAKeyAndALanguage) {
  Settings s = japaneseOnly();
  EXPECT_TRUE(syncRowShown(s));
  s.enabled = false;
  EXPECT_FALSE(syncRowShown(s));
  s = japaneseOnly();
  s.apiKey.clear();
  EXPECT_FALSE(syncRowShown(s));
  s = japaneseOnly();
  s.japanese.enabled = false;
  EXPECT_FALSE(syncRowShown(s));
}

TEST(HomeSync, AFirstSyncJoinsOnceThenRunsTheFullPassAndTheIncrementalOne) {
  Rig r;
  ManualSync sync(r.store, r.api, joinOk, japaneseOnly());
  EXPECT_EQ(sync.step(1000, kEpochS), ManualSync::Result::Running);  // the join, first
  EXPECT_EQ(joins, 1);
  EXPECT_TRUE(r.api.vocabRequests.empty());
  EXPECT_EQ(r.run(sync), ManualSync::Result::Synced);
  EXPECT_EQ(joins, 1);  // once
  EXPECT_EQ(sync.changed(), 120u);
  EXPECT_TRUE(r.store.syncState(Language::Japanese).synced);
  EXPECT_EQ(r.store.size(Language::Japanese), 120u);
  EXPECT_EQ(sync.percent(), 100u);
  // The last page asked was the incremental pass's probe, from the top.
  EXPECT_NE(r.api.vocabRequests.back().path.find("limit=5&offset=0"), std::string::npos);
}

TEST(HomeSync, NothingNewIsUpToDateAfterOneProbe) {
  Rig r;
  ManualSync first(r.store, r.api, joinOk, japaneseOnly());
  r.run(first);
  const size_t before = r.api.vocabRequests.size();
  ManualSync again(r.store, r.api, joinOk, japaneseOnly());
  EXPECT_EQ(r.run(again), ManualSync::Result::UpToDate);
  EXPECT_EQ(r.api.vocabRequests.size(), before + 1);  // the probe
  EXPECT_EQ(again.changed(), 0u);
}

TEST(HomeSync, AChangeInTheAppIsCounted) {
  Rig r;
  ManualSync first(r.store, r.api, joinOk, japaneseOnly());
  r.run(first);
  r.lexirise.items[3].proficiency = 4;
  r.lexirise.items[3].updatedMs = kSept2026Ms + 900000;
  ManualSync again(r.store, r.api, joinOk, japaneseOnly());
  EXPECT_EQ(r.run(again), ManualSync::Result::Synced);
  EXPECT_EQ(again.changed(), 1u);
  EXPECT_EQ(r.store.find(Language::Japanese, 504)->proficiency, 4);
}

TEST(HomeSync, NoWifiEndsItWithoutAPage) {
  Rig r;
  ManualSync sync(r.store, r.api, joinFails, japaneseOnly());
  EXPECT_EQ(r.run(sync), ManualSync::Result::NoWifi);
  EXPECT_TRUE(r.api.vocabRequests.empty());
}

TEST(HomeSync, InputStopsIt) {
  Rig r;
  ManualSync sync(r.store, r.api, joinOk, japaneseOnly());
  sync.step(1000, kEpochS);
  sync.step(1000, kEpochS);  // a page
  sync.stop();               // a button between pages
  EXPECT_EQ(sync.result(), ManualSync::Result::Stopped);
  const size_t asked = r.api.vocabRequests.size();
  EXPECT_EQ(sync.step(1000, kEpochS), ManualSync::Result::Stopped);  // nothing more
  EXPECT_EQ(r.api.vocabRequests.size(), asked);
  // Given up while a page streams: stopped too, and the pass resumes on the next press.
  ManualSync next(r.store, r.api, joinOk, japaneseOnly());
  next.step(1000, kEpochS);
  EXPECT_EQ(next.step(1000, kEpochS, [] { return true; }), ManualSync::Result::Stopped);
  EXPECT_TRUE(r.store.syncState(Language::Japanese).full.running);
}

TEST(HomeSync, AFailedPageEndsIt) {
  Rig r;
  r.api.vocabServer = nullptr;
  r.api.vocabReplies = {lexipoint::fakes::apiFailure(ApiError::Server)};
  ManualSync sync(r.store, r.api, joinOk, japaneseOnly());
  EXPECT_EQ(r.run(sync), ManualSync::Result::Failed);
}

TEST(HomeSync, ARunIsCappedAndTheNextPressGoesOn) {
  Rig r;
  for (uint32_t n = 121; n <= 121 + config::kVocabPageItems * (config::kVocabManualSyncPagesMax + 2); n++) {
    r.lexirise.items.push_back({100 + n, 500 + n, 1, false, kSept2026Ms + n * 1000});
  }
  ManualSync sync(r.store, r.api, joinOk, japaneseOnly());
  EXPECT_EQ(r.run(sync, 1000), ManualSync::Result::Synced);
  EXPECT_EQ(sync.pages(), config::kVocabManualSyncPagesMax);
  EXPECT_TRUE(r.store.syncState(Language::Japanese).full.running);  // resumable
  ManualSync next(r.store, r.api, joinOk, japaneseOnly());
  r.run(next, 1000);
  EXPECT_TRUE(r.store.syncState(Language::Japanese).synced);
}

// --- review round 2 ---

TEST(HomeSync, AWeeklyFullPassWithNothingChangedIsUpToDate) {
  Rig r;
  ManualSync first(r.store, r.api, joinOk, japaneseOnly());
  ASSERT_EQ(r.run(first), ManualSync::Result::Synced);
  // Eight days later a full pass is due again: every word gets the new pass's mark, none changes for the reader.
  ManualSync later(r.store, r.api, joinOk, japaneseOnly());
  ManualSync::Result result = ManualSync::Result::Running;
  for (int i = 0; i < 100 && result == ManualSync::Result::Running; i++) {
    result = later.step(1000, kEpochS + 8 * 86400);
  }
  EXPECT_EQ(result, ManualSync::Result::UpToDate);
  EXPECT_EQ(later.changed(), 0u);
}

TEST(HomeSync, ACappedRunIsNeverUpToDate) {
  Rig r;
  for (uint32_t n = 121; n <= 121 + config::kVocabPageItems * (config::kVocabManualSyncPagesMax + 2); n++) {
    r.lexirise.items.push_back({100 + n, 500 + n, 1, false, kSept2026Ms + n * 1000});
  }
  for (int press = 0; press < 3 && !r.store.syncState(Language::Japanese).synced; press++) {
    ManualSync sync(r.store, r.api, joinOk, japaneseOnly());
    r.run(sync, 1000);
  }
  ASSERT_TRUE(r.store.syncState(Language::Japanese).synced);
  // A week later nothing has changed, but the full pass due is longer than one press: "Synced", not "up to date".
  ManualSync later(r.store, r.api, joinOk, japaneseOnly());
  ManualSync::Result result = ManualSync::Result::Running;
  for (int i = 0; i < 1000 && result == ManualSync::Result::Running; i++)
    result = later.step(1000, kEpochS + 8 * 86400);
  EXPECT_EQ(later.changed(), 0u);
  EXPECT_TRUE(r.store.syncState(Language::Japanese).full.running);
  EXPECT_EQ(result, ManualSync::Result::Synced);
}

TEST(HomeSync, ARejectedKeyOrARateLimitIsSaidAsSuch) {
  Rig r;
  r.api.vocabServer = nullptr;
  r.api.vocabReplies = {lexipoint::fakes::apiFailure(ApiError::Unauthorized)};
  ManualSync sync(r.store, r.api, joinOk, japaneseOnly());
  EXPECT_EQ(r.run(sync), ManualSync::Result::Failed);
  EXPECT_EQ(sync.error(), ApiError::Unauthorized);
}

TEST(HomeSync, ItsPagesArentInTheIdlePagesBudget) {
  Rig r;
  ManualSync first(r.store, r.api, joinOk, japaneseOnly());
  r.run(first);  // three pages and a probe
  // The idle pages' hourly budget is untouched: a page is still due for idle cards (after the interval).
  EXPECT_TRUE(r.store.next(Language::Japanese, 1000 + config::kVocabSyncIntervalMs + 1, kEpochS).has_value());
}

TEST(HomeSyncAction, StopOnAPressStepOnceDrawnDismissOnTheRelease) {
  using A = HomeSyncAction;
  HomeSyncFlow f;
  // next(pressed, released, held, drawn, running, resultOver)
  EXPECT_EQ(f.next(false, false, false, false, true, false), A::Wait);  // its frame on its way
  EXPECT_EQ(f.next(false, false, false, true, true, false), A::Step);
  EXPECT_EQ(f.next(true, false, true, false, true, false), A::Stop);  // a press stops it, drawn or not
  // The stopping press's release, the result not drawn yet, then drawn: nothing dismissed by it.
  EXPECT_EQ(f.next(false, true, false, false, false, false), A::Wait);
  EXPECT_EQ(f.next(false, false, false, true, false, false), A::Wait);
  // A press on the result: Wait (its release mustn't reach the menu); the release dismisses.
  EXPECT_EQ(f.next(true, false, true, true, false, false), A::Wait);
  EXPECT_EQ(f.next(false, true, false, true, false, false), A::Dismiss);
}

TEST(HomeSyncAction, APressThatStoppedItInsideACallDismissesNothingThoughTheResultIsDrawnFirst) {
  using A = HomeSyncAction;
  HomeSyncFlow f;
  EXPECT_EQ(f.next(false, false, false, true, true, false), A::Step);  // the step's call gives up for the press
  // "Sync stopped" drawn by the next pass, which only now sees the press's edge (held), then its release.
  EXPECT_EQ(f.next(true, false, true, true, false, false), A::Wait);
  EXPECT_EQ(f.next(false, true, false, true, false, false), A::Wait);  // the result stays up
  EXPECT_EQ(f.next(false, false, false, true, false, false), A::Wait);
  EXPECT_EQ(f.next(false, false, false, true, false, true), A::Dismiss);  // its time up
}

TEST(HomeSyncAction, APressStoppingItWithTheResultNotDrawnYetDismissesNothingEither) {
  using A = HomeSyncAction;
  HomeSyncFlow f;
  EXPECT_EQ(f.next(false, false, false, true, true, false), A::Step);
  EXPECT_EQ(f.next(true, false, true, false, false, false), A::Wait);  // the edge, the result not drawn yet
  EXPECT_EQ(f.next(false, false, true, true, false, false), A::Wait);  // drawn, the button still held
  EXPECT_EQ(f.next(false, true, false, true, false, false), A::Wait);  // its release
  // Then a press of its own on the result as shown: its release dismisses.
  EXPECT_EQ(f.next(false, false, false, true, false, false), A::Wait);
  EXPECT_EQ(f.next(true, false, true, true, false, false), A::Wait);
  EXPECT_EQ(f.next(false, true, false, true, false, false), A::Dismiss);
}

TEST(HomeSyncAction, TheResultsTimeUpDismissesOnlyWithNothingHeld) {
  using A = HomeSyncAction;
  HomeSyncFlow f;
  EXPECT_EQ(f.next(false, false, true, true, false, true), A::Wait);  // a finger still down: wait for its lift
  EXPECT_EQ(f.next(false, false, false, true, false, true), A::Dismiss);
}

TEST(HomeSync, PressesShareAnHourlyPageBudget) {
  Rig r;
  for (uint32_t n = 121; n <= 121 + config::kVocabPageItems * (config::kVocabManualSyncPagesPerHour + 50); n++) {
    r.lexirise.items.push_back({100 + n, 500 + n, 1, false, kSept2026Ms + n * 1000});
  }
  unsigned sent = 0;
  for (int press = 0; press < 3; press++) {
    ManualSync sync(r.store, r.api, joinOk, japaneseOnly());
    r.run(sync, 1000);
    sent += sync.pages();
  }
  EXPECT_EQ(sent, config::kVocabManualSyncPagesPerHour);  // the third press sends none within the hour
  EXPECT_FALSE(r.store.manualBudgetLeft(1000));
  EXPECT_TRUE(r.store.manualBudgetLeft(1000 + 60UL * 60UL * 1000UL));  // an hour on
}

TEST(HomeSync, APressWithTheHoursPagesSpentDoesntJoinAndSaysRateLimited) {
  Rig r;
  for (uint32_t n = 121; n <= 121 + config::kVocabPageItems * (config::kVocabManualSyncPagesPerHour + 50); n++) {
    r.lexirise.items.push_back({100 + n, 500 + n, 1, false, kSept2026Ms + n * 1000});
  }
  for (int press = 0; press < 2; press++) {
    ManualSync sync(r.store, r.api, joinOk, japaneseOnly());
    r.run(sync, 1000);
  }
  ASSERT_FALSE(r.store.manualBudgetLeft(1000));
  joins = 0;
  ManualSync spent(r.store, r.api, joinOk, japaneseOnly());
  EXPECT_EQ(spent.step(1000, kEpochS), ManualSync::Result::Failed);
  EXPECT_EQ(spent.error(), ApiError::RateLimited);
  EXPECT_EQ(joins, 0);  // no radio
}

namespace {
uint32_t wallS = 0;
uint32_t wallNow() { return wallS; }
}  // namespace

TEST(HomeSync, AColdBootsFirstPageGetsTheTimeItsCallSet) {
  Rig r;
  wallS = 0;  // the clock not set: the first page's TLS open sets it
  auto served = r.api.vocabServer;
  r.api.vocabServer = [served](const lexipoint::net::Request& request) {
    wallS = kEpochS;
    return served(request);
  };
  ManualSync sync(r.store, r.api, joinOk, japaneseOnly(), wallNow);
  ManualSync::Result result = ManualSync::Result::Running;
  for (int i = 0; i < 100 && result == ManualSync::Result::Running; i++) result = sync.step(1000, 0);
  EXPECT_EQ(result, ManualSync::Result::Synced);
  EXPECT_EQ(r.store.syncState(Language::Japanese).fullDoneS, kEpochS);  // the pass has its time
  EXPECT_EQ(r.store.syncState(Language::Japanese).lastSyncS, kEpochS);
  wallS = 0;
}

namespace {
ApiError rateLimited() { return ApiError::RateLimited; }
}  // namespace

TEST(HomeSync, APressWhileLexiriseRefusesCallsDoesntJoin) {
  Rig r;
  joins = 0;
  ManualSync sync(r.store, r.api, joinOk, japaneseOnly(), nullptr, rateLimited);
  EXPECT_EQ(sync.step(1000, kEpochS), ManualSync::Result::Failed);
  EXPECT_EQ(sync.error(), ApiError::RateLimited);
  EXPECT_EQ(joins, 0);
  EXPECT_FALSE(sync.joined());
  ManualSync fine(r.store, r.api, joinOk, japaneseOnly());
  fine.step(1000, kEpochS);
  EXPECT_TRUE(fine.joined());
}

namespace {
ManualSync::KeyHour keyHour{};
ManualSync::KeyHour keyHourNow() { return keyHour; }
}  // namespace

TEST(HomeSync, NearTheKeysHourlyLimitAPressDoesntJoinAndARunStopsBetweenPages) {
  Rig r;
  // 90% of 1200 is 1080: the page analysis and the idle pages spent that much already.
  keyHour = {1080, 1200};
  joins = 0;
  ManualSync spent(r.store, r.api, joinOk, japaneseOnly(), nullptr, nullptr, keyHourNow);
  EXPECT_EQ(spent.step(1000, kEpochS), ManualSync::Result::Failed);
  EXPECT_EQ(spent.error(), ApiError::RateLimited);
  EXPECT_EQ(joins, 0);  // no radio
  keyHour = {1079, 1200};
  ManualSync sync(r.store, r.api, joinOk, japaneseOnly(), nullptr, nullptr, keyHourNow);
  EXPECT_EQ(sync.step(1000, kEpochS), ManualSync::Result::Running);  // joined
  EXPECT_EQ(sync.step(1000, kEpochS), ManualSync::Result::Running);  // a page
  EXPECT_EQ(sync.pages(), 1u);
  keyHour = {1080, 1200};  // reached between pages: no page more, the run ends as a capped one does
  EXPECT_EQ(sync.step(1000, kEpochS), ManualSync::Result::Synced);
  EXPECT_EQ(sync.pages(), 1u);
  keyHour = {1080, 2400};  // /v1/me said more
  ManualSync more(r.store, r.api, joinOk, japaneseOnly(), nullptr, nullptr, keyHourNow);
  EXPECT_EQ(more.step(1000, kEpochS), ManualSync::Result::Running);
}
