#pragma once

// LEXIPOINT dev harness wire protocol: line assembly and command parsing (pure, host-testable).
// One command per line, "LX:<VERB> [args]". See docs/v0.1/dev-harness.md in the lexipoint repo.
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
  LegacyScreenshot,  // upstream "CMD:SCREENSHOT", kept working
};

enum class ButtonName { Left, Right, Power };

struct Command {
  Verb verb = Verb::None;
  int x = 0, y = 0, x2 = 0, y2 = 0;
  ButtonName button = ButtonName::Left;
  int ms = config::kButtonDefaultMs;  // buttons, clamped to [kButtonMinMs, kButtonMaxMs]
  AwakeMode awake = AwakeMode::Lease;
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
