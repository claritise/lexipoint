#include "VocabMirror.h"

#include <Logging.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>

#include "lexirise/api/AccessPolicy.h"
#include "lexirise/api/Requests.h"
#include "lexirise/util/ByteOrder.h"
#include "lexirise/util/Crc32.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::vocab {
namespace {

using bytes::crc32;
using bytes::get16;
using bytes::get32;
using bytes::get64;
using bytes::get8;
using bytes::put16;
using bytes::put32;
using bytes::put64;

constexpr char kMagic[4] = {'L', 'X', 'V', 'M'};
// 2: the incremental pass's progress; 3 (V7b R5): each record's time and own flag, 20 bytes. An older file is set
// aside and synced again (unreleased: no migration).
constexpr uint16_t kVersion = 3;
constexpr uint8_t kFlagSynced = 0x01;
constexpr uint8_t kFlagFullRunning = 0x02;
constexpr uint8_t kFlagFullCounted = 0x04;    // the running full pass's list count is known
constexpr uint8_t kFlagFullUnbounded = 0x08;  // a page of it said no count: it sweeps nothing
// Bits 4-5: how far short of config::kVocabPageOverlap the running full pass's next page's slack is (0: the whole
// overlap; the overlap itself: a re-read, rereadFrom).
// The incremental pass's flags byte (kAtIncFlags): under way, its count set, unbounded, and its slack as bits 4-5.
constexpr uint8_t kIncFlagRunning = 0x01;
constexpr uint8_t kIncFlagCounted = 0x02;
constexpr uint8_t kIncFlagUnbounded = 0x04;
constexpr uint8_t kIncFlagReread = 0x08;   // it has read again (PassProgress::reread); 0 in files written before it
constexpr uint8_t kFlagResyncSoon = 0x40;  // the next full pass was brought forward (resyncSoon)
constexpr int kFlagSlackShift = 4;
constexpr uint8_t kFlagSlackMask = 0x30;
static_assert(config::kVocabPageOverlap <= (kFlagSlackMask >> kFlagSlackShift), "the slack fits its flag bits");
static_assert(config::kVocabPageItems > config::kVocabPageOverlap, "a full page leaves the whole overlap");
// A record's flags byte (not the header's flags).
constexpr uint8_t kRecFlagSuspended = 0x01;
constexpr uint8_t kRecFlagLive = 0x02;  // a record put by a card's live answer (Entry::live)
constexpr uint8_t kRecFlagOwn = 0x04;   // the reader's own write (Entry::own)

// Where a pass's progress sits in the header, and its flag bits (the full pass's share the header's flags byte).
struct PassLayout {
  size_t offset, newest, count, flags;
  uint8_t running, counted, unbounded, reread;  // reread: 0 for the full pass (it has no tie stop)
};

// The header's fields, by offset (little-endian).
constexpr size_t kAtVersion = 4;
constexpr size_t kAtRecordBytes = 6;
constexpr size_t kAtLanguage = 8;
constexpr size_t kAtFlags = 10;
constexpr size_t kAtGeneration = 11;
constexpr size_t kAtCount = 12;
constexpr size_t kAtCursor = 16;
constexpr size_t kAtFullNewest = 24;
constexpr size_t kAtFullOffset = 32;
constexpr size_t kAtFullDone = 36;
constexpr size_t kAtFullCount = 40;
constexpr size_t kAtIncOffset = 44;  // the incremental pass under way (version 2)
constexpr size_t kAtIncNewest = 48;
constexpr size_t kAtIncCount = 56;
constexpr size_t kAtIncFlags = 60;
constexpr size_t kAtLastSync = 61;   // SyncState::lastSyncS (V7b R1; 0 in files written before it)
constexpr size_t kAtMoreFlags = 65;  // V7b R7: 1 overflowed, 2 the full pass under way refused an item; then two spare
constexpr uint8_t kMoreOverflowed = 0x01;
constexpr uint8_t kMoreFullRefused = 0x02;
constexpr size_t kAtCrc = 68;
static_assert(kAtCrc + 4 == config::kVocabHeaderBytes, "the header ends with its CRC");
constexpr PassLayout kFullLayout{kAtFullOffset,    kAtFullNewest,    kAtFullCount,       kAtFlags,
                                 kFlagFullRunning, kFlagFullCounted, kFlagFullUnbounded, 0};
constexpr PassLayout kIncLayout{kAtIncOffset,    kAtIncNewest,    kAtIncCount,       kAtIncFlags,
                                kIncFlagRunning, kIncFlagCounted, kIncFlagUnbounded, kIncFlagReread};
static_assert(config::kLanguageCodeBytes == 2, "the header keeps a two-letter language code");
// A record's fields, by offset: entry id, saved id, next review (u32 each), level, flags, mark, one spare byte.
constexpr size_t kRecAtEntry = 0;
constexpr size_t kRecAtSaved = 4;
constexpr size_t kRecAtReview = 8;
constexpr size_t kRecAtAsOf = 12;
constexpr size_t kRecAtLevel = 16;
constexpr size_t kRecAtFlags = 17;
constexpr size_t kRecAtMark = 18;
constexpr size_t kRecordFields = kRecAtMark + 2;  // the mark, then one spare byte
static_assert(kRecordFields == config::kVocabRecordBytes, "a record is 20 bytes");

// A pass's progress into the header (its flag bits OR'd into the flags byte `out` already holds), and back; false:
// a slack past the overlap (not a file serializeMirror writes).
void putPass(std::string& out, const PassLayout& at, const PassProgress& p) {
  put32(out, at.offset, p.offset);
  put64(out, at.newest, p.newestMs);
  put32(out, at.count, p.lastCount.value_or(0));
  const uint32_t shortOfOverlap = config::kVocabPageOverlap - std::min<uint32_t>(p.slack, config::kVocabPageOverlap);
  out[at.flags] = static_cast<char>(static_cast<uint8_t>(out[at.flags]) | (p.running ? at.running : 0) |
                                    (p.lastCount ? at.counted : 0) | (p.unbounded ? at.unbounded : 0) |
                                    (p.reread ? at.reread : 0) | (shortOfOverlap << kFlagSlackShift));
}
bool getPass(const std::string_view in, const PassLayout& at, PassProgress& p) {
  const uint8_t flags = get8(in, at.flags);
  const uint32_t shortOfOverlap = static_cast<uint32_t>(flags & kFlagSlackMask) >> kFlagSlackShift;
  if (shortOfOverlap > config::kVocabPageOverlap) return false;
  p.running = (flags & at.running) != 0;
  p.offset = get32(in, at.offset);
  p.newestMs = get64(in, at.newest);
  if ((flags & at.counted) != 0) p.lastCount = get32(in, at.count);
  p.unbounded = (flags & at.unbounded) != 0;
  p.reread = at.reread != 0 && (flags & at.reread) != 0;
  p.slack = static_cast<uint8_t>(config::kVocabPageOverlap - shortOfOverlap);
  return true;
}

bool isRefusal(const api::ApiError error) {
  return error == api::ApiError::RateLimited || error == api::ApiError::Unauthorized;
}

const char* passName(const Pass pass) { return pass == Pass::Full ? "full" : "incremental"; }

// An incremental page's size: a probe at the top, else a whole page.
uint32_t pageLimit(const uint32_t offset) { return offset == 0 ? config::kVocabProbeItems : config::kVocabPageItems; }

// When the list shrank since the last page (`lastCount` to `count`: deletions), where to read again from so that no
// item is skipped. Deletions before this page moved everything after them up by at most the drop: an addition or a
// change moves an item to the top (a shift down: a duplicate, never a skip), so deletions the additions hid move items
// up no more than the count says. This page started `slack` items before the end of the page it follows (up to
// config::kVocabPageOverlap; less after a short page; none for a re-read, which starts anywhere): a drop up to that is
// covered, and past it the pass reads again from drop - slack before this page's start. This relies on the count being
// of the very list paged (languageCount, sentence cards included: api::VocabPage::listCount). nullopt: nothing to read
// again.
std::optional<uint32_t> rereadFrom(const uint32_t offset, const std::optional<uint32_t> lastCount,
                                   const std::optional<uint32_t> count, const uint32_t slack) {
  if (!lastCount || !count || *count >= *lastCount || offset == 0) return std::nullopt;
  const uint32_t drop = *lastCount - *count;
  if (drop <= slack) return std::nullopt;
  const uint32_t back = drop - slack;
  return offset > back ? offset - back : 0;
}

// Where the page after `plan`'s starts: the server's next offset, a few items back (an item deleted meanwhile moves
// the rest up: config::kVocabPageOverlap), never at or before this page's start. nullopt: this was the last page.
std::optional<uint32_t> followingOffset(const PagePlan& plan, const api::VocabPage& page) {
  if (!page.nextOffset || page.items.empty() || *page.nextOffset <= plan.offset) return std::nullopt;
  const uint32_t back = *page.nextOffset > config::kVocabPageOverlap ? *page.nextOffset - config::kVocabPageOverlap : 0;
  return std::max(back, plan.offset + 1);
}

// Where the next page starts and its slack (rereadFrom): a re-read has none; a following page starts before the
// server's next offset by the overlap, or less when this page was short (it never starts at or before this one).
struct NextPage {
  std::optional<uint32_t> offset;  // nullopt: this was the last page
  uint8_t slack = config::kVocabPageOverlap;
  bool reread = false;
};
NextPage nextAfter(const PagePlan& plan, const api::VocabPage& page, const std::optional<uint32_t> reread) {
  if (reread) return {reread, 0, true};
  const std::optional<uint32_t> following = followingOffset(plan, page);
  if (!following) return {std::nullopt, config::kVocabPageOverlap, false};
  return {following, static_cast<uint8_t>(*page.nextOffset - *following), false};
}

// A pass that couldn't bound its deletions (a page without a count) may have skipped a word: the next full pass falls
// due one incremental interval from now (by the wall clock), not a week on.
// Once: when the pass brought forward can't bound its deletions either (Lexirise no longer sends a count), the weekly
// rule stands, rather than a full pass every interval.
void resyncSoon(SyncState& s, const uint32_t epochS) {
  constexpr uint32_t kSoonS = static_cast<uint32_t>(config::kVocabSyncIntervalMs / timing::kMsPerSecond);
  static_assert(config::kVocabResyncS > kSoonS, "soon comes before the weekly pass");
  if (epochS < static_cast<uint32_t>(config::kMinValidEpochS)) return;  // no clock: the weekly rule stands
  if (s.resyncSoon) {
    LOG_INF(kLogTag, "still no list count: the next full pass comes by the weekly rule");
    return;
  }
  s.resyncSoon = true;
  s.fullDoneS = epochS - (config::kVocabResyncS - kSoonS);
}

// Whether putting `entry` changes what the reader sees of the word (saved or not, its level, suspended, its review
// time): a new pass's mark or the live flag alone don't (they're the file's bookkeeping, written all the same).
bool visiblyChanges(const Entry* known, const Entry& entry) {
  return !known || known->savedId != entry.savedId || known->proficiency != entry.proficiency ||
         known->suspended != entry.suspended || known->nextReviewS != entry.nextReviewS;
}

// When a page's item was known: the page's read time, else (no clock) its updated_at, a time it was true at least.
uint32_t readAsOf(const api::VocabItem& item, const uint32_t epochS) {
  if (epochS >= static_cast<uint32_t>(config::kMinValidEpochS)) return epochS;
  return static_cast<uint32_t>(item.updatedMs / timing::kMsPerSecond);
}

Entry entryOf(const api::VocabItem& item, const uint8_t mark, const uint32_t asOfS) {
  Entry e;
  e.entryId = item.entryId;
  e.savedId = item.savedId;
  e.nextReviewS = item.nextReviewS;
  e.proficiency = static_cast<uint8_t>(item.proficiency);
  e.suspended = item.suspended;
  e.mark = mark;
  e.asOfS = asOfS;
  return e;
}

}  // namespace

const Entry* Mirror::find(const uint32_t entryId) const {
  const auto it = std::lower_bound(entries_.begin(), entries_.end(), entryId,
                                   [](const Entry& e, const uint32_t id) { return e.entryId < id; });
  return it != entries_.end() && it->entryId == entryId ? &*it : nullptr;
}

Mirror::Put Mirror::put(const Entry& entry) {
  const auto it = std::lower_bound(entries_.begin(), entries_.end(), entry.entryId,
                                   [](const Entry& e, const uint32_t id) { return e.entryId < id; });
  if (it != entries_.end() && it->entryId == entry.entryId) {
    if (it->sameState(entry)) {
      // The same state known again: only its time moves on (in memory: not a change the file must record).
      if (entry.asOfS > it->asOfS) {
        it->asOfS = entry.asOfS;
        it->own = entry.own;
      }
      return Put::Unchanged;
    }
    *it = entry;
    return Put::Changed;
  }
  if (entries_.size() >= config::kVocabMirrorMax) return Put::Full;
  entries_.insert(it, entry);
  return Put::Added;
}

bool Mirror::erase(const uint32_t entryId) {
  const auto it = std::lower_bound(entries_.begin(), entries_.end(), entryId,
                                   [](const Entry& e, const uint32_t id) { return e.entryId < id; });
  if (it == entries_.end() || it->entryId != entryId) return false;
  entries_.erase(it);
  return true;
}

size_t Mirror::dropOwnUnknownRemovals() {
  const size_t before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [](const Entry& e) { return e.savedId == 0 && e.own && e.asOfS == 0; }),
                 entries_.end());
  return before - entries_.size();
}

size_t Mirror::sweep(const uint8_t generation) {
  const size_t before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [generation](const Entry& e) { return e.mark != generation && e.savedId != 0; }),
                 entries_.end());
  return before - entries_.size();
}

size_t Mirror::dropRemovalsUpTo(const uint32_t completeAsOfS) {
  const size_t before = entries_.size();
  // A removal known as of the mirror's completeness or before (its absence now says the same), or of unknown time but
  // an answer's; the reader's own of unknown time stays (it outranks any page) until a full pass ends (dropOwnUnknown).
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [completeAsOfS](const Entry& e) {
                                  return e.savedId == 0 &&
                                         ((e.asOfS == 0 && !e.own) || (e.asOfS != 0 && e.asOfS <= completeAsOfS));
                                }),
                 entries_.end());
  return before - entries_.size();
}

const SafeFilePaths& mirrorFile(const Language language) {
  return language == Language::Japanese ? config::kVocabFileJa : config::kVocabFileZh;
}

std::string serializeMirror(const Mirror& mirror, const Language language) {
  const std::vector<Entry>& entries = mirror.entries();
  std::string out(config::kVocabHeaderBytes + entries.size() * config::kVocabRecordBytes, '\0');
  std::memcpy(out.data(), kMagic, sizeof(kMagic));
  put16(out, kAtVersion, kVersion);
  put16(out, kAtRecordBytes, config::kVocabRecordBytes);
  const char* code = languageCode(language);
  out[kAtLanguage] = code[0];
  out[kAtLanguage + 1] = code[1];
  const SyncState& s = mirror.sync;
  out[kAtFlags] = static_cast<char>((s.synced ? kFlagSynced : 0) | (s.resyncSoon ? kFlagResyncSoon : 0));
  out[kAtGeneration] = static_cast<char>(s.generation);
  put32(out, kAtCount, static_cast<uint32_t>(entries.size()));
  put64(out, kAtCursor, s.cursorMs);
  put32(out, kAtFullDone, s.fullDoneS);
  put32(out, kAtLastSync, s.lastSyncS);
  out[kAtMoreFlags] = static_cast<char>((s.overflowed ? kMoreOverflowed : 0) | (s.fullRefused ? kMoreFullRefused : 0));
  putPass(out, kFullLayout, s.full);
  putPass(out, kIncLayout, s.inc);
  size_t at = config::kVocabHeaderBytes;
  for (const Entry& e : entries) {
    put32(out, at + kRecAtEntry, e.entryId);
    put32(out, at + kRecAtSaved, e.savedId);
    put32(out, at + kRecAtReview, e.nextReviewS);
    put32(out, at + kRecAtAsOf, e.asOfS);
    out[at + kRecAtLevel] = static_cast<char>(e.proficiency);
    out[at + kRecAtFlags] = static_cast<char>((e.suspended ? kRecFlagSuspended : 0) | (e.live ? kRecFlagLive : 0) |
                                              (e.own ? kRecFlagOwn : 0));
    out[at + kRecAtMark] = static_cast<char>(e.mark);
    at += config::kVocabRecordBytes;
  }
  const std::string_view view(out);
  put32(out, kAtCrc, crc32(view.substr(0, kAtCrc), view.substr(config::kVocabHeaderBytes)));
  return out;
}

bool parseMirror(const std::string_view bytes, const Language language, Mirror& out) {
  if (bytes.size() < config::kVocabHeaderBytes || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) return false;
  const char* code = languageCode(language);
  if (get16(bytes, kAtVersion) != kVersion || get16(bytes, kAtRecordBytes) != config::kVocabRecordBytes ||
      bytes[kAtLanguage] != code[0] || bytes[kAtLanguage + 1] != code[1]) {
    return false;
  }
  const uint32_t count = get32(bytes, kAtCount);
  if (count > config::kVocabMirrorMax ||
      bytes.size() != config::kVocabHeaderBytes + static_cast<size_t>(count) * config::kVocabRecordBytes) {
    return false;
  }
  if (get32(bytes, kAtCrc) != crc32(bytes.substr(0, kAtCrc), bytes.substr(config::kVocabHeaderBytes))) return false;
  Mirror parsed;
  const uint8_t flags = get8(bytes, kAtFlags);
  parsed.sync.synced = (flags & kFlagSynced) != 0;
  parsed.sync.resyncSoon = (flags & kFlagResyncSoon) != 0;
  parsed.sync.generation = get8(bytes, kAtGeneration);
  parsed.sync.cursorMs = get64(bytes, kAtCursor);
  parsed.sync.fullDoneS = get32(bytes, kAtFullDone);
  parsed.sync.lastSyncS = get32(bytes, kAtLastSync);
  parsed.sync.overflowed = (get8(bytes, kAtMoreFlags) & kMoreOverflowed) != 0;
  parsed.sync.fullRefused = (get8(bytes, kAtMoreFlags) & kMoreFullRefused) != 0;
  if (!getPass(bytes, kFullLayout, parsed.sync.full) || !getPass(bytes, kIncLayout, parsed.sync.inc)) return false;
  parsed.reserve(count);
  size_t at = config::kVocabHeaderBytes;
  uint32_t last = 0;
  for (uint32_t i = 0; i < count; i++, at += config::kVocabRecordBytes) {
    Entry e;
    e.entryId = get32(bytes, at + kRecAtEntry);
    e.savedId = get32(bytes, at + kRecAtSaved);
    e.nextReviewS = get32(bytes, at + kRecAtReview);
    e.proficiency = get8(bytes, at + kRecAtLevel);
    e.suspended = (get8(bytes, at + kRecAtFlags) & kRecFlagSuspended) != 0;
    e.live = (get8(bytes, at + kRecAtFlags) & kRecFlagLive) != 0;
    e.own = (get8(bytes, at + kRecAtFlags) & kRecFlagOwn) != 0;
    e.asOfS = get32(bytes, at + kRecAtAsOf);
    e.mark = get8(bytes, at + kRecAtMark);
    // Sorted, unique, in range (a removal is always live): anything else wasn't written by serializeMirror.
    if (e.entryId <= last || (e.savedId == 0 && !e.live) || e.proficiency > config::kMaxProficiency) return false;
    last = e.entryId;
    parsed.put(e);  // appends: each is past the last
  }
  out = std::move(parsed);
  return true;
}

std::optional<LiveState> liveStateOf(const Language language, const uint32_t entryId,
                                     const std::optional<api::EntryState>& saved) {
  if (entryId == 0) return std::nullopt;
  LiveState state{language, entryId, false, 0, 0};
  if (!saved || saved->savedExpressionId.empty()) return state;  // not in the vocabulary
  const std::string& id = saved->savedExpressionId;
  uint32_t savedId = 0;
  const auto [end, error] = std::from_chars(id.data(), id.data() + id.size(), savedId);
  if (error != std::errc() || end != id.data() + id.size() || savedId == 0) return std::nullopt;
  if (saved->proficiency < 0 || saved->proficiency > static_cast<int>(config::kMaxProficiency)) return std::nullopt;
  state.saved = true;
  state.savedId = savedId;
  state.proficiency = static_cast<uint8_t>(saved->proficiency);
  return state;
}

void addLiveState(std::vector<LiveState>& out, const Language language, const uint32_t id, const api::EntryState* state,
                  const bool whole) {
  if (id == 0 || (!state && !whole)) return;
  if (std::any_of(out.begin(), out.end(), [id](const LiveState& l) { return l.entryId == id; })) return;
  const std::optional<api::EntryState> saved = state ? std::optional<api::EntryState>(*state) : std::nullopt;
  if (const std::optional<LiveState> l = liveStateOf(language, id, saved)) out.push_back(*l);
}

std::optional<api::EntryState> savedStateOf(const Entry& entry) {
  if (entry.savedId == 0) return std::nullopt;
  api::EntryState state;
  char digits[std::numeric_limits<uint32_t>::digits10 + 2];  // any uint32_t
  state.savedExpressionId.assign(digits, std::to_chars(digits, digits + sizeof(digits), entry.savedId).ptr);
  state.proficiency = entry.proficiency;
  return state;
}

LiveApplied applyLive(Mirror& mirror, const LiveState& state) {
  const Entry* known = mirror.find(state.entryId);
  // The mirror knows this entry as of later than the answer (a sync page read since, or a newer write): nothing.
  if (known && known->asOfS != 0 && state.asOfS != 0 && known->asOfS > state.asOfS) return LiveApplied::None;
  if (!state.saved) {
    // Kept as a removal (not erased): a page analyzed before it may still have the word saved in its snapshot, and
    // the mirror must speak for it (page::applyMirrorStates). For an entry the mirror had, and for any the reader
    // removed themselves; not for every word an answer lists as unsaved (most of a page: the mirror would fill up).
    if (known && known->savedId == 0) {
      if (state.asOfS <= known->asOfS && !(state.own && !known->own)) return LiveApplied::None;
      Entry later = *known;  // a removal known later (or now the reader's own): in memory, as the time is
      later.asOfS = std::max(known->asOfS, state.asOfS);
      later.own = known->own || state.own;
      mirror.put(later);
      return LiveApplied::MarkOnly;
    }
    if (!known && !state.own) return LiveApplied::None;
    const Mirror::Put put =
        mirror.put(Entry{state.entryId, 0, 0, 0, false, mirror.sync.generation, true, state.asOfS, state.own});
    if (put == Mirror::Put::Full) mirror.sync.overflowed = true;  // written: the absence no longer speaks
    return LiveApplied::Changed;
  }
  Entry entry;
  if (known && known->savedId == state.savedId && known->proficiency == state.proficiency) {
    // The answer agrees. It moves the entry's time on, and with an older mark (a running full pass hasn't reached it,
    // or a pass that couldn't sweep left it) takes this generation's, live (the pass still takes the page's item): in
    // memory only, so a card agreeing with the mirror doesn't cost a whole-file write. With this generation's mark and
    // no newer time: nothing (it stays a page's entry, so a tie at the cursor still reads Unchanged).
    const bool newer = state.asOfS > known->asOfS || (state.own && !known->own);
    if (known->mark == mirror.sync.generation && !newer) return LiveApplied::None;
    Entry marked = *known;
    if (known->mark != mirror.sync.generation) {
      marked.mark = mirror.sync.generation;
      marked.live = true;
    }
    marked.asOfS = std::max(known->asOfS, state.asOfS);
    marked.own = known->own || state.own;
    mirror.put(marked);
    return LiveApplied::MarkOnly;
  }
  if (known && known->savedId == state.savedId) entry = *known;  // the same item: its review time and suspension stay
  entry.entryId = state.entryId;
  entry.savedId = state.savedId;
  entry.proficiency = state.proficiency;
  entry.mark = mirror.sync.generation;  // seen now: a full pass under way keeps it (and still takes its item)
  entry.live = true;
  entry.asOfS = state.asOfS;
  entry.own = state.own;
  const Mirror::Put put = mirror.put(entry);
  if (put == Mirror::Put::Full && !mirror.sync.overflowed) {
    mirror.sync.overflowed = true;  // a saved word the mirror can't hold: its absence no longer says unsaved
    return LiveApplied::Changed;
  }
  return put == Mirror::Put::Added || put == Mirror::Put::Changed ? LiveApplied::Changed : LiveApplied::None;
}

PageCall sendPage(api::LexiriseApi& api, const PagePlan& plan, const api::VocabPageReader::Cancel cancel) {
  PageCall call;
  call.plan = plan;
  api::VocabPageReader reader(cancel);
  const api::ApiResponse got =
      api.vocabularyPage(api::vocabularyPageRequest(plan.language, plan.offset, plan.limit), reader, cancel);
  call.error = got.error;
  call.retryAfterS = got.retryAfterS;
  call.sent = got.sent;
  if (reader.cancelled() || got.error == api::ApiError::Cancelled) {  // mid-body, or in a wait of the call (V7b)
    call.cancelled = true;
    return call;
  }
  if (got.ok()) {
    const api::ParseStatus status = reader.finish(call.page);
    if (status != api::ParseStatus::Ok) {
      call.error = api::ApiError::Malformed;
      call.unreadable = true;
    }
  }
  return call;
}

bool fullPassDue(const SyncState& s, const uint32_t epochS) {
  if (!s.synced) return true;
  // A last pass stamped after now means the clock moved back (or the stamp is wrong): due, rather than waiting it out.
  const bool clockSet = epochS >= static_cast<uint32_t>(config::kMinValidEpochS);
  return clockSet && s.fullDoneS != 0 && (epochS < s.fullDoneS || epochS - s.fullDoneS >= config::kVocabResyncS);
}

std::optional<PagePlan> nextPage(const Mirror& mirror, const RunState& run, const Language language,
                                 const unsigned long nowMs, const uint32_t epochS) {
  if (run.waitUntilMs && !timing::reached(nowMs, *run.waitUntilMs)) return std::nullopt;
  const SyncState& s = mirror.sync;
  if (s.full.running) return PagePlan{language, Pass::Full, s.full.offset, config::kVocabPageItems};
  if (fullPassDue(s, epochS)) return PagePlan{language, Pass::Full, 0, config::kVocabPageItems};
  // An incremental pass's first page is a probe (config::kVocabProbeItems): the usual answer is "nothing new", and a
  // few items say so as well as a whole page; the pages after it, and one sent back to the top, as their offset says.
  if (s.inc.running) return PagePlan{language, Pass::Incremental, s.inc.offset, pageLimit(s.inc.offset)};
  if (!run.incDone || timing::reached(nowMs, run.incDoneMs + config::kVocabSyncIntervalMs)) {
    return PagePlan{language, Pass::Incremental, 0, config::kVocabProbeItems};
  }
  return std::nullopt;
}

std::optional<PagePlan> manualPage(const Mirror& mirror, const Language language, const uint32_t epochS,
                                   const bool incStarted) {
  const SyncState& s = mirror.sync;
  if (s.full.running) return PagePlan{language, Pass::Full, s.full.offset, config::kVocabPageItems};
  if (!s.synced || (fullPassDue(s, epochS) && !incStarted)) {
    return PagePlan{language, Pass::Full, 0, config::kVocabPageItems};
  }
  if (s.inc.running) return PagePlan{language, Pass::Incremental, s.inc.offset, pageLimit(s.inc.offset)};
  if (!incStarted) return PagePlan{language, Pass::Incremental, 0, config::kVocabProbeItems, true};
  return std::nullopt;
}

namespace {

// A pass's page, before its items (both kinds): where to read again from after a drop in the list's count (with the
// slack the page started with), the pass unbounded when a page past the first has no count (or follows one without),
// and the count kept for the next page.
std::optional<uint32_t> beginPage(PassProgress& p, const PageCall& call) {
  const std::optional<uint32_t> count = call.page.listCount();
  const std::optional<uint32_t> reread = rereadFrom(call.plan.offset, p.lastCount, count, p.slack);
  if (call.plan.offset != 0 && (!count || !p.lastCount)) p.unbounded = true;
  p.lastCount = count;
  return reread;
}

// After its items: the next page, and its slack kept with the pass.
NextPage endPage(PassProgress& p, const PageCall& call, const std::optional<uint32_t> reread) {
  const NextPage next = nextAfter(call.plan, call.page, reread);
  p.slack = next.slack;
  return next;
}

// A full pass's page (applyPage, after the checks). False: a stale answer (nothing applied); `full`: words not kept.
bool applyFullPage(Mirror& mirror, RunState& run, const PageCall& call, const uint32_t epochS, size_t& full,
                   std::vector<uint32_t>* changedEntries) {
  SyncState& s = mirror.sync;
  const api::VocabPage& page = call.page;
  if (!s.full.running) {
    if (call.plan.offset != 0) return false;  // a stale answer: a pass starts at the top
    s.full.start(page.newestMs);
    s.fullRefused = false;
    // Wraps after 256 passes: harmless, since every entry carries the last pass's mark once it ends (the sweep drops
    // the rest), except after a pass that couldn't sweep, whose older marks could match again 255 passes later.
    s.generation = static_cast<uint8_t>(s.generation + 1);
    s.inc = PassProgress();  // a full pass supersedes an incremental one left under way
    run.incDone = false;
    // A first sync fills an empty mirror: one block for the whole list (not one growth per insertion).
    if (const std::optional<uint32_t> count = page.listCount(); mirror.size() == 0 && count) {
      mirror.reserve(std::min<size_t>(*count, config::kVocabMirrorMax));
    }
  } else if (call.plan.offset != s.full.offset) {
    return false;  // stale
  }
  const std::optional<uint32_t> reread = beginPage(s.full, call);
  for (const api::VocabItem& item : page.items) {
    if (!item.usable()) continue;
    const Entry* known = mirror.find(item.entryId);
    // This pass read it already, newer (newest first). One a live answer put still takes the page's item.
    if (known && known->mark == s.generation && !known->live) continue;
    const Entry entry = entryOf(item, s.generation, readAsOf(item, epochS));
    const bool visible = visiblyChanges(known, entry);  // before the put: `known` points into the mirror
    const Mirror::Put put = mirror.put(entry);
    if (put == Mirror::Put::Full) full++;
    if (changedEntries && visible && (put == Mirror::Put::Added || put == Mirror::Put::Changed)) {
      changedEntries->push_back(item.entryId);
    }
  }
  if (full > 0) s.overflowed = s.fullRefused = true;  // written with the page (a full pass's pages always are)
  const NextPage next = endPage(s.full, call, reread);
  if (next.offset) {
    s.full.offset = *next.offset;
    return true;
  }
  // The last page: whatever this pass didn't see is gone from the account.
  s.fullDoneS = epochS >= static_cast<uint32_t>(config::kMinValidEpochS) ? epochS : 0;
  if (s.full.unbounded) {
    LOG_INF(kLogTag, "full pass without a list count: nothing dropped, another soon");
    resyncSoon(s, epochS);
  } else {
    s.resyncSoon = false;  // a bounded pass: a later one without a count may be brought forward again
    if (const size_t dropped = mirror.sweep(s.generation); dropped > 0) {
      LOG_INF(kLogTag, "full pass dropped %u words no longer in Lexirise", static_cast<unsigned>(dropped));
    }
  }
  s.synced = true;
  if (!s.fullRefused) s.overflowed = false;  // the whole account fits again: absence speaks once more
  mirror.dropOwnUnknownRemovals();           // the account as read is the truth now (no clock to say otherwise)
  s.fullRefused = false;
  s.cursorMs = std::max(s.cursorMs, s.full.newestMs);
  s.full = PassProgress();
  run.incDone = false;  // what changed during the pass comes with an incremental pass straight after
  return true;
}

// An incremental pass's page. False: a stale answer (nothing applied); `changed`: the mirror changed.
bool applyIncrementalPage(Mirror& mirror, RunState& run, const PageCall& call, const unsigned long nowMs,
                          const uint32_t epochS, size_t& full, bool& changed, std::vector<uint32_t>* changedEntries) {
  SyncState& s = mirror.sync;
  const api::VocabPage& page = call.page;
  // What the file must be written for: an entry changed, the cursor moved, or the pass's progress, which the file keeps
  // (a pass longer than one wake carries on after a restart): left under way, or ending when the file holds it under
  // way (a pass past its first page, or one sent back to the top by a re-read), so it must be cleared. A quiet pass
  // that starts and ends on its first page, changing nothing, writes nothing.
  const bool progressInFile = s.inc.running;
  if (call.plan.offset == 0) {
    const bool reread = s.inc.running && s.inc.reread;  // a re-read sent back to the top is still that pass
    // A pass beginning from the top: the mirror will be complete as of now once it ends (a re-read sent back to the
    // top keeps its earlier start: what it read before may be older).
    if (!s.inc.running) run.incStartS = epochS >= static_cast<uint32_t>(config::kMinValidEpochS) ? epochS : 0;
    s.inc.start(page.newestMs);
    s.inc.reread = reread;
  } else if (!s.inc.running || call.plan.offset != s.inc.offset) {
    return false;  // stale
  }
  // A following page's first `slack` items were put by this pass a page ago: they read Unchanged, so they mustn't
  // trip the tie stop below (it would end the pass inside a group tied at the cursor).
  const size_t repeated = call.plan.offset == 0 ? 0 : s.inc.slack;
  const std::optional<uint32_t> reread = beginPage(s.inc, call);
  bool reachedCursor = false;
  for (size_t i = 0; i < page.items.size(); i++) {
    const api::VocabItem& item = page.items[i];
    // Older than the cursor: the mirror has it and all before it (the cursor assumes updated_at is in commit order:
    // anything else is caught by the weekly full pass). One as old as the cursor is taken again: the first of a
    // group tied at the cursor that changed in the cursor's own millisecond isn't missed.
    if (item.updatedMs != 0 && item.updatedMs < s.cursorMs) {
      reachedCursor = true;
      break;
    }
    if (!item.usable()) continue;
    const Entry entry = entryOf(item, s.generation, readAsOf(item, epochS));
    const bool visible = visiblyChanges(mirror.find(item.entryId), entry);
    const Mirror::Put put = mirror.put(entry);
    if (put == Mirror::Put::Full) full++;
    const bool entryChanged = put == Mirror::Put::Added || put == Mirror::Put::Changed;
    changed = changed || entryChanged;  // the file records marks and the live flag too
    if (changedEntries && entryChanged && visible) changedEntries->push_back(item.entryId);
    // As old as the cursor and already in the mirror as it is: the ties after it in tie order (stable across pages,
    // measured) were taken before, so a large group tied at the cursor isn't read again every pass. A later one of
    // the group changed in the same millisecond is missed here (a known limit: caught by the weekly full pass). Not
    // once the pass has read again (PassProgress::reread): its pages then repeat items anywhere.
    if (!s.inc.reread && i >= repeated && item.updatedMs != 0 && item.updatedMs == s.cursorMs &&
        put == Mirror::Put::Unchanged) {
      reachedCursor = true;
      break;
    }
  }
  if (full > 0 && !s.overflowed) {
    s.overflowed = true;  // an item refused at the cap: absence no longer says unsaved
    changed = true;
  }
  const NextPage next = endPage(s.inc, call, reread);
  if (next.reread) s.inc.reread = true;  // from now on its pages may repeat anything: no tie stop
  // The re-read wins, cursor or not: items newer than the cursor may have moved into the gap.
  if (next.reread || (!reachedCursor && next.offset)) {
    s.inc.offset = *next.offset;
    changed = true;  // left under way: its progress to the file
    return true;
  }
  if (progressInFile) changed = true;  // ended: the file's pass under way is cleared
  if (s.inc.newestMs > s.cursorMs) {
    s.cursorMs = s.inc.newestMs;
    changed = true;
  }
  if (s.inc.unbounded && s.synced) {  // it may have skipped a word the cursor has now passed
    LOG_INF(kLogTag, "incremental pass without a list count: a full pass soon");
    resyncSoon(s, epochS);
    changed = true;
  }
  // Complete as of the pass's start, when it ran from the top this boot and could bound what it read; memory only when
  // nothing else changed (a quiet pass still writes nothing: the file's time may read older).
  if (run.incStartS != 0 && !s.inc.unbounded && s.synced) {
    s.lastSyncS = std::max(s.lastSyncS, run.incStartS);
    // A removal known as of the mirror's completeness or before is no longer needed: its absence now says unsaved to
    // any page it would outrank. One known later (made during this pass, after its start) stays.
    // Not while overflowed: then absence doesn't say unsaved (applyMirrorStates), so a removal still has to.
    if (!s.overflowed && mirror.dropRemovalsUpTo(s.lastSyncS) > 0) changed = true;
  }
  run.incStartS = 0;
  s.inc = PassProgress();
  run.incDone = true;
  run.incDoneMs = nowMs;
  return true;
}

}  // namespace

bool applyPage(Mirror& mirror, RunState& run, const PageCall& call, const unsigned long nowMs, const uint32_t epochS,
               std::vector<uint32_t>* changedEntries) {
  if (call.cancelled) return false;  // the reader had input to handle: nothing learned, nothing to wait out
  if (call.error != api::ApiError::None) {
    run.waitUntilMs =
        isRefusal(call.error) ? api::retryAtMs(call.retryAfterS, nowMs) : nowMs + config::kVocabFailureWaitMs;
    return false;
  }
  run.waitUntilMs.reset();
  SyncState& s = mirror.sync;
  bool changed = false;
  if (s.synced && !s.full.running && s.fullDoneS == 0 && epochS >= static_cast<uint32_t>(config::kMinValidEpochS)) {
    s.fullDoneS = epochS;  // the pass ended before the clock was set
    changed = true;
  }
  size_t full = 0;
  if (call.plan.pass == Pass::Full) {
    if (!applyFullPage(mirror, run, call, epochS, full, changedEntries)) return changed;
    changed = true;
  } else if (!applyIncrementalPage(mirror, run, call, nowMs, epochS, full, changed, changedEntries)) {
    return changed;
  }
  if (full > 0) {
    LOG_ERR(kLogTag, "mirror full (%u words): %u not kept", static_cast<unsigned>(config::kVocabMirrorMax),
            static_cast<unsigned>(full));
  }
  LOG_INF(kLogTag, "%s %s offset %u: %u items, mirror %u words", passName(call.plan.pass),
          languageCode(call.plan.language), static_cast<unsigned>(call.plan.offset),
          static_cast<unsigned>(call.page.items.size()), static_cast<unsigned>(mirror.size()));
  return changed;
}

void VocabStore::loadLocked(const Language language) {
  Slot& s = slot(language);
  if (s.loaded) return;
  s.loaded = true;
  std::string bytes;
  const SafeFilePaths& paths = mirrorFile(language);
  switch (readSafely(files_, paths, bytes)) {
    case SafeRead::Ok:
    case SafeRead::RecoveredBackup:
      if (!parseMirror(bytes, language, s.mirror)) {
        LOG_ERR(kLogTag, "%s unreadable: moved to %s, syncing again", paths.path, paths.bad);
        setAside(files_, paths);
        s.mirror = Mirror();
      }
      break;
    case SafeRead::Missing:
      break;
    case SafeRead::Unreadable:
    default:
      LOG_ERR(kLogTag, "%s unreadable: moved to %s, syncing again", paths.path, paths.bad);
      break;
  }
  // What cards learned before it was loaded, in the order they learned it.
  for (const LiveState& state : pending_) {
    if (state.language == language && applyLive(s.mirror, state) == LiveApplied::Changed) s.dirty = true;
  }
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                [language](const LiveState& state) { return state.language == language; }),
                 pending_.end());
}

void VocabStore::load(const Language language) {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked(language);
}

bool VocabStore::loaded(const Language language) {
  std::lock_guard<std::mutex> lock(mutex_);
  return slot(language).loaded;
}

bool VocabStore::budgetLeftLocked(const unsigned long nowMs) {
  pageTimes_.erase(
      std::remove_if(pageTimes_.begin(), pageTimes_.end(),
                     [nowMs](const unsigned long t) { return timing::reached(nowMs, t + timing::kMsPerHour); }),
      pageTimes_.end());
  return pageTimes_.size() < config::kVocabPagesPerHour;
}

std::optional<PagePlan> VocabStore::next(const Language language, const unsigned long nowMs, const uint32_t epochS) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Slot& s = slot(language);
  if (!s.loaded || !budgetLeftLocked(nowMs)) return std::nullopt;
  return nextPage(s.mirror, s.run, language, nowMs, epochS);
}

bool VocabStore::cardProbeDueLocked(const Language language, const unsigned long nowMs) {
  const Slot& s = slot(language);
  if (!s.loaded || !s.mirror.sync.synced || s.mirror.sync.full.running || s.mirror.sync.inc.running) return false;
  if (s.run.waitUntilMs && !timing::reached(nowMs, *s.run.waitUntilMs)) return false;
  if (lastCardProbeMs_ && !timing::reached(nowMs, *lastCardProbeMs_ + config::kVocabCardProbeIntervalMs)) return false;
  return budgetLeftLocked(nowMs);
}

std::optional<LiveState> VocabStore::pendingState(const Language language, const uint32_t entryId) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (slot(language).loaded) return std::nullopt;
  const auto it = std::find_if(pending_.rbegin(), pending_.rend(), [language, entryId](const auto& p) {
    return p.language == language && p.entryId == entryId;
  });
  if (it == pending_.rend()) return std::nullopt;
  return *it;
}

bool VocabStore::manualBudgetLeft(const unsigned long nowMs) {
  std::lock_guard<std::mutex> lock(mutex_);
  manualTimes_.erase(
      std::remove_if(manualTimes_.begin(), manualTimes_.end(),
                     [nowMs](const unsigned long t) { return timing::reached(nowMs, t + timing::kMsPerHour); }),
      manualTimes_.end());
  return manualTimes_.size() < config::kVocabManualSyncPagesPerHour;
}

std::optional<PagePlan> VocabStore::manualNext(const Language language, const uint32_t epochS, const bool incStarted) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Slot& s = slot(language);
  if (!s.loaded) return std::nullopt;
  return manualPage(s.mirror, language, epochS, incStarted);
}

std::optional<unsigned> VocabStore::passPercent(const Language language) {
  std::lock_guard<std::mutex> lock(mutex_);
  const SyncState& s = slot(language).mirror.sync;
  const PassProgress& p = s.full.running ? s.full : s.inc;
  if (!p.running || !p.lastCount || *p.lastCount == 0) return std::nullopt;
  return static_cast<unsigned>(std::min<uint64_t>(100, static_cast<uint64_t>(p.offset) * 100 / *p.lastCount));
}

bool VocabStore::cardProbeDue(const Language language, const unsigned long nowMs) {
  std::lock_guard<std::mutex> lock(mutex_);
  return cardProbeDueLocked(language, nowMs);
}

std::optional<PagePlan> VocabStore::takeCardProbe(const Language language, const unsigned long nowMs) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!cardProbeDueLocked(language, nowMs)) return std::nullopt;
  lastCardProbeMs_ = nowMs;
  return PagePlan{language, Pass::Incremental, 0, config::kVocabProbeItems};
}

bool VocabStore::writeLocked(const Language language) {
  Slot& s = slot(language);
  if (!replaceSafely(files_, mirrorFile(language), serializeMirror(s.mirror, language))) {
    LOG_ERR(kLogTag, "%s not saved", mirrorFile(language).path);
    s.dirty = true;  // the next write takes it
    return false;
  }
  s.dirty = false;
  return true;
}

PageApplied VocabStore::apply(const PageCall& call, const unsigned long nowMs, const uint32_t epochS) {
  std::lock_guard<std::mutex> lock(mutex_);
  PageApplied out;
  Slot& s = slot(call.plan.language);
  if (!s.loaded) return out;
  if (call.sent && !call.manual) {  // the reader's own sync has its own budget (manualBudgetLeft), not the idle one
    if (pageTimes_.empty()) pageTimes_.reserve(config::kVocabPagesPerHour);
    pageTimes_.push_back(nowMs);
  } else if (call.sent) {
    if (manualTimes_.empty()) manualTimes_.reserve(config::kVocabManualSyncPagesPerHour);
    manualTimes_.push_back(nowMs);
  }
  if (call.cancelled) {
    LOG_INF(kLogTag, "%s %s offset %u given up: input came", passName(call.plan.pass), languageCode(call.plan.language),
            static_cast<unsigned>(call.plan.offset));
  } else if (call.error != api::ApiError::None) {
    LOG_INF(kLogTag, "%s %s offset %u failed (%s)", passName(call.plan.pass), languageCode(call.plan.language),
            static_cast<unsigned>(call.plan.offset), api::apiErrorName(call.error));
  }
  const bool changed = applyPage(s.mirror, s.run, call, nowMs, epochS, &out.changedEntries);
  // A page given up for input writes nothing, even a slot left dirty by a failed write: the reader has input to
  // handle, and that write waits for a later page, idle window or close.
  if (call.cancelled || (!changed && !s.dirty)) return out;
  out.file = writeLocked(call.plan.language) ? PageApplied::File::Written : PageApplied::File::Failed;
  return out;
}

void VocabStore::record(const std::vector<LiveState>& given) {
  std::lock_guard<std::mutex> lock(mutex_);
  const uint32_t now = clock_ ? clock_() : 0;
  for (LiveState state : given) {
    if (state.asOfS == 0) state.asOfS = now;  // known now (0 still with no clock)
    Slot& s = slot(state.language);
    if (s.loaded) {
      if (applyLive(s.mirror, state) == LiveApplied::Changed) s.dirty = true;
      continue;
    }
    if (pending_.size() >= config::kVocabPendingMax) pending_.erase(pending_.begin());  // the oldest
    if (pending_.empty()) pending_.reserve(config::kVocabPendingMax);
    pending_.push_back(state);
  }
}

bool VocabStore::dirty() {
  std::lock_guard<std::mutex> lock(mutex_);
  return slots_[0].dirty || slots_[1].dirty;
}

bool VocabStore::flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  bool ok = true;
  for (const Language language : kLanguages) {
    if (slot(language).dirty && !writeLocked(language)) ok = false;
  }
  return ok;
}

std::optional<Entry> VocabStore::find(const Language language, const uint32_t entryId) {
  std::lock_guard<std::mutex> lock(mutex_);
  const Entry* e = slot(language).mirror.find(entryId);
  return e ? std::optional<Entry>(*e) : std::nullopt;
}

std::optional<api::EntryState> VocabStore::savedState(const Language language, const uint32_t entryId) {
  const std::optional<Entry> e = find(language, entryId);
  return e ? savedStateOf(*e) : std::nullopt;
}

SyncState VocabStore::syncState(const Language language) {
  std::lock_guard<std::mutex> lock(mutex_);
  return slot(language).mirror.sync;
}

size_t VocabStore::size(const Language language) {
  std::lock_guard<std::mutex> lock(mutex_);
  return slot(language).mirror.size();
}

}  // namespace lexipoint::vocab
