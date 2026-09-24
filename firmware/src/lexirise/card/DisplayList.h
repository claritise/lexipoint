#pragma once

// What the card layout produces: draw commands in screen coordinates (portrait 480×800) and the touch
// targets. Pure data: the device paints it with GfxRenderer (CardPainter), the host rasterizes it for the
// conformance images (test/lexirise_card). Tests: test/lexirise_card.

#include <cstdint>
#include <string>
#include <vector>

namespace lexipoint::card {

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  int right() const { return x + w; }
  int bottom() const { return y + h; }
  bool contains(const int px, const int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

// The fonts the card draws with (popup-ui.md §1.1 "Typefaces"): CrossPoint's UI sans for everything but
// Japanese/Chinese text, which uses the reader's font family. Each has one real size on the device; the
// layout picks the nearest to §1.1's size (logged in the P4 ledger note).
enum class Font : uint8_t {
  UiSmall,       // SMALL (16.7 px): §1.1 sizes 14-18
  Ui,            // UI_10 (20.8 px): 20-23
  UiBold,        // UI_10 bold
  ReaderSmall,   // the reader family at 8 pt (16.7 px): readings, the surface form
  ReaderMedium,  // 10 pt (20.8 px): sentences, forms
  ReaderLarge,   // 18 pt (37.5 px): the word, a character
  Page,          // the reader's own font and size: the page and the strips
};

// Glyphs no device font has (✕ ▼ ▲ ⋯), drawn as shapes in their box (sanctioned deviation 2).
enum class Shape : uint8_t { Cross, TriangleDown, TriangleUp, Ellipsis, Chevron };

struct Command {
  enum class Kind : uint8_t { Fill, Frame, RoundedFrame, Text, Shape, Line };
  Kind kind = Kind::Fill;
  Rect rect;          // Fill/Frame/RoundedFrame/Shape: the box. Text: x, y = top-left of the line box
  int thickness = 1;  // Frame/RoundedFrame/Line
  int radius = 0;     // RoundedFrame
  bool black = true;
  Font font = Font::Ui;  // Text
  Shape shape = Shape::Cross;
  std::string text;  // Text (UTF-8)
};

enum class Target : uint8_t {
  Level,        // index: 0-3 (T L F K)
  RankRow,      // ▼ / ▲: toggle the view
  Close,        // ✕
  ReadingLine,  // Japanese: kana ⇄ romaji
  Tab,          // index: the tab
  Action,       // index: the ⋯ action
  ToastUndo,    // the "… · Undo" toast (popup-ui.md §3.2): tapping it reverts the save
  Card,         // anywhere else on the card (swallows the tap)
};

struct Hit {
  Target target = Target::Card;
  int index = 0;
  Rect rect;
};

// The first hit containing the point (hits are front-most first), or nullptr.
inline const Hit* hitAt(const std::vector<Hit>& hits, const int x, const int y) {
  for (const Hit& h : hits) {
    if (h.rect.contains(x, y)) return &h;
  }
  return nullptr;
}

struct DisplayList {
  std::vector<Command> commands;
  std::vector<Hit> hits;  // front-most first: hitAt() returns the first that contains the point
  Rect card;              // the card's outer box
  Rect toast;             // empty when there's none

  void fill(const Rect& r, const bool black = true) { add(Command::Kind::Fill, r, 1, 0, black); }
  void frame(const Rect& r, const int thickness, const bool black = true) {
    add(Command::Kind::Frame, r, thickness, 0, black);
  }
  void roundedFrame(const Rect& r, const int thickness, const int radius) {
    add(Command::Kind::RoundedFrame, r, thickness, radius, true);
  }
  void text(const Font font, const int x, const int y, std::string s, const bool black = true) {
    Command& c = add(Command::Kind::Text, {x, y, 0, 0}, 1, 0, black);
    c.font = font;
    c.text = std::move(s);
  }
  void shape(const Shape s, const Rect& r, const bool black = true) {
    add(Command::Kind::Shape, r, 1, 0, black).shape = s;
  }
  void line(const int x, const int y, const int w, const int thickness) {  // horizontal
    add(Command::Kind::Line, {x, y, w, thickness}, thickness, 0, true);
  }
  void hit(const Target t, const Rect& r, const int index = 0) { hits.push_back({t, index, r}); }

  const Hit* hitAt(const int x, const int y) const { return card::hitAt(hits, x, y); }

 private:
  Command& add(const Command::Kind kind, const Rect& r, const int thickness, const int radius, const bool black) {
    Command c;
    c.kind = kind;
    c.rect = r;
    c.thickness = thickness;
    c.radius = radius;
    c.black = black;
    commands.push_back(std::move(c));
    return commands.back();
  }
};

// Text measurement, per font: the device measures with GfxRenderer, tests with fixed or real metrics.
class TextMetrics {
 public:
  virtual ~TextMetrics() = default;
  virtual int lineHeight(Font font) const = 0;  // the font's own line box (CSS line-height: normal)
  virtual int ascender(Font font) const = 0;    // line box top to baseline
  // Advance width. For a UI font, of its runs (TextRuns.h: CJK in the reader family), as it's drawn.
  virtual int width(Font font, const std::string& text) const = 0;
};

}  // namespace lexipoint::card
