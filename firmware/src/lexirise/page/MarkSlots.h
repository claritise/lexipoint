#pragma once

// V9a's kept pages (page/ReaderMarks.h): the analyses of the pages around the one on screen, which the render task
// draws the marks from, and when a page drawn before the loop kept it is read as it's drawn. Pure (the file reads are
// the caller's); tests: test/lexirise_page/PageMarksTest.cpp.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "PageAnalysis.h"
#include "PageStore.h"
#include "lexirise/text/BookLanguage.h"

namespace lexipoint::page {

// A page's analysis as the reader has its text now: where, in which language, and the text's length and hash.
struct PageId {
  PageKey key;
  Language language = Language::Japanese;
  uint32_t units = 0;
  uint32_t hash = 0;
  bool operator==(const PageId&) const = default;
};

// The languages whose vocab mirror a book's marks read, loaded as it opens (ReaderMarks::open): the book's own when
// its override or metadata says (none when that one is switched off), else each language switched on (its text
// decides page by page).
std::vector<Language> marksLanguages(const text::BookLanguage& book, const Settings& settings);

class MarkSlots {
 public:
  // A new page on screen, at `page` in its section (the kept pages' distance from it decides which goes first).
  void onScreen(uint32_t spine, int page);
  // The loop: a page around the one on screen has `id` now. True when its analysis is kept already (or known not to be
  // analyzed), so nothing is read, and this drawing has asked for it; false: read its file and put() it. Its
  // place isn't refreshed: the same key and text at another place (pages before it laid out anew) only changes which
  // kept page goes first, once.
  bool have(const PageId& id);
  // Keeps a page's analysis (null: not analyzed), in a free slot or in place of a kept page this drawing hasn't asked
  // for: one at the same place with another key (an old layout's), else the farthest from the page on screen (so a
  // turn reads one file: the page just left stays kept).
  void put(const PageId& id, uint32_t spine, int page, std::unique_ptr<PageAnalysis> analysis);
  // The prefetcher wrote this page again, or its language changed: the next have() reads its file, and a drawing of it
  // (read as drawn already) reads it again.
  void reload(const PageKey& key);
  void clear();  // another book, or marks turned off: nothing kept, nothing peeked

  // The render task: the analysis kept for a drawn page's key, length and hash (any language: the loop drops a
  // page whose language changed); null: none.
  const PageAnalysis* find(const PageKey& key, uint32_t units, uint32_t hash);
  // Whether to read a drawn page's file as it's drawn (it wasn't found): once per page and text until a page is found
  // or another page (or text: a reflow) is drawn (a page left and come back to is read again).
  bool peekDue(const PageKey& key, uint32_t hash);
  // What that read found (null: not analyzed), kept like put().
  void peeked(const PageId& id, uint32_t spine, int page, std::unique_ptr<PageAnalysis> analysis) {
    put(id, spine, page, std::move(analysis));
  }

  // Whether this very text (key, length, hash) is kept as not analyzed (no file read for it as it's drawn).
  bool knownNotAnalyzed(const PageKey& key, uint32_t units, uint32_t hash) const;
  size_t kept() const;  // slots holding an analysis (tests, the log)

 private:
  struct Slot {
    bool used = false;
    PageId id;
    uint32_t spine = 0;
    int page = 0;
    bool asked = false;  // wanted by the page on screen's drawing
    std::unique_ptr<PageAnalysis> analysis;
  };
  Slot* slotFor(uint32_t spine, int page);  // where a new page goes
  std::array<Slot, config::kMarkPages> slots_;
  uint32_t spine_ = 0;
  int page_ = 0;
  std::optional<std::pair<PageKey, uint32_t>> peeked_;
};

}  // namespace lexipoint::page
