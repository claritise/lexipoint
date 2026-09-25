#pragma once

// LEXIPOINT dev harness wire protocol: line assembly and command parsing (pure, host-testable).
// One command per line, "LX:<VERB> [args]". See docs/v0.1/dev-harness.md.
// Tests: test/lexirise_dev/DevProtocolTest.cpp.

#include <cstddef>

#include "DevConfig.h"
#include "DevTiming.h"

namespace lexipoint::dev {

enum class Verb {
  None,  // not a command (ignored), or a parse error when Command::error is set
  Ping,
  Tap,
  Long,
  Swipe,
  Button,
  Home,
  HomeHold,
  Shot,
  Mem,
  SelfTest,
  Sync,
  Awake,
  Reboot,
  Lexi,              // LX:LEXI ME | ANALYZE ja|zh | SOAK n: the Lexirise client (P1); CARD ja|zh [LOW]: the bench (P4)
  LegacyScreenshot,  // CrossPoint's "CMD:SCREENSHOT", kept working
};

enum class ButtonName { Left, Right, Power };
enum class LexiAction { Me, Analyze, Soak, Card, Settings };

struct Command {
  Verb verb = Verb::None;
  int x = 0, y = 0, x2 = 0, y2 = 0;
  ButtonName button = ButtonName::Left;
  int ms = config::kButtonDefaultMs;  // buttons, clamped to [kButtonMinMs, kButtonMaxMs]
  AwakeMode awake = AwakeMode::Lease;
  LexiAction lexi = LexiAction::Me;
  bool chinese = false;         // LEXI ANALYZE / CARD language
  bool low = false;             // LEXI CARD: the sentence low on the page (D17)
  bool kana = false;            // LEXI CARD … KANA: opens in kana, never saves the reading (card-smoke)
  int count = 0;                // LEXI SOAK calls, in [1, config::kLexiSoakMax]
  bool cold = false;            // LEXI SOAK n COLD: WiFi and TLS torn down between calls
  const char* error = nullptr;  // set when an LX: line is malformed; the verb is None
};

// Parses one complete line (no newline). Trailing whitespace is ignored. Lines that aren't
// "LX:" or "CMD:" commands give Verb::None with no error, so ordinary chatter is ignored.
Command parseLine(const char* line);

// The name used in "LX:OK <name>" replies, e.g. "TAP", "HOME".
const char* verbName(Verb verb);

// Assembles serial bytes into lines. A line longer than config::kLineMax is dropped whole, and CR
// and NUL bytes are ignored.
class LineAssembler {
 public:
  // Feeds one byte. Returns true when a complete, non-empty, non-overflowed line is ready in line().
  bool feed(char ch);
  const char* line() const { return buf_; }

 private:
  char buf_[config::kLineMax] = {};
  size_t len_ = 0;
  bool overflow_ = false;
};

}  // namespace lexipoint::dev
