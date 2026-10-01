#pragma once

// The vocab mirror (C13, v0.2 V7a; docs/v0.2/page-annotations.md §1.2): a copy of the user's Lexirise vocabulary on
// the SD card, one file per language, read-only from their account. It holds only what the card's saved state and
// V9's page marks need per saved word: the entry a save targets (`dictionary_id`, stateByEntryId's key), the saved
// expression's id, the level, `suspended` and the next review. Sentence cards aren't kept (they mark no word).
//
// Synced from GET /v1/vocabulary, newest change first, a page at a time (config::kVocabPageItems, streamed: a page is
// never held, api/VocabPage.h; an incremental pass's first page is a config::kVocabProbeItems probe, since the usual
// answer is "nothing new"): a full pass pages through everything, then incremental passes (both resumable across
// cards and boots: their progress is in the file) page from the newest until the first item older than the cursor (the
// newest updated_at the mirror holds everything up to), or the first one as old that the mirror already has. A pass
// that sees the list shrink by more than its page's slack reads again from where items may have moved to. An item
// deleted in Lexirise never shows in that order: a dictionary word's DELETE doesn't remove it (it stays, at level 0,
// with a new updated_at: the incremental pass takes it), but a gone item (a sentence card, or anything removed
// outright) is only noticed by the next full pass, which drops every entry it didn't see (config::kVocabResyncS after
// the last); meanwhile the live answer (analyze/text's stateByEntryId) wins for the words the card analyzes, and
// corrects the mirror (record()).
//
// The file (docs/v0.1/settings.md §3): a config::kVocabHeaderBytes header (magic "LXVM", version, the language, the
// sync's progress, a CRC-32 over everything), then config::kVocabRecordBytes records sorted by entry id. Pure parts
// plus the store; tests: test/lexirise_vocab.

#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lexirise/api/LexiriseApi.h"
#include "lexirise/api/Responses.h"
#include "lexirise/api/VocabPage.h"
#include "lexirise/settings/SafeFile.h"
#include "lexirise/settings/Settings.h"

namespace lexipoint::vocab {

constexpr const char* kLogTag = "LXVOCAB";  // the mirror's log lines, here and in the card activity

// One saved word, or (savedId 0, always `live`) a removal: the word isn't saved. Every entry carries the time its
// state was known (`asOfS`, V7b R5): a sync page's read time, a live answer's time, the reader's own write's time, a
// removal's time. A page analysis's snapshot and the entry: the newer says (page::applyMirrorStates). A removal is
// kept for an entry the mirror held, and for any entry the reader removed themselves (`own`); a page's item for the
// entry replaces it; a full pass's sweep never drops one; it's dropped once the mirror is complete as of a time past
// it (SyncState::lastSyncS: then the word's absence says unsaved). Bounded by the mirror's cap
// (config::kVocabMirrorMax entries in all, removals included: past it a new one isn't kept).
struct Entry {
  uint32_t entryId = 0;      // the entry a save targets (stateByEntryId's key)
  uint32_t savedId = 0;      // the saved expression's id (stateByEntryId's saved_expression_id); 0: a removal
  uint32_t nextReviewS = 0;  // seconds since the epoch; 0: none
  uint8_t proficiency = 0;   // 0-4
  bool suspended = false;    // suspended in Lexirise (V9: no mark); never the reader's own Ignore (C17)
  uint8_t mark = 0;          // the full pass that last saw it (an entry a finished pass didn't see is dropped)
  // Put by a card's live answer, not a page: its review time and suspension may be old or unknown, so a full pass
  // that reaches it still takes the page's item (a mark of this pass alone doesn't mean the pass has read it).
  bool live = false;
  // When this state was known (seconds since the epoch; 0: unknown, the clock wasn't set). Not part of whether an
  // entry changed (sameState): a page that reads it again only moves it on, in memory (the file's may read older,
  // never newer: an unchanged state was the same before).
  uint32_t asOfS = 0;
  bool own = false;  // the reader's own write on this device put it: with a time unknown, it still wins
  bool operator==(const Entry&) const = default;
  // The same state and bookkeeping, whatever its time (and whoever put it).
  bool sameState(const Entry& o) const {
    return entryId == o.entryId && savedId == o.savedId && nextReviewS == o.nextReviewS &&
           proficiency == o.proficiency && suspended == o.suspended && mark == o.mark && live == o.live;
  }
};

// Whether the mirror's word (known as of `asOfS`, `own` the reader's write) outranks a page analyzed at `analyzedMs`
// (ms since the epoch; 0: unknown): the newer says (the same second: the mirror). With either time unknown, the
// reader's own write still wins and anything else leaves the page's snapshot (the conservative way).
inline bool mirrorOutranks(const uint32_t asOfS, const bool own, const uint64_t analyzedMs) {
  if (asOfS != 0 && analyzedMs != 0) return static_cast<uint64_t>(asOfS) >= analyzedMs / timing::kMsPerSecond;
  return own;
}

// One pass's progress (a full pass's or an incremental one's; both kept in the file's header).
struct PassProgress {
  bool running = false;   // under way
  uint32_t offset = 0;    // its next page
  uint64_t newestMs = 0;  // the newest updated_at when it began: the cursor once it ends
  // Its list count at its last page (api::VocabPage::listCount): a drop is deletions, and the next page reads again
  // from where items may have moved to (VocabMirror.cpp rereadFrom).
  std::optional<uint32_t> lastCount;
  // Its next page's slack (rereadFrom): config::kVocabPageOverlap after a full page, less after a short one, 0 for a
  // re-read.
  uint8_t slack = config::kVocabPageOverlap;
  bool unbounded = false;  // a page of it (past the first) had no count
  // An incremental pass that has read again (rereadFrom): its later pages hold items it read before, anywhere, so an
  // item as old as the cursor and unchanged no longer says the rest of a tie was read; it ends only at an older item
  // or the list's end. Kept across a re-read sent back to the top, and in the file (a restart carries it on).
  bool reread = false;
  // Under way from its first page, whose newest updated_at is `newestMs`.
  void start(uint64_t newest) {
    *this = PassProgress();
    running = true;
    newestMs = newest;
  }
  bool operator==(const PassProgress&) const = default;
};

// The sync's progress (in the file's header).
struct SyncState {
  bool synced = false;     // a full pass has ended: the mirror holds the vocabulary as of cursorMs
  uint64_t cursorMs = 0;   // every change up to this updated_at (ms since the epoch) is in
  uint32_t fullDoneS = 0;  // when the last full pass ended (seconds since the epoch; 0: unknown)
  uint8_t generation = 0;  // the full pass's mark
  // The time the mirror is complete as of (seconds; 0: unknown): the first page's time of the last incremental pass
  // that ran from the top to its end (it holds every change made before it began, over a completed full pass). Not a
  // full pass's (an item changed during it moves to the top, already read: the incremental pass after it takes it),
  // nor a pass resumed after a restart (its start is gone: RunState). Kept in memory as a pass ends, in the file
  // whenever it's written (a quiet pass doesn't write it: after a restart it can read older, never newer).
  uint32_t lastSyncS = 0;
  // V7b R7: the mirror refused an entry at its cap (kVocabMirrorMax: a page's item or a live answer), so a word's
  // absence no longer says it's unsaved (page::applyMirrorStates then keeps a page's snapshot). Cleared when a full
  // pass ends having refused nothing (`fullRefused`: this full pass refused an item). Both in the header (offset 65).
  bool overflowed = false;
  bool fullRefused = false;
  bool resyncSoon = false;  // the next full pass was brought forward once; until a bounded pass, not again
  // The full pass under way (unbounded: it sweeps nothing when it ends), and the incremental one: every sleep is a deep
  // sleep (a restart), and a pass longer than one wake's pages carries on where it stopped, not at the top.
  PassProgress full;
  PassProgress inc;
  bool operator==(const SyncState&) const = default;
};

// One language's mirror in memory: the entries sorted by entry id (a binary search). sizeof(Entry) each; a large one
// lands in PSRAM on the device (malloc prefers it past 4 KB).
class Mirror {
 public:
  const Entry* find(uint32_t entryId) const;
  enum class Put : uint8_t { Added, Changed, Unchanged, Full };  // Full: a new entry past config::kVocabMirrorMax
  Put put(const Entry& entry);
  bool erase(uint32_t entryId);
  size_t sweep(uint8_t generation);  // drops the entries not marked with it, never a removal; how many
  // Removals no longer needed: known as of `completeAsOfS` or before, or an answer's of unknown time; how many.
  size_t dropRemovalsUpTo(uint32_t completeAsOfS);
  size_t dropOwnUnknownRemovals();  // the reader's own of unknown time (a full pass's end); how many
  size_t size() const { return entries_.size(); }
  const std::vector<Entry>& entries() const { return entries_; }
  void reserve(size_t n) { entries_.reserve(n); }
  SyncState sync;
  bool operator==(const Mirror&) const = default;

 private:
  std::vector<Entry> entries_;
};

// The file's bytes, and back. parseMirror is false for anything else (a wrong magic, version, language or size, a
// CRC that doesn't match, entries out of order or out of range): the store sets such a file aside and starts again.
std::string serializeMirror(const Mirror& mirror, Language language);
bool parseMirror(std::string_view bytes, Language language, Mirror& out);
const SafeFilePaths& mirrorFile(Language language);

// What the reader's own answers say about an entry: a live analyze/text answer (its stateByEntryId), or a write the
// card made. `saved` false: not in the vocabulary (the entry is dropped).
struct LiveState {
  Language language = Language::Japanese;
  uint32_t entryId = 0;
  bool saved = false;
  uint32_t savedId = 0;
  uint8_t proficiency = 0;
  uint32_t asOfS = 0;  // when it was known (0: now, as VocabStore::record takes it; still 0 with no clock)
  bool own = false;    // the reader's own write (a save, a level, a removal), not an answer
  bool operator==(const LiveState&) const = default;
};
// The live state of an entry the card holds (its saved state, as api::EntryState); nullopt when it can't be kept (no
// entry id, or a saved id that isn't a whole number: the mirror stores it as one).
std::optional<LiveState> liveStateOf(Language language, uint32_t entryId, const std::optional<api::EntryState>& saved);
// What an answer says of `id` (its state: saved; none: unsaved when the answer listed every state, `whole`), added to
// `out` once per entry (the card's analysis and a page's, V7b R5: one helper).
void addLiveState(std::vector<LiveState>& out, Language language, uint32_t id, const api::EntryState* state,
                  bool whole);
// The mirror's entry as a saved state: nullopt when it isn't saved (a removal).
std::optional<api::EntryState> savedStateOf(const Entry& entry);

// The next page to fetch.
enum class Pass : uint8_t { Full, Incremental };
struct PagePlan {
  Language language = Language::Japanese;
  Pass pass = Pass::Full;
  uint32_t offset = 0;
  uint32_t limit = config::kVocabPageItems;  // items asked for (an incremental pass's first page: a probe)
  bool startsIncremental = false;            // manualPage: this page begins the sync's incremental pass from the top
};
// One page's call and what came.
struct PageCall {
  PagePlan plan;
  api::ApiError error = api::ApiError::None;
  uint32_t retryAfterS = 0;
  bool sent = false;        // reached Lexirise (counts toward the budget)
  bool cancelled = false;   // given up for the reader's input (VocabPageReader's cancel): no page, no wait
  bool unreadable = false;  // a 2xx whose body couldn't be read
  bool probe = false;       // a card's probe as it settles (VocabStore::takeCardProbe, V7b): not the card's share
  bool manual = false;      // the home screen's sync (V7b): not in the idle pages' hourly budget
  api::VocabPage page;
  unsigned long tookMs = 0;  // the call's time, for the log
};
// Sends `plan`'s request and reads the page as it streams; `cancel` (optional) is asked before each piece of the body,
// and true gives the page up (PageCall::cancelled).
PageCall sendPage(api::LexiriseApi& api, const PagePlan& plan, api::VocabPageReader::Cancel cancel = nullptr);

// A language's sync between pages, in memory only (the passes' progress is in the mirror's SyncState, in the file).
struct RunState {
  bool incDone = false;  // an incremental pass ended since boot (or since the last full pass ended)
  unsigned long incDoneMs = 0;
  std::optional<unsigned long> waitUntilMs;  // after a failure or a refusal: no page before this
  uint32_t incStartS = 0;  // the incremental pass under way began from the top at this wall-clock time (0: unknown)
};

// A full pass is due: never synced, or config::kVocabResyncS since the last ended by the wall clock `epochS` (or the
// clock moved back past it).
bool fullPassDue(const SyncState& s, uint32_t epochS);

// The page due for `language` now; nullopt: none. A full pass while there's none yet (or one is under way, or the last
// ended config::kVocabResyncS ago by the wall clock `epochS`, when it's set); else an incremental pass at most every
// config::kVocabSyncIntervalMs, continued page by page. Nothing while a failure's wait lasts.
std::optional<PagePlan> nextPage(const Mirror& mirror, const RunState& run, Language language, unsigned long nowMs,
                                 uint32_t epochS);
// The page a sync the reader asked for (the home screen's Sync Vocabulary, V7b) fetches next: the full pass while one
// is under way or due (never synced, or kVocabResyncS since the last), then an incremental pass from the top (a probe
// first) to the cursor. No interval, no failure's wait (the reader asked now). `incStarted`: this sync's incremental
// pass began (its first page was fetched); nullopt: nothing left.
std::optional<PagePlan> manualPage(const Mirror& mirror, Language language, uint32_t epochS, bool incStarted);

// A page's answer into the mirror. A refusal (429, a rejected key) waits its retry time, any other failure
// config::kVocabFailureWaitMs, and changes nothing; a cancelled page changes nothing and waits nothing. True: the
// mirror (entries or progress) changed. `changedEntries` (optional): each entry whose saved state, level, suspension or
// review time the page added or changed, appended (a pass's new mark alone is written, not counted).
bool applyPage(Mirror& mirror, RunState& run, const PageCall& call, unsigned long nowMs, uint32_t epochS,
               std::vector<uint32_t>* changedEntries = nullptr);

// What VocabStore::apply did with a page: the file written, not needed (nothing changed, the language not loaded, or
// the page given up), or its write failed; and the entries the page added or changed (an open card redraws a word the
// page changed: V7b's probe as a card opens).
struct PageApplied {
  enum class File : uint8_t { Unchanged, Written, Failed } file = File::Unchanged;
  std::vector<uint32_t> changedEntries;
};
// A live answer into the mirror (the live answer wins). Changed: an entry changed (the file must be written); MarkOnly:
// the answer agrees, and an entry an older pass generation marked now carries this one's mark, in memory only (the
// next page's write carries it; lost to a restart, it's harmless: the running full pass reaches the item, or the
// incremental pass after it); None: nothing to do.
enum class LiveApplied : uint8_t { None, MarkOnly, Changed };
LiveApplied applyLive(Mirror& mirror, const LiveState& state);

// The device's mirrors (both languages) and their sync. Safe across tasks; loaded per language on first use outside
// RenderLock (load()), then asked from memory.
class VocabStore {
 public:
  explicit VocabStore(SettingsFiles& files) : files_(files) {}
  // The wall clock for record()'s answers and writes (seconds; 0 while it isn't set: timing::epochOrZero).
  using EpochClock = uint32_t (*)();
  void setClock(const EpochClock clock) { clock_ = clock; }

  void load(Language language);  // reads the file once (as a card opens: not under RenderLock)
  bool loaded(Language language);
  // The next page for `language`, within the budget (config::kVocabPagesPerHour); nullopt when it isn't loaded.
  std::optional<PagePlan> next(Language language, unsigned long nowMs, uint32_t epochS);
  // V7b (claritise, 2026-09-28: fresher saved states on the card): the probe a card makes once its answers are on
  // screen, an incremental pass's first page (config::kVocabProbeItems) from the top, for a loaded mirror that has
  // synced, with no pass under way and no failure's wait, at most once per config::kVocabCardProbeIntervalMs (all
  // cards), within the hourly page budget. takeCardProbe counts it made (its answer goes through apply()).
  bool cardProbeDue(Language language, unsigned long nowMs);
  // The home screen's sync (manualPage), outside the hourly page budget (the reader asked; ManualSync caps a run);
  // nullopt when the language isn't loaded or nothing's left.
  std::optional<PagePlan> manualNext(Language language, uint32_t epochS, bool incStarted);
  // Whether the home screen's sync may send another page within config::kVocabManualSyncPagesPerHour (its pages
  // counted by apply(), PageCall::manual).
  bool manualBudgetLeft(unsigned long nowMs);
  // How far the pass under way is, 0-100, from its offset and list count (nullopt: no pass under way or no count).
  std::optional<unsigned> passPercent(Language language);
  std::optional<PagePlan> takeCardProbe(Language language, unsigned long nowMs);
  // A page's answer: into memory, then the file (SD I/O: never under RenderLock). File::Failed: the memory keeps the
  // change, and the next write takes it.
  PageApplied apply(const PageCall& call, unsigned long nowMs, uint32_t epochS);
  // Live answers into memory (no SD I/O, so safe under RenderLock), then flush() writes. For a language not loaded
  // yet they wait (the newest config::kVocabPendingMax) and are applied when it is.
  void record(const std::vector<LiveState>& states);
  bool flush();  // writes the languages record() changed (SD I/O); false when a write failed
  bool dirty();  // record() changed something not yet written
  std::optional<Entry> find(Language language, uint32_t entryId);
  // The newest card answer or write waiting for `language` to load (record()), for the entry; nullopt: none (or it's
  // loaded: then find() says).
  std::optional<LiveState> pendingState(Language language, uint32_t entryId);
  // The open card's level changes the mirror doesn't hold yet (queued in their Undo window, or sent and not yet
  // record()ed), newest last: the reader's own newest choice for those entries, for the page marks only
  // (page::StoreSources; never the file, never a card's sentences, never a sync). The card (LiveSource) gives the whole
  // set each time it changes, and none as it goes. A change counts in revision() (the page under the card is worked out
  // again on its next frame).
  void setUnsent(const std::vector<LiveState>& states);
  std::optional<LiveState> unsentState(Language language, uint32_t entryId);  // the newest; nullopt: none
  // An entry's saved state from the mirror, for V9 (nothing calls it yet; nullopt: not saved, or not loaded).
  std::optional<api::EntryState> savedState(Language language, uint32_t entryId);
  SyncState syncState(Language language);
  size_t size(Language language);
  // Counts every load, page applied and live answer recorded (V9a: the marks under a card are worked out again when it
  // moves, not on every frame).
  uint32_t revision() const { return revision_.load(); }

 private:
  struct Slot {
    bool loaded = false;
    bool dirty = false;  // record() changed it since the last write
    Mirror mirror;
    RunState run;
  };
  Slot& slot(Language language) { return slots_[languageSlot(language)]; }
  void loadLocked(Language language);  // requires mutex_
  bool writeLocked(Language language);
  bool budgetLeftLocked(unsigned long nowMs);

  SettingsFiles& files_;
  EpochClock clock_ = nullptr;
  std::mutex mutex_;
  std::atomic<uint32_t> revision_{0};
  Slot slots_[std::size(kLanguages)];
  bool cardProbeDueLocked(Language language, unsigned long nowMs);
  std::vector<unsigned long> pageTimes_;          // pages fetched in the last hour
  std::vector<unsigned long> manualTimes_;        // the home screen's sync's pages in the last hour
  std::optional<unsigned long> lastCardProbeMs_;  // the last card probe (takeCardProbe)
  std::vector<LiveState> pending_;                // record()s for a language not loaded yet
  std::vector<LiveState> unsent_;                 // setUnsent()'s
};

// The device-wide store over the SD card (SettingsFilesHal.cpp; not linked into host tests).
VocabStore& vocabStore();

}  // namespace lexipoint::vocab
