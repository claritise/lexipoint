#pragma once

// Who a long-press on the reading page belongs to (lookup-flow.md §1, D15). The touch long-press fires
// at 500 ms while the finger is still down; CrossPoint's own "long-press behaviour" setting (chapter skip
// / rotate, off by default) acts on a ≥700 ms hold of a page-turn tap zone, on release. So with that
// setting on in a tap mode, the outer page-turn zones stay CrossPoint's and the lookup owns the centre;
// with it off (or in swipe mode or with touch controls off, which have no hold action), the lookup owns
// the whole page. It takes only a press on a word, and none when nothing can answer: any other long-press
// is left alone, so its lift still turns the page or opens the menu as before. Pure; tests: test/lexirise_lookup.

#include <optional>

namespace lexipoint::lookup {

// Word select has something to ask: a StarDict dictionary, or Lexirise (enabled, with a key).
inline bool lookupsAvailable(const bool starDictSet, const bool lexiriseConfigured) {
  return starDictSet || lexiriseConfigured;
}

struct LongPressRules {
  int screenWidth = 0;
  int pageTurnZoneWidth = 0;  // CrossPoint's outer page-turn zones (ReaderUtils::pageTurnZoneWidth)
  bool holdActionOn = false;  // CrossPoint's longPressButtonBehavior isn't off
  bool tapZones = false;      // tap page turns (normal or inverted); not swipe, not touch controls off
};

// x is the press's touch-down point, as the page-turn zones use it (a long-press may drift a little).
inline bool lookupOwnsLongPress(const int x, const LongPressRules& rules) {
  if (!rules.holdActionOn || !rules.tapZones) return true;
  return x >= rules.pageTurnZoneWidth && x < rules.screenWidth - rules.pageTurnZoneWidth;
}

// Whether the reader takes a long-press that fired at pressX (nullopt: none this frame). `available`
// (lookupsAvailable for the open book, which reads the settings) is only asked once one has fired and
// the lookup owns its zone, and `onWord` (whether the press is on a word of the page, which loads and
// measures it) only after that: a long-press on a margin, an image or blank space stays CrossPoint's.
template <typename AvailableFn, typename OnWordFn>
bool takeLongPress(const std::optional<int> pressX, const LongPressRules& rules, AvailableFn&& available,
                   OnWordFn&& onWord) {
  return pressX && lookupOwnsLongPress(*pressX, rules) && available() && onWord();
}

}  // namespace lexipoint::lookup
