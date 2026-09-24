#if LEXIRISE

#include "CardInput.h"

namespace lexipoint::card {

Outcome handleInput(CardController& controller, const ShownTargets& targets, const PendingInput& input,
                    const unsigned long nowMs) {
  const ReadingMode readingBefore = controller.state().reading;
  Outcome outcome;
  bool changed = false;
  for (size_t i = 0; i < input.size(); i++) {
    const InputEvent& e = input[i];
    changed = controller.tick(e.ms) || changed;
    Outcome o;
    if (e.kind == InputEvent::Kind::Step) {
      changed = controller.step(e.direction, e.ms) || changed;
      continue;
    }
    if (e.kind == InputEvent::Kind::Home) {
      o = controller.home();
    } else {
      const ShownFrame* shown = targets.at(e.ms);
      // Nothing on screen yet, or a card for another word (tapped while a step redrew): dropped.
      if (!shown || shown->step != controller.steps()) continue;
      o = controller.tap(hitAt(shown->hits, e.x, e.y), e.ms);
    }
    outcome.changes.insert(outcome.changes.end(), o.changes.begin(), o.changes.end());
    if (o.effect == Effect::Close) {
      outcome.effect = Effect::Close;
      break;
    }
    changed = o.effect == Effect::Redraw || changed;
  }
  if (outcome.effect != Effect::Close) {
    changed = controller.tick(nowMs) || changed;
    if (changed) outcome.effect = Effect::Redraw;
  }
  outcome.readingChanged = controller.state().reading != readingBefore;
  return outcome;
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
