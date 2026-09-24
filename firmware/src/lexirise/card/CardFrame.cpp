#if LEXIRISE

#include "CardFrame.h"

#include "CardLayout.h"
#include "CardMetrics.h"

namespace lexipoint::card {

Frame composeFrame(const CardController& controller, const TextMetrics& metrics, const bool pageVisible) {
  Frame f;
  f.pageShown = controller.state().view == View::Card && pageVisible;
  f.scene = controller.source().scene(controller.word(), f.pageShown, metrics, controller.highlightCodepoints());
  CardState state = controller.state();
  // Not on screen at all: covered, whatever its box on the page it was laid out for.
  state.wordOnPage = pageVisible ? f.scene.wordOnPage : Rect{0, 0, metrics::kScreenWidth, metrics::kScreenHeight};
  state.strip = f.scene.strip;
  state.contextSentence = f.scene.sentence;
  f.card = layoutCard(controller.currentWord(), state, metrics, controller.strings());
  return f;
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
