// V8 R4: every keyboard layer fits the hit table (activities/util/KeyboardLayers.h). Past it, InteractionBuffer
// drops the extra keys and they stop answering taps, so an SDK layout that grows fails here instead.

#include <gtest/gtest.h>

#include "activities/util/KeyboardCursorHit.h"
#include "activities/util/KeyboardLayers.h"

namespace {

size_t keysIn(const freeink::ui::KeyboardLayout& layout) {
  size_t keys = 0;
  for (uint8_t r = 0; r < layout.rowCount; r++) keys += layout.rows[r].count;
  return keys;
}

}  // namespace

TEST(KeyboardLayers, EveryLayerFitsTheHitTable) {
  struct Layer {
    const char* name;
    bool shifted;
    bool symbols;
  };
  for (const Layer layer : {Layer{"lower case", false, false}, Layer{"upper case", true, false},
                            Layer{"symbols 1", false, true}, Layer{"symbols 2", true, true}}) {
    const size_t keys = keysIn(keyboardLayer(layer.shifted, layer.symbols));
    EXPECT_GT(keys, 0u) << layer.name;
    EXPECT_LE(keys, kKeyboardInteractions) << layer.name;
  }
}

// V8 R7: where a tap on a line of the text field puts the cursor (activities/util/KeyboardCursorHit.h). A line of
// "abc" drawn from x = 100, 10 px a character.
namespace {

size_t hitAbc(const int x) {
  return cursorInLine(
      x, 100, 30, 0, 3, [](const size_t i) { return i + 1; }, [](const size_t i) { return static_cast<int>(i) * 10; });
}

}  // namespace

TEST(KeyboardCursorHit, LeftOfTheLineIsItsStartAndRightOfItItsEnd) {
  EXPECT_EQ(hitAbc(0), 0u);
  EXPECT_EQ(hitAbc(100), 0u);  // on the left edge
  EXPECT_EQ(hitAbc(130), 3u);  // on the right edge
  EXPECT_EQ(hitAbc(400), 3u);
}

TEST(KeyboardCursorHit, InsideACharacterItsMidpointDecides) {
  EXPECT_EQ(hitAbc(104), 0u);  // before a's midpoint (105)
  EXPECT_EQ(hitAbc(105), 1u);  // on it: after a
  EXPECT_EQ(hitAbc(114), 1u);
  EXPECT_EQ(hitAbc(125), 3u);  // c's midpoint
  EXPECT_EQ(hitAbc(124), 2u);
}

TEST(KeyboardCursorHit, AMultiByteCharacterIsOneStep) {
  // "é" (2 bytes) then "b": boundaries at bytes 0, 2 and 3.
  const auto next = [](const size_t i) { return i == 0 ? size_t{2} : i + 1; };
  const auto widthTo = [](const size_t i) { return i == 0 ? 0 : (i == 2 ? 10 : 20); };
  EXPECT_EQ(cursorInLine(104, 100, 20, 0, 3, next, widthTo), 0u);
  EXPECT_EQ(cursorInLine(106, 100, 20, 0, 3, next, widthTo), 2u);
  EXPECT_EQ(cursorInLine(116, 100, 20, 0, 3, next, widthTo), 3u);
}
