#pragma once

// The reader's side of the page analysis (C12, v0.2 V7b): what EpubReaderActivity::loop() calls on every pass, once
// the page on screen and the next are laid out (page/Prefetch.h says when a page is analyzed; this supplies the
// pages' text from the section's layout, the conditions from the device, and the log). Device only (the section, the
// render lock, the service): the decisions are host-tested in PagePass and PagePrefetcher. Main task.

#include <cstdint>
#include <string>

#include "Prefetch.h"
#include "lexirise/settings/SettingsStore.h"

class Section;

namespace lexipoint::page {

// The page the render task drew last: which, and when (EpubReaderActivity::renderBook). Written by the render task and
// read by the loop, so it travels packed in one atomic word (packDrawn / unpackDrawn: 16 bits each for the section and
// the page, 32 for millis(); a section or page past 65535 reads as not drawn).
struct Drawn {
  int spine = -1;
  int page = -1;
  unsigned long ms = 0;
};
inline uint64_t packDrawn(const int spine, const int page, const unsigned long ms) {
  if (spine < 0 || page < 0 || spine > 0xFFFF || page > 0xFFFF) return 0;
  return (static_cast<uint64_t>(spine) << 48) | (static_cast<uint64_t>(page) << 32) | (ms & 0xFFFFFFFFUL);
}
inline Drawn unpackDrawn(const uint64_t packed) {
  if (packed == 0) return {};
  return {static_cast<int>(packed >> 48), static_cast<int>((packed >> 32) & 0xFFFF),
          static_cast<unsigned long>(packed & 0xFFFFFFFFUL)};
}

class ReaderPages {
 public:
  ReaderPages();
  ~ReaderPages();  // the page index to the card (PageStore::flush)
  // One pass: `section` the laid-out section on screen (its currentPage), `spine` its index, `drawn` the page drawn
  // last; `fingerDown` the debounced touch state (the touch line's idle level is learned from passes without one);
  // `readerBusy`: a menu or panel is open over the page, or pages turn by themselves: nothing starts; nor while a
  // button is held or a finger is down.
  // Blocks for a page's analysis (a few seconds) only when one is due; a button pressed or the screen touched gives it
  // up at once.
  // `layingOut`: a background build tick is due now (EpubReaderActivity::buildTickDue); a build merely left open,
  // paused pages ahead during a chapter's first read, isn't.
  void step(Section& section, int spine, const Drawn& drawn, bool fingerDown, bool readerBusy, bool layingOut,
            const std::string& bookPath, const std::string& bookLanguage);

 private:
  PagePrefetcher prefetch_;
  PagePass pass_;  // the pass's decision (the drawn page's start read once per drawing: an SD read)
  // Whether Lexirise is usable for the book, worked out once per page shown and when the settings change (never on
  // every pass: it reads the settings and the book's language choice).
  std::string usableFor_;
  int usableSpine_ = -1;
  int usablePage_ = -1;
  bool usable_ = false;
  SettingsWatch settingsWatch_;
};

}  // namespace lexipoint::page
