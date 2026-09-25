#pragma once

// Who a long-press on the reading page belongs to (lookup-flow.md §1, D15). The touch long-press fires
// at 500 ms while the finger is still down; CrossPoint's own "long-press behaviour" setting (chapter skip
// / rotate, off by default) acts on a ≥700 ms hold of a page-turn tap zone, on release. So with that
// setting on in a tap mode, the outer page-turn zones stay CrossPoint's and the lookup owns the centre;
// with it off (or in swipe mode or with touch controls off, which have no hold action), the lookup owns
// the whole page. On a word, a long-press is a lookup; anywhere else in the lookup's zone it does nothing
// (claritise, 2026-09-25: it's taken and dropped, so its lift is no tap either). When nothing can answer, the
// gesture isn't taken at all, so a slow tap still turns the page or opens the menu as before. Pure; tests:
// test/lexirise_lookup.

#include <cstdint>
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

// What the reader does with a long-press.
enum class LongPressUse : uint8_t {
  Leave,   // not the lookup's (none this frame, CrossPoint's zone, nothing can answer): CrossPoint handles it
  LookUp,  // on a word: consume it and look the word up
  Ignore,  // the lookup's zone but no word under it (a margin, an image, blank space): consume it, nothing else
};

// Whether the reader consumes a long-press of this use, so its lift is no tap (a lookup, or nothing).
constexpr bool consumes(const LongPressUse use) { return use != LongPressUse::Leave; }

// The use of a long-press that fired at pressX (nullopt: none this frame). `available` (lookupsAvailable for
// the open book, which reads the settings) is only asked once one has fired and the lookup owns its zone,
// and `onWord` (whether the press is on a word of the page, which loads and measures it) only after that.
template <typename AvailableFn, typename OnWordFn>
LongPressUse longPressUse(const std::optional<int> pressX, const LongPressRules& rules, AvailableFn&& available,
                          OnWordFn&& onWord) {
  if (!pressX || !lookupOwnsLongPress(*pressX, rules) || !available()) return LongPressUse::Leave;
  return onWord() ? LongPressUse::LookUp : LongPressUse::Ignore;
}

// Its name in the reader's debug log (`lxctl reader-longpress` reads it).
inline const char* longPressUseName(const LongPressUse use) {
  switch (use) {
    case LongPressUse::Leave:
      return "left";
    case LongPressUse::LookUp:
      return "taken";
    case LongPressUse::Ignore:
      return "ignored";
  }
  return "left";  // not reached
}

}  // namespace lexipoint::lookup
