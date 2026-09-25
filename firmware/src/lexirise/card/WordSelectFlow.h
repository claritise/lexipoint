#pragma once

// What word select does around a lookup's answer (lookup-flow.md §5c, popup-ui.md §3): where it goes when the
// answer (the card, or StarDict's definition) closes, and what follows a notice. The card's own ending is
// CardSession.h afterCard. Pure; tests: test/lexirise_card/LiveSourceTest.cpp.

#include <cstdint>
#include <optional>

#include "CardController.h"

namespace lexipoint::card {

// Where word select goes as an answer closes: the word a tap or long-press on the page landed on (`lookUpAt`,
// and `wordThere`: it's on a word); else back to the reader when a long-press on the page opened word select
// (`touchEntry`); else its page again. A tap or long-press that found no word closes as any close would.
enum class CloseStep : uint8_t { Redraw, BackToReader, LookUp };
inline CloseStep closeStep(const std::optional<PagePoint>& lookUpAt, const bool wordThere, const bool touchEntry) {
  if (lookUpAt && wordThere) return CloseStep::LookUp;
  return touchEntry ? CloseStep::BackToReader : CloseStep::Redraw;
}

// What waits for the notice on screen to be read, at most one thing: StarDict's turn after a Lexirise notice,
// or the rest of a card's close whose saves went unsent (with its tap or long-press point, if any).
struct AfterPopup {
  enum class Kind : uint8_t { RunStarDict, FinishClose } kind = Kind::RunStarDict;
  std::optional<PagePoint> lookUpAt{};  // FinishClose
  static AfterPopup runStarDict() { return {}; }
  static AfterPopup finishClose(const std::optional<PagePoint>& lookUpAt) { return {Kind::FinishClose, lookUpAt}; }
};

// What word select does once a notice has been read (its popup timed out): what was waiting for it, else
// back to the reader when a long-press opened word select, else its page again.
enum class AfterNotice : uint8_t { RunStarDict, FinishClose, BackToReader, Redraw };
inline AfterNotice afterNotice(const std::optional<AfterPopup>& waiting, const bool touchEntry) {
  if (waiting)
    return waiting->kind == AfterPopup::Kind::RunStarDict ? AfterNotice::RunStarDict : AfterNotice::FinishClose;
  return touchEntry ? AfterNotice::BackToReader : AfterNotice::Redraw;
}

}  // namespace lexipoint::card
