#pragma once

// The orientation the card is drawn in (popup-ui.md §1.1 is measured on the 480×800 portrait panel): an
// upside-down portrait reader keeps its orientation, a landscape one is drawn in portrait while the card
// is open. A template over the orientation enum, so it's host-tested without GfxRenderer.

namespace lexipoint::card {

template <typename Orientation>
Orientation cardOrientation(const Orientation current, const Orientation portrait, const Orientation portraitInverted) {
  return current == portrait || current == portraitInverted ? current : portrait;
}

}  // namespace lexipoint::card
