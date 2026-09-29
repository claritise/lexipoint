#pragma once

// The reader's page analysis (C12, v0.2 V7b; docs/v0.2/page-annotations.md §1.1 "V7b design", "claritise's
// decisions" and "As built (V7b)"): while a page is on screen, its analysis and then the next page's, each asked once
// (one analyze/text for the whole page, the default mode; a refined answer also gets its word-level split and V1's
// merge) and kept on the SD card (PageStore), so a tap on the page skips request ①. Only over WiFi already up
// ("Only if already on", claritise 2026-09-28: never a join, no radio time added), once the page has been up
// config::kPagePrefetchDwellMs (the debounce: pages turned faster are never analyzed), within
// config::kPageBudgetPercent of the key's hourly limit, never while rendering; a call gives way to the reader's input
// (its abort). Pure apart from what's injected; the reader activity supplies the pages' text. Tests:
// test/lexirise_page/PrefetchTest.cpp.

#include <optional>
#include <string>

#include "PageStore.h"
#include "lexirise/api/LexiriseApi.h"
#include "lexirise/api/RequestWindow.h"

namespace lexipoint::vocab {
class VocabStore;
}

namespace lexipoint::page {

// A page's text as the reader has it now (text::buildPageText), and where it's kept.
struct PageText {
  PageKey key;
  Language language = Language::Japanese;
  std::string text;
  uint32_t units = 0;  // UTF-16 units
};

// Where the pages' text comes from (the reader activity on the device, a script in tests): 0 the page on screen, 1
// the next; nullopt: none.
class PageTexts {
 public:
  virtual ~PageTexts() = default;
  virtual std::optional<PageText> textOf(int which) = 0;
  virtual int pageIndex() const { return 0; }  // the page on screen's index in its section (V9a's kept pages)
};

// What the reader says about now.
struct Conditions {
  bool wifiUp = false;   // the station is connected (never brought up for this)
  bool busy = false;     // a render holds the lock, a build tick is due now, a menu is open, pages turn by themselves,
                         // input is queued or held
  bool usable = false;   // Lexirise is on, with a key, for this book
  bool blocked = false;  // a 429's wait or a rejected key (AccessPolicy)
  unsigned usedLastHour = 0;
  uint32_t rateLimit = config::kRateLimitDefault;
};

// What the reader activity knows on a loop pass (ReaderPages::step), into Conditions: pure, host-tested.
struct ReaderInputs {
  bool readerBusy = false;  // a menu or panel over the page, pages turning by themselves, input queued
  bool inputHeld = false;   // a button still held (release-mode page turns, a long press) or a finger down
  bool layingOut = false;   // a build tick due now (not a build paused ahead: a chapter's first read isn't busy)
  bool rendering = false;   // a render holds the lock
  bool usable = false;
  bool blocked = false;
  unsigned usedLastHour = 0;
  uint32_t rateLimit = config::kRateLimitDefault;
  bool wifiConnected = false;
};
inline Conditions readerConditions(const ReaderInputs& in) {
  Conditions c;
  c.busy = in.readerBusy || in.inputHeld || in.layingOut || in.rendering;
  c.wifiUp = in.wifiConnected;
  c.usable = in.usable;
  c.blocked = in.blocked;
  c.usedLastHour = in.usedLastHour;
  c.rateLimit = in.rateLimit;
  return c;
}

// Whether a pass works out again whether Lexirise is usable for the book (and V9a's marks shown): once per page shown,
// and at once when the settings, the book, or the book's Lookup language (the reader menu) changed. Never on every pass
// (it reads the settings and the book's language choice).
inline bool usableStale(const bool settingsChanged, const bool otherBook, const bool newPage,
                        const bool lookupLanguageChanged) {
  return settingsChanged || otherBook || newPage || lookupLanguageChanged;
}

// Whether the reader's own requests leave room for a page analysis: below config::kPageBudgetPercent of the limit.
inline bool budgetLeft(const unsigned used, const uint32_t limit) {
  return api::belowPercent(used, limit, config::kPageBudgetPercent);
}

class PagePrefetcher {
 public:
  using Clock = unsigned long (*)();  // millis() on the device, a fake in tests: the call's end, the file's write time
  // The wall clock (seconds; 0 while it isn't set: timing::epochNowS), read before a page's call, and again after it
  // when it wasn't set: the boot's first call sets it (the TLS connection waits for SNTP), so a page analyzed then
  // still gets its time.
  using WallClock = uint32_t (*)();
  PagePrefetcher(api::LexiriseApi& api, PageStore& store, Clock clock, WallClock wall)
      : api_(api), store_(store), clock_(clock), wall_(wall) {}
  void setMirror(vocab::VocabStore* mirror) { mirror_ = mirror; }

  // The page on screen, drawn at `drawnMs` (call it only once it's drawn), known by its section and its first visible
  // character (`start`: a reflow gives the same page index other text). Another page restarts the dwell and the
  // targets; the same page drawn again (a card closed over it, a menu) restarts the dwell only: nothing starts just
  // as the reader is back on the page. The same drawing told again (every loop pass) changes nothing.
  void shown(uint32_t spine, uint32_t start, unsigned long drawnMs);
  // Whether step() would do anything now: cheap (no SD, no page loaded), for every loop pass.
  bool due(unsigned long nowMs, const Conditions& conditions) const;

  struct Step {
    // Unusable: an answer that can't be read or kept (malformed, over a cap): the page isn't asked again this time.
    enum class Kind : uint8_t { None, NoText, Cached, Analyzed, Failed, Unusable, Cancelled } kind = Kind::None;
    int which = 0;  // 0: the page on screen; 1: the next
    api::ApiError error = api::ApiError::None;
    size_t occurrences = 0;
    bool refined = false;
    bool written = false;
    PageKey key;                // the page analyzed (V9a: its kept analysis is read again once written)
    unsigned calls = 0;         // requests answered with an HTTP status (the log's count too)
    unsigned long callMs = 0;   // the calls' time
    unsigned long writeMs = 0;  // the file's write time
  };
  // The next page not done yet (the one on screen, then the next): `texts` gives its text (nullopt: none, or
  // too long to send), a file holding that very text ends it, else it's analyzed (blocking; `abort` asked in every
  // wait), written, and its saved states given to the vocab mirror as a live answer. Input there already (`abort` true
  // before anything): nothing (Kind::None).
  Step step(PageTexts& texts, net::Abort abort);

 private:
  api::LexiriseApi& api_;
  PageStore& store_;
  Clock clock_;
  WallClock wall_;
  vocab::VocabStore* mirror_ = nullptr;
  bool known_ = false;
  uint32_t spine_ = 0;
  uint32_t start_ = 0;
  unsigned long shownMs_ = 0;      // the dwell counts from here
  unsigned long lastDrawnMs_ = 0;  // the drawing told last (shown)
  bool done_[2] = {false, false};
  std::optional<unsigned long> waitUntilMs_;  // after a failure: nothing before this
};

// Where the drawn page's first visible character comes from (the section's table on the device: an SD read under the
// render lock); nullopt: not now (a render under way), asked again next pass.
class PageStarts {
 public:
  virtual ~PageStarts() = default;
  virtual std::optional<uint32_t> startOf() = 0;
};

// What ReaderPages knows on a loop pass, gathered cheaply (no SD).
struct Pass {
  bool onScreen = false;      // the page drawn last is the section's page on screen
  unsigned long drawnMs = 0;  // its drawing (0: not drawn)
  uint32_t spine = 0;
  ReaderInputs reader;
};

// One loop pass's decision, before any SD work (ReaderPages::step, device glue, runs the step it allows): the cheap
// gates (drawn, usable, WiFi), the page's start read once per drawing, the page told to the prefetcher, its conditions.
// Pure: host-tested (test/lexirise_page/PrefetchTest.cpp).
class PagePass {
 public:
  explicit PagePass(PagePrefetcher& prefetch) : prefetch_(prefetch) {}
  // Whether prefetch.step() is due now.
  bool ready(const Pass& pass, unsigned long nowMs, PageStarts& starts);
  // The cheap gates alone (the page on screen and drawn, Lexirise usable, WiFi up): ReaderPages asks them before the
  // pass's other reads, ready() first of all.
  static bool cheapGates(const Pass& pass) {
    return pass.onScreen && pass.drawnMs != 0 && pass.reader.usable && pass.reader.wifiConnected;
  }

 private:
  PagePrefetcher& prefetch_;
  unsigned long startForMs_ = 0;  // the drawing whose start is read
  uint32_t start_ = 0;
};

}  // namespace lexipoint::page
