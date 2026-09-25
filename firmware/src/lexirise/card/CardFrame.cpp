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
  // P10: the word's own highlight on the page, piece by piece (a word can break over lines), behind
  // everything the card draws: a tap or long-press on it does nothing (the word is already looked up), where
  // anywhere else on the page it would close the card to look that word up.
  if (f.pageShown) {
    for (const Rect& piece : f.scene.wordPieces) f.card.hits.push_back(Hit{Target::OwnWord, 0, piece});
  }
  return f;
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
