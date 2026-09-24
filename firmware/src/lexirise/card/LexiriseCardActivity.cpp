#if LEXIRISE

#include "LexiriseCardActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>

#include "lexirise/LexiriseConfig.h"
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
      controller_(book, options.smoke ? ReadingMode::Kana : savedReading(), options.low),
      persistReading_(!options.smoke),
      smoke_(options.smoke) {}

void LexiriseCardActivity::onEnter() {
  Activity::onEnter();
  {
    RenderLock lock;
    // §1.1 is measured on the 480×800 portrait panel: a landscape reader would clip the bottom-anchored card.
    const GfxRenderer::Orientation current = renderer.getOrientation();
    orientation_ = static_cast<int>(current);
    renderer.setOrientation(
        cardOrientation(current, GfxRenderer::Orientation::Portrait, GfxRenderer::Orientation::PortraitInverted));
    controller_.open(millis());
    nextDueMs_ = controller_.nextDueMs();
  }
  requestUpdate();
}

void LexiriseCardActivity::loop() {
  // Read every pass, stamped now, even while a refresh runs: see CardInput.h.
  const unsigned long now = millis();
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) input_.tap(x, y, now);
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) input_.step(+1, now);
  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) input_.step(-1, now);
  // Nothing to do, or a render holds the lock (its refresh takes ~0.5 s): come back next pass.
  if (input_.empty() && !(nextDueMs_ && timing::reached(now, *nextDueMs_))) return;
  if (RenderLock::peek()) return;

  const bool hadInput = !input_.empty();
  Outcome outcome;
  int word = 0;
  {
    RenderLock lock;
    outcome = handleInput(controller_, targets_, input_, now);
    nextDueMs_ = controller_.nextDueMs();
    word = controller_.word();
  }
  input_.clear();
  // lxctl card-smoke checks the side buttons stepped to the word it meant (their mapping follows settings).
  if (smoke_ && hadInput) LOG_INF("LXCARD", "word %d", word);
  apply(outcome);
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
  if (outcome.effect == Effect::Close) return close();
  if (outcome.effect == Effect::Redraw) requestUpdate();
}

void LexiriseCardActivity::close() { finish(); }

void LexiriseCardActivity::onExit() {
  // Ghost cleanup: after every Nth card the reader's redraw is a half refresh (popup-ui.md §2). Here,
  // under the lock exitActivity holds, so no card redraw in flight can take the promotion.
  if (++cardsShown_ % config::kCardHalfRefreshEvery == 0) renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation_));
  Activity::onExit();
}

void LexiriseCardActivity::render(RenderLock&&) {
  const CardFonts fonts = resolveCardFonts(renderer);
  const RendererMetrics metrics(renderer, fonts);
  const Frame frame = composeFrame(controller_, metrics);
  targets_.drawing(frame.card.hits, controller_.word());

  renderer.clearScreen();
  // Scan, prewarm the SD glyphs, then draw for real (the reader's pattern).
  auto scope = renderer.getFontCacheManager()->createPrewarmScope();
  const auto draw = [&] {
    if (frame.pageShown) paint(renderer, frame.scene.page, fonts, metrics);
    paint(renderer, frame.card, fonts, metrics);
  };
  draw();
  scope.endScanAndPrewarm();
  draw();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);  // every phase is a partial refresh (popup-ui.md §2)
  targets_.shown(millis());
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
