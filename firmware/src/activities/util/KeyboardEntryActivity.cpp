#include "KeyboardEntryActivity.h"

#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "KeyboardCursorHit.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

constexpr fui::ActionId ACTION_KEY = 1;

}  // namespace

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();
  cursorPos = text.length();
  shifted = false;
  symbols = false;
  cursorMode = false;
  togglePos = false;
  passwordVisible = false;
  selRow = 0;
  selCol = 0;
  delPressCount = 0;
  hintVisible = false;
  hintShowTime = 0;
  rightHeld = false;
  rightLongHandled = false;
  savedCursorPos = 0;
  rightStartCursorPos = 0;
  touchRouter.reset();
  touchRouter.holdMs = TOUCH_LONG_PRESS_MS;
  touchRouter.overrideHoldMs = TOUCH_DEL_LONG_PRESS_MS;
  interactionsReady = false;
  requestUpdate();
}

void KeyboardEntryActivity::onExit() { Activity::onExit(); }

const fui::KeyboardLayout& KeyboardEntryActivity::currentLayout() const { return keyboardLayer(shifted, symbols); }

const fui::KeyboardKey* KeyboardEntryActivity::selectedKey() const {
  const fui::KeyboardLayout& layout = currentLayout();
  if (selRow < 0 || selRow >= layout.rowCount) return nullptr;
  const fui::KeyboardRow& row = layout.rows[selRow];
  if (selCol < 0 || selCol >= row.count) return nullptr;
  return &row.keys[selCol];
}

int KeyboardEntryActivity::selectedLogicalIndex() const {
  const fui::KeyboardLayout& layout = currentLayout();
  int index = 0;
  for (int r = 0; r < selRow && r < layout.rowCount; r++) {
    index += layout.rows[r].count;
  }
  return index + selCol;
}

void KeyboardEntryActivity::clampSelection() {
  const fui::KeyboardLayout& layout = currentLayout();
  if (layout.rowCount == 0) {
    selRow = 0;
    selCol = 0;
    return;
  }
  if (selRow < 0) selRow = 0;
  if (selRow >= layout.rowCount) selRow = layout.rowCount - 1;
  const int cols = layout.rows[selRow].count;
  if (selCol < 0) selCol = 0;
  if (selCol >= cols) selCol = cols > 0 ? cols - 1 : 0;
}

void KeyboardEntryActivity::moveSelectionRow(const int delta) {
  const fui::KeyboardLayout& layout = currentLayout();
  if (layout.rowCount == 0) return;
  const int oldCols = selRow < layout.rowCount ? layout.rows[selRow].count : 1;
  selRow = (selRow + delta + layout.rowCount) % layout.rowCount;
  const int newCols = layout.rows[selRow].count;
  // Proportional column mapping keeps vertical travel intuitive between rows
  // of different key counts (e.g. a 10-key letter row over a 6-key bottom row).
  if (oldCols > 0 && newCols > 0 && oldCols != newCols) {
    selCol = selCol * newCols / oldCols;
  }
  clampSelection();
}

void KeyboardEntryActivity::moveSelectionCol(const int delta) {
  const fui::KeyboardLayout& layout = currentLayout();
  if (selRow < 0 || selRow >= layout.rowCount) return;
  const int cols = layout.rows[selRow].count;
  if (cols <= 0) return;
  selCol = (selCol + delta + cols) % cols;
}

bool KeyboardEntryActivity::syncSelectionToValue(const int16_t value) {
  const fui::KeyboardLayout& layout = currentLayout();
  for (int r = 0; r < layout.rowCount; r++) {
    for (int c = 0; c < layout.rows[r].count; c++) {
      if (layout.rows[r].keys[c].value == value) {
        selRow = r;
        selCol = c;
        return true;
      }
    }
  }
  return false;
}

size_t KeyboardEntryActivity::utf8Prev(const std::string& s, size_t pos) {
  if (pos == 0) return 0;
  pos--;
  while (pos > 0 && (static_cast<uint8_t>(s[pos]) & 0xC0) == 0x80) pos--;
  return pos;
}

size_t KeyboardEntryActivity::utf8Next(const std::string& s, size_t pos) {
  if (pos >= s.length()) return s.length();
  pos++;
  while (pos < s.length() && (static_cast<uint8_t>(s[pos]) & 0xC0) == 0x80) pos++;
  return pos;
}

void KeyboardEntryActivity::insertUtf8(const char* out) {
  if (!out || !*out) return;
  const size_t n = strlen(out);
  if (maxLength != 0 && text.length() + n > maxLength) return;
  if (cursorPos > text.length()) cursorPos = text.length();
  text.insert(cursorPos, out, n);
  cursorPos += n;
}

bool KeyboardEntryActivity::backspaceUtf8() {
  if (text.empty() || cursorPos == 0) return false;
  const size_t prev = utf8Prev(text, cursorPos);
  text.erase(prev, cursorPos - prev);
  cursorPos = prev;
  return true;
}

bool KeyboardEntryActivity::activateValue(const int16_t value, const bool longPress) {
  switch (value) {
    case fui::QWERTY_KEY_SHIFT:
      delPressCount = 0;
      hintVisible = false;
      // Letters: case toggle. Symbols: pages between "?123" and "#+=".
      shifted = !shifted;
      clampSelection();
      return true;
    case fui::QWERTY_KEY_MODE:
      delPressCount = 0;
      hintVisible = false;
      symbols = !symbols;
      shifted = false;
      clampSelection();
      return true;
    case fui::QWERTY_KEY_ENTER:
      onComplete(text);
      return false;
    case fui::QWERTY_KEY_BACKSPACE:
      if (longPress) {
        text.clear();
        cursorPos = 0;
        return true;
      }
      delPressCount++;
      if (delPressCount >= 2) {
        hintVisible = true;
        hintShowTime = millis();
      }
      backspaceUtf8();
      return true;
    default: {
      delPressCount = 0;
      hintVisible = false;
      const fui::KeyboardLayout& layer = currentLayout();
      // keyboardAltOutputFor covers explicit alts and the letter case-flip.
      const char* out = longPress ? fui::keyboardAltOutputFor(layer, value) : nullptr;
      if (!out) out = fui::keyboardOutputFor(layer, value);
      if (!out) return false;
      insertUtf8(out);
      if (shifted && !symbols) {
        shifted = false;  // shift auto-releases after one character
        clampSelection();
      }
      return true;
    }
  }
}

bool KeyboardEntryActivity::clearAllOrAltOnSelected() {
  const fui::KeyboardKey* key = selectedKey();
  if (!key) return false;
  if (key->value == fui::QWERTY_KEY_BACKSPACE) {
    text.clear();
    cursorPos = 0;
    return true;
  }
  // Explicit alts and the letter case-flip, same as touch long-press.
  const char* alt = fui::keyboardAltOutputFor(currentLayout(), key->value);
  if (alt) {
    insertUtf8(alt);
    return true;
  }
  return false;
}

std::string KeyboardEntryActivity::displayTextForCurrentState() const {
  std::string displayText = text;
  if (inputType != InputType::Password || passwordVisible) {
    return displayText;
  }

  size_t revealPos;
  if (cursorMode) {
    revealPos = text.length();  // no reveal in displayText; block draws actual char directly
  } else {
    revealPos = (text.length() > 0 && cursorPos > 0) ? cursorPos - 1 : std::string::npos;
  }
  for (size_t i = 0; i < displayText.length(); i++) {
    if (i != revealPos) {
      displayText[i] = '*';
    }
  }
  return displayText;
}

int KeyboardEntryActivity::measureRange(std::string& s, const int start, const int end) const {
  if (end <= start) return 0;
  // s[end] is writable even at s.length() (the terminator slot); only '\0' may
  // be written there, which is exactly what the measurement needs.
  const char saved = s[end];
  s[end] = '\0';
  const int width = renderer.getTextAdvanceX(UI_12_FONT_ID, s.c_str() + start, EpdFontFamily::REGULAR);
  s[end] = saved;
  return width;
}

int KeyboardEntryActivity::lineBreakEnd(std::string& s, const int start, const int maxWidth) const {
  const int len = static_cast<int>(s.length());
  if (measureRange(s, start, len) <= maxWidth) return len;
  int lo = start + 1;
  int hi = len - 1;
  int best = start + 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    if (measureRange(s, start, mid) <= maxWidth) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }

  // The byte-index search can stop inside a character; snap back to a boundary,
  // keeping one whole character so the wrap loop always advances.
  const int firstCharEnd = static_cast<int>(utf8Next(s, static_cast<size_t>(start)));
  while (best > start && (static_cast<uint8_t>(s[best]) & 0xC0) == 0x80) best--;
  // Widths measured mid-character are unreliable, so the search can overshoot.
  while (best > firstCharEnd && measureRange(s, start, best) > maxWidth) {
    best = static_cast<int>(utf8Prev(s, static_cast<size_t>(best)));
  }
  return best < firstCharEnd ? firstCharEnd : best;
}

bool KeyboardEntryActivity::cursorPositionFromPoint(const int x, const int y, size_t& position) const {
  // Key taps are the overwhelmingly common case; they land on the keyboard,
  // never the text field, so skip the wrap/measure work entirely.
  if (y >= keyboardRect().y) return false;

  const int pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing +
                          metrics.verticalSpacing * 4 + metrics.keyboardVerticalOffset;

  const int availableWidth = pageWidth;
  const int effectiveMargin = (pageWidth - availableWidth * metrics.keyboardTextFieldWidthPercent / 100) / 2;
  const int toggleGap = inputType == InputType::Password ? 4 : 0;
  const int toggleReserve = inputType == InputType::Password ? std::max(renderer.getTextWidth(UI_12_FONT_ID, "[abc]"),
                                                                        renderer.getTextWidth(UI_12_FONT_ID, "[***]")) +
                                                                   toggleGap
                                                             : 0;
  const int textAreaWidth = pageWidth - 2 * effectiveMargin - toggleReserve;
  const int maxLineWidth = textAreaWidth;
  const bool centerText = metrics.keyboardCenteredText;
  std::string displayText = displayTextForCurrentState();

  int lineStartIdx = 0;
  int lineY = inputStartY;
  int lastLineStartIdx = 0;
  int lastLineEndIdx = static_cast<int>(displayText.length());
  int lastLineStartX = effectiveMargin;
  int lastLineWidth = 0;

  while (true) {
    const int lineEndIdx = lineBreakEnd(displayText, lineStartIdx, maxLineWidth);
    const int textWidth = measureRange(displayText, lineStartIdx, lineEndIdx);
    const int lineStartX = centerText ? effectiveMargin + (maxLineWidth - textWidth) / 2 : effectiveMargin;
    lastLineStartIdx = lineStartIdx;
    lastLineEndIdx = lineEndIdx;
    lastLineStartX = lineStartX;
    lastLineWidth = textWidth;

    if (y >= lineY - metrics.verticalSpacing && y < lineY + lineHeight + metrics.verticalSpacing) {
      position = cursorInLine(
          x, lineStartX, textWidth, static_cast<size_t>(lineStartIdx), static_cast<size_t>(lineEndIdx),
          [&displayText](const size_t i) { return utf8Next(displayText, i); },
          [&](const size_t i) { return measureRange(displayText, lineStartIdx, static_cast<int>(i)); });
      return true;
    }

    if (lineEndIdx == static_cast<int>(displayText.length())) {
      break;
    }

    lineY += lineHeight;
    lineStartIdx = lineEndIdx;
  }

  const int underlineBottom = lineY + lineHeight + metrics.verticalSpacing + 8;
  if (y >= inputStartY - metrics.verticalSpacing && y < underlineBottom && x >= effectiveMargin &&
      x < effectiveMargin + maxLineWidth + toggleReserve) {
    const bool insideText = x < lastLineStartX + lastLineWidth;
    position = static_cast<size_t>(insideText ? lastLineStartIdx : lastLineEndIdx);
    return true;
  }

  return false;
}

fui::Rect KeyboardEntryActivity::keyboardRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int rows = currentLayout().rowCount;
  const int gap = metrics.keyboardKeySpacing;
  const int height = rows * metrics.keyboardKeyHeight + (rows > 1 ? (rows - 1) * gap : 0);
  const int width = pageWidth * metrics.keyboardWidthPercent / 100;
  const int x = (pageWidth - width) / 2;
  const int y =
      pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - height + metrics.keyboardVerticalOffset;
  return fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(width),
                   static_cast<int16_t>(height)};
}

void KeyboardEntryActivity::loop() {
  int tx = 0;
  int ty = 0;

  size_t touchedCursorPos = 0;
  if (mappedInput.wasScreenTapped(tx, ty) && cursorPositionFromPoint(tx, ty, touchedCursorPos)) {
    cursorPos = std::min(touchedCursorPos, text.length());
    // The masked text field maps taps per byte; snap back to a boundary so
    // the cursor never lands inside a multi-byte character.
    while (cursorPos > 0 && cursorPos < text.length() && (static_cast<uint8_t>(text[cursorPos]) & 0xC0) == 0x80) {
      cursorPos--;
    }
    cursorMode = false;
    togglePos = false;
    hintVisible = false;
    touchRouter.reset();
    requestUpdate();
    return;
  }

  if (!cursorMode && interactionsReady) {
    const bool pressedDown = mappedInput.wasScreenTouchDown(tx, ty);
    int tapX = 0;
    int tapY = 0;
    const bool tapped = mappedInput.wasScreenTapped(tapX, tapY);
    int hx = 0;
    int hy = 0;
    const bool inContact = mappedInput.isScreenTouchHeld(hx, hy);

    const fui::TouchHoldRouter::Result result =
        touchRouter.update(interactions, pressedDown, static_cast<int16_t>(tx), static_cast<int16_t>(ty), tapped,
                           static_cast<int16_t>(tapX), static_cast<int16_t>(tapY), inContact, millis());
    if (result.event) {
      syncSelectionToValue(result.event.value);
      if (activateValue(result.event.value, result.event.longPress)) {
        requestUpdate();
      }
      return;
    }
    if (result.activeChanged) {
      requestUpdate();
    }
    if (pressedDown || tapped) {
      return;
    }
  }

  if (!cursorMode && mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    upHeld = true;
    upLongHandled = false;
  }

  if (upHeld && !upLongHandled && mappedInput.isPressed(MappedInputManager::Button::Up) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    cursorMode = true;
    upLongHandled = true;
    hintVisible = true;
    hintShowTime = millis();
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (upHeld && !upLongHandled && !cursorMode) {
      moveSelectionRow(-1);
      requestUpdate();
    }
    upHeld = false;
    upLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    downHeld = true;
    if (cursorMode) {
      togglePos = false;
      passwordVisible = false;
      cursorMode = false;
      hintVisible = false;
      downLongHandled = true;
      requestUpdate();
    } else {
      downLongHandled = false;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (downHeld && !downLongHandled && !cursorMode) {
      moveSelectionRow(1);
      requestUpdate();
    }
    downHeld = false;
    downLongHandled = false;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
    if (cursorMode) return;
    moveSelectionCol(-1);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (cursorMode) {
      if (togglePos) {
        cursorPos = savedCursorPos;
        togglePos = false;
        requestUpdate();
      } else if (cursorPos > 0) {
        cursorPos = utf8Prev(text, cursorPos);
        requestUpdate();
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (cursorMode && inputType == InputType::Password && !togglePos) {
      rightHeld = true;
      rightLongHandled = false;
      rightStartCursorPos = cursorPos;
    }
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] {
    if (cursorMode) return;
    moveSelectionCol(1);
    requestUpdate();
  });

  if (rightHeld && !rightLongHandled && mappedInput.isPressed(MappedInputManager::Button::Right) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    if (cursorMode && inputType == InputType::Password && !togglePos) {
      savedCursorPos = rightStartCursorPos;
      togglePos = true;
      rightLongHandled = true;
      requestUpdate();
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (cursorMode && inputType == InputType::Password) {
      rightHeld = false;
      rightLongHandled = false;
    }
    if (cursorMode && !togglePos && cursorPos < text.length()) {
      cursorPos = utf8Next(text, cursorPos);
      requestUpdate();
    }
    if (cursorMode) return;
    rightHeld = false;
    rightLongHandled = false;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }

  const fui::KeyboardKey* selKey = selectedKey();
  const bool selectedDel = selKey && selKey->value == fui::QWERTY_KEY_BACKSPACE;

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > DEL_LONG_PRESS_MS && selectedDel) {
    clearAllOrAltOnSelected();
    confirmLongHandled = true;
    requestUpdate();
  }

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    if (!selectedDel && clearAllOrAltOnSelected()) {
      requestUpdate();
      confirmLongHandled = true;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (confirmHeld && !confirmLongHandled && !cursorMode) {
      if (selKey && activateValue(selKey->value, false)) {
        requestUpdate();
      }
    } else if (confirmHeld && !confirmLongHandled && cursorMode && inputType == InputType::Password && togglePos) {
      passwordVisible = !passwordVisible;
      requestUpdate();
    }
    confirmHeld = false;
    confirmLongHandled = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onCancel();
  }

  if (hintVisible && !cursorMode && millis() - hintShowTime > 4000) {
    hintVisible = false;
    requestUpdate();
  }
}

void KeyboardEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int inputStartY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing +
                          metrics.verticalSpacing * 4 + metrics.keyboardVerticalOffset;
  int inputHeight = 0;

  std::string displayText = displayTextForCurrentState();

  const bool isPassword = (inputType == InputType::Password);
  const int availableWidth = pageWidth;
  const int effectiveMargin = (pageWidth - availableWidth * metrics.keyboardTextFieldWidthPercent / 100) / 2;
  const int toggleGap = isPassword ? 4 : 0;
  const int toggleReserve = isPassword ? std::max(renderer.getTextWidth(UI_12_FONT_ID, "[abc]"),
                                                  renderer.getTextWidth(UI_12_FONT_ID, "[***]")) +
                                             toggleGap
                                       : 0;
  const int textAreaWidth = pageWidth - 2 * effectiveMargin - toggleReserve;
  const int maxLineWidth = textAreaWidth;
  const bool centerText = metrics.keyboardCenteredText;

  // The cursor spans a whole code point: a lone byte of it renders as a replacement glyph.
  // Masking is per byte, so displayText keeps text's length and the same span applies to both.
  const size_t cursorCharBytes = (cursorPos < text.length()) ? utf8Next(text, cursorPos) - cursorPos : 0;
  char cursorChar[8] = {};         // the character under the cursor
  char displayCursorChar[8] = {};  // same span of displayText, masked for passwords
  if (cursorCharBytes > 0) {
    const size_t n = std::min(cursorCharBytes, sizeof(cursorChar) - 1);
    memcpy(cursorChar, text.data() + cursorPos, n);
    memcpy(displayCursorChar, displayText.data() + cursorPos, n);
  }

  int cursorCharWidth = 6;
  if (cursorCharBytes > 0) {
    int w = renderer.getTextWidth(UI_12_FONT_ID, cursorChar);
    if (w > cursorCharWidth) cursorCharWidth = w;
  }

  int lineStartIdx = 0;
  int textWidth = 0;
  int cursorPixelX = effectiveMargin;
  int cursorLineY = inputStartY;
  bool cursorDrawn = false;

  while (true) {
    const int lineEndIdx = lineBreakEnd(displayText, lineStartIdx, maxLineWidth);
    const std::string lineText = displayText.substr(lineStartIdx, lineEndIdx - lineStartIdx);
    textWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, lineText.c_str(), EpdFontFamily::REGULAR);
    {
      const int lineStartX = centerText ? effectiveMargin + (maxLineWidth - textWidth) / 2 : effectiveMargin;
      const bool isLastLine = (lineEndIdx == static_cast<int>(displayText.length()));
      bool isCursorLine = false;
      if (!cursorDrawn && cursorPos >= lineStartIdx &&
          (isLastLine ? cursorPos <= lineEndIdx : cursorPos < lineEndIdx)) {
        std::string beforeCursor;
        if (isPassword && !passwordVisible && cursorMode) {
          beforeCursor = std::string(cursorPos - lineStartIdx, '*');
        } else {
          beforeCursor = displayText.substr(lineStartIdx, cursorPos - lineStartIdx);
        }
        int beforeWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, beforeCursor.c_str(), EpdFontFamily::REGULAR);
        int throughCursorWidth = beforeWidth;
        int kernOffset = 0;
        if (cursorCharBytes > 0) {
          std::string beforeAndCursor = beforeCursor + displayCursorChar;
          throughCursorWidth = renderer.getTextAdvanceX(UI_12_FONT_ID, beforeAndCursor.c_str(), EpdFontFamily::REGULAR);
          int charAdvance = renderer.getTextAdvanceX(UI_12_FONT_ID, displayCursorChar, EpdFontFamily::REGULAR);
          kernOffset = throughCursorWidth - beforeWidth - charAdvance;
        }
        cursorPixelX = lineStartX + beforeWidth + kernOffset;
        cursorLineY = inputStartY + inputHeight;
        cursorDrawn = true;
        isCursorLine = true;
      }

      if (isCursorLine && cursorMode && isPassword && !passwordVisible && !togglePos) {
        // Draw text in 3 parts to avoid block cursor overflowing onto next char.
        // displayText uses '*' for all chars; actual char may be wider than '*'.
        // Part 1: chars before cursor position
        const std::string part1 = displayText.substr(lineStartIdx, cursorPos - lineStartIdx);
        renderer.drawText(UI_12_FONT_ID, lineStartX, inputStartY + inputHeight, part1.c_str());
        // Part 2: skip cursor slot (block + actual char drawn later)
        // Part 3: chars after cursor position (skip char under cursor), starting at cursorPixelX + cursorCharWidth
        const int afterStart = static_cast<int>(cursorPos + cursorCharBytes);
        const int afterEnd = lineEndIdx;
        if (afterStart < afterEnd) {
          const std::string part3 = displayText.substr(afterStart, afterEnd - afterStart);
          renderer.drawText(UI_12_FONT_ID, cursorPixelX + cursorCharWidth, inputStartY + inputHeight, part3.c_str());
        }
      } else {
        renderer.drawText(UI_12_FONT_ID, lineStartX, inputStartY + inputHeight, lineText.c_str());
      }
      if (lineEndIdx == static_cast<int>(displayText.length())) {
        break;
      }

      inputHeight += lineHeight;
      lineStartIdx = lineEndIdx;
    }
  }

  const int fieldWidth = (inputHeight > 0) ? maxLineWidth : textWidth;
  const int lineMargin = effectiveMargin;
  GUI.drawTextField(renderer, Rect{0, inputStartY, pageWidth, inputHeight}, fieldWidth, cursorMode, lineMargin,
                    pageWidth - 2 * lineMargin);

  if (cursorMode && !togglePos && cursorPos <= displayText.length()) {
    static constexpr int blockPadding = 1;
    renderer.fillRect(cursorPixelX - blockPadding, cursorLineY, cursorCharWidth + blockPadding * 2, lineHeight, true);
    if (cursorCharBytes > 0) {
      renderer.drawText(UI_12_FONT_ID, cursorPixelX, cursorLineY, cursorChar, false);
    }
  } else if (cursorPos <= displayText.length()) {
    static constexpr int serifW = 3;
    const int cX = cursorPixelX;
    const int cY = cursorLineY;
    const int cBottom = cursorLineY + lineHeight - 1;
    renderer.fillRect(cX, cY, 2, lineHeight, true);
    renderer.drawLine(cX - serifW, cY, cX - 1, cY, 2, true);
    renderer.drawLine(cX + 1, cY, cX + serifW, cY, 2, true);
    renderer.drawLine(cX - serifW, cBottom, cX - 1, cBottom, 2, true);
    renderer.drawLine(cX + 1, cBottom, cX + serifW, cBottom, 2, true);
  }

  if (isPassword) {
    const char* toggleLabel = passwordVisible ? "[***]" : "[abc]";
    const int toggleWidth = renderer.getTextWidth(UI_12_FONT_ID, toggleLabel);
    const int toggleX = pageWidth - effectiveMargin - toggleWidth;
    const int toggleY = inputStartY + inputHeight;
    const bool toggleSelected = cursorMode && togglePos;

    if (toggleSelected) {
      renderer.fillRect(toggleX - 2, toggleY, toggleWidth + 5, lineHeight + 3, true);
      renderer.drawText(UI_12_FONT_ID, toggleX, toggleY, toggleLabel, false);
    } else {
      renderer.drawText(UI_12_FONT_ID, toggleX, toggleY, toggleLabel, true);
    }
  }

  if (hintVisible && !text.empty()) {
    const int hintLh = renderer.getLineHeight(SMALL_FONT_ID);
    const int underlineY = inputStartY + inputHeight + lineHeight + metrics.verticalSpacing;
    const int hintY = underlineY + 4;
    if (cursorMode) {
      int hintLineY = hintY;
      if (inputType == InputType::Password && togglePos) {
        renderer.drawCenteredText(
            SMALL_FONT_ID, hintLineY,
            passwordVisible ? tr(STR_KB_HINT_TOGGLE_HIDE_PASSWORD) : tr(STR_KB_HINT_TOGGLE_SHOW_PASSWORD), true);
        hintLineY += hintLh;
        renderer.drawCenteredText(SMALL_FONT_ID, hintLineY, tr(STR_KB_HINT_RETURN_CURSOR), true);
      } else {
        renderer.drawCenteredText(SMALL_FONT_ID, hintLineY, tr(STR_KB_HINT_MOVE_CURSOR), true);
        hintLineY += hintLh;
        if (inputType == InputType::Password) {
          const char* passTip = passwordVisible ? tr(STR_KB_HINT_HIDE_PASSWORD) : tr(STR_KB_HINT_SHOW_PASSWORD);
          renderer.drawCenteredText(SMALL_FONT_ID, hintLineY, passTip, true);
        }
      }
    } else {
      renderer.drawCenteredText(SMALL_FONT_ID, hintY, tr(STR_KB_HINT_EDIT_ENTRY), true);
    }
  }

  const fui::Rect kbRect = keyboardRect();

  const int tipsLh = renderer.getLineHeight(SMALL_FONT_ID);
  const int underlineBottom = inputStartY + inputHeight + lineHeight + metrics.verticalSpacing + 4;
  auto drawTip = [&](const char* tip, int y) { renderer.drawCenteredText(SMALL_FONT_ID, y, tip, true); };

  int tipCount = 0;
  if (cursorMode) {
    tipCount = 1;
  } else if (symbols) {
    tipCount = !text.empty() ? 1 : 0;
  } else {
    tipCount = 1 + (!text.empty() ? 1 : 0);
  }

  if (tipCount > 0) {
    int y = (underlineBottom + kbRect.y) / 2 - (tipCount + 1) * tipsLh / 2;
    drawTip(tr(STR_KB_TIPS), y);
    y += tipsLh;
    if (cursorMode) {
      drawTip(tr(STR_KB_HINT_RETURN_KEYBOARD), y);
    } else if (symbols) {
      if (!text.empty()) {
        drawTip(tr(STR_KB_HINT_CLEAR_TEXT), y);
      }
    } else {
      drawTip(shifted ? tr(STR_KB_HINT_LOWER_SECONDARY) : tr(STR_KB_HINT_UPPER_SECONDARY), y);
      y += tipsLh;
      if (!text.empty()) {
        drawTip(tr(STR_KB_HINT_CLEAR_TEXT), y);
      }
    }
  }

  // The FreeInkUI keyboard draws the keys and registers their hit rects into
  // `interactions`; loop() (the main task) routes touch snapshots against
  // that table via TouchHoldRouter, which reads the published generation
  // (routePublished()/publishedData()) — beginPublishCycle() here makes this
  // render build into the OTHER generation, so loop() never sees a
  // half-rebuilt table no matter when it runs relative to this.
  interactions.beginPublishCycle();
  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_SMALL, SMALL_FONT_ID);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
  const fui::DeviceContext device = target.deviceContext();
  const fui::InputSnapshot noInput{};
  fui::Frame<kKeyboardInteractions> frame(target, device, noInput, interactions);

  fui::KeyboardProps props;
  const fui::KeyboardLayout& layout = currentLayout();
  props.layout = &layout;
  props.keyAction = ACTION_KEY;  // one action id; loop() dispatches on key value
  props.okLabel = tr(STR_OK_BUTTON);
  props.shiftLabel = tr(STR_KEY_SHIFT);
  // Match the label to the layer the mode key leads back from.
  props.modeLabel = symbols ? tr(STR_KEY_MODE_ABC) : tr(STR_KEY_MODE_SYMBOLS);
  props.inputMask = static_cast<uint16_t>(fui::InputTouch | fui::InputLongPress);
  props.selectedIndex = cursorMode ? -1 : static_cast<int16_t>(selectedLogicalIndex());
  props.labelText.font = fui::GfxRendererTarget::FONT_BODY;
  props.altText.font = fui::GfxRendererTarget::FONT_SMALL;
  props.gap = static_cast<int16_t>(metrics.keyboardKeySpacing);
  props.padding = fui::Insets{0, 0, 0, 0};
  // Fingers land low on the bottom row (occlusion) and there is no key below
  // to catch the miss — extend its hit band down to the bottom reserve (the
  // screen edge, as buttonHintsHeight is 0).
  const int hintsTop = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  props.bottomHitOverflow = static_cast<int16_t>(std::max(0, hintsTop - (kbRect.y + kbRect.height)));
  fui::keyboard(frame, kbRect, props);
  interactions.publish();
  interactionsReady = true;

  renderer.displayBuffer();
}

void KeyboardEntryActivity::onComplete(std::string text) {
  setResult(KeyboardResult{std::move(text)});
  finish();
}

void KeyboardEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
