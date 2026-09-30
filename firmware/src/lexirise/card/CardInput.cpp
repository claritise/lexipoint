#include "CardInput.h"

#include <cstdio>

namespace lexipoint::card {

void formatTapSeen(const TapSeen& tap, char* out, const size_t size) {
  if (tap.dropped) {
    std::snprintf(out, size, "tap %d %d dropped", tap.x, tap.y);
  } else if (!tap.hit) {
    std::snprintf(out, size, "tap %d %d none", tap.x, tap.y);
  } else {
    std::snprintf(out, size, "tap %d %d %s %d", tap.x, tap.y, targetName(tap.hit->target), tap.hit->index);
  }
}

bool formatTargetLine(const Hit& hit, const bool saved, char* out, const size_t size) {
  const Rect& r = hit.rect;
  switch (hit.target) {
    case Target::Level:
      std::snprintf(out, size, "level %d %d %d %d %d %d", hit.index, r.x, r.y, r.w, r.h, saved ? 1 : 0);
      return true;
    case Target::RankRow:
    case Target::Tab:
    case Target::Action:
    case Target::ToastUndo:
    case Target::ReadingLine:
    case Target::Close:
      std::snprintf(out, size, "target %s %d %d %d %d %d", targetName(hit.target), hit.index, r.x, r.y, r.w, r.h);
      return true;
    case Target::Card:
    case Target::OwnWord:
      return false;
  }
  return false;
}

Outcome handleInput(CardController& controller, const ShownTargets& targets, const PendingInput& input,
                    const unsigned long nowMs, TapsSeen* seen) {
  if (seen) seen->count = 0;
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
      const bool dropped = !shown || shown->step != controller.steps() || shown->view != controller.state().view;
      const Hit* hit = dropped ? nullptr : hitAt(shown->hits, e.x, e.y);
      if (seen && e.kind == InputEvent::Kind::Tap && seen->count < seen->taps.size()) {
        TapSeen& tap = seen->taps[seen->count++];
        tap = TapSeen{e.x, e.y, dropped, hit ? std::optional<Hit>(*hit) : std::nullopt};
      }
      if (dropped) continue;
      if (e.kind == InputEvent::Kind::Tap) {
        o = controller.tap(hit, e.ms, PagePoint{e.x, e.y});
      } else if (e.kind == InputEvent::Kind::LongPress) {
        o = controller.longPress(hit, e.x, e.y);
      } else if (hit && hit->target != Target::OwnWord) {  // a swipe, started on the card (not the page's word)
        o = controller.swipe(e.swipe);
      }
    }
    outcome.changes.insert(outcome.changes.end(), o.changes.begin(), o.changes.end());
    outcome.ignores.insert(outcome.ignores.end(), o.ignores.begin(), o.ignores.end());
    outcome.sentences.insert(outcome.sentences.end(), o.sentences.begin(), o.sentences.end());
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
