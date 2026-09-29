#pragma once

#include <EpdFontFamily.h>

#include <deque>
#include <string>

class GfxRenderer {
 public:
  bool isFontCacheScanning() const { return false; }
  void drawLine(int, int, int, int, int, bool) const {}
  void drawText(int, int, int, const char*, bool, EpdFontFamily::Style) const {}
  int getTextWidth(int font, const char* text, EpdFontFamily::Style style) const {
    return getTextAdvanceX(font, text, style);
  }
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  int getLineHeight(int, float = 1.0f) const { return 16; }
  int getFontAscenderSize(int) const { return 12; }
  int getSpaceWidth(int, EpdFontFamily::Style) const { return 4; }
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style) const {
    int width = 0;
    while (*text++) width += 8;
    return width;
  }
  int getKerning(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 0; }
  int getSpaceAdvance(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 4; }
  bool isSdCardFont(int) const { return false; }
  void ensureSdCardFontReady(int, const std::deque<std::string>&, bool, uint8_t) const {}
};
