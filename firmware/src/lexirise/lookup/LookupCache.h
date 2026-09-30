#pragma once

// The lemma cache (C21, v0.2 V7c; docs/v0.2/00-overview.md C21 "V7c design"): phase B's answers (dictionary/lookup)
// on the SD card, so a word looked up before, on this card or an earlier one, needs no call. Only what the card keeps
// of an answer (api::LookupResult: the word, its reading, the first senses, level, rank, frequency, the other readings)
// and when it was fetched; never a saved state (that comes from ①, the page's file and the vocab mirror), so a save or
// a level change invalidates nothing. Keyed by the language and the exact text phase B sends (LookupCard::headword()),
// which the record holds (a hash collision is a miss), and by the account (V8): the senses are in the account's
// translation target, which the request can't name, so a bucket written under another API key (accountTag) is read as
// empty. The tag is a 32-bit FNV-1a of the key: it keeps nothing secret the card doesn't already hold (the key is in
// plain text in /.lexirise/config.ini), and it can only confirm a guessed key, not reveal one.
//
// `/.lexirise/lookups/<ja|zh>/<nn>.bin`: config::kLookupBuckets files per language, the bucket from the text's FNV-1a
// 32; each a 20-byte header (magic, version, language, record count, the records' size, a CRC-32 of the records, the
// account's tag) and its records, oldest first, at most config::kLookupBucketMax and kLookupBucketMaxBytes. A record:
// its length, when it was fetched, rank, frequency, the text asked, the word, its reading, its level, its senses
// (each a translation and a part of speech) and its other readings. A read is one file of at most
// kLookupBucketMaxBytes; nothing is held between calls, and a read decodes only the record it looks for. Written
// plainly (regenerable: a file that doesn't check out or is too large is removed and is a miss; one that fails to read
// is a miss and stays). A record older than config::kLookupMaxAgeS (or of an unknown time) is stale (a miss; the new
// answer replaces it); with the clock not set it's taken whatever its age. Main task only. Tests:
// test/lexirise_lookup/LookupCacheTest.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lexirise/api/Responses.h"
#include "lexirise/settings/SafeFile.h"
#include "lexirise/settings/Settings.h"

namespace lexipoint::lookup {

constexpr const char* kCacheLogTag = "LXLOOK";

// Whether an answer may be kept: translated (not pending) with a sense, and small enough
// (config::kLookupRecordMaxBytes).
bool cacheable(std::string_view text, const api::LookupResult& entry);

struct CacheRead {
  // Pending: the card's own answer not written yet (LiveSource, V7c R8): no SD read.
  enum class Outcome : uint8_t { Off, Hit, Miss, Stale, Pending } outcome = Outcome::Off;
  std::optional<api::LookupResult> entry;  // Hit
  unsigned long ms = 0;                    // the read's time (the clock given; 0 without one)
  unsigned reads = 0;                      // the cache's reads so far, this one included, and the hits among them
  unsigned hits = 0;
};
const char* cacheOutcomeName(CacheRead::Outcome outcome);
// What the log says of a read, without its entry (the card's Fetched carries this, not the entry, across the call).
struct CacheReadLog {
  CacheRead::Outcome outcome = CacheRead::Outcome::Off;
  unsigned long ms = 0;
  unsigned reads = 0;
  unsigned hits = 0;
};
inline CacheReadLog logOf(const CacheRead& read) { return {read.outcome, read.ms, read.reads, read.hits}; }

// The account a bucket belongs to: FNV-1a 32 of the API key (the key itself is never written here). Two keys share a
// tag about once in 2^32: then each reads the other's answers (at worst in another translation target) until they age
// out (config::kLookupMaxAgeS).
uint32_t accountTag(std::string_view apiKey);

// One answer to keep: the language, the text asked, the answer, when it came (0: stamped with the wall clock as it's
// written, seconds later; a record whose time is unknown is stale once the clock is set).
struct CachedLookup {
  Language language = Language::Japanese;
  std::string text;
  api::LookupResult entry;
  uint32_t fetchedS = 0;
  // The accountTag it was fetched under (none: the writer's). write() drops an answer whose account is no longer the
  // current one (the key changed while it waited), so it never lands in the new account's bucket.
  std::optional<uint32_t> account;
};

class LookupCache {
 public:
  using Clock = unsigned long (*)();  // millis, for the read's time in the log
  using WallClock = uint32_t (*)();   // seconds since the epoch, 0 while not set (timing::epochNowS)
  using Account = uint32_t (*)();     // the current account's accountTag (none: 0); read once per read() / write()
  explicit LookupCache(SettingsFiles& files, const WallClock wall = nullptr, const Clock clock = nullptr,
                       const Account account = nullptr)
      : files_(files), wall_(wall), clock_(clock), account_(account) {}

  // One bucket read: the entry when a fresh record holds this very text.
  CacheRead read(Language language, std::string_view text);
  // The answers into their buckets (one read and one write per bucket touched; a record for the same text replaced,
  // the oldest dropped past the caps; nothing touched for answers it can't keep). False: something wasn't written (a
  // cache: nothing else to do).
  bool write(const std::vector<CachedLookup>& lookups);
  // Reads this boot, and the hits among them (the log's running count: the device's hit rate).
  unsigned reads() const { return reads_; }
  unsigned hits() const { return hits_; }
  // The current account's tag (CachedLookup::account, taken when an answer comes).
  uint32_t account() const { return account_ ? account_() : 0; }

 private:
  SettingsFiles& files_;
  WallClock wall_;
  Clock clock_;
  Account account_;
  unsigned reads_ = 0;
  unsigned hits_ = 0;
};

// A bucket's path, and its records to bytes and back (a file that doesn't check out: false; another account's: no
// records).
std::string bucketPath(Language language, std::string_view text);
std::string serializeBucket(const std::vector<CachedLookup>& records, uint32_t account = 0);
bool parseBucket(std::string_view bytes, Language language, std::vector<CachedLookup>& out, uint32_t account = 0);
// A read's walk: each record's text compared in place, only the match decoded (no per-record strings). Another
// account's bucket: Absent.
enum class BucketFind : uint8_t { Found, Absent, Malformed };
BucketFind findInBucket(std::string_view bytes, Language language, std::string_view text, CachedLookup& out,
                        uint32_t account = 0);

// The device's cache over the SD card (SettingsFilesHal.cpp; not linked into host tests).
LookupCache& lookupCache();

}  // namespace lexipoint::lookup
