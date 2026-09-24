#if LEXIRISE

#include "LexiriseCardActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/LexiriseService.h"
#include "lexirise/card/BenchSource.h"
#include "lexirise/card/CardFrame.h"
#include "lexirise/card/CardOrientation.h"
#include "lexirise/card/CardPainter.h"
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
      controller_(*source_, savedReading()),
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
    nextDueMs_ = controller_.nextDueMs();
  }
  if (live_) service().holdWifi(true);  // one WiFi join per card, whatever the idle setting
  redraw();
}

void LexiriseCardActivity::loop() {
  // Read every pass, stamped now, even while a refresh runs: see CardInput.h.
  const unsigned long now = millis();
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) input_.tap(x, y, now);
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) input_.step(+1, now);
  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) input_.step(-1, now);
  // Back (the left-edge swipe, as everywhere in CrossPoint) is Home: expanded → card, card → close.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) input_.home(now);
  // Input and due phases wait while a render holds the lock (its refresh takes ~0.5 s): next pass.
  const bool due = !input_.empty() || (nextDueMs_ && timing::reached(now, *nextDueMs_));
  if (due && !RenderLock::peek()) handleQueuedInput(now);
  // The network call waits until what was asked for is on screen (CardSession::shouldFetch).
  if (live_ && !finishing_ && session_.shouldFetch(millis(), RenderLock::peek())) fetchAnswer();
}

void LexiriseCardActivity::handleQueuedInput(const unsigned long nowMs) {
  const bool hadInput = !input_.empty();
  Outcome outcome;
  int word = 0;
  {
    RenderLock lock;
    outcome = session_.handleInput(nowMs);
    nextDueMs_ = controller_.nextDueMs();
    word = controller_.word();
  }
  // lxctl card-smoke checks the side buttons stepped to the word it meant (their mapping follows settings).
  if (smoke_ && hadInput) LOG_INF("LXCARD", "word %d", word);
  apply(outcome);
}

void LexiriseCardActivity::fetchAnswer() {
  LiveSource::Fetched fetched = session_.fetch(millis());  // blocking: WiFi, TLS, one request
  CardSession::Answer answer;
  {
    RenderLock lock;
    answer = session_.apply(std::move(fetched), millis());
    nextDueMs_ = controller_.nextDueMs();
  }
  logAnswer(answer);
  if (answer.ended) return end(*answer.ended);
  if (answer.redraw) redraw();
}

void LexiriseCardActivity::logAnswer(const CardSession::Answer& answer) const {
  if (answer.writeFailed) LOG_INF("LXCARD", "level change failed (%s)", api::apiErrorName(live_->error()));
  if (answer.clearFailed) LOG_INF("LXCARD", "removed, but its notes and tags weren't cleared");
}

void LexiriseCardActivity::flushWrites(const bool lockHeld) {
  // The card stays on screen meanwhile. A failure can't be shown any more: it's logged.
  while (session_.hasPendingWrites()) {
    LiveSource::Fetched fetched = session_.fetch(millis(), /*closing=*/true);
    CardSession::Answer answer;
    if (lockHeld) {
      answer = session_.apply(std::move(fetched), millis());
    } else {
      RenderLock lock;
      answer = session_.apply(std::move(fetched), millis());
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
  if (outcome.effect == Effect::Close) return end({});
  if (outcome.effect == Effect::Redraw) redraw();
}

void LexiriseCardActivity::end(const LiveOutcome ending) {
  if (finishing_) return;
  finishing_ = true;
  flushWrites(/*lockHeld=*/false);
  if (outcome_) *outcome_ = ending;
  finish();
}

void LexiriseCardActivity::onExit() {
  // Leaving without end() (sleep, or the stack cleared under the card): the queued writes still go.
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

void LexiriseCardActivity::render(RenderLock&&) {
  const CardFonts fonts = resolveCardFonts(renderer);
  const RendererMetrics metrics(renderer, fonts);
  const Frame frame = composeFrame(controller_, metrics, pageUnderCard_);
  targets_.drawing(frame.card.hits, controller_.steps());

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
