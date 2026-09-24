#pragma once

// Fixed, round font metrics for layout tests: every expected position can be worked out by hand.

#include <Utf8.h>

#include "lexirise/card/DisplayList.h"

namespace lexipoint::card::test {

class FakeMetrics final : public TextMetrics {
 public:
  int lineHeight(const Font f) const override {
    switch (f) {
      case Font::UiSmall:
        return 23;
      case Font::Ui:
      case Font::UiBold:
        return 24;
      case Font::ReaderSmall:
        return 24;
      case Font::ReaderMedium:
        return 30;
      case Font::ReaderLarge:
        return 54;
      case Font::Page:
        return 36;
    }
    return 0;
  }
  int ascender(const Font f) const override { return lineHeight(f) * 4 / 5; }
  // CJK: the font's em; anything else: half of it.
  int width(const Font f, const std::string& text) const override {
    int w = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
    while (const uint32_t cp = utf8NextCodepoint(&p)) w += utf8IsCjkCodepoint(cp) ? em(f) : em(f) / 2;
    return w;
  }
  static int em(const Font f) {
    switch (f) {
      case Font::UiSmall:
      case Font::ReaderSmall:
        return 16;
      case Font::Ui:
      case Font::UiBold:
      case Font::ReaderMedium:
        return 20;
      case Font::ReaderLarge:
        return 38;
      case Font::Page:
        return 26;
    }
    return 0;
  }
};

}  // namespace lexipoint::card::test
