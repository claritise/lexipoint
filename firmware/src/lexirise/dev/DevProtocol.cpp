// Compiled into the dev harness build, and into host tests (which define the flag themselves).
#if LEXIPOINT_DEV_HARNESS

#include "DevProtocol.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace lexipoint::dev {
namespace {

using config::kMaxTokens;
using config::kTokenMax;

struct Tokens {
  char text[kMaxTokens][kTokenMax] = {};
  size_t count = 0;
  bool tooMany = false;
  bool tooLong = false;
};

// Splits on spaces into bounded, NUL-terminated copies.
Tokens tokenize(const char* s) {
  Tokens t;
  while (*s) {
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) break;
    if (t.count == kMaxTokens) {
      t.tooMany = true;
      break;
    }
    size_t n = 0;
    while (*s && *s != ' ' && *s != '\t') {
      if (n + 1 < kTokenMax) {
        t.text[t.count][n++] = *s;
      } else {
        t.tooLong = true;
      }
      s++;
    }
    t.text[t.count][n] = '\0';
    t.count++;
  }
  return t;
}

bool parseInt(const char* s, int& out) {
  if (!*s) return false;
  errno = 0;
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (errno != 0 || *end != '\0' || v < -config::kIntArgLimit || v > config::kIntArgLimit) return false;
  out = static_cast<int>(v);
  return true;
}

bool parseInts(const Tokens& t, const size_t first, int* out[], const size_t n) {
  if (t.count != first + n) return false;
  for (size_t i = 0; i < n; i++) {
    if (!parseInt(t.text[first + i], *out[i])) return false;
  }
  return true;
}

Command fail(const char* error) {
  Command c;
  c.error = error;
  return c;
}

// Copies line into buf without trailing whitespace.
void trimRight(const char* line, char* buf, const size_t size) {
  size_t n = std::strlen(line);
  if (n >= size) n = size - 1;
  std::memcpy(buf, line, n);
  while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t')) n--;
  buf[n] = '\0';
}

}  // namespace

Command parseLine(const char* rawLine) {
  char line[config::kLineMax];
  trimRight(rawLine, line, sizeof(line));

  if (std::strncmp(line, "CMD:", 4) == 0) {
    const char* rest = line + 4;
    while (*rest == ' ') rest++;
    Command c;
    if (std::strcmp(rest, "SCREENSHOT") == 0) c.verb = Verb::LegacyScreenshot;
    return c;  // other upstream CMD: lines: ignored, as upstream does
  }
  if (std::strncmp(line, "LX:", 3) != 0) return Command{};

  const Tokens t = tokenize(line + 3);
  if (t.count == 0) return fail("empty command");
  if (t.tooMany || t.tooLong) return fail("too many or too long arguments");
  const char* verb = t.text[0];
  Command c;

  if (!std::strcmp(verb, "PING")) {
    if (t.count != 1) return fail("usage: PING");
    c.verb = Verb::Ping;
  } else if (!std::strcmp(verb, "TAP") || !std::strcmp(verb, "LONG")) {
    int* xy[] = {&c.x, &c.y};
    if (!parseInts(t, 1, xy, 2)) return fail(verb[0] == 'T' ? "usage: TAP x y" : "usage: LONG x y");
    c.verb = verb[0] == 'T' ? Verb::Tap : Verb::Long;
  } else if (!std::strcmp(verb, "SWIPE")) {
    int* pts[] = {&c.x, &c.y, &c.x2, &c.y2};
    if (!parseInts(t, 1, pts, 4)) return fail("usage: SWIPE x1 y1 x2 y2");
    c.verb = Verb::Swipe;
  } else if (!std::strcmp(verb, "BTN")) {
    if (t.count < 2 || t.count > 3) return fail("usage: BTN LEFT|RIGHT|POWER [ms]");
    const char* name = t.text[1];
    if (!std::strcmp(name, "LEFT") || !std::strcmp(name, "PREV")) {
      c.button = ButtonName::Left;
    } else if (!std::strcmp(name, "RIGHT") || !std::strcmp(name, "NEXT")) {
      c.button = ButtonName::Right;
    } else if (!std::strcmp(name, "POWER")) {
      c.button = ButtonName::Power;
    } else {
      return fail("usage: BTN LEFT|RIGHT|POWER [ms]");
    }
    if (t.count == 3 && !parseInt(t.text[2], c.ms)) return fail("usage: BTN LEFT|RIGHT|POWER [ms]");
    if (c.ms < config::kButtonMinMs) c.ms = config::kButtonMinMs;
    if (c.ms > config::kButtonMaxMs) c.ms = config::kButtonMaxMs;
    c.verb = Verb::Button;
  } else if (!std::strcmp(verb, "HOME")) {
    if (t.count == 1) {
      c.verb = Verb::Home;
    } else if (t.count == 2 && !std::strcmp(t.text[1], "HOLD")) {
      c.verb = Verb::HomeHold;
    } else {
      return fail("usage: HOME [HOLD]");
    }
  } else if (!std::strcmp(verb, "LEXI")) {
    constexpr const char* kUsage = "usage: LEXI ME | ANALYZE ja|zh | SOAK n [COLD] | CARD ja|zh [LOW] [KANA]";
    if (t.count == 2 && !std::strcmp(t.text[1], "ME")) {
      c.lexi = LexiAction::Me;
    } else if (t.count == 3 && !std::strcmp(t.text[1], "ANALYZE")) {
      if (!std::strcmp(t.text[2], "ja")) {
        c.chinese = false;
      } else if (!std::strcmp(t.text[2], "zh")) {
        c.chinese = true;
      } else {
        return fail(kUsage);
      }
      c.lexi = LexiAction::Analyze;
    } else if ((t.count == 3 || t.count == 4) && !std::strcmp(t.text[1], "SOAK")) {
      if (!parseInt(t.text[2], c.count) || c.count < 1 || c.count > config::kLexiSoakMax) return fail(kUsage);
      if (t.count == 4 && std::strcmp(t.text[3], "COLD") != 0) return fail(kUsage);
      c.cold = t.count == 4;
      c.lexi = LexiAction::Soak;
    } else if (t.count >= 3 && t.count <= 5 && !std::strcmp(t.text[1], "CARD")) {
      if (!std::strcmp(t.text[2], "ja")) {
        c.chinese = false;
      } else if (!std::strcmp(t.text[2], "zh")) {
        c.chinese = true;
      } else {
        return fail(kUsage);
      }
      int next = 3;  // then [LOW] [KANA], in that order
      c.low = next < t.count && !std::strcmp(t.text[next], "LOW");
      if (c.low) next++;
      c.kana = next < t.count && !std::strcmp(t.text[next], "KANA");
      if (c.kana) next++;
      if (next != t.count) return fail(kUsage);
      c.lexi = LexiAction::Card;
    } else {
      return fail(kUsage);
    }
    c.verb = Verb::Lexi;
  } else if (!std::strcmp(verb, "AWAKE")) {
    int v = -1;
    if (t.count != 2 || !parseInt(t.text[1], v) || v < 0 || v > 2) return fail("usage: AWAKE 0|1|2");
    c.awake = static_cast<AwakeMode>(v);
    c.verb = Verb::Awake;
  } else {
    struct Simple {
      const char* name;
      Verb verb;
    };
    static constexpr Simple kSimple[] = {{"SHOT", Verb::Shot},
                                         {"MEM", Verb::Mem},
                                         {"SELFTEST", Verb::SelfTest},
                                         {"SYNC", Verb::Sync},
                                         {"REBOOT", Verb::Reboot}};
    for (const auto& s : kSimple) {
      if (!std::strcmp(verb, s.name)) {
        if (t.count != 1) return fail("this command takes no arguments");
        c.verb = s.verb;
        return c;
      }
    }
    return fail("unknown command");
  }
  return c;
}

const char* verbName(const Verb verb) {
  switch (verb) {
    case Verb::Ping:
      return "PING";
    case Verb::Tap:
      return "TAP";
    case Verb::Long:
      return "LONG";
    case Verb::Swipe:
      return "SWIPE";
    case Verb::Button:
      return "BTN";
    case Verb::Home:
    case Verb::HomeHold:
      return "HOME";
    case Verb::Shot:
      return "SHOT";
    case Verb::Mem:
      return "MEM";
    case Verb::SelfTest:
      return "SELFTEST";
    case Verb::Sync:
      return "SYNC";
    case Verb::Awake:
      return "AWAKE";
    case Verb::Reboot:
      return "REBOOT";
    case Verb::Lexi:
      return "LEXI";
    case Verb::LegacyScreenshot:
      return "SCREENSHOT";
    case Verb::None:
    default:
      return "NONE";
  }
}

bool LineAssembler::feed(const char ch) {
  if (ch == '\r' || ch == '\0') return false;
  if (ch == '\n') {
    const bool ready = !overflow_ && len_ > 0;
    buf_[ready ? len_ : 0] = '\0';
    len_ = 0;
    overflow_ = false;
    return ready;
  }
  if (overflow_) return false;
  if (len_ + 1 < sizeof(buf_)) {
    buf_[len_++] = ch;
  } else {
    overflow_ = true;
  }
  return false;
}

}  // namespace lexipoint::dev

#endif  // LEXIPOINT_DEV_HARNESS
