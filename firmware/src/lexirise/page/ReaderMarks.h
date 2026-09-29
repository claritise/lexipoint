#pragma once

// The reader's side of V9a's page marks (page/PageMarks.h; page-annotations.md §2 "V9a decisions" and "As built
// (V9a)"): MarkKeeper (host-tested) keeps the analyses of the page on screen, the next and the one before, and says
// what a drawn page's marks come from; this adds the device's sources (the ignore list and the vocab mirror, loaded on
// the main task) and the drawing: the render task draws a page's marks as it draws the page, over it
// (EpubReaderActivity::renderContents, and word select's page under the card): no second refresh.

#include <string>

#include "MarkKeeper.h"
#include "PageMarks.h"

class GfxRenderer;
class Page;

namespace lexipoint::page {

class ReaderMarks {
 public:
  ReaderMarks();
  // Main task, as a book opens (EpubReaderActivity::loadBook, before its first page is drawn) and after its Lookup
  // language changed: whether it shows marks, its row, its languages; nothing kept; its sources loaded now.
  void open(const std::string& bookPath, const std::string& dcLanguage);
  // Main task, a loop pass (ReaderPages::step), with the page on screen drawn at `drawnMs`: `settingsShow` Lexirise
  // usable for the book and "Mark words on the page". True when a page is due: then read() (unless the reader is busy).
  bool due(uint32_t book, int spine, int page, unsigned long drawnMs, bool settingsShow) {
    return keeper_.due(book, spine, page, drawnMs, settingsShow);
  }
  void read(PageTexts& texts);
  void reload(const PageKey& key) { keeper_.reload(key); }  // the prefetcher wrote this page again
  void dropCardMarks() { keeper_.dropCardMarks(); }         // a card opens over the page: its marks afresh
  void clear() { keeper_.clear(); }                         // the reader closed
  // Render task: the marks of `page` (the reader's, drawn at the margins with `fontId`), if its analysis is kept (or
  // its file read as it's drawn: `pageIndex` >= 0) for this very text. `cached`: the page under a card (drawn twice a
  // frame): its marks are kept while nothing that decides them changes (CardMarks).
  void draw(GfxRenderer& renderer, int fontId, const Page& page, uint32_t book, int spine, int pageIndex,
            int marginLeft, int marginTop, bool cached = false);
  bool shown() const { return keeper_.drawn(); }                // the book shows marks now (for A3 as a card opens)
  bool settingsShow() const { return keeper_.settingsShow(); }  // Lexirise usable and "Mark words" (the menu's row)
  void setBookOn(const bool on) { keeper_.setBookOn(on); }      // the book's "Page marks" row tapped

 private:
  class DeviceLoader final : public MarkKeeper::Loader {
   public:
    void load(Language language) override;
  };
  int emOf(const GfxRenderer& renderer, int fontId);  // the page font's CJK advance, measured once per font
  DeviceLoader loader_;
  MarkKeeper keeper_;
  int emFont_ = 0;  // the render task's own
  int em_ = 0;
};

ReaderMarks& readerMarks();

}  // namespace lexipoint::page
