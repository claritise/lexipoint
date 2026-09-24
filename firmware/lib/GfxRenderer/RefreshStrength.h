#pragma once

// LEXIPOINT: which of two refresh modes cleans the panel more (full > half > fast), for a promoted
// refresh that must never weaken the one asked for. A template over the mode enum so it's host-tested
// without the display HAL (test/lexirise_card).

template <typename Mode>
Mode strongerRefresh(const Mode requested, const Mode promoted, const Mode full, const Mode half) {
  const auto strength = [&](const Mode m) { return m == full ? 2 : m == half ? 1 : 0; };
  return strength(requested) > strength(promoted) ? requested : promoted;
}
