#if LEXIRISE

#include "CardFrame.h"

#include "CardLayout.h"

namespace lexipoint::card {

Frame composeFrame(const CardController& controller, const TextMetrics& metrics) {
  Frame f;
  f.pageShown = controller.state().view == View::Card;
  f.scene = bench::layoutPage(controller.book(), controller.word(), controller.low(), f.pageShown, metrics,
                              controller.highlightCodepoints());
  CardState state = controller.state();
  state.wordOnPage = f.scene.wordOnPage;
  state.strip = f.scene.strip;
  state.contextSentence = f.scene.sentence;
  f.card = layoutCard(controller.currentWord(), state, metrics, controller.strings());
  return f;
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
