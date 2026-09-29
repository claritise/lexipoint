#pragma once

// V9a: everything ReaderMarks does but drawing (page-annotations.md §2 "As built (V9a)"): which book shows marks,
// the pages around the one on screen looked at on the loop (MarkGate) and their analyses kept (MarkSlots, read from
// PageStore), what a drawn page's marks come from (kept, or its file read once as it's drawn), and the card's copy.
// Host-compiled over PageTexts and a PageStore (FakeFiles in tests); ReaderMarks adds the device's sources and the
// drawing. Safe across the loop and the render task: one mutex, never held while the loop reads a page's text (the
// render lock) or its file. Tests: test/lexirise_page/PageMarksTest.cpp.

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

#include "CardMarks.h"
#include "MarkGate.h"
#include "MarkSlots.h"
#include "MarkVisibility.h"
#include "PageStore.h"
#include "Prefetch.h"

namespace lexipoint::page {

class MarkKeeper {
 public:
  // What a page's marks read as it's drawn (the ignore list, a language's vocab mirror), loaded on the main task.
  class Loader {
   public:
    virtual ~Loader() = default;
    virtual void load(Language language) = 0;
  };
  using Clock = unsigned long (*)();  // millis() on the device: the log's times (none: 0)
  explicit MarkKeeper(PageStore& store, Loader* loader = nullptr, Clock clock = nullptr)
      : store_(store), loader_(loader), clock_(clock) {}

  // Main task. A book opens, or its Lookup language changed: whether Lexirise and "Mark words on the page" show marks
  // for it (`settingsShow`), its "Page marks" row, its languages (marksLanguages); nothing kept; their sources loaded.
  void open(bool settingsShow, bool bookOn, const std::vector<Language>& languages);
  void setBookOn(bool on);  // the row tapped; turned on, its sources loaded
  // A loop pass with the page on screen drawn (`drawnMs`, its latest drawing): true when a page is due, then read().
  // Mark words on the page turned on with the book showing marks: its sources loaded first.
  bool due(uint32_t book, int spine, int page, unsigned long drawnMs, bool settingsShow);
  void read(PageTexts& texts);      // one page looked at (its text: the render lock), its file read if not kept
  void reload(const PageKey& key);  // the prefetcher wrote this page again
  void clear();
  bool drawn() const { return bookShowsMarks(settingsShow_.load(), bookOn_.load()); }  // what the render task follows
  bool settingsShow() const { return settingsShow_.load(); }

  // Render task: hold hold() while using what these give.
  std::unique_lock<std::mutex> hold() const { return std::unique_lock<std::mutex>(mutex_); }
  // The analysis for a drawn page's text (key, length, hash): kept, or (`pageIndex` >= 0) its file read once as it's
  // drawn when it's in one of the book's languages; null: none. `read` (when given): whether a file was read.
  const PageAnalysis* analysisLocked(const PageKey& key, uint32_t units, uint32_t hash, int spine, int pageIndex,
                                     bool* read = nullptr);
  CardMarks& cardMarksLocked() { return cardMarks_; }
  void dropCardMarks();

 private:
  void resetLocked();  // requires mutex_
  void loadBookSources();
  unsigned long now() const { return clock_ ? clock_() : 0; }
  PageStore& store_;
  Loader* loader_;
  Clock clock_;
  mutable std::mutex mutex_;
  MarkSlots slots_;
  CardMarks cardMarks_;
  std::array<bool, std::size(kLanguages)> languages_{};
  std::atomic<bool> settingsShow_{false};
  std::atomic<bool> bookOn_{true};
  // The loop's own.
  MarkVisibility visibility_;
  MarkGate gate_;
  std::array<std::optional<PageKey>, config::kMarkPages> wanted_;  // each offset's page, as read() last saw it
  std::optional<PageId> onScreen_;  // the page on screen's text as read() last saw it (a reflow changes it)
};

}  // namespace lexipoint::page
