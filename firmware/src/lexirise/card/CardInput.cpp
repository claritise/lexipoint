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
      changed = controller.step(e.direction, e.ms, e.pressedMs) || changed;
      continue;
    }
    if (e.kind == InputEvent::Kind::Home) {
      o = controller.home();
    } else {
      const ShownFrame* shown = targets.at(e.ms);
      // Nothing on screen yet, or a card for another word (tapped while a step redrew) or in the other view
      // (touched while a view change redrew): dropped.
      if (!shown || shown->step != controller.steps() || shown->view != controller.state().view) continue;
      const Hit* hit = hitAt(shown->hits, e.x, e.y);
      if (e.kind == InputEvent::Kind::Tap) {
        o = controller.tap(hit, e.ms, PagePoint{e.x, e.y});
      } else if (e.kind == InputEvent::Kind::LongPress) {
        o = controller.longPress(hit, e.x, e.y);
      } else if (hit && hit->target != Target::OwnWord) {  // a swipe, started on the card (not the page's word)
        o = controller.swipe(e.swipe);
      }
    }
    outcome.changes.insert(outcome.changes.end(), o.changes.begin(), o.changes.end());
    if (o.effect == Effect::Close) {
      outcome.effect = Effect::Close;
      outcome.lookUpAt = o.lookUpAt;
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
