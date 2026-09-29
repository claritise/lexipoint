#include "ReaderPages.h"

#include <Epub/Page.h>
#include <Epub/Section.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <Logging.h>

#include <ctime>

#include "PageSentences.h"
#include "activities/RenderLock.h"
#include "lexirise/LexiriseService.h"
#include "lexirise/input/InputAbort.h"
#include "lexirise/lookup/PageTap.h"
#include "lexirise/settings/SettingsStore.h"
#include "lexirise/text/PageModelAdapter.h"
#include "lexirise/util/Epoch.h"
#include "lexirise/vocab/VocabMirror.h"

namespace lexipoint::page {
namespace {

// The page model for its text only (text::buildPageText): no font is measured, so the line ends, the em and the
// furigana height the paragraph heuristic uses are placeholders (the text's pieces and their joins don't need them).
constexpr int kTextOnlyEm = 1;
constexpr int kTextOnlyAscender = 0;
int measureNothing(const char*, EpdFontFamily::Style) { return 0; }

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

// The section's pages' text: the page on screen (0) or the next (1), laid out already (the reader keeps a few pages
// ahead built). The next section's first page isn't read: it's analyzed once it's on screen.
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
      // Only the tokens matter for the text (text::buildPageText): no font is measured.
      model = text::buildPageModel(*page, measureNothing, kTextOnlyEm, kTextOnlyAscender);
    }
    std::optional<DescribedPage> described =
        describePage(model, book_, settings_, bookPath_, static_cast<uint32_t>(spine_), start);
    if (!described) return std::nullopt;
    return std::move(described->page);
  }

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

ReaderPages::~ReaderPages() { pageStore().flush(); }

void ReaderPages::step(Section& section, const int spine, const Drawn& drawn, const bool fingerDown,
                       const bool readerBusy, const bool layingOut, const std::string& bookPath,
                       const std::string& bookLanguage) {
  input::sampleIdle(fingerDown);
  // Worked out again per page (the book's Lookup language may have changed from the reader menu) and when the settings
  // change: never on every pass.
  const bool newPage = spine != usableSpine_ || section.currentPage != usablePage_;
  if (settingsWatch_.changed(settingsStore()) || usableFor_ != bookPath || newPage) {
    usableFor_ = bookPath;
    usableSpine_ = spine;
    usablePage_ = section.currentPage;
    usable_ = lookup::lexiriseConfigured(lookup::bookLanguageFor(bookLanguage, bookPath));
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
