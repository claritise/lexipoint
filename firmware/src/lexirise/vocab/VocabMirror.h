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
// sync's progress, a CRC-32 over everything), then 16-byte records sorted by entry id. Pure parts plus the store;
// tests: test/lexirise_vocab.

#include <cstdint>
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

// One saved word.
struct Entry {
  uint32_t entryId = 0;      // the entry a save targets (stateByEntryId's key)
  uint32_t savedId = 0;      // the saved expression's id (stateByEntryId's saved_expression_id)
  uint32_t nextReviewS = 0;  // seconds since the epoch; 0: none
  uint8_t proficiency = 0;   // 0-4
  bool suspended = false;    // suspended in Lexirise (V9: no mark); never the reader's own Ignore (C17)
  uint8_t mark = 0;          // the full pass that last saw it (an entry a finished pass didn't see is dropped)
  // Put by a card's live answer, not a page: its review time and suspension may be old or unknown, so a full pass
  // that reaches it still takes the page's item (a mark of this pass alone doesn't mean the pass has read it).
  bool live = false;
  bool operator==(const Entry&) const = default;
};

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
  bool synced = false;      // a full pass has ended: the mirror holds the vocabulary as of cursorMs
  uint64_t cursorMs = 0;    // every change up to this updated_at (ms since the epoch) is in
  uint32_t fullDoneS = 0;   // when the last full pass ended (seconds since the epoch; 0: unknown)
  uint8_t generation = 0;   // the full pass's mark
  bool resyncSoon = false;  // the next full pass was brought forward once; until a bounded pass, not again
  // The full pass under way (unbounded: it sweeps nothing when it ends), and the incremental one: every sleep is a deep
  // sleep (a restart), and a pass longer than one wake's pages carries on where it stopped, not at the top.
  PassProgress full;
  PassProgress inc;
  bool operator==(const SyncState&) const = default;
};

// One language's mirror in memory: the entries sorted by entry id (a binary search). 16 bytes each; a large one
// lands in PSRAM on the device (malloc prefers it past 4 KB).
class Mirror {
 public:
  const Entry* find(uint32_t entryId) const;
  enum class Put : uint8_t { Added, Changed, Unchanged, Full };  // Full: a new entry past config::kVocabMirrorMax
  Put put(const Entry& entry);
  bool erase(uint32_t entryId);
  size_t sweep(uint8_t generation);  // drops the entries not marked with it; how many
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
};
// The live state of an entry the card holds (its saved state, as api::EntryState); nullopt when it can't be kept (no
// entry id, or a saved id that isn't a whole number: the mirror stores it as one).
std::optional<LiveState> liveStateOf(Language language, uint32_t entryId, const std::optional<api::EntryState>& saved);
// The mirror's entry as a saved state (for V9's marks and the offline state they'll show; nothing calls it yet):
// nullopt when it isn't saved.
std::optional<api::EntryState> savedStateOf(const Entry& entry);

// The next page to fetch.
enum class Pass : uint8_t { Full, Incremental };
struct PagePlan {
  Language language = Language::Japanese;
  Pass pass = Pass::Full;
  uint32_t offset = 0;
  uint32_t limit = config::kVocabPageItems;  // items asked for (an incremental pass's first page: a probe)
};
// One page's call and what came.
struct PageCall {
  PagePlan plan;
  api::ApiError error = api::ApiError::None;
  uint32_t retryAfterS = 0;
  bool sent = false;        // reached Lexirise (counts toward the budget)
  bool cancelled = false;   // given up for the reader's input (VocabPageReader's cancel): no page, no wait
  bool unreadable = false;  // a 2xx whose body couldn't be read
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
};

// The page due for `language` now; nullopt: none. A full pass while there's none yet (or one is under way, or the last
// ended config::kVocabResyncS ago by the wall clock `epochS`, when it's set); else an incremental pass at most every
// config::kVocabSyncIntervalMs, continued page by page. Nothing while a failure's wait lasts.
std::optional<PagePlan> nextPage(const Mirror& mirror, const RunState& run, Language language, unsigned long nowMs,
                                 uint32_t epochS);
// A page's answer into the mirror. A refusal (429, a rejected key) waits its retry time, any other failure
// config::kVocabFailureWaitMs, and changes nothing; a cancelled page changes nothing and waits nothing. True: the
// mirror (entries or progress) changed.
bool applyPage(Mirror& mirror, RunState& run, const PageCall& call, unsigned long nowMs, uint32_t epochS);
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

  void load(Language language);  // reads the file once (as a card opens: not under RenderLock)
  bool loaded(Language language);
  // The next page for `language`, within the budget (config::kVocabPagesPerHour); nullopt when it isn't loaded.
  std::optional<PagePlan> next(Language language, unsigned long nowMs, uint32_t epochS);
  // A page's answer: into memory, then the file (SD I/O: never under RenderLock). False: the file wasn't written (the
  // memory keeps the change, and the next write takes it).
  bool apply(const PageCall& call, unsigned long nowMs, uint32_t epochS);
  // Live answers into memory (no SD I/O, so safe under RenderLock), then flush() writes. For a language not loaded
  // yet they wait (the newest config::kVocabPendingMax) and are applied when it is.
  void record(const std::vector<LiveState>& states);
  bool flush();  // writes the languages record() changed (SD I/O); false when a write failed
  bool dirty();  // record() changed something not yet written
  std::optional<Entry> find(Language language, uint32_t entryId);
  // An entry's saved state from the mirror, for V9 (nothing calls it yet; nullopt: not saved, or not loaded).
  std::optional<api::EntryState> savedState(Language language, uint32_t entryId);
  SyncState syncState(Language language);
  size_t size(Language language);

 private:
  struct Slot {
    bool loaded = false;
    bool dirty = false;  // record() changed it since the last write
    Mirror mirror;
    RunState run;
  };
  Slot& slot(Language language) { return slots_[language == Language::Japanese ? 0 : 1]; }
  void loadLocked(Language language);  // requires mutex_
  bool writeLocked(Language language);
  bool budgetLeftLocked(unsigned long nowMs);

  SettingsFiles& files_;
  std::mutex mutex_;
  Slot slots_[2];
  std::vector<unsigned long> pageTimes_;  // pages fetched in the last hour
  std::vector<LiveState> pending_;        // record()s for a language not loaded yet
};

// The device-wide store over the SD card (SettingsFilesHal.cpp; not linked into host tests).
VocabStore& vocabStore();

}  // namespace lexipoint::vocab
