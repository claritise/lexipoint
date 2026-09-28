// v0.2 V7b R5: a page sentence's saved states, one rule (page::applyMirrorStates, vocab::mirrorOutranks): each word's
// state is the newer of the mirror's entry (its time) and the page's snapshot (its analysis time); a word the mirror
// doesn't hold is unsaved only once the mirror is complete as of a time after the page; with a time unknown only the
// reader's own writes outrank the snapshot. Every earlier review's scenario (R1 M1, R2 S2, R3 S1, R4 S1, R5 M1/M2) and
// the reviewers' probes, over one helper.

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "FakeApi.h"
#include "Fakes.h"
#include "VocabFixtures.h"
#include "lexirise/page/PageSentences.h"
#include "lexirise/vocab/VocabMirror.h"

using lexipoint::Language;
using lexipoint::fakes::FakeVocabItem;
using lexipoint::fakes::kSept2026Ms;
using lexipoint::vocab::LiveState;
using lexipoint::vocab::Pass;
using lexipoint::vocab::VocabStore;

namespace {

constexpr uint32_t kB = 1790300000;  // a wall-clock second the scenarios count from
constexpr uint32_t kWeek = 8 * 86400;
uint32_t nowS = 0;
uint32_t clockNow() { return nowS; }

// A reader: its SD card, its mirror (with the wall clock `nowS`), the account it syncs from.
struct World {
  lexipoint::fakes::FakeFiles files;
  lexipoint::fakes::FakeApi api;
  lexipoint::fakes::FakeVocabAccount account;
  std::unique_ptr<VocabStore> store;
  unsigned long ms = 1000;

  explicit World(const bool load = true, const bool clock = true) {
    nowS = 0;
    store = std::make_unique<VocabStore>(files);
    if (clock) store->setClock(clockNow);
    account.serve(api);
    if (load) store->load(Language::Japanese);
  }
  FakeVocabItem& item(const uint32_t savedId, const uint32_t entryId, const int level, const uint32_t updatedS) {
    account.items.push_back({savedId, entryId, level, false, static_cast<uint64_t>(updatedS) * 1000ULL});
    return account.items.back();
  }
  // Idle pages at `atS` until none is due (a full pass if due, then the incremental pass), or only the full pass.
  void sync(const uint32_t atS, const bool fullOnly = false) {
    nowS = atS;
    ms += 100000000UL;  // past any interval
    while (const auto plan = store->next(Language::Japanese, ms, atS)) {
      if (fullOnly && plan->pass != Pass::Full) break;
      store->apply(lexipoint::vocab::sendPage(api, *plan), ms, atS);
      ms += 1000;
    }
  }
  void record(const LiveState& state, const uint32_t atS) {
    nowS = atS;
    store->record({state});
  }
  void restart() {  // every sleep is a restart: the mirror from its file
    store->flush();
    store = std::make_unique<VocabStore>(files);
    store->setClock(clockNow);
    store->load(Language::Japanese);
  }
  // A card on a page analyzed at `analyzedS` (0: unknown) whose snapshot says `snapshot` (a level; nullopt: unsaved):
  // the level shown (nullopt: unsaved).
  std::optional<int> card(const uint32_t entry, const std::optional<int> snapshot, const uint32_t analyzedS) {
    lexipoint::api::AnalyzeResult r;
    lexipoint::api::Occurrence o;
    o.word = "本";
    o.entryId = entry;
    o.charEnd = 1;
    o.wordLike = true;
    r.occurrences.push_back(o);
    if (snapshot) {
      r.state[entry].savedExpressionId = "7";
      r.state[entry].proficiency = *snapshot;
    }
    lexipoint::page::applyMirrorStates(r, Language::Japanese, static_cast<uint64_t>(analyzedS) * 1000ULL, *store);
    const auto* state = r.stateFor(entry);
    return state ? std::optional<int>(state->proficiency) : std::nullopt;
  }
};

LiveState removal(const uint32_t entry, const bool own) {
  return LiveState{Language::Japanese, entry, false, 0, 0, 0, own};
}
LiveState level(const uint32_t entry, const uint32_t savedId, const uint8_t to, const bool own) {
  return LiveState{Language::Japanese, entry, true, savedId, to, 0, own};
}

struct Case {
  const char* name;
  std::function<std::optional<int>()> run;
  std::optional<int> shown;
};

}  // namespace

TEST(SavedStateRule, EveryScenarioOverOneRule) {
  const std::vector<Case> cases = {
      {"R1 M1: the reader's removal beats an older page",
       [] {
         World w;
         w.item(77, 3, 2, kB - 100);
         w.sync(kB);
         w.record(removal(3, true), kB + 200);
         return w.card(3, 2, kB + 100);
       },
       std::nullopt},
      {"R1 M1: ... across a restart",
       [] {
         World w;
         w.item(77, 3, 2, kB - 100);
         w.sync(kB);
         w.record(removal(3, true), kB + 200);
         w.restart();
         return w.card(3, 2, kB + 100);
       },
       std::nullopt},
      {"R2 S2: the reader's save before the mirror loads beats an older page",
       [] {
         World w(/*load=*/false);
         w.record(level(9, 99, 2, true), kB + 200);
         return w.card(9, std::nullopt, kB + 100);
       },
       2},
      {"R3 S1: a word saved in the app during a full pass keeps a page's saved snapshot",
       [] {
         World w;
         for (uint32_t n = 1; n <= 60; n++) w.item(100 + n, 500 + n, 1, kB - 1000 + n);
         nowS = kB;
         w.ms += 100000000UL;
         auto plan = w.store->next(Language::Japanese, w.ms, kB);
         w.store->apply(lexipoint::vocab::sendPage(w.api, *plan), w.ms, kB);  // the full pass's first page at B
         w.item(99, 9, 2, kB + 50);                                           // saved in the app: at the top, read
         while ((plan = w.store->next(Language::Japanese, w.ms += 1000, kB + 200)) && plan->pass == Pass::Full) {
           w.store->apply(lexipoint::vocab::sendPage(w.api, *plan), w.ms, kB + 200);
         }
         return w.card(9, 2, kB + 100);
       },
       2},
      {"R4 S1: an app deletion an answer saw beats an older page through a weekly full pass",
       [] {
         World w;
         w.item(77, 3, 2, kB - 100);
         w.item(78, 5, 2, kB - 200);
         w.sync(kB);
         w.account.items.pop_back();             // deleted outright in the app
         w.record(removal(5, false), kB + 150);  // a later answer lists it unsaved
         w.sync(kB + kWeek, /*fullOnly=*/true);
         return w.card(5, 2, kB + 100);
       },
       std::nullopt},
      {"R4 S1: ... and after the incremental pass that drops the removal",
       [] {
         World w;
         w.item(77, 3, 2, kB - 100);
         w.item(78, 5, 2, kB - 200);
         w.sync(kB);
         w.account.items.pop_back();
         w.record(removal(5, false), kB + 150);
         w.sync(kB + kWeek);
         EXPECT_FALSE(w.store->find(Language::Japanese, 5));  // dropped: the mirror is complete past it
         return w.card(5, 2, kB + 100);
       },
       std::nullopt},
      {"R5 M1: the reader's removal of a word the mirror lacks beats the page",
       [] {
         World w;
         w.record(removal(9, true), kB + 200);
         return w.card(9, 2, kB + 100);
       },
       std::nullopt},
      {"R5 M1: the reader's removal while the mirror isn't loaded, before the load",
       [] {
         World w(/*load=*/false);
         w.record(removal(9, true), kB + 200);
         return w.card(9, 2, kB + 100);
       },
       std::nullopt},
      {"R5 M1: ... and after the load",
       [] {
         World w(/*load=*/false);
         w.record(removal(9, true), kB + 200);
         w.store->load(Language::Japanese);
         return w.card(9, 2, kB + 100);
       },
       std::nullopt},
      {"R5 M2: the reader's level change survives a full pass reading its item",
       [] {
         World w;
         FakeVocabItem& it = w.item(77, 3, 2, kB - 100);
         (void)it;
         w.sync(kB);
         w.record(level(3, 77, 4, true), kB + 200);
         w.account.items[0].proficiency = 4;
         w.account.items[0].updatedMs = (static_cast<uint64_t>(kB) + 200) * 1000ULL;
         w.sync(kB + kWeek, /*fullOnly=*/true);
         return w.card(3, 2, kB + 100);
       },
       4},
      {"a page analyzed after the mirror's sync says the newer state",
       [] {
         World w;
         w.item(77, 3, 2, kB - 100);
         w.sync(kB);
         return w.card(3, 4, kB + 60);
       },
       4},
      {"a sync after the page says the newer state",
       [] {
         World w;
         w.item(77, 3, 2, kB - 100);
         w.sync(kB);
         return w.card(3, 4, kB - 60);
       },
       2},
      {"a word the mirror lacks, the mirror complete after the page: unsaved",
       [] {
         World w;
         w.item(77, 3, 2, kB - 100);
         w.sync(kB);
         return w.card(9, 2, kB - 60);
       },
       std::nullopt},
      {"a word the mirror lacks, the page newer than its completeness: the snapshot",
       [] {
         World w;
         w.item(77, 3, 2, kB - 100);
         w.sync(kB);
         return w.card(9, 2, kB + 60);
       },
       2},
      {"no clock: the reader's own write still wins",
       [] {
         World w(true, /*clock=*/false);
         w.record(level(9, 99, 4, true), 0);
         return w.card(9, 2, kB + 100);
       },
       4},
      {"no clock: an answer of unknown time leaves the page's snapshot",
       [] {
         World w(true, /*clock=*/false);
         w.record(level(9, 99, 4, false), 0);
         return w.card(9, 2, kB + 100);
       },
       2},
      {"a page of unknown time: a sync's entry leaves its snapshot",
       [] {
         World w;
         w.item(77, 3, 2, kB - 100);
         w.sync(kB);
         return w.card(3, 4, 0);
       },
       4},
      {"a page of unknown time: the reader's own write outranks it",
       [] {
         World w;
         w.item(77, 3, 2, kB - 100);
         w.sync(kB);
         w.record(level(3, 77, 3, true), kB + 10);
         return w.card(3, 4, 0);
       },
       3},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    EXPECT_EQ(c.run(), c.shown);
  }
}

// The reviewer's R5 probes, as they were written (the reader's removal is the reader's own write: `own`).
TEST(SavedStateRule, R5ProbeAReadersRemovalOfAWordTheMirrorLacksBeatsThePageSnapshot) {
  World w;
  EXPECT_EQ(w.card(9, 2, kB), 2);
  w.record(removal(9, true), kB + 1);
  EXPECT_EQ(w.card(9, 2, kB), std::nullopt);
}

// V7b R6 (a known limit, pinned): a change stamped earlier than the mirror's cursor (updated_at not in commit order)
// is missed by the incremental pass, which still makes the mirror complete as of its start: a page analyzed before
// that says the word saved, and its card shows it unsaved. It heals with a later answer (a card's, a page's) or the
// weekly full pass.
TEST(SavedStateRule, AChangeStampedBehindTheCursorIsMissedUntilALaterAnswerOrTheWeeklyPass) {
  World w;
  w.item(77, 3, 2, kB - 100);
  w.sync(kB);                  // the cursor: kB - 100
  w.item(99, 9, 2, kB - 500);  // saved in the app, but stamped before the cursor
  w.sync(kB + 900);            // the incremental pass stops at the cursor: 9 missed; complete as of kB + 900
  EXPECT_FALSE(w.store->find(Language::Japanese, 9));
  EXPECT_EQ(w.card(9, 2, kB + 600), std::nullopt);  // the page said saved; shown unsaved (the limit)
  w.record(level(9, 99, 2, false), kB + 1000);      // a later answer lists it
  EXPECT_EQ(w.card(9, 2, kB + 600), 2);
}

// V7b R7: at the mirror's cap (kVocabMirrorMax) a refused entry makes absence meaningless: the page's snapshot stands.
namespace {

void fill(World& w, const uint32_t atS) {
  std::vector<LiveState> saves;
  saves.reserve(lexipoint::config::kVocabMirrorMax);
  for (uint32_t n = 0; w.store->size(Language::Japanese) + saves.size() < lexipoint::config::kVocabMirrorMax; n++) {
    saves.push_back(level(1000000 + n, 2000000 + n, 1, true));
  }
  nowS = atS;
  w.store->record(saves);
  ASSERT_EQ(w.store->size(Language::Japanese), lexipoint::config::kVocabMirrorMax);
  ASSERT_FALSE(w.store->syncState(Language::Japanese).overflowed);  // full, but nothing refused yet
}

}  // namespace

TEST(SavedStateRule, AtTheCapAnAppSaveTheMirrorRefusesKeepsThePagesSnapshot) {
  World w;
  w.item(77, 3, 2, kB - 100);
  w.sync(kB);
  fill(w, kB + 10);
  w.item(99, 9, 2, kB + 50);  // saved in the app: the incremental pass can't keep it
  w.sync(kB + 900);
  EXPECT_FALSE(w.store->find(Language::Japanese, 9));
  EXPECT_TRUE(w.store->syncState(Language::Japanese).overflowed);
  EXPECT_EQ(w.card(9, 2, kB + 600), 2);  // not "absent, so unsaved": the snapshot
}

TEST(SavedStateRule, AtTheCapTheReadersOwnSaveTheMirrorRefusesKeepsThePagesSnapshot) {
  World w;
  w.item(77, 3, 2, kB - 100);
  w.sync(kB);
  fill(w, kB + 10);
  w.record(level(9, 99, 3, true), kB + 100);  // refused: the mirror is full
  EXPECT_TRUE(w.store->syncState(Language::Japanese).overflowed);
  w.item(99, 9, 3, kB + 100);
  w.sync(kB + 900);  // the incremental pass would make absence speak for the page analyzed before it
  EXPECT_EQ(w.card(9, 3, kB + 200), 3);
}

TEST(SavedStateRule, AFullPassThatRefusesNothingClearsTheOverflow) {
  World w;
  w.item(77, 3, 2, kB - 100);
  w.sync(kB);
  fill(w, kB + 10);
  w.record(level(9, 99, 3, true), kB + 100);
  ASSERT_TRUE(w.store->syncState(Language::Japanese).overflowed);
  // The weekly full pass: the account has one word, so it keeps it and sweeps the rest; nothing refused.
  w.sync(kB + kWeek);
  EXPECT_FALSE(w.store->syncState(Language::Japanese).overflowed);
  EXPECT_EQ(w.card(9, 2, kB + 200), std::nullopt);  // absence speaks again (the account doesn't have 9)
}

TEST(SavedStateRule, AnOverflowedMirrorKeepsARemovalAnOlderPageNeeds) {
  World w;
  w.item(77, 3, 2, kB - 100);
  w.item(78, 5, 2, kB - 200);
  w.sync(kB);
  fill(w, kB + 10);
  w.record(level(9, 99, 3, true), kB + 20);  // refused: overflowed
  ASSERT_TRUE(w.store->syncState(Language::Japanese).overflowed);
  w.account.items.pop_back();             // 5 removed outright in the app
  w.record(removal(5, false), kB + 300);  // a later answer lists it unsaved: a removal
  EXPECT_EQ(w.card(5, std::nullopt, kB + 200), std::nullopt);
  w.sync(kB + 900);  // an incremental pass from the top: complete as of kB + 900, but overflowed
  EXPECT_TRUE(w.store->find(Language::Japanese, 5));  // the removal stays
  EXPECT_EQ(w.card(5, 2, kB + 200), std::nullopt);    // an older page's saved snapshot doesn't come back
}
