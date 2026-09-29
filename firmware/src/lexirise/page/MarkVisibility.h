#pragma once

// V9a: which books show page marks (page-annotations.md §2 "V9a decisions", "As built (V9a)"), and A3's switch. Pure,
// header-only; tests: test/lexirise_page/PageMarksTest.cpp.

namespace lexipoint::page {

// Whether a book shows marks: Lexirise usable for it and Settings' "Mark words on the page" (settingsShowMarks), and
// then its own "Page marks" row (bookShowsMarks).
inline bool settingsShowMarks(const bool usable, const bool markWords) { return usable && markWords; }
inline bool bookShowsMarks(const bool settingsShow, const bool bookOn) { return settingsShow && bookOn; }
// A3 on a card: the marks are shown and the reader chose "Marked words".
inline bool stepsMarked(const bool shown, const bool stepMarked) { return shown && stepMarked; }

// Whether the book shows marks, as the loop last saw it: Lexirise usable with "Mark words on the page" (`shown`), and
// the book's "Page marks" row. What's kept goes when that changes (update() true), so a book shown again reads afresh.
// The render task reads the two flags themselves (ReaderMarks), so the page drawn right after the menu follows the
// row whichever task runs first.
class MarkVisibility {
 public:
  bool update(const bool shown, const bool bookOn) {
    const bool now = bookShowsMarks(shown, bookOn);
    const bool changed = now != shownNow_;
    shownNow_ = now;
    return changed;
  }
  bool shown() const { return shownNow_; }

 private:
  bool shownNow_ = false;
};

}  // namespace lexipoint::page
