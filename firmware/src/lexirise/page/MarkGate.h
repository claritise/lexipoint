#pragma once

// V9a: which of the reader's kept pages (page/ReaderMarks.h) to read next, and where a written page is kept. Pure,
// header-only; tests: test/lexirise_page/PageMarksTest.cpp.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "PageStore.h"
#include "lexirise/LexiriseConfig.h"

namespace lexipoint::page {

// The kept page a written page's key is (for the prefetcher's reload: it names the page it wrote, which may no longer
// be where it was when the loop last looked); nullopt: none.
inline std::optional<size_t> slotOfKey(const std::array<std::optional<PageKey>, config::kMarkPages>& wanted,
                                       const PageKey& key) {
  const auto at = std::find(wanted.begin(), wanted.end(), std::optional<PageKey>(key));
  if (at == wanted.end()) return std::nullopt;
  return static_cast<size_t>(at - wanted.begin());
}

// The pages kept, by slot: the page on screen, the next, the one before (offsets from the section's current page), and
// each one's name in the log.
struct MarkPage {
  int offset;
  const char* name;
};
constexpr std::array<MarkPage, config::kMarkPages> kMarkPageSlots = {{{0, "this"}, {1, "next"}, {-1, "previous"}}};

// Which of the pages around the one on screen to look at next, one a loop pass: each once per page on screen, the
// page on screen first, then the next, then the one before; a page the prefetcher just wrote again: reload(). Pure;
// tested (test/lexirise_page/PageMarksTest.cpp).
class MarkGate {
 public:
  // Another page on screen (or another book): every page is due. The same page drawn again (a card or a menu closed
  // over it, a reflow landing on the same index) looks at the page on screen only: when its text changed (a reflow),
  // rearm() makes the others due too.
  void drawn(const uint32_t book, const int spine, const int page, const unsigned long drawnMs) {
    if (book == book_ && spine == spine_ && page == page_) {
      if (drawnMs != drawnMs_) due_[0] = true;
      drawnMs_ = drawnMs;
      return;
    }
    book_ = book;
    spine_ = spine;
    page_ = page;
    drawnMs_ = drawnMs;
    due_.fill(true);
  }
  void rearm() { due_.fill(true); }
  int due() const {  // the next due slot, or -1: none
    const auto next = std::find(due_.begin(), due_.end(), true);
    return next == due_.end() ? -1 : static_cast<int>(next - due_.begin());
  }
  void done(const int slot) { set(slot, false); }
  void reload(const int slot) { set(slot, true); }

 private:
  void set(const int slot, const bool due) {
    if (slot >= 0 && static_cast<size_t>(slot) < due_.size()) due_[static_cast<size_t>(slot)] = due;
  }
  uint32_t book_ = 0;
  int spine_ = -1;
  int page_ = -1;
  unsigned long drawnMs_ = 0;
  std::array<bool, config::kMarkPages> due_{};  // nothing due before the first drawing
};

}  // namespace lexipoint::page
