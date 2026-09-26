#if LEXIRISE

#include "LexiriseCardActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>

#include <cstdio>
#include <string>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/LexiriseService.h"
#include "lexirise/card/BenchSource.h"
#include "lexirise/card/CardFrame.h"
#include "lexirise/card/CardOrientation.h"
#include "lexirise/card/CardPainter.h"
#include "lexirise/card/CardStringsI18n.h"
#include "lexirise/settings/SettingsStore.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::card {

int LexiriseCardActivity::cardsShown_ = 0;

namespace {

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
  // made and released entirely during a call (that analysis, a write, or a book deck step on an idle card) is
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
  } else if (live_ && !finishing_ && deckStepDue()) {
    session_.applyDeck(session_.fetchDeck(), millis());  // blocking, like a write; no redraw: nothing shown changes
  }
}

bool LexiriseCardActivity::deckStepDue() {
  if (deckSettings_.changed(settingsStore())) session_.setDeckAllowed(deck::deckAllowed(settingsStore().snapshot()));
  int x = 0;
  int y = 0;
  const bool touching = mappedInput.isScreenTouchHeld(x, y);
  if (touching) session_.touched(millis());
  return session_.shouldFetchDeck(millis(), RenderLock::peek(), touching, nextDueMs_);
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
  if (answer.ended) return end(*answer.ended);
  if (answer.redraw) redraw();
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
  }
}

bool LexiriseCardActivity::handleHomeGesture() {
  // Queued behind any earlier tap; ActivityManager skips loop() this pass, so the next pass handles it.
  input_.home(millis());
  return true;
}

void LexiriseCardActivity::apply(const Outcome& outcome) {
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
  if (outcome.effect == Effect::Redraw) redraw();
}

void LexiriseCardActivity::end(const LiveOutcome ending) {
  if (finishing_) return;
  finishing_ = true;
  flushWrites(/*lockHeld=*/false);
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
  if (!finishing_) flushWrites(/*lockHeld=*/true);
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
void LexiriseCardActivity::logLevelButtons(const std::vector<Hit>& hits) {
  // lxctl deck-smoke taps L where the card drew it: "level <i> <x> <y> <w> <h> <saved 0|1>", when they change.
  const int saved = controller_.state().level == Level::None ? 0 : 1;
  std::string lines;
  for (const Hit& hit : hits) {
    if (hit.target != Target::Level) continue;
    char line[64];
    std::snprintf(line, sizeof(line), "level %d %d %d %d %d %d", hit.index, hit.rect.x, hit.rect.y, hit.rect.w,
                  hit.rect.h, saved);
    lines += line;
    lines += '\n';
  }
  if (lines == loggedLevels_) return;
  loggedLevels_ = lines;
  size_t start = 0;
  while (start < lines.size()) {
    const size_t end = lines.find('\n', start);
    LOG_INF("LXCARD", "%s", lines.substr(start, end - start).c_str());
    start = end + 1;
  }
}
#endif

void LexiriseCardActivity::render(RenderLock&&) {
  const CardFonts fonts = resolveCardFonts(renderer);
  const RendererMetrics metrics(renderer, fonts);
  const Frame frame = composeFrame(controller_, metrics, pageUnderCard_);
  targets_.drawing(frame.card.hits, controller_.steps(), controller_.state().view);
#if LEXIPOINT_DEV_HARNESS
  logLevelButtons(frame.card.hits);
#endif

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
  session_.frameShown();
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
