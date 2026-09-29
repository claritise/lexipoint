#include "ReaderMarks.h"

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "lexirise/card/ReaderPageFor.h"
#include "lexirise/lookup/PageTap.h"
#include "lexirise/settings/BookMarks.h"
#include "lexirise/settings/IgnoredWords.h"
#include "lexirise/settings/SettingsStore.h"
#include "lexirise/text/PageModelAdapter.h"
#include "lexirise/vocab/VocabMirror.h"

namespace lexipoint::page {
namespace {

constexpr const char* kCjkEm = "\xE4\xB8\x80";  // 一: a full-width character, the page font's CJK em

// The device's sources: the vocab mirror by the saved-state rule, and the reader's ignore list (both from memory:
// loaded on the loop before a page is drawn with marks).
class DeviceSources final : public MarkSources {
 public:
  DeviceSources(const Language language, const uint64_t analyzedMs)
      : view_(language, analyzedMs, vocab::vocabStore()) {}
  MirrorSays mirror(const uint32_t entryId) const override { return view_.says(entryId); }
  bool ignored(const IgnoredKey& key) const override { return ignoredWordStore().contains(key); }

 private:
  MirrorView view_;
};

// The page's own width measure (Font::Page only: the marks measure nothing else).
class PageMetrics final : public card::TextMetrics {
 public:
  PageMetrics(GfxRenderer& renderer, const int fontId) : renderer_(renderer), fontId_(fontId) {}
  int lineHeight(card::Font) const override { return renderer_.getLineHeight(fontId_); }
  int ascender(card::Font) const override { return renderer_.getFontAscenderSize(fontId_); }
  int width(card::Font, const std::string& text) const override {
    return renderer_.getTextAdvanceX(fontId_, text.c_str(), EpdFontFamily::REGULAR);
  }
  int pageWidth(const std::string& text, const uint8_t style) const override {
    return renderer_.getTextAdvanceX(fontId_, text.c_str(), static_cast<EpdFontFamily::Style>(style));
  }

 private:
  GfxRenderer& renderer_;
  int fontId_;
};

}  // namespace

// What a page's marks read as it's drawn, loaded on the main task, outside the render lock.
void ReaderMarks::DeviceLoader::load(const Language language) {
  ignoredWordStore().load();
  vocab::VocabStore& mirror = vocab::vocabStore();
  if (mirror.loaded(language)) return;
  const unsigned long start = millis();
  mirror.load(language);
  LOG_DBG(kLogTag, "marks: the %s mirror loaded in %lu ms", languageCode(language), millis() - start);
}

ReaderMarks::ReaderMarks() : keeper_(pageStore(), &loader_, millis) {}

void ReaderMarks::open(const std::string& bookPath, const std::string& dcLanguage) {
  const Settings settings = settingsStore().snapshot();
  const text::BookLanguage book = lookup::bookLanguageFor(dcLanguage, bookPath);
  keeper_.open(settingsShowMarks(lookup::lexiriseConfigured(book), settings.markWords), bookMarksStore().on(bookPath),
               marksLanguages(book, settings));
}

void ReaderMarks::read(PageTexts& texts) { keeper_.read(texts); }

int ReaderMarks::emOf(const GfxRenderer& renderer, const int fontId) {
  if (emFont_ != fontId || em_ == 0) {
    emFont_ = fontId;
    em_ = renderer.getTextAdvanceX(fontId, kCjkEm, EpdFontFamily::REGULAR);
  }
  return em_;
}

void ReaderMarks::draw(GfxRenderer& renderer, const int fontId, const Page& page, const uint32_t book, const int spine,
                       const int pageIndex, const int marginLeft, const int marginTop, const bool cached) {
  if (spine < 0 || !keeper_.drawn()) return;
  const auto lock = keeper_.hold();  // held while this page is drawn: the loop only changes what's kept under it
  const PageKey key{book, static_cast<uint32_t>(spine), page.visibleTextOffset};
  CardMarks& cardMarks = keeper_.cardMarksLocked();
  const CardMarks::Key cardKey{key, &page, vocab::vocabStore().revision(), ignoredWordStore().revision()};
  if (!cached) {
    cardMarks.drop();  // the reader draws its page: the card's copy is done with
  } else if (cardMarks.holds(cardKey)) {
    for (const card::Rect& r : cardMarks.fills) renderer.fillRect(r.x, r.y, r.w, r.h, true);
    return;
  }
  const unsigned long start = millis();
  const std::optional<text::BuiltSentence> built = text::pageTextOf(page);
  if (!built) return;
  const PageAnalysis* analysis =
      keeper_.analysisLocked(key, text::utf16Length(built->text), textHash(built->text), spine, pageIndex);
  std::vector<card::Rect> fills;
  if (analysis) {
    const DeviceSources sources(analysis->language, analysis->analyzedMs);
    const std::vector<SpanMark> marks = pageMarks(*analysis, sources);
    const PageMetrics metrics(renderer, fontId);
    const card::ReaderPage readerPage = card::readerPageFor(renderer, fontId, page, marginLeft, marginTop);
    const int ascender = renderer.getFontAscenderSize(fontId);
    const int below = markBelowBaseline(emOf(renderer, fontId));
    for (const MarkRun& run : markRuns(readerPage, *built, marks, metrics)) markFills(run, ascender, below, fills);
    for (const card::Rect& r : fills) renderer.fillRect(r.x, r.y, r.w, r.h, true);
    const HalMemory::HeapStats heap = HalMemory::getInternalHeap();
    LOG_DBG(kLogTag, "marks: %u words, %u fills in %lu ms; heap %u free", static_cast<unsigned>(marks.size()),
            static_cast<unsigned>(fills.size()), millis() - start, static_cast<unsigned>(heap.freeBytes));
  }
  if (cached) cardMarks.keep(cardKey, std::move(fills));  // a page with no marks is kept too: nothing to redo
}

ReaderMarks& readerMarks() {
  static ReaderMarks marks;
  return marks;
}

}  // namespace lexipoint::page
