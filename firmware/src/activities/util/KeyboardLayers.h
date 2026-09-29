#pragma once

// The on-screen keyboard's layers and the hit table they must fit (KeyboardEntryActivity). Host-testable:
// test/keyboard_layers checks every layer against kKeyboardInteractions.

#include <FreeInkUI.h>

#include <cstddef>

// The English QWERTY layout: WiFi passwords, the Lexirise key and tags, library search.
inline constexpr freeink::ui::KeyboardLayoutId kKeyboardLayoutId = freeink::ui::KeyboardLayoutId::QwertyEn;

// Key hit rects registered by the keyboard component during render(); loop() routes touch snapshots against them.
// Only the keyboard registers: the letter layers with their number row are the largest, and 48 leaves headroom over
// them (test/keyboard_layers fails if an SDK layout outgrows it). Two generations of 16-byte entries, no heap.
// (CrossPoint's 56 fit Cyrillic's wider rows, gone in v0.2 V8.)
inline constexpr size_t kKeyboardInteractions = 48;

// A layer as the keyboard shows it: the letters (lower or upper case) with the number row, or a symbols page.
inline const freeink::ui::KeyboardLayout& keyboardLayer(const bool shifted, const bool symbols) {
  if (symbols) return freeink::ui::builtinKeyboardLayout(kKeyboardLayoutId, shifted, true);
  return freeink::ui::builtinKeyboardLayout(kKeyboardLayoutId, shifted, false, /*numberRow=*/true, /*langKey=*/false);
}
