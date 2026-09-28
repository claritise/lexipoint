#if LEXIRISE

#include "PageStore.h"

#include <Logging.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>

#include "lexirise/util/ByteOrder.h"
#include "lexirise/util/Crc32.h"

namespace lexipoint::page {
namespace {

using bytes::get32;
using bytes::put32;

constexpr char kMagic[4] = {'L', 'X', 'P', 'I'};
constexpr uint16_t kVersion = 1;
constexpr size_t kAtVersion = 4;
constexpr size_t kAtRecordBytes = 6;
constexpr size_t kAtCount = 8;
constexpr size_t kAtChecksum = 12;
constexpr size_t kHeaderBytes = 16;
// A record: the book, the section, the start (u32 each at 0, 4, 8): config::kPageIndexRecordBytes is its one home.
constexpr size_t kRecordFields = 3 * sizeof(uint32_t);
static_assert(kRecordFields == config::kPageIndexRecordBytes, "an index record is the book, the section, the start");
static_assert(config::kPageIndexMaxBytes == kHeaderBytes + config::kPageCacheFiles * config::kPageIndexRecordBytes,
              "the index's cap is its header and a record per kept page");

// The paths' buffers (pagePath, bookDir): the folder, "/", 8 hex digits, "/", two u32 in decimal, "-", ".bin", NUL.
constexpr size_t kBookDirBytes = 48;
constexpr size_t kPagePathBytes = 80;
constexpr size_t kCacheDirLength = std::char_traits<char>::length(config::kPageCacheDir);
static_assert(kCacheDirLength + 1 + 8 + 1 <= kBookDirBytes, "a book's folder fits its buffer");
static_assert(kCacheDirLength + 1 + 8 + 1 + 10 + 1 + 10 + 4 + 1 <= kPagePathBytes, "a page's path fits its buffer");

constexpr SafeFilePaths kIndexFile{config::kPageCacheDir,        config::kPageIndexPath,    config::kPageIndexTmpPath,
                                   config::kPageIndexBackupPath, config::kPageIndexBadPath, config::kPageIndexMaxBytes};

// FNV-1a 32 over the header's first bytes and the records (the CRC's stand-in: an index is small and regenerable).
uint32_t checksum(const std::string_view a, const std::string_view b) { return bytes::Fnv1a().add(a).add(b).value(); }

}  // namespace

uint32_t bookKey(const std::string_view bookPath) { return textHash(bookPath); }

std::string bookDir(const uint32_t book) {
  char buffer[kBookDirBytes];
  std::snprintf(buffer, sizeof(buffer), "%s/%08" PRIx32, config::kPageCacheDir, book);
  return buffer;
}

std::string pagePath(const PageKey& key) {
  char buffer[kPagePathBytes];
  std::snprintf(buffer, sizeof(buffer), "%s/%08" PRIx32 "/%" PRIu32 "-%" PRIu32 ".bin", config::kPageCacheDir, key.book,
                key.spine, key.start);
  return buffer;
}

std::string serializeIndex(const std::vector<PageKey>& index) {
  std::string out(kHeaderBytes + index.size() * config::kPageIndexRecordBytes, '\0');
  std::memcpy(out.data(), kMagic, sizeof(kMagic));
  out[kAtVersion] = static_cast<char>(kVersion);
  out[kAtRecordBytes] = static_cast<char>(config::kPageIndexRecordBytes);
  put32(out, kAtCount, static_cast<uint32_t>(index.size()));
  size_t at = kHeaderBytes;
  for (const PageKey& k : index) {
    put32(out, at, k.book);
    put32(out, at + 4, k.spine);
    put32(out, at + 8, k.start);
    at += config::kPageIndexRecordBytes;
  }
  const std::string_view view(out);
  put32(out, kAtChecksum, checksum(view.substr(0, kAtChecksum), view.substr(kHeaderBytes)));
  return out;
}

bool parseIndex(const std::string_view bytes, std::vector<PageKey>& out) {
  if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) return false;
  if (static_cast<uint8_t>(bytes[kAtVersion]) != kVersion ||
      static_cast<uint8_t>(bytes[kAtRecordBytes]) != config::kPageIndexRecordBytes) {
    return false;
  }
  const uint32_t count = get32(bytes, kAtCount);
  if (count > config::kPageCacheFiles || bytes.size() != kHeaderBytes + count * config::kPageIndexRecordBytes) {
    return false;
  }
  if (get32(bytes, kAtChecksum) != checksum(bytes.substr(0, kAtChecksum), bytes.substr(kHeaderBytes))) return false;
  std::vector<PageKey> index;
  index.reserve(count);
  for (size_t at = kHeaderBytes; at < bytes.size(); at += config::kPageIndexRecordBytes) {
    index.push_back(PageKey{get32(bytes, at), get32(bytes, at + 4), get32(bytes, at + 8)});
  }
  out = std::move(index);
  return true;
}

void PageStore::loadIndex() {
  if (loaded_) return;
  loaded_ = true;
  std::string bytes;
  switch (readSafely(files_, kIndexFile, bytes)) {
    case SafeRead::Ok:
    case SafeRead::RecoveredBackup:
      if (!parseIndex(bytes, index_)) {
        // The pages it named can't be found again to evict (they're named by book, section and start): the whole
        // cache goes, once, and starts again (it's regenerable: every page is asked again when shown).
        LOG_ERR(kLogTag, "%s unreadable: the page cache starts again", config::kPageIndexPath);
        index_.clear();
        if (!files_.removeTree(config::kPageCacheDir)) setAside(files_, kIndexFile);
      }
      break;
    case SafeRead::Unreadable:
      // Too large or an I/O failure (readSafely set it aside): as unreadable as one that doesn't parse (R4).
      LOG_ERR(kLogTag, "%s unreadable: the page cache starts again", config::kPageIndexPath);
      index_.clear();
      files_.removeTree(config::kPageCacheDir);
      break;
    case SafeRead::Missing:
    default:
      break;
  }
}

bool PageStore::saveIndex() { return replaceSafely(files_, kIndexFile, serializeIndex(index_)); }

size_t PageStore::kept() {
  loadIndex();
  return index_.size();
}

std::optional<PageAnalysis> PageStore::read(const PageKey& key, const Language language, const uint32_t textUnits,
                                            const uint32_t textHash) {
  const std::string path = pagePath(key);
  std::string bytes;
  if (files_.read(path.c_str(), config::kPageFileMaxBytes, bytes) != SettingsFiles::ReadStatus::Ok) {
    return std::nullopt;
  }
  PageAnalysis page;
  if (!parsePageFile(bytes, page)) {
    LOG_ERR(kLogTag, "%s unreadable: removed", path.c_str());
    files_.remove(path.c_str());
    return std::nullopt;
  }
  if (page.language != language || page.textUnits != textUnits || page.textHash != textHash) return std::nullopt;
  return page;
}

bool PageStore::write(const PageKey& key, const PageAnalysis& page) {
  if (!fitsFile(page)) {
    LOG_ERR(kLogTag, "page %u-%u over a cap: not written", unsigned(key.spine), unsigned(key.start));
    return false;
  }
  loadIndex();
  const std::string dir = bookDir(key.book);
  if (!files_.ensureDir(config::kSettingsDir) || !files_.ensureDir(config::kPageCacheDir) ||
      !files_.ensureDir(dir.c_str())) {
    return false;
  }
  const std::string path = pagePath(key);
  if (!files_.write(path.c_str(), serializePage(page))) {
    files_.remove(path.c_str());  // a short write: never left to be read (its CRC would refuse it anyway)
    return false;
  }
  const auto it = std::find(index_.begin(), index_.end(), key);
  if (it != index_.end()) {
    if (it + 1 == index_.end()) return true;  // already the newest: the index doesn't change
    index_.erase(it);
  }
  if (index_.empty()) index_.reserve(config::kPageCacheFiles + 1);
  index_.push_back(key);
  while (index_.size() > config::kPageCacheFiles) {
    files_.remove(pagePath(index_.front()).c_str());  // one at a time: never a long delete
    index_.erase(index_.begin());
  }
  // The index is saved every config::kPageIndexSaveEvery writes and at flush() (the reader closing), not after every
  // page: it's rewritten whole, crash-safely, 12 KB when full. A power loss between saves leaves the pages written
  // since unindexed: still read by name, and replaced when analyzed again, but never evicted (at most
  // kPageIndexSaveEvery - 1 each time).
  if (++unsaved_ < config::kPageIndexSaveEvery) return true;
  return flush();
}

bool PageStore::flush() {
  if (unsaved_ == 0) return true;
  unsaved_ = 0;
  return saveIndex();
}

}  // namespace lexipoint::page

#endif  // LEXIRISE
