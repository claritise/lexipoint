// The vocab mirror's sync against a simulated account (V7a, review round 3's simulation brought in): an account the
// user changes in the Lexirise app while the reader syncs (words added, changed, removed at level 0, sentence cards and
// words removed outright, many sharing one updated_at), live answers and the card's own writes, pages given up for
// input, failed pages, missed file writes and reboots from the last file written. Pages are applied straight to the
// pure sync (applyPage), any page size. After the churn stops, a full pass and an incremental one must leave the mirror
// equal to the account's words, field by field. Fixed seeds; pure, no network or SD card.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "lexirise/vocab/VocabMirror.h"

using namespace lexipoint;
using namespace lexipoint::vocab;

namespace {

constexpr uint64_t kStartMs = 1790208000000ULL;
constexpr uint32_t kStartS = 1790208000;

struct Item {
  uint32_t savedId = 0;
  uint32_t entryId = 0;
  int proficiency = 0;
  bool suspended = false;
  uint32_t nextReviewS = 0;
  uint64_t updatedMs = 0;
  bool word = true;
};

struct Mode {
  int tiePercent = 0;            // changes in the same millisecond as the one before
  bool incrementalChurn = true;  // the account changes during incremental passes too (not just full ones)
  bool live = true;              // live answers and the card's own writes
  int churnPercent = 30;         // pages the account changes before
  int maxChurn = 6;              // changes before such a page
  uint32_t pageItems = 50;
  int deletePercent = 0;   // changes that remove an item outright, before anything else is tried
  uint32_t pageMin = 0;    // the server's pages are cut short at random, down to this many items (0: never)
  int noCountPercent = 0;  // pages that come without languageCount while the account changes
  int rebootEvery = 0;     // a restart (every sleep is one) after this many pages, quiet or not (0: only at random)
};

class Sim {
 public:
  Sim(const uint32_t seed, const Mode mode) : rng_(seed), mode_(mode) {}

  void add(const bool word) {
    account_.push_back({nextSaved_++, nextEntry_++, static_cast<int>(rng_() % 5), rng_() % 10 == 0,
                        rng_() % 2 ? kStartS + static_cast<uint32_t>(rng_() % 100000) : 0u, tick(), word});
  }
  std::mt19937& rng() { return rng_; }
  void resyncDue() { epochS_ += config::kVocabResyncS; }

  // One idle window: the account may change (the server's clock moves on past every write before a page is read,
  // so updated_at is in commit order), then the page due, if any, is fetched and applied, the file written (or not:
  // a write that failed), and sometimes the reader reboots from the last file written.
  void step(const bool churning) {
    nowMs_ += 1000;
    epochS_ += 1;
    const std::optional<PagePlan> plan = nextPage(mirror_, run_, Language::Japanese, nowMs_, epochS_);
    if (!plan) {  // nothing due: time passes (the wall clock too)
      nowMs_ += config::kVocabSyncIntervalMs;
      epochS_ += static_cast<uint32_t>(config::kVocabSyncIntervalMs / 1000);
      return;
    }
    churning_ = churning;
    if (churning && (mode_.incrementalChurn || plan->pass == Pass::Full)) churn();
    PageCall call;
    call.plan = *plan;
    if (churning && rng_() % 12 == 0) {
      call.cancelled = true;
    } else if (churning && rng_() % 12 == 0) {
      call.error = api::ApiError::Network;
      call.sent = true;
    } else {
      call = page(*plan);
    }
    applyPage(mirror_, run_, call, nowMs_, epochS_);
    if (!(churning && rng_() % 10 == 0)) file_ = serializeMirror(mirror_, Language::Japanese);
    pagesSinceBoot_++;
    if ((churning && rng_() % 25 == 0) || (mode_.rebootEvery > 0 && pagesSinceBoot_ >= mode_.rebootEvery)) reboot();
  }

  // Quiet for a while (the account no longer changes): what's under way ends, incremental passes follow every
  // interval, and a full pass falls due soon after one that had no count, but no weekly pass comes.
  void quiet() {
    for (int round = 0; round < 4; round++) {
      for (int i = 0; i < 5000; i++) {  // until nothing's under way
        const std::optional<PagePlan> due = nextPage(mirror_, run_, Language::Japanese, nowMs_ + 1000, epochS_ + 1);
        if (!due && !mirror_.sync.full.running && !mirror_.sync.inc.running) break;
        step(false);
      }
      nowMs_ += config::kVocabSyncIntervalMs + 1;  // then an interval passes
      epochS_ += static_cast<uint32_t>(config::kVocabSyncIntervalMs / 1000) + 1;
    }
  }

  // What the mirror has wrong about the account's words: a missing or stale one; `exact`: an extra one too.
  std::vector<std::string> problems(const bool exact) const {
    std::vector<std::string> out;
    std::map<uint32_t, const Item*> words;
    for (const Item& item : account_) {
      if (item.word) words[item.entryId] = &item;
    }
    for (const auto& [entry, item] : words) {
      const Entry* e = mirror_.find(entry);
      if (!e) {
        out.push_back("missing " + std::to_string(entry));
      } else if (e->savedId != item->savedId || e->proficiency != item->proficiency ||
                 e->suspended != item->suspended || e->nextReviewS != item->nextReviewS) {
        out.push_back("stale " + std::to_string(entry));
      }
    }
    if (exact) {
      for (const Entry& e : mirror_.entries()) {
        if (!words.count(e.entryId)) out.push_back("extra " + std::to_string(e.entryId));
      }
    }
    return out;
  }

 private:
  uint64_t tick() {
    if (static_cast<int>(rng_() % 100) >= mode_.tiePercent) clockMs_ += 1 + rng_() % 1000;
    return clockMs_;
  }
  Item* someWord() {
    std::vector<size_t> words;
    for (size_t k = 0; k < account_.size(); k++) {
      if (account_[k].word) words.push_back(k);
    }
    return words.empty() ? nullptr : &account_[words[rng_() % words.size()]];
  }
  void liveAnswer(const Item& item) {
    applyLive(mirror_,
              LiveState{Language::Japanese, item.entryId, true, item.savedId, static_cast<uint8_t>(item.proficiency)});
  }
  void removeOne() {
    if (!account_.empty()) account_.erase(account_.begin() + static_cast<std::ptrdiff_t>(rng_() % account_.size()));
  }
  void churnOne() {
    if (static_cast<int>(rng_() % 100) < mode_.deletePercent) return removeOne();
    switch (rng_() % 10) {
      case 0:
        add(true);
        break;
      case 1:
        add(rng_() % 2 == 0);  // a word or a sentence card
        break;
      case 2:
      case 3:
        if (Item* w = someWord()) {  // changed in the app
          w->proficiency = static_cast<int>(rng_() % 5);
          w->suspended = rng_() % 5 == 0;
          w->nextReviewS = kStartS + static_cast<uint32_t>(rng_() % 100000);
          w->updatedMs = tick();
        }
        break;
      case 4:
        if (Item* w = someWord()) {  // a dictionary word's DELETE: it stays, at level 0
          w->proficiency = 0;
          w->updatedMs = tick();
        }
        break;
      case 5:
        removeOne();  // a sentence card, or anything removed outright
        break;
      case 6:
        if (Item* w = someWord(); w && mode_.live) liveAnswer(*w);
        break;
      case 7:
        if (mode_.live) {  // a live answer: an entry not in the account isn't saved
          const uint32_t entry = 1 + static_cast<uint32_t>(rng_() % (nextEntry_ + 5));
          const bool saved = std::any_of(account_.begin(), account_.end(),
                                         [entry](const Item& i) { return i.word && i.entryId == entry; });
          if (!saved) applyLive(mirror_, LiveState{Language::Japanese, entry, false, 0, 0});
        }
        break;
      case 8:
        if (Item* w = someWord(); w && mode_.live) {  // a level set on the card
          w->proficiency = static_cast<int>(rng_() % 5);
          w->updatedMs = tick();
          liveAnswer(*w);
        }
        break;
      default:
        if (mode_.live) {  // a new save on the card
          add(true);
          account_.back().nextReviewS = kStartS + static_cast<uint32_t>(rng_() % 100000);
          liveAnswer(account_.back());
        }
        break;
    }
  }
  void churn() {
    if (static_cast<int>(rng_() % 100) >= mode_.churnPercent) return;
    for (int c = 1 + static_cast<int>(rng_() % static_cast<uint32_t>(mode_.maxChurn)); c > 0; c--) churnOne();
  }
  PageCall page(const PagePlan& plan) {
    PageCall call;
    call.plan = plan;
    call.sent = true;
    clockMs_ += 1;  // a read comes after every write before it
    std::vector<Item> list = account_;
    std::stable_sort(list.begin(), list.end(), [](const Item& a, const Item& b) {
      return a.updatedMs != b.updatedMs ? a.updatedMs > b.updatedMs : a.savedId < b.savedId;  // stable ties
    });
    // The server's page: as many as asked for (an incremental pass's probe is smaller), at most its own page size,
    // sometimes cut short.
    const uint32_t served = mode_.pageMin
                                ? mode_.pageMin + static_cast<uint32_t>(rng_() % (mode_.pageItems - mode_.pageMin + 1))
                                : mode_.pageItems;
    const uint32_t items = std::min(plan.limit, served);
    for (uint32_t i = plan.offset; i < list.size() && i < plan.offset + items; i++) {
      api::VocabItem v;
      v.savedId = list[i].savedId;
      v.entryId = list[i].entryId;
      v.proficiency = list[i].proficiency;
      v.suspended = list[i].suspended;
      v.nextReviewS = list[i].nextReviewS;
      v.updatedMs = list[i].updatedMs;
      v.word = list[i].word;
      call.page.items.push_back(v);
      call.page.newestMs = std::max(call.page.newestMs, v.updatedMs);
    }
    if (plan.offset + items < list.size()) call.page.nextOffset = plan.offset + items;
    if (!(churning_ && static_cast<int>(rng_() % 100) < mode_.noCountPercent)) {
      call.page.languageCount = static_cast<uint32_t>(list.size());
    }
    return call;
  }
  void reboot() {
    Mirror loaded;
    if (!file_.empty()) ASSERT_TRUE(parseMirror(file_, Language::Japanese, loaded));
    mirror_ = loaded;
    run_ = RunState();
    pagesSinceBoot_ = 0;
  }

  std::mt19937 rng_;
  Mode mode_;
  std::vector<Item> account_;
  uint32_t nextSaved_ = 1;
  uint32_t nextEntry_ = 1;
  uint64_t clockMs_ = kStartMs;
  Mirror mirror_;
  RunState run_;
  std::string file_;  // the last file written
  bool churning_ = false;
  int pagesSinceBoot_ = 0;
  unsigned long nowMs_ = 1000;
  uint32_t epochS_ = kStartS;
};

// One account: churn through a first full pass and incremental ones (a weekly pass falling due now and then), then
// quiet (every word right); then a full pass and quiet again (the mirror equal to the account).
void runSeed(const uint32_t seed, const Mode& mode) {
  Sim sim(seed, mode);
  const int items = 60 + static_cast<int>(sim.rng()() % 300);
  for (int i = 0; i < items; i++) sim.add(sim.rng()() % 6 != 0);
  const int steps = 30 + static_cast<int>(sim.rng()() % 150);
  for (int i = 0; i < steps; i++) {
    if (sim.rng()() % 60 == 0) sim.resyncDue();
    sim.step(true);
  }
  sim.quiet();
  EXPECT_EQ(sim.problems(false), std::vector<std::string>{}) << "seed " << seed << ", once quiet";
  sim.resyncDue();
  sim.quiet();
  EXPECT_EQ(sim.problems(true), std::vector<std::string>{}) << "seed " << seed << ", after a quiet full pass";
}

constexpr uint32_t kSeeds = 60;

void runSeeds(const Mode& mode) {
  for (uint32_t seed = 1; seed <= kSeeds && !::testing::Test::HasFailure(); seed++) runSeed(seed, mode);
}

}  // namespace

TEST(VocabSyncSim, NoTies) { runSeeds(Mode{}); }

TEST(VocabSyncSim, ManyTies) { runSeeds(Mode{40, true, true, 30, 6, 50, 0, 0, 0, 0}); }

TEST(VocabSyncSim, ManyTiesSmallPagesAndDeletions) { runSeeds(Mode{60, true, true, 30, 6, 7, 20, 0, 0, 0}); }

TEST(VocabSyncSim, NoLiveAnswers) { runSeeds(Mode{0, true, false, 30, 6, 50, 0, 0, 0, 0}); }

TEST(VocabSyncSim, HeavyChurn) { runSeeds(Mode{0, true, false, 60, 12, 50, 0, 0, 0, 0}); }

TEST(VocabSyncSim, ChurnDuringFullPassesOnly) { runSeeds(Mode{0, false, false, 30, 6, 50, 0, 0, 0, 0}); }

TEST(VocabSyncSim, SmallPages) { runSeeds(Mode{0, true, false, 70, 10, 5, 0, 0, 0, 0}); }

TEST(VocabSyncSim, SmallPagesWithLiveAnswers) { runSeeds(Mode{0, true, true, 70, 10, 7, 0, 0, 0, 0}); }

TEST(VocabSyncSim, SmallPagesMostlyDeletions) { runSeeds(Mode{0, true, true, 70, 10, 6, 35, 0, 0, 0}); }

TEST(VocabSyncSim, MidPagesDeletions) { runSeeds(Mode{0, true, true, 50, 12, 20, 30, 0, 0, 0}); }

TEST(VocabSyncSim, ShortPagesWithDeletions) { runSeeds(Mode{40, true, true, 50, 6, 7, 30, 1, 0, 0}); }

TEST(VocabSyncSim, OneOrTwoItemPages) { runSeeds(Mode{40, true, true, 50, 6, 2, 30, 1, 0, 0}); }

TEST(VocabSyncSim, PagesSometimesWithoutACount) { runSeeds(Mode{30, true, true, 50, 8, 20, 15, 5, 10, 0}); }

TEST(VocabSyncSim, ARestartEveryFewPagesStillConverges) { runSeeds(Mode{20, true, true, 60, 12, 5, 20, 0, 0, 2}); }
