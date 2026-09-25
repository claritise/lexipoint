// Host tool (not a test): lays out one card state with the device's own font metrics and writes the
// display list as JSON, for scripts/lexipoint/cardshots.py to rasterize and compare with the reference
// (popup-ui.md §1.1, the P4 design conformance pre-check before the on-device screenshots).
//
//   LexiriseCardRender <out.json> <ja|zh> <word> <card|expanded> <tab> <high|low> <kana|romaji> [level]
//
// Also the source of the golden display lists (test/lexirise_card/golden, scripts/lexipoint/cardgolden.py)
// that pin the approved layout in ctest.
//
// UI fonts: the built-in headers (exact advances, kerning, line heights). Reader fonts: the NotoSerifCJK
// .cpfont metrics (line heights from their headers; CJK advances are one em at 150 dpi).

#include <EpdFont.h>
#include <Utf8.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "builtinFonts/notosans_8_regular.h"
#include "builtinFonts/ubuntu_10_bold.h"
#include "builtinFonts/ubuntu_10_regular.h"
#include "lexirise/card/BenchSource.h"
#include "lexirise/card/CardFrame.h"
#include "lexirise/card/CardLayout.h"
#include "lexirise/card/ShapeGeometry.h"
#include "lexirise/card/TextRuns.h"

using namespace lexipoint::card;

namespace {

// The NotoSerifCJK files' metrics (their headers; sd-card/fonts/NotoSerifCJK at the repo root, gitignored).
struct ReaderSize {
  int lineHeight;
  int ascender;
  float em;  // px: pt × 150 / 72
};
constexpr ReaderSize kReader8{24, 20, 16.6667f};
constexpr ReaderSize kReader10{30, 24, 20.8333f};
constexpr ReaderSize kReader12{36, 29, 25.0f};  // the bench page: the size nearest the reference's 19 px × 1.41
constexpr ReaderSize kReader18{54, 44, 37.5f};

class DeviceMetrics final : public TextMetrics {
 public:
  int lineHeight(const Font f) const override {
    switch (f) {
      case Font::UiSmall:
        return notosans_8_regular.advanceY;
      case Font::Ui:
        return ubuntu_10_regular.advanceY;
      case Font::UiBold:
        return ubuntu_10_bold.advanceY;
      case Font::ReaderSmall:
        return kReader8.lineHeight;
      case Font::ReaderMedium:
        return kReader10.lineHeight;
      case Font::ReaderLarge:
        return kReader18.lineHeight;
      case Font::Page:
        return kReader12.lineHeight;
    }
    return 0;
  }

  int ascender(const Font f) const override {
    switch (f) {
      case Font::UiSmall:
        return notosans_8_regular.ascender;
      case Font::Ui:
        return ubuntu_10_regular.ascender;
      case Font::UiBold:
        return ubuntu_10_bold.ascender;
      case Font::ReaderSmall:
        return kReader8.ascender;
      case Font::ReaderMedium:
        return kReader10.ascender;
      case Font::ReaderLarge:
        return kReader18.ascender;
      case Font::Page:
        return kReader12.ascender;
    }
    return 0;
  }

  int width(const Font f, const std::string& text) const override {
    return runsWidth(f, text, [this](const Font rf, const std::string& t) { return single(rf, t); });
  }

  int single(const Font f, const std::string& text) const {
    switch (f) {
      case Font::UiSmall:
        return ui(small_, text);
      case Font::Ui:
        return ui(regular_, text);
      case Font::UiBold:
        return ui(bold_, text);
      case Font::ReaderSmall:
        return reader(kReader8, text);
      case Font::ReaderMedium:
        return reader(kReader10, text);
      case Font::ReaderLarge:
        return reader(kReader18, text);
      case Font::Page:
        return reader(kReader12, text);
    }
    return 0;
  }

 private:
  // As GfxRenderer::getTextAdvanceX: 12.4 advances plus kerning, summed, rounded once.
  static int ui(const EpdFont& font, const std::string& text) {
    int32_t fp = 0;
    uint32_t prev = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
    while (const uint32_t cp = utf8NextCodepoint(&p)) {
      const EpdGlyph* g = font.getGlyph(cp);
      if (g) fp += g->advanceX;
      if (prev) fp += font.getKerning(prev, cp);
      prev = cp;
    }
    return fp4::toPixel(fp);
  }
  // CJK full width; anything else about half (the reader font's Latin is proportional; close enough for
  // the few Latin characters the card sets in it).
  static int reader(const ReaderSize& size, const std::string& text) {
    float w = 0;
    const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
    while (const uint32_t cp = utf8NextCodepoint(&p)) w += utf8IsCjkCodepoint(cp) ? size.em : size.em * 0.5f;
    return static_cast<int>(std::lround(w));
  }

  EpdFont small_{&notosans_8_regular};
  EpdFont regular_{&ubuntu_10_regular};
  EpdFont bold_{&ubuntu_10_bold};
};

const char* fontName(const Font f) {
  switch (f) {
    case Font::UiSmall:
      return "UiSmall";
    case Font::Ui:
      return "Ui";
    case Font::UiBold:
      return "UiBold";
    case Font::ReaderSmall:
      return "ReaderSmall";
    case Font::ReaderMedium:
      return "ReaderMedium";
    case Font::ReaderLarge:
      return "ReaderLarge";
    case Font::Page:
      return "Page";
  }
  return "?";
}

const char* shapeName(const Shape s) {
  switch (s) {
    case Shape::Cross:
      return "cross";
    case Shape::TriangleDown:
      return "down";
    case Shape::TriangleUp:
      return "up";
    case Shape::Ellipsis:
      return "ellipsis";
    case Shape::Chevron:
      return "chevron";
  }
  return "?";
}

std::string json(const std::string& s) {
  std::string out = "\"";
  for (const char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

void write(std::ofstream& f, const DisplayList& list, const DeviceMetrics& metrics) {
  bool first = true;
  for (const Command& c : list.commands) {
    f << (first ? "" : ",\n") << "{";
    first = false;
    f << "\"x\":" << c.rect.x << ",\"y\":" << c.rect.y << ",\"w\":" << c.rect.w << ",\"h\":" << c.rect.h
      << ",\"black\":" << (c.black ? "true" : "false");
    switch (c.kind) {
      case Command::Kind::Fill:
        f << ",\"kind\":\"fill\"";
        break;
      case Command::Kind::Frame:
        f << ",\"kind\":\"frame\",\"t\":" << c.thickness;
        break;
      case Command::Kind::RoundedFrame:
        f << ",\"kind\":\"rframe\",\"t\":" << c.thickness << ",\"r\":" << c.radius;
        break;
      case Command::Kind::Line:
        f << ",\"kind\":\"line\",\"t\":" << c.thickness;
        break;
      case Command::Kind::Shape: {  // expanded to its ink (ShapeGeometry.h), as the device draws it
        const ShapeInk ink = shapeInk(c.shape, c.rect);
        f << ",\"kind\":\"ink\",\"shape\":\"" << shapeName(c.shape) << "\",\"sw\":" << ink.strokeWidth
          << ",\"polygons\":[";
        for (size_t i = 0; i < ink.polygons.size(); i++) {
          f << (i ? "," : "") << "[";
          for (size_t j = 0; j < ink.polygons[i].size(); j++) {
            f << (j ? "," : "") << "[" << ink.polygons[i][j].x << "," << ink.polygons[i][j].y << "]";
          }
          f << "]";
        }
        f << "],\"strokes\":[";
        for (size_t i = 0; i < ink.strokes.size(); i++) {
          const auto& s = ink.strokes[i];
          f << (i ? "," : "") << "[" << s.from.x << "," << s.from.y << "," << s.to.x << "," << s.to.y << "]";
        }
        f << "],\"dots\":[";
        for (size_t i = 0; i < ink.dots.size(); i++) {
          const Rect& d = ink.dots[i];
          f << (i ? "," : "") << "[" << d.x << "," << d.y << "," << d.w << "," << d.h << "]";
        }
        f << "]";
        break;
      }
      case Command::Kind::Text: {  // as runs (TextRuns.h), as the device draws it
        f << ",\"kind\":\"text\",\"runs\":[";
        const auto runs = placeRuns(metrics, c.font, c.rect.x, c.rect.y, c.text,
                                    [&](const Font rf, const std::string& t) { return metrics.single(rf, t); });
        for (size_t i = 0; i < runs.size(); i++) {
          const auto& r = runs[i];
          f << (i ? "," : "") << "{\"font\":\"" << fontName(r.font) << "\",\"text\":" << json(r.text)
            << ",\"x\":" << r.x << ",\"y\":" << r.y << ",\"asc\":" << metrics.ascender(r.font) << "}";
        }
        f << "]";
        break;
      }
    }
    f << "}";
  }
}

const char* targetName(const Target t) {
  switch (t) {
    case Target::Level:
      return "level";
    case Target::RankRow:
      return "rank";
    case Target::Close:
      return "close";
    case Target::ReadingLine:
      return "reading";
    case Target::Tab:
      return "tab";
    case Target::Action:
      return "action";
    case Target::ToastUndo:
      return "undo";
    case Target::Card:
      return "card";
    case Target::OwnWord:
      return "word";
  }
  return "?";
}

// The touch targets, front-most first (scripts/lexipoint/lxctl.py card-smoke taps their centres).
void writeHits(std::ofstream& f, const DisplayList& list) {
  for (size_t i = 0; i < list.hits.size(); i++) {
    const Hit& h = list.hits[i];
    f << (i ? ",\n" : "") << "{\"target\":\"" << targetName(h.target) << "\",\"index\":" << h.index
      << ",\"x\":" << h.rect.x << ",\"y\":" << h.rect.y << ",\"w\":" << h.rect.w << ",\"h\":" << h.rect.h << "}";
  }
}

struct Point2 {
  int x;
  int y;
};

// Taps the first target of that kind (and index) in the current frame, as a finger would; records where
// (the target's centre), for lxctl.py card-smoke to replay on the device.
bool tapTarget(CardController& c, const DeviceMetrics& metrics, const Target target, const int index,
               const unsigned long now, std::vector<Point2>& taps) {
  const Frame frame = composeFrame(c, metrics);
  for (const Hit& h : frame.card.hits) {
    if (h.target == target && h.index == index) {
      taps.push_back({h.rect.x + h.rect.w / 2, h.rect.y + h.rect.h / 2});
      c.tap(&h, now);
      return true;
    }
  }
  return false;
}

}  // namespace

int main(const int argc, char** argv) {
  if (argc < 8) {
    std::fprintf(stderr, "usage: %s out.json ja|zh word card|expanded tab high|low kana|romaji [level]\n", argv[0]);
    return 2;
  }
  const BenchBook& book = std::strcmp(argv[2], "zh") == 0 ? benchChinese() : benchJapanese();
  const int word = std::atoi(argv[3]);
  if (word < 0 || word >= static_cast<int>(book.words.size())) return 2;
  const DeviceMetrics metrics;
  // The state is reached through the controller's own inputs, as on the device: open, step to the word,
  // let the phases finish, then tap ▼, the tab, the reading line, a level.
  BenchSource source(book, std::strcmp(argv[6], "low") == 0);
  CardController c(source, ReadingMode::Kana);
  unsigned long now = 0;
  c.open(now);
  const int steps = word - c.word();  // side-button presses: + next, − previous
  while (c.word() < word) c.step(+1, now);
  while (c.word() > word) c.step(-1, now);
  now += 60 * 1000;
  c.tick(now);
  bool ok = true;
  std::vector<Point2> taps;
  if (std::strcmp(argv[4], "expanded") == 0) ok = ok && tapTarget(c, metrics, Target::RankRow, 0, now, taps);
  if (const int tab = std::atoi(argv[5]); tab != 0) ok = ok && tapTarget(c, metrics, Target::Tab, tab, now, taps);
  if (std::strcmp(argv[7], "romaji") == 0) ok = ok && tapTarget(c, metrics, Target::ReadingLine, 0, now, taps);
  // Then optional arguments: a level index to tap (the save toast), and +extra error states (P6) the
  // reference doesn't have: +unanswered (phase B brought no meaning), +save-failed, +rate-limited.
  std::string extra;
  for (int i = 8; i < argc; i++) {
    if (argv[i][0] == '+') {
      extra = argv[i] + 1;
    } else {
      ok = ok && tapTarget(c, metrics, Target::Level, std::atoi(argv[i]), now, taps);
    }
  }
  if (extra == "unanswered") {
    source.unanswered();
    c.sourceChanged(0);
  } else if (extra == "save-failed") {
    c.levelFailed(c.word(), c.state().level, now, CallFailure::Network, Level::Learning, 0);
  } else if (extra == "rate-limited") {  // the longest toast: the largest back-off (config::kRetryAfterMaxS)
    c.levelFailed(c.word(), c.state().level, now, CallFailure::RateLimited, Level::Learning,
                  lexipoint::config::kRetryAfterMaxS);
  } else if (!extra.empty()) {
    std::fprintf(stderr, "unknown state +%s\n", extra.c_str());
    return 2;
  }
  if (!ok) {
    std::fprintf(stderr, "a target to reach the state wasn't there\n");
    return 3;
  }
  const Frame frame = composeFrame(c, metrics);

  std::ofstream f(argv[1]);
  f << "{\"page\":[\n";
  if (frame.pageShown) write(f, frame.scene.page, metrics);
  f << "],\n\"card\":[\n";
  write(f, frame.card, metrics);
  f << "],\n\"hits\":[\n";
  writeHits(f, frame.card);
  f << "],\n\"extra\":\"" << extra << "\",\n\"word\":" << c.word() << ",\n\"steps\":" << steps << ",\n\"taps\":[";
  for (size_t i = 0; i < taps.size(); i++) f << (i ? "," : "") << "[" << taps[i].x << "," << taps[i].y << "]";
  f << "]}\n";
  return f.good() ? 0 : 1;
}
