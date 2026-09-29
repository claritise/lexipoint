#pragma once

// V9a: the page under a card, marked once per what decides its marks (page/ReaderMarks.h). Pure, header-only; tests:
// test/lexirise_page/PageMarksTest.cpp.

#include <cstdint>
#include <optional>
#include <vector>

#include "PageStore.h"
#include "lexirise/card/DisplayList.h"

namespace lexipoint::page {

// The page under a card is drawn twice a frame; its marks are worked out once and drawn again from here while the page
// (the same laid-out page object, at the same place) and what decides them (the vocab mirror's and the ignore list's
// revisions) are the same: a save or an ignore on the card shows on its next frame.
struct CardMarks {
  struct Key {
    PageKey page;
    const void* laidOut = nullptr;  // word select's page object: its text can't change under the card
    uint32_t vocab = 0;
    uint32_t ignored = 0;
    bool operator==(const Key&) const = default;
  };
  std::optional<Key> key;
  std::vector<card::Rect> fills;
  bool holds(const Key& now) const { return key && *key == now; }
  void keep(const Key& now, std::vector<card::Rect> drawn) {
    key = now;
    fills = std::move(drawn);
  }
  void drop() {
    key.reset();
    fills.clear();
  }
};

}  // namespace lexipoint::page
