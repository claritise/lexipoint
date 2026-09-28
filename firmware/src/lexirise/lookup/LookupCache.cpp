#if LEXIRISE

#include "LookupCache.h"

#include <Logging.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "lexirise/util/ByteOrder.h"
#include "lexirise/util/Crc32.h"

namespace lexipoint::lookup {
namespace {

using bytes::get16;
using bytes::get32;
using bytes::get8;
using bytes::put16;
using bytes::put32;

constexpr char kMagic[4] = {'L', 'X', 'L', 'K'};
constexpr uint8_t kVersion = 1;  // the record's fields: a build that keeps others bumps it (an old file is a miss)
constexpr size_t kAtVersion = 4;
constexpr size_t kAtLanguage = 5;
constexpr size_t kAtCount = 6;
constexpr size_t kAtBytes = 8;
constexpr size_t kAtCrc = 12;
constexpr size_t kHeaderBytes = 16;
// A record: its length (u16), then the fetch time, rank and frequency (u32 each), the text, word, reading and level
// (a u16 length and the bytes each), the sense count (u8) and each sense's translation and part of speech (as strings).
constexpr size_t kLengthBytes = 2;  // a u16 length before a record and before each string
constexpr size_t kRecAtFetched = kLengthBytes;
constexpr size_t kRecAtRank = kRecAtFetched + 4;
constexpr size_t kRecAtFrequency = kRecAtRank + 4;
constexpr size_t kRecordFixedBytes = kRecAtFrequency + 4;
static_assert(config::kLookupBucketMax <= 0xFFFF, "a bucket's count is a u16");
static_assert(config::kLookupRecordMaxBytes <= 0xFFFF, "a record's length is a u16");
static_assert(config::kLookupBucketMaxBytes >= kHeaderBytes + config::kLookupRecordMaxBytes,
              "a bucket holds at least one record of the largest size");

// The bucket's path: the folder, "/", the language, "/", 2 hex digits, ".bin", NUL.
constexpr size_t kPathBytes = 48;
static_assert(std::char_traits<char>::length(config::kLookupCacheDir) + 1 + 2 + 1 + 2 + 4 + 1 <= kPathBytes,
              "a bucket's path fits its buffer");
static_assert(config::kLookupBuckets <= 0x100, "a bucket's name is 2 hex digits");

size_t stringBytes(const std::string_view s) { return kLengthBytes + s.size(); }

size_t recordBytes(const std::string_view text, const api::LookupResult& e) {
  size_t n =
      kRecordFixedBytes + stringBytes(text) + stringBytes(e.word) + stringBytes(e.reading) + stringBytes(e.level) + 1;
  for (const api::Sense& s : e.senses) n += stringBytes(s.translation) + stringBytes(s.partOfSpeech);
  return n;
}

void putString(std::string& out, size_t& at, const std::string_view s) {
  put16(out, at, static_cast<uint16_t>(s.size()));
  std::memcpy(out.data() + at + kLengthBytes, s.data(), s.size());
  at += stringBytes(s);
}

// Reads a string at `at` inside [.., end): false when it runs past it.
bool getString(const std::string_view in, size_t& at, const size_t end, std::string& out) {
  if (at + kLengthBytes > end) return false;
  const size_t n = get16(in, at);
  if (at + kLengthBytes + n > end) return false;
  out.assign(in.substr(at + kLengthBytes, n));
  at += kLengthBytes + n;
  return true;
}

uint32_t floatBits(const float f) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(f), "a float is 32 bits");
  std::memcpy(&bits, &f, sizeof(bits));
  return bits;
}

float bitsFloat(const uint32_t bits) {
  float f = 0;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

std::string_view languageDir(const Language language) { return languageCode(language); }

// With the clock set: a record fetched longer ago than kLookupMaxAgeS, at a time unknown, or stamped further ahead than
// that (a clock that was wrong when it was written: it would never age). With it not set (a card offline after a
// restart, before any NTP answer): none.
bool stale(const uint32_t fetchedS, const uint32_t nowS) {
  if (nowS == 0) return false;
  if (fetchedS == 0) return true;
  const uint32_t apart = nowS > fetchedS ? nowS - fetchedS : fetchedS - nowS;
  return apart > config::kLookupMaxAgeS;
}

}  // namespace

bool cacheable(const std::string_view text, const api::LookupResult& entry) {
  return !entry.translationPending && !entry.senses.empty() && !text.empty() &&
         entry.senses.size() <= config::kMaxTranslations && recordBytes(text, entry) <= config::kLookupRecordMaxBytes;
}

const char* cacheOutcomeName(const CacheRead::Outcome outcome) {
  switch (outcome) {
    case CacheRead::Outcome::Hit:
      return "hit";
    case CacheRead::Outcome::Miss:
      return "miss";
    case CacheRead::Outcome::Stale:
      return "stale";
    case CacheRead::Outcome::Pending:
      return "pending";
    case CacheRead::Outcome::Off:
    default:
      return "off";
  }
}

std::string bucketPath(const Language language, const std::string_view text) {
  const uint32_t bucket = bytes::Fnv1a().add(text).value() % config::kLookupBuckets;
  char buffer[kPathBytes];
  std::snprintf(buffer, sizeof(buffer), "%s/%s/%02" PRIx32 ".bin", config::kLookupCacheDir,
                std::string(languageDir(language)).c_str(), bucket);
  return buffer;
}

std::string serializeBucket(const std::vector<CachedLookup>& records) {
  size_t total = 0;
  for (const CachedLookup& r : records) total += recordBytes(r.text, r.entry);
  std::string out(kHeaderBytes + total, '\0');
  std::memcpy(out.data(), kMagic, sizeof(kMagic));
  out[kAtVersion] = static_cast<char>(kVersion);
  out[kAtLanguage] = static_cast<char>(records.empty() ? 0 : static_cast<uint8_t>(records.front().language));
  put16(out, kAtCount, static_cast<uint16_t>(records.size()));
  put32(out, kAtBytes, static_cast<uint32_t>(total));
  size_t at = kHeaderBytes;
  for (const CachedLookup& r : records) {
    put16(out, at, static_cast<uint16_t>(recordBytes(r.text, r.entry) - kLengthBytes));
    put32(out, at + kRecAtFetched, r.fetchedS);
    put32(out, at + kRecAtRank, r.entry.rank);
    put32(out, at + kRecAtFrequency, floatBits(r.entry.frequency));
    at += kRecordFixedBytes;
    for (const std::string_view s : {std::string_view(r.text), std::string_view(r.entry.word),
                                     std::string_view(r.entry.reading), std::string_view(r.entry.level)}) {
      putString(out, at, s);
    }
    out[at++] = static_cast<char>(r.entry.senses.size());
    for (const api::Sense& s : r.entry.senses) {
      putString(out, at, s.translation);
      putString(out, at, s.partOfSpeech);
    }
  }
  put32(out, kAtCrc, bytes::crc32(std::string_view(out).substr(kHeaderBytes)));
  return out;
}

namespace {

// The header checks out (magic, version, count, size, language, CRC): the record count; nullopt otherwise.
std::optional<size_t> checkedCount(const std::string_view bytes, const Language language) {
  if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) return std::nullopt;
  if (get8(bytes, kAtVersion) != kVersion) return std::nullopt;
  const size_t count = get16(bytes, kAtCount);
  if (count > config::kLookupBucketMax || get32(bytes, kAtBytes) != bytes.size() - kHeaderBytes) return std::nullopt;
  if (count > 0 && get8(bytes, kAtLanguage) != static_cast<uint8_t>(language)) return std::nullopt;
  if (get32(bytes, kAtCrc) != bytes::crc32(bytes.substr(kHeaderBytes))) return std::nullopt;
  return count;
}

// The record at `at`: its end (past its length field's bytes), or 0 when it runs past the file.
size_t recordEnd(const std::string_view bytes, const size_t at) {
  if (at + kRecordFixedBytes > bytes.size()) return 0;
  const size_t end = at + kLengthBytes + get16(bytes, at);
  return end > bytes.size() || end < at + kRecordFixedBytes ? 0 : end;
}

// The record's text, without a copy (its first string); false when it doesn't fit.
bool recordText(const std::string_view bytes, const size_t at, const size_t end, std::string_view& text) {
  const size_t from = at + kRecordFixedBytes;
  if (from + kLengthBytes > end) return false;
  const size_t n = get16(bytes, from);
  if (from + kLengthBytes + n > end) return false;
  text = bytes.substr(from + kLengthBytes, n);
  return true;
}

// The whole record in [at, end).
bool decodeRecord(const std::string_view bytes, size_t at, const size_t end, const Language language, CachedLookup& r) {
  r.language = language;
  r.fetchedS = get32(bytes, at + kRecAtFetched);
  r.entry.rank = get32(bytes, at + kRecAtRank);
  r.entry.frequency = bitsFloat(get32(bytes, at + kRecAtFrequency));
  at += kRecordFixedBytes;
  if (!getString(bytes, at, end, r.text) || !getString(bytes, at, end, r.entry.word) ||
      !getString(bytes, at, end, r.entry.reading) || !getString(bytes, at, end, r.entry.level)) {
    return false;
  }
  if (at + 1 > end) return false;
  const size_t senses = get8(bytes, at++);
  if (senses > config::kMaxTranslations) return false;
  r.entry.senses.resize(senses);
  for (api::Sense& s : r.entry.senses) {
    if (!getString(bytes, at, end, s.translation) || !getString(bytes, at, end, s.partOfSpeech)) return false;
  }
  return at == end;
}

}  // namespace

bool parseBucket(const std::string_view bytes, const Language language, std::vector<CachedLookup>& out) {
  const std::optional<size_t> count = checkedCount(bytes, language);
  if (!count) return false;
  std::vector<CachedLookup> records;
  records.reserve(*count);
  size_t at = kHeaderBytes;
  for (size_t i = 0; i < *count; i++) {
    const size_t end = recordEnd(bytes, at);
    CachedLookup r;
    if (end == 0 || !decodeRecord(bytes, at, end, language, r)) return false;
    records.push_back(std::move(r));
    at = end;
  }
  if (at != bytes.size()) return false;
  out = std::move(records);
  return true;
}

BucketFind findInBucket(const std::string_view bytes, const Language language, const std::string_view text,
                        CachedLookup& out) {
  const std::optional<size_t> count = checkedCount(bytes, language);
  if (!count) return BucketFind::Malformed;
  size_t at = kHeaderBytes;
  size_t match = 0;  // the newest record for the text (one per text, as written: the last)
  size_t matchEnd = 0;
  for (size_t i = 0; i < *count; i++) {
    const size_t end = recordEnd(bytes, at);
    std::string_view recorded;
    if (end == 0 || !recordText(bytes, at, end, recorded)) return BucketFind::Malformed;
    if (recorded == text) {
      match = at;
      matchEnd = end;
    }
    at = end;
  }
  if (at != bytes.size()) return BucketFind::Malformed;
  if (matchEnd == 0) return BucketFind::Absent;
  return decodeRecord(bytes, match, matchEnd, language, out) ? BucketFind::Found : BucketFind::Malformed;
}

namespace {

// Removes a bucket file that is bad in itself (too large, or read whole but not checking out), logged. Callers don't
// call it for a read that failed (an SD error, maybe passing): that file stays.
void removeBad(SettingsFiles& files, const std::string& path) {
  LOG_ERR(kCacheLogTag, "%s unreadable: removed", path.c_str());
  files.remove(path.c_str());
}

// The bucket's records for a write: a file that doesn't check out is removed (and reads as empty); nullopt when it
// couldn't be read (an SD error, maybe passing: it isn't overwritten with less).
std::optional<std::vector<CachedLookup>> readBucket(SettingsFiles& files, const std::string& path,
                                                    const Language language) {
  std::vector<CachedLookup> records;
  std::string bytes;
  const SettingsFiles::ReadStatus status = files.read(path.c_str(), config::kLookupBucketMaxBytes, bytes);
  if (status == SettingsFiles::ReadStatus::Error) return std::nullopt;
  if (status == SettingsFiles::ReadStatus::Missing) return records;
  if (status == SettingsFiles::ReadStatus::TooLarge || !parseBucket(bytes, language, records)) {
    removeBad(files, path);
    records.clear();
  }
  return records;
}

}  // namespace

CacheRead LookupCache::read(const Language language, const std::string_view text) {
  CacheRead result;
  const unsigned long start = clock_ ? clock_() : 0;
  result.outcome = CacheRead::Outcome::Miss;
  const std::string path = bucketPath(language, text);
  std::string bytes;  // the file (at most kLookupBucketMaxBytes: PSRAM past 4 KB), freed on return
  const SettingsFiles::ReadStatus status = files_.read(path.c_str(), config::kLookupBucketMaxBytes, bytes);
  if (status == SettingsFiles::ReadStatus::TooLarge) {
    removeBad(files_, path);
  } else if (status == SettingsFiles::ReadStatus::Ok) {
    CachedLookup found;
    switch (findInBucket(bytes, language, text, found)) {  // only the match is decoded
      case BucketFind::Found:
        if (stale(found.fetchedS, wall_ ? wall_() : 0)) {
          result.outcome = CacheRead::Outcome::Stale;
        } else {
          result.outcome = CacheRead::Outcome::Hit;
          result.entry = std::move(found.entry);
        }
        break;
      case BucketFind::Malformed:
        removeBad(files_, path);
        break;
      case BucketFind::Absent:
      default:
        break;
    }
  }
  result.ms = clock_ ? clock_() - start : 0;
  reads_++;
  if (result.entry) hits_++;
  result.reads = reads_;
  result.hits = hits_;
  return result;
}

bool LookupCache::write(const std::vector<CachedLookup>& lookups) {
  // The answers to keep, grouped by bucket once (each path worked out once), in the order they came within a bucket.
  struct Keep {
    std::string path;
    size_t index;
  };
  std::vector<Keep> keeps;
  keeps.reserve(lookups.size());
  for (size_t i = 0; i < lookups.size(); i++) {
    if (cacheable(lookups[i].text, lookups[i].entry))
      keeps.push_back({bucketPath(lookups[i].language, lookups[i].text), i});
  }
  std::stable_sort(keeps.begin(), keeps.end(), [](const Keep& a, const Keep& b) { return a.path < b.path; });
  bool ok = true;
  // The folders, made once per write() and language (not once per bucket).
  bool made[2] = {false, false};
  const auto dirsReady = [&](const Language language) {
    bool& done = made[language == Language::Chinese ? 1 : 0];
    if (!done) {
      const std::string dir = std::string(config::kLookupCacheDir) + "/" + std::string(languageDir(language));
      done = files_.ensureDir(config::kSettingsDir) && files_.ensureDir(config::kLookupCacheDir) &&
             files_.ensureDir(dir.c_str());
    }
    return done;
  };
  for (size_t first = 0; first < keeps.size();) {
    size_t last = first;
    while (last < keeps.size() && keeps[last].path == keeps[first].path) last++;
    const std::string& path = keeps[first].path;
    const Language language = lookups[keeps[first].index].language;  // the path names it
    std::optional<std::vector<CachedLookup>> read = readBucket(files_, path, language);
    if (!read) {  // an SD error: the file isn't replaced with less
      LOG_ERR(kCacheLogTag, "%s couldn't be read: not written", path.c_str());
      ok = false;
      first = last;
      continue;
    }
    std::vector<CachedLookup>& records = *read;
    for (size_t k = first; k < last; k++) {  // newest last
      const CachedLookup& answer = lookups[keeps[k].index];
      records.erase(
          std::remove_if(records.begin(), records.end(), [&](const CachedLookup& r) { return r.text == answer.text; }),
          records.end());
      records.push_back(answer);
      if (records.back().fetchedS == 0 && wall_) records.back().fetchedS = wall_();  // written seconds after it came
    }
    first = last;
    // The caps: the oldest go first.
    size_t bytes = kHeaderBytes;
    for (const CachedLookup& r : records) bytes += recordBytes(r.text, r.entry);
    size_t drop = 0;
    while (drop < records.size() &&
           (records.size() - drop > config::kLookupBucketMax || bytes > config::kLookupBucketMaxBytes)) {
      bytes -= recordBytes(records[drop].text, records[drop].entry);
      drop++;
    }
    records.erase(records.begin(), records.begin() + static_cast<std::ptrdiff_t>(drop));
    if (!dirsReady(language) || !files_.write(path.c_str(), serializeBucket(records))) {
      LOG_ERR(kCacheLogTag, "%s not written", path.c_str());
      ok = false;
    }
  }
  return ok;
}

}  // namespace lexipoint::lookup

#endif  // LEXIRISE
