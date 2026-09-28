#pragma once

// Page analyses on the SD card (C12, v0.2 V7b; docs/v0.2/page-annotations.md §1.1 "As built (V7b)", the files in
// docs/v0.1/settings.md §3): one file per page, `/.lexirise/pages/<book>/<section>-<start>.bin`, where <book> is the
// FNV-1a 32 of the book's path (8 hex digits), <section> its spine index and <start> the page's first visible
// character in the section (Page::visibleTextOffset: the layout moves pages, not this). A file is used only for the
// very text the page has now (its length and hash): a font, margin or orientation change that moves the page's end
// is a miss, and the analysis asked again replaces it. The cache is regenerable, so a page file is written plainly
// (a torn one fails its CRC and is removed); the index of kept pages (oldest first, config::kPageCacheFiles at most:
// one more removes the oldest page's file) is written crash-safely every config::kPageIndexSaveEvery pages and at
// flush(); an unreadable index takes the whole cache with it (it can't be evicted page by page any more). Main task
// only. Tests: test/lexirise_page.

#include <optional>
#include <string>
#include <vector>

#include "PageAnalysis.h"
#include "lexirise/settings/SafeFile.h"

namespace lexipoint::page {

struct PageKey {
  uint32_t book = 0;   // bookKey(path)
  uint32_t spine = 0;  // the section
  uint32_t start = 0;  // the page's first visible character in it
  bool operator==(const PageKey&) const = default;
};

uint32_t bookKey(std::string_view bookPath);
std::string pagePath(const PageKey& key);
std::string bookDir(uint32_t book);

class PageStore {
 public:
  explicit PageStore(SettingsFiles& files) : files_(files) {}

  // The page's analysis when the file holds this very text (its language, length and hash); nullopt otherwise. A
  // file that doesn't check out is removed.
  std::optional<PageAnalysis> read(const PageKey& key, Language language, uint32_t textUnits, uint32_t textHash);
  // Writes it and records it in the index (dropping the oldest past config::kPageCacheFiles). False: not written.
  bool write(const PageKey& key, const PageAnalysis& page);
  size_t kept();  // pages in the index
  // The index to the card now, if pages were written since it was last (the reader closing: ReaderPages).
  bool flush();

 private:
  void loadIndex();
  bool saveIndex();
  SettingsFiles& files_;
  bool loaded_ = false;
  unsigned unsaved_ = 0;        // pages written since the index was last saved
  std::vector<PageKey> index_;  // oldest first
};

// The index's bytes, and back (a 16-byte header with a CRC, then config::kPageIndexRecordBytes per page).
std::string serializeIndex(const std::vector<PageKey>& index);
bool parseIndex(std::string_view bytes, std::vector<PageKey>& out);

// The device's store over the SD card (SettingsFilesHal.cpp; not linked into host tests).
PageStore& pageStore();

}  // namespace lexipoint::page
