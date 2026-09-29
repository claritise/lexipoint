#include "ReaderPages.h"

#include <Epub/Page.h>
#include <Epub/Section.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <Logging.h>

#include <ctime>

#include "PageSentences.h"
#include "ReaderMarks.h"
#include "activities/RenderLock.h"
#include "lexirise/LexiriseService.h"
#include "lexirise/input/InputAbort.h"
#include "lexirise/lookup/PageTap.h"
#include "lexirise/settings/BookLanguages.h"
#include "lexirise/settings/SettingsStore.h"
#include "lexirise/text/PageModelAdapter.h"
#include "lexirise/util/Epoch.h"
#include "lexirise/vocab/VocabMirror.h"

namespace lexipoint::page {
namespace {

const char* kindName(const PagePrefetcher::Step::Kind kind) {
  switch (kind) {
    case PagePrefetcher::Step::Kind::NoText:
      return "no text";
    case PagePrefetcher::Step::Kind::Cached:
      return "kept already";
    case PagePrefetcher::Step::Kind::Analyzed:
      return "analyzed";
    case PagePrefetcher::Step::Kind::Failed:
      return "failed";
    case PagePrefetcher::Step::Kind::Unusable:
      return "unusable";
    case PagePrefetcher::Step::Kind::Cancelled:
      return "given up for input";
    case PagePrefetcher::Step::Kind::None:
    default:
      return "nothing";
  }
}

// The section's pages' text: the page on screen (0), the next (1) or, for the marks, the one before (-1), from the
// section's laid-out pages. Another section's pages aren't read: the next one's first page is analyzed, and its marks
// read, once it's on screen.
class SectionTexts final : public PageTexts {
 public:
  SectionTexts(Section& section, const int spine, const text::BookLanguage& book, const Settings& settings,
               const std::string& bookPath)
      : section_(section), spine_(spine), book_(book), settings_(settings), bookPath_(bookPath) {}
  std::optional<PageText> textOf(const int which) override {
    text::PageModel model;
    uint32_t start = 0;
    {
      RenderLock lock;
      const int index = section_.currentPage + which;
      if (index < 0 || index >= static_cast<int>(section_.pageCount)) return std::nullopt;
      const std::unique_ptr<Page> page = section_.loadPage(index);
      if (!page) return std::nullopt;
      start = page->visibleTextOffset;
      model = text::textOnlyModel(*page);
    }
    std::optional<DescribedPage> described =
        describePage(model, book_, settings_, bookPath_, static_cast<uint32_t>(spine_), start);
    if (!described) return std::nullopt;
    return std::move(described->page);
  }
  int pageIndex() const override { return section_.currentPage; }

 private:
  Section& section_;
  int spine_;
  const text::BookLanguage& book_;
  const Settings& settings_;
  const std::string& bookPath_;
};

// The page on screen's first visible character: the section's table, read under the render lock.
class SectionStart final : public PageStarts {
 public:
  explicit SectionStart(Section& section) : section_(section) {}
  std::optional<uint32_t> startOf() override {
    if (RenderLock::peek()) return std::nullopt;  // a render under way: next pass
    RenderLock lock;
    return section_.getVisibleTextOffsetForPage(static_cast<uint16_t>(section_.currentPage));
  }

 private:
  Section& section_;
};

}  // namespace

ReaderPages::ReaderPages() : prefetch_(service(), pageStore(), millis, timing::epochNowS), pass_(prefetch_) {
  prefetch_.setMirror(&vocab::vocabStore());
}

ReaderPages::~ReaderPages() {
  pageStore().flush();
  readerMarks().clear();  // V9a
}

void ReaderPages::step(Section& section, const int spine, const Drawn& drawn, const bool fingerDown,
                       const bool readerBusy, const bool layingOut, const std::string& bookPath,
                       const std::string& bookLanguage) {
  input::sampleIdle(fingerDown);
  // Worked out again per page (the book's Lookup language may have changed from the reader menu) and when the settings
  // change: never on every pass.
  const bool newPage = spine != usableSpine_ || section.currentPage != usablePage_;
  // The book's Lookup language counts too (V9a: the reader menu changes it without a new page).
  const uint32_t languages = bookLanguageStore().revision();
  if (usableStale(settingsWatch_.changed(settingsStore()), usableFor_ != bookPath, newPage,
                  languages != languagesSeen_)) {
    languagesSeen_ = languages;
    usableFor_ = bookPath;
    usableSpine_ = spine;
    usablePage_ = section.currentPage;
    usable_ = lookup::lexiriseConfigured(lookup::bookLanguageFor(bookLanguage, bookPath));
    marksShown_ = settingsShowMarks(usable_, settingsStore().snapshot().markWords);  // the book's row: ReaderMarks
    bookKey_ = bookKey(bookPath);
  }
  // V9a: the marks of the page on screen and the next, kept from their files (offline too: no WiFi needed).
  if (drawn.spine == spine && drawn.page == section.currentPage && drawn.ms != 0 && !RenderLock::peek() &&
      readerMarks().due(bookKey_, spine, section.currentPage, drawn.ms, marksShown_) && !readerBusy && !layingOut &&
      !fingerDown && !gpio.rawInputActive()) {
    const text::BookLanguage book = lookup::bookLanguageFor(bookLanguage, bookPath);
    const Settings settings = settingsStore().snapshot();
    SectionTexts texts(section, spine, book, settings, bookPath);
    readerMarks().read(texts);
  }
  LexiriseService& lexirise = service();
  Pass pass;
  pass.onScreen = drawn.spine == spine && drawn.page == section.currentPage;
  pass.drawnMs = drawn.ms;
  pass.reader.usable = usable_;
  pass.reader.wifiConnected = lexirise.wifiConnected();
  // PagePass::ready's cheap gates, before the rest of the pass's reads (the hardware, the service's counts).
  if (!PagePass::cheapGates(pass)) return;
  pass.spine = static_cast<uint32_t>(spine);
  pass.reader.readerBusy = readerBusy;
  // A button still held (release-mode page turns, a long Confirm) queues no edge: read from the hardware, as the
  // call's abort does, so no pass loads a page only to give its call up.
  pass.reader.inputHeld = fingerDown || gpio.rawInputActive();
  pass.reader.layingOut = layingOut;
  pass.reader.rendering = RenderLock::peek();
  pass.reader.blocked = lexirise.blocked() != api::AccessPolicy::Block::None;
  pass.reader.usedLastHour = lexirise.requestsLastHour();
  pass.reader.rateLimit = lexirise.rateLimit();
  SectionStart starts(section);
  if (!pass_.ready(pass, millis(), starts)) return;
  const text::BookLanguage book = lookup::bookLanguageFor(bookLanguage, bookPath);
  const Settings settings = settingsStore().snapshot();
  SectionTexts texts(section, spine, book, settings, bookPath);
  const unsigned long start = millis();
  const PagePrefetcher::Step s = prefetch_.step(texts, input::inputCame);
  if (s.kind == PagePrefetcher::Step::Kind::None) return;
  if (s.kind == PagePrefetcher::Step::Kind::Analyzed && s.written) readerMarks().reload(s.key);  // V9a
  const HalMemory::HeapStats heap = HalMemory::getInternalHeap();
  LOG_INF(kLogTag,
          "%s page (%d of section %d): %s in %lu ms (%s), %u occurrences%s, %u calls in %lu ms, written in %lu ms%s; "
          "heap %u free, %u min, %u largest",
          s.which == 0 ? "this" : "next", section.currentPage + s.which, spine, kindName(s.kind), millis() - start,
          api::apiErrorName(s.error), static_cast<unsigned>(s.occurrences), s.refined ? " (refined, merged)" : "",
          s.calls, s.callMs, s.writeMs, s.kind == PagePrefetcher::Step::Kind::Analyzed && !s.written ? " (failed)" : "",
          static_cast<unsigned>(heap.freeBytes), static_cast<unsigned>(heap.minFreeBytes),
          static_cast<unsigned>(heap.largestBlockBytes));
}

}  // namespace lexipoint::page
