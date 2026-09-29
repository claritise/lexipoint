#include "LexiriseCardActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <Logging.h>

#include <cstdio>
#include <ctime>
#include <string>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/LexiriseService.h"
#include "lexirise/card/BenchSource.h"
#include "lexirise/card/CardFrame.h"
#include "lexirise/card/CardOrientation.h"
#include "lexirise/card/CardPainter.h"
#include "lexirise/card/CardStringsI18n.h"
#include "lexirise/input/InputAbort.h"
#include "lexirise/settings/SettingsStore.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::card {

int LexiriseCardActivity::cardsShown_ = 0;

namespace {

// The wall clock (seconds since the epoch; before the first NTP sync it reads 1970, which the mirror's schedule
// takes as not set).
uint32_t epochNow() {
  const time_t now = time(nullptr);
  return now > 0 ? static_cast<uint32_t>(now) : 0;
}

ReadingMode savedReading() {
  return settingsStore().snapshot().japaneseReading == Reading::Romaji ? ReadingMode::Romaji : ReadingMode::Kana;
}

}  // namespace

LexiriseCardActivity::LexiriseCardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const BenchBook& book, const Options options)
    : Activity("LexiriseCard", renderer, mappedInput),
      source_(std::make_unique<BenchSource>(book, options.low)),
      controller_(*source_, options.smoke ? ReadingMode::Kana : savedReading()),
      session_(controller_, targets_, input_, nullptr),
      persistReading_(!options.smoke),
      smoke_(options.smoke) {}

LexiriseCardActivity::LexiriseCardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           std::unique_ptr<LiveSource> source,
                                           std::function<void(GfxRenderer&)> drawPage,
                                           std::shared_ptr<LiveOutcome> outcome)
    : Activity("LexiriseCard", renderer, mappedInput),
      source_(std::move(source)),
      live_(static_cast<LiveSource*>(source_.get())),
      drawPage_(std::move(drawPage)),
      outcome_(std::move(outcome)),
      controller_(*source_, savedReading(), cardStringsFromI18n()),  // the bench keeps the reference's English
      session_(controller_, targets_, input_, live_) {}

void LexiriseCardActivity::onEnter() {
  Activity::onEnter();
  {
    RenderLock lock;
    // §1.1 is measured on the 480×800 portrait panel: a landscape reader would clip the bottom-anchored card.
    const GfxRenderer::Orientation current = renderer.getOrientation();
    orientation_ = static_cast<int>(current);
    const GfxRenderer::Orientation card =
        cardOrientation(current, GfxRenderer::Orientation::Portrait, GfxRenderer::Orientation::PortraitInverted);
    renderer.setOrientation(card);
    // The reader's page was laid out for its own orientation: turned, it can't be drawn under the card
    // (the bench's page is laid out for the card's).
    pageUnderCard_ = card == current || !drawPage_;
    controller_.open(millis());
    session_.opened(millis());
    loggedWord_ = controller_.word();  // the smoke log starts from the word it opened on
    nextDueMs_ = controller_.nextDueMs();
  }
#if LEXIPOINT_DEV_HARNESS
  if (live_) live_->setClock(millis);  // "[LXCARD] names <n> words <ms> ms, stack <bytes> B free"
#endif
  if (live_) service().holdWifi(true);  // one WiFi join per card, whatever the idle setting
  redraw();
}

void LexiriseCardActivity::loop() {
  // Read every pass, stamped now, even while a refresh runs: see CardInput.h.
  const unsigned long now = millis();
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) input_.tap(x, y, now);
  readGestures(now);
  // A step keeps when its press was first seen: one held through a blocking call (the next sentence's
  // analysis) is first seen just after it, and mustn't count after the jump (CardController::step). A press
  // made and released entirely during a call (that analysis, a write, or an idle card's deck step or vocab page) is
  // never seen at all: the side buttons are only read between loop passes.
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    input_.step(+1, now, now - mappedInput.getHeldTime());
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    input_.step(-1, now, now - mappedInput.getHeldTime());
  }
  // Back (the left-edge swipe, as everywhere in CrossPoint) is Home: expanded → card, card → close.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) input_.home(now);
  // Input and due phases wait while a render holds the lock (its refresh takes ~0.5 s): next pass.
  const bool due = !input_.empty() || (nextDueMs_ && timing::reached(now, *nextDueMs_));
  if (due && !RenderLock::peek()) handleQueuedInput(now);
  // The network call waits until what was asked for is on screen (CardSession::shouldFetch).
  if (live_ && !finishing_ && session_.shouldFetch(millis(), RenderLock::peek())) {
    fetchAnswer();
  } else if (live_ && !finishing_) {
    idleStep();
  }
}

void LexiriseCardActivity::idleStep() {
  if (deckSettings_.changed(settingsStore())) session_.setDeckAllowed(deck::deckAllowed(settingsStore().snapshot()));
  int x = 0;
  int y = 0;
  const bool touching = mappedInput.isScreenTouchHeld(x, y);
  if (touching) session_.touched(millis());
  input::sampleIdle(touching);
  const uint32_t epochS = epochNow();
  const CardSession::IdleStep next = session_.nextIdleStep(millis(), RenderLock::peek(), touching, nextDueMs_, epochS);
  if (next == CardSession::IdleStep::Deck) {
    session_.applyDeck(session_.fetchDeck(), millis());  // blocking, like a write; no redraw: nothing shown changes
    return;
  }
  if (next == CardSession::IdleStep::Flush) {
    flushFiles(/*closing=*/false);
    return;
  }
  if (next == CardSession::IdleStep::Probe) {
    // V7b: the mirror's probe as the card settles (a few items: well under a second or two); a button or a touch
    // gives it up. A word on the card the probe changed is redrawn with its new level.
    const unsigned long start = millis();
    const std::optional<vocab::PageCall> probe = session_.fetchProbe(start, input::inputCame);
    const vocab::PageApplied applied = session_.applyVocab(probe, millis(), epochS);
    const bool redrawNeeded = takeMirrorChanges(applied);
    if (probe) {
      LOG_INF(vocab::kLogTag, "card probe: %u items in %lu ms (%s%s), %u entries changed%s",
              static_cast<unsigned>(probe->page.items.size()), millis() - start, api::apiErrorName(probe->error),
              probe->cancelled ? ", given up for input" : "", static_cast<unsigned>(applied.changedEntries.size()),
              redrawNeeded ? ", the card redrawn" : "");
    }
    if (redrawNeeded) redraw();
    return;
  }
  if (next != CardSession::IdleStep::Vocab) return;
  // Blocking too (a page streams in a few seconds); nothing shown changes. Logged for the device check (V7a): the
  // page's time and the heap during it (the lowest free and largest block since boot, internal RAM).
  const unsigned long start = millis();
  // A button pressed or the screen touched while it waits or streams gives the page up (read straight from the
  // hardware: the debounced state isn't updated during the call), so the input is handled, not lost (V7b: the touch
  // controller's line too, and in every wait of the call).
  const std::optional<vocab::PageCall> page = session_.fetchVocab(start, epochS, input::inputCame);
  const unsigned long took = millis() - start;
  const unsigned long applyStart = millis();
  // The mirror's file rewritten whole when the page changed it (SD I/O).
  const vocab::PageApplied result = session_.applyVocab(page, applyStart, epochS);
  const unsigned long applied = millis() - applyStart;
  // A word on the card the page changed (a change made in the app) takes the mirror's level, as after the probe.
  const bool redrawNeeded = takeMirrorChanges(result);
  if (page) {
    const HalMemory::HeapStats heap = HalMemory::getInternalHeap();
    const char* file = result.file == vocab::PageApplied::File::Written  ? "written"
                       : result.file == vocab::PageApplied::File::Failed ? "write failed"
                                                                         : "not written";
    LOG_INF(vocab::kLogTag,
            "page %u items in %lu ms (%s%s), applied in %lu ms (file %s); heap %u free, %u min, %u largest",
            static_cast<unsigned>(page->page.items.size()), took, api::apiErrorName(page->error),
            page->cancelled ? ", given up for input" : "", applied, file, static_cast<unsigned>(heap.freeBytes),
            static_cast<unsigned>(heap.minFreeBytes), static_cast<unsigned>(heap.largestBlockBytes));
  }
  if (redrawNeeded) redraw();
}

bool LexiriseCardActivity::takeMirrorChanges(const vocab::PageApplied& applied) {
  if (applied.changedEntries.empty()) return false;
  RenderLock lock;
  const bool redrawNeeded = session_.mirrorChanged(applied.changedEntries);
  nextDueMs_ = controller_.nextDueMs();
  return redrawNeeded;
}

void LexiriseCardActivity::readGestures(const unsigned long now) {
  int x = 0;
  int y = 0;
  // A long-press is always taken (consumed, so the finger's lift isn't also a tap on the card), and it
  // replaces the card (popup-ui.md §3.2) only where word select's page is what's under the card (not the
  // bench, not a landscape page), in the same coordinates.
  if (mappedInput.wasScreenLongPress(x, y) && pagePressLooksUp(live_ != nullptr, pageUnderCard_)) {
    input_.longPress(x, y, now);
  }
  // Card swipes (up / down / tabs). The edge swipes stay CrossPoint's: Back arrives as Button::Back.
  int endX = 0;
  int endY = 0;
  if (mappedInput.peekSwipe(x, y, endX, endY) &&
      swipeClearOfEdges(x, y, endX, endY, renderer.getScreenWidth(), renderer.getScreenHeight())) {
    if (const auto swipe = swipeBetween(x, y, endX, endY)) input_.swipe(*swipe, x, y, now);
  }
}

void LexiriseCardActivity::handleQueuedInput(const unsigned long nowMs) {
  const bool hadInput = !input_.empty();
  Outcome outcome;
  SmokeState shown;
  {
    RenderLock lock;
    outcome = session_.handleInput(nowMs);
    nextDueMs_ = controller_.nextDueMs();
    shown = smokeState();
  }
  logWord(shown, hadInput);
  apply(outcome);
}

LexiriseCardActivity::SmokeState LexiriseCardActivity::smokeState() const {
  return {controller_.word(), controller_.state().view == View::Expanded, controller_.state().tab};
}

void LexiriseCardActivity::logWord(const SmokeState& shown, const bool hadInput) {
  // lxctl card-smoke checks the side buttons stepped to the word it meant (their mapping follows settings),
  // card-gestures where each swipe left the card, and card-sentence the jump into the next sentence (which
  // comes with no input, from a tick or an answer: logged when the word changes).
  if (!smoke_ || (!hadInput && shown.word == loggedWord_)) return;
  LOG_INF("LXCARD", "word %d view %s tab %d", shown.word, shown.expanded ? "expanded" : "card", shown.tab);
  loggedWord_ = shown.word;
}

void LexiriseCardActivity::fetchAnswer() {
  LiveSource::Fetched fetched = session_.fetch(millis());  // blocking: WiFi, TLS, one request (two for an analysis
                                                           // that came back refined: lookup::wholeWords)
  logCacheRead(fetched);
  CardSession::Answer answer;
  SmokeState shown;
  {
    RenderLock lock;
    answer = session_.apply(std::move(fetched), millis());
    nextDueMs_ = controller_.nextDueMs();
    shown = smokeState();
  }
  logAnswer(answer);
  logWord(shown, false);
  session_.recordMirror();  // what the answer taught the vocab mirror (V7a), in memory: its file waits for idle
  if (answer.ended) return end(*answer.ended);
  if (answer.redraw) redraw();
}

void LexiriseCardActivity::logCacheRead(const LiveSource::Fetched& fetched) const {
  // The lemma cache's read before phase B (V7c): its time on every card (the read a miss adds), and this boot's hits.
  if (fetched.kind != LiveSource::Fetched::Kind::Entry ||
      fetched.cacheRead.outcome == lookup::CacheRead::Outcome::Off) {
    return;
  }
  if (fetched.cacheRead.outcome == lookup::CacheRead::Outcome::Pending) {
    LOG_INF(lookup::kCacheLogTag, "cache pending: this card's answer, not written yet");
    return;
  }
  LOG_INF(lookup::kCacheLogTag, "cache %s in %lu ms (%u of %u hits this boot)",
          lookup::cacheOutcomeName(fetched.cacheRead.outcome), fetched.cacheRead.ms, fetched.cacheRead.hits,
          fetched.cacheRead.reads);
}

void LexiriseCardActivity::flushFiles(const bool closing) {
  // SD I/O outside the lock (CardSession::flushFiles: the lemma cache's answers, V7c, up to kLookupPendingMax buckets
  // on a close; then the vocab mirror's file, V7a), each part logged with its time for the device checks.
  const CardSession::FilesFlushed done = session_.flushFiles(closing, millis(), millis);
  const char* when = closing ? " (closing)" : "";
  if (done.lookups.answers > 0) {
    LOG_INF(lookup::kCacheLogTag, "cache: %u answers %s in %lu ms%s", static_cast<unsigned>(done.lookups.answers),
            done.lookups.written ? "written" : "not written", done.lookupsMs, when);
  }
  if (done.mirrorFailed) {
    LOG_ERR(vocab::kLogTag, "mirror file not written (write failed) in %lu ms%s", done.mirrorMs, when);
  } else if (done.mirrorIo) {
    LOG_INF(vocab::kLogTag, "mirror file read or written in %lu ms%s", done.mirrorMs, when);
  }
}

void LexiriseCardActivity::logAnswer(const CardSession::Answer& answer) const {
  if (answer.writeFailed) LOG_INF("LXCARD", "level change failed (%s)", api::apiErrorName(live_->error()));
  if (answer.clearFailed) LOG_INF("LXCARD", "removed, but its notes and tags weren't cleared");
  if (!answer.unreadable.empty()) LOG_INF("LXCARD", "unreadable response: %s", answer.unreadable.c_str());
}

void LexiriseCardActivity::flushWrites(const bool lockHeld) {
  // The card stays on screen meanwhile. A write that fails now can't be shown on the card: the session
  // counts it, and word select says so after the card (LiveOutcome::unsentSaves). The book's deck waits for a
  // later idle card (CardSession::shouldFetchDeck).
  while (session_.hasPendingWrites()) {
    LiveSource::Fetched fetched = session_.fetch(millis(), /*closing=*/true);
    CardSession::Answer answer;
    if (lockHeld) {
      answer = session_.applyClosing(std::move(fetched), millis());
    } else {
      RenderLock lock;
      answer = session_.applyClosing(std::move(fetched), millis());
    }
    logAnswer(answer);
    session_.recordMirror();  // into the vocab mirror's memory (its file: end(), else the next card)
  }
}

bool LexiriseCardActivity::handleHomeGesture() {
  // Queued behind any earlier tap; ActivityManager skips loop() this pass, so the next pass handles it.
  input_.home(millis());
  return true;
}

void LexiriseCardActivity::apply(const Outcome& outcome) {
  // The reader's ignores (C17, V5) to the SD card, outside the lock and before this outcome's redraw (the toast
  // waits for the write; an earlier frame may still render: HalStorage serialises the SD card); one that can't be
  // written is taken back. A bench card (smoke included) has no live source: saveIgnores writes nothing for it.
  const CardSession::IgnoresSaved ignores = session_.saveIgnores(outcome, millis(), [this](auto&& f) {
    RenderLock lock;
    f();
    nextDueMs_ = controller_.nextDueMs();
  });
  if (ignores.failed) LOG_ERR("LXCARD", "ignore list not saved");
  if (outcome.readingChanged && persistReading_) {
    ReadingMode mode;
    {
      RenderLock lock;
      mode = controller_.state().reading;
    }
    settingsStore().update([mode](Settings& s) {
      s.japaneseReading = mode == ReadingMode::Romaji ? Reading::Romaji : Reading::Kana;
      return true;
    });
  }
  if (outcome.effect == Effect::Close) {
    LiveOutcome closed;
    closed.lookUpAt = lookUpOnClose(outcome.lookUpAt, live_ != nullptr, pageUnderCard_);  // a tap's too (P10)
    return end(closed);
  }
  if (outcome.effect == Effect::Redraw || ignores.redraw) redraw();
}

void LexiriseCardActivity::end(const LiveOutcome ending) {
  if (finishing_) return;
  finishing_ = true;
  flushWrites(/*lockHeld=*/false);
  flushFiles(/*closing=*/true);  // the lemma cache's answers and what the card taught the mirror (if it's loaded)
  if (outcome_) {
    *outcome_ = ending;
    outcome_->unsentSaves = session_.unsentSaves();
    outcome_->unsentError = session_.unsentError();
  }
  finish();
}

void LexiriseCardActivity::onExit() {
  // Leaving without end() (sleep, or the stack cleared under the card): the queued writes are still
  // attempted; a failure here can only be logged (word select's result handler doesn't run).
  if (!finishing_) {
    flushWrites(/*lockHeld=*/true);
    // V7c R8: the lemma cache's answers and the mirror's file too, as end() writes them (SD I/O under the lock
    // exitActivity holds, as the reader's page loads are; HalStorage serialises the card).
    flushFiles(/*closing=*/true);
  }
  if (live_) service().holdWifi(false);
  // Ghost cleanup: after every Nth card the reader's redraw is a half refresh (popup-ui.md §2). Here,
  // under the lock exitActivity holds, so no card redraw in flight can take the promotion.
  if (++cardsShown_ % config::kCardHalfRefreshEvery == 0) renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation_));
  Activity::onExit();
}

void LexiriseCardActivity::redraw() {
  session_.redrawAsked();  // no network call until render() has put the frame on screen
  requestUpdate();
}

#if LEXIPOINT_DEV_HARNESS
void LexiriseCardActivity::logTapTargets(const std::vector<Hit>& hits) {
  // lxctl's smokes tap where the card drew its targets, logged as a whole set when they change, once the frame is on
  // screen (after ShownTargets::shown: a tap stamped before it would land on the previous frame), after a "targets <n>"
  // header (n: the lines that follow; the level lines are logged again whenever any target changes): deck-smoke L,
  // "level <i> <x> <y> <w> <h> <saved 0|1>"; ignore-smoke the rank row, the tabs, the ⋯ rows and the toast's Undo,
  // "target <rank|tab|action|undo> <i> <x> <y> <w> <h>".
  const int saved = controller_.state().level == Level::None ? 0 : 1;
  std::string lines;
  int count = 0;
  for (const Hit& hit : hits) {
    char line[64];
    const char* kind = hit.target == Target::RankRow     ? "rank"
                       : hit.target == Target::Tab       ? "tab"
                       : hit.target == Target::Action    ? "action"
                       : hit.target == Target::ToastUndo ? "undo"
                                                         : nullptr;
    if (hit.target == Target::Level) {
      std::snprintf(line, sizeof(line), "level %d %d %d %d %d %d", hit.index, hit.rect.x, hit.rect.y, hit.rect.w,
                    hit.rect.h, saved);
    } else if (kind) {
      std::snprintf(line, sizeof(line), "target %s %d %d %d %d %d", kind, hit.index, hit.rect.x, hit.rect.y, hit.rect.w,
                    hit.rect.h);
    } else {
      continue;
    }
    lines += line;
    lines += '\n';
    count++;
  }
  if (lines == loggedTargets_) return;
  loggedTargets_ = lines;
  LOG_INF("LXCARD", "targets %d", count);
  size_t start = 0;
  while (start < lines.size()) {
    const size_t lineEnd = lines.find('\n', start);
    LOG_INF("LXCARD", "%s", lines.substr(start, lineEnd - start).c_str());
    start = lineEnd + 1;
  }
}
#endif

void LexiriseCardActivity::render(RenderLock&&) {
  const CardFonts fonts = resolveCardFonts(renderer);
  const RendererMetrics metrics(renderer, fonts);
  const Frame frame = composeFrame(controller_, metrics, pageUnderCard_);
  targets_.drawing(frame.card.hits, controller_.steps(), controller_.state().view);

  renderer.clearScreen();
  // Scan, prewarm the SD glyphs, then draw for real (the reader's pattern).
  auto scope = renderer.getFontCacheManager()->createPrewarmScope();
  const auto draw = [&] {
    if (frame.pageShown && drawPage_) drawPage_(renderer);  // the reader's page, then the highlight over it
    if (frame.pageShown) paint(renderer, frame.scene.page, fonts, metrics);
    paint(renderer, frame.card, fonts, metrics);
  };
  draw();
  scope.endScanAndPrewarm();
  draw();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);  // every phase is a partial refresh (popup-ui.md §2)
  targets_.shown(millis());
#if LEXIPOINT_DEV_HARNESS
  logTapTargets(frame.card.hits);  // once on screen: a tap the smoke sends now lands on this frame (ShownTargets::at)
#endif
  session_.frameShown();
}

}  // namespace lexipoint::card
