#pragma once

// The home screen's Sync Vocabulary (v0.2 V7b; claritise 2026-09-28: "put it on the home screen"; the look signed off
// on docs/v0.2/reference/v7b-home-sync.html). The reader's own request, so it may bring WiFi up (the saved network, as
// a card does: `join`, asked once, first); then, a page per step() (each a blocking call the home screen's loop makes
// with its popup and progress bar up), for each language switched on: the full pass while one is under way or due,
// then an incremental pass from the top to the cursor (VocabStore::manualNext), at most
// config::kVocabManualSyncPagesMax pages. Any input gives it up (stop(), or the page's cancel). Pure; tests:
// test/lexirise_vocab/ManualSyncTest.cpp.

#include <vector>

#include "VocabMirror.h"

namespace lexipoint::vocab {

// Whether the home screen shows the row: Lexirise on, a key set, a language switched on.
bool syncRowShown(const Settings& settings);

// What the home screen does with a sync on a loop pass (HomeActivity::loopVocabSync). Input comes as it happens:
// `pressed` a button or a finger went down this pass, `released` one came up, `held` one is still down. Stop the sync
// on a press; step once its frame is drawn (the reader sees every step's progress, and a tap lands on what's shown).
// The result is dismissed on the release of a press that began while it was on screen (R9: never on the press, whose
// release would then reach the menu: a Confirm starting a second sync, a Back opening the last book, a tap acting
// on the row under the popup), or once its time is up with nothing held; the release that follows a stopping press
// dismisses nothing, even when the press's edge comes only after the result is drawn (R10: one given up inside a call).
// The pass that dismisses is the release's own, so the menu never sees it.
enum class HomeSyncAction : uint8_t { Wait, Stop, Step, Dismiss };
class HomeSyncFlow {
 public:
  HomeSyncAction next(const bool pressed, const bool released, const bool held, const bool drawn, const bool running,
                      const bool resultOver) {
    if (running) {
      if (pressed) return HomeSyncAction::Stop;
      return drawn ? HomeSyncAction::Step : HomeSyncAction::Wait;
    }
    // A press on the result as shown, after a pass with it on screen and nothing held: its release dismisses it. A
    // press that stopped the sync inside a call (the call's abort) comes as an edge only now, maybe with the result
    // drawn already: nothing held has been seen since, so it doesn't arm.
    if (drawn && !held && !pressed) idleSeen_ = true;
    if (pressed && drawn && idleSeen_) armed_ = true;
    if (released && armed_) return HomeSyncAction::Dismiss;
    if (resultOver && !held && !pressed) return HomeSyncAction::Dismiss;
    return HomeSyncAction::Wait;
  }

 private:
  bool armed_ = false;
  bool idleSeen_ = false;  // a pass with the result on screen and nothing held
};

class ManualSync {
 public:
  using Join = api::ApiError (*)();  // bring WiFi up for the reader (LexiriseService::joinForUser on the device)
  // The wall clock (seconds; 0 while it isn't set), read after each page's call: a cold boot's first call sets it
  // (the TLS connection waits for SNTP), so that page still gets its time (R8).
  using WallClock = uint32_t (*)();
  // Lexirise refusing calls now (a 429's wait: RateLimited; a rejected key: Unauthorized; else None): asked before
  // the join, so a blocked press spares the radio (R9).
  using Blocked = api::ApiError (*)();
  // This reader's Lexirise requests in the last hour and the key's hourly limit (LexiriseService: every call counted,
  // the page analysis's too): at config::kVocabManualSyncStopPercent of it a press doesn't join (the card's "rate
  // limited" words) and a run stops between pages (R10).
  struct KeyHour {
    unsigned used = 0;
    uint32_t limit = config::kRateLimitDefault;
  };
  using KeyUse = KeyHour (*)();
  ManualSync(VocabStore& store, api::LexiriseApi& api, Join join, const Settings& settings, WallClock wall = nullptr,
             Blocked blocked = nullptr, KeyUse keyUse = nullptr);

  enum class Result : uint8_t { Running, UpToDate, Synced, NoWifi, Failed, Stopped };
  // One step: the join first, then one page. `cancel` (optional) is asked while a page waits and streams. `epochS`
  // plans the page (whether a full pass is due); the page is applied with the wall clock read after its call (else
  // `epochS`).
  Result step(unsigned long nowMs, uint32_t epochS, api::VocabPageReader::Cancel cancel = nullptr);
  void stop();  // the reader's input between steps
  Result result() const { return result_; }
  unsigned changed() const { return static_cast<unsigned>(changed_.size()); }  // words changed (distinct entries)
  unsigned percent();                                                          // 0-100, for the progress bar
  unsigned pages() const { return pages_; }
  api::ApiError error() const { return error_; }  // Failed: why (a rejected key, a rate limit: their own words)
  bool joinTried() const { return joinTried_; }
  bool joined() const { return joined_; }  // WiFi came up for it (a join that failed, or none tried: false)

 private:
  Result finish(Result r);
  bool keyHourLeft() const;  // below config::kVocabManualSyncStopPercent of the key's hour (no KeyUse: yes)
  VocabStore& store_;
  api::LexiriseApi& api_;
  Join join_;
  std::vector<Language> languages_;
  size_t at_ = 0;            // the language under way
  bool incStarted_ = false;  // its incremental pass began in this sync
  bool joinTried_ = false;
  bool joined_ = false;
  WallClock wall_ = nullptr;
  Blocked blocked_ = nullptr;
  KeyUse keyUse_ = nullptr;
  unsigned pages_ = 0;
  std::vector<uint32_t> changed_;
  Result result_ = Result::Running;
  api::ApiError error_ = api::ApiError::None;
};

}  // namespace lexipoint::vocab
