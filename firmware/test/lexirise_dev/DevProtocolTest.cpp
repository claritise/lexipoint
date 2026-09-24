// Parser and line-assembly edge cases for the dev harness protocol (src/lexirise/dev/DevProtocol).

#include <gtest/gtest.h>

#include <string>

#include "DevProtocol.h"

using namespace lexipoint::dev;

namespace {

Command parse(const std::string& s) { return parseLine(s.c_str()); }

TEST(DevProtocol, IgnoresNonCommandLines) {
  for (const char* s : {"", "hello", "[123] [INF] [MAIN] log line", "lx:PING", "LXPING"}) {
    const Command c = parse(s);
    EXPECT_EQ(c.verb, Verb::None) << s;
    EXPECT_EQ(c.error, nullptr) << s;
  }
}

TEST(DevProtocol, SimpleVerbs) {
  EXPECT_EQ(parse("LX:PING").verb, Verb::Ping);
  EXPECT_EQ(parse("LX:SHOT").verb, Verb::Shot);
  EXPECT_EQ(parse("LX:MEM").verb, Verb::Mem);
  EXPECT_EQ(parse("LX:SELFTEST").verb, Verb::SelfTest);
  EXPECT_EQ(parse("LX:SYNC").verb, Verb::Sync);
  EXPECT_EQ(parse("LX:REBOOT").verb, Verb::Reboot);
  EXPECT_NE(parse("LX:SHOT now").error, nullptr);
}

TEST(DevProtocol, TrailingWhitespaceIsIgnored) {
  EXPECT_EQ(parse("LX:PING ").verb, Verb::Ping);
  EXPECT_EQ(parse("LX:TAP 10 20\t ").verb, Verb::Tap);
}

TEST(DevProtocol, TapAndLongPress) {
  Command c = parse("LX:TAP 12 345");
  EXPECT_EQ(c.verb, Verb::Tap);
  EXPECT_EQ(c.x, 12);
  EXPECT_EQ(c.y, 345);
  c = parse("LX:LONG 1 2");
  EXPECT_EQ(c.verb, Verb::Long);
  EXPECT_STREQ(parse("LX:TAP 10").error, "usage: TAP x y");
  EXPECT_STREQ(parse("LX:TAP 10 20 30").error, "usage: TAP x y");
  EXPECT_STREQ(parse("LX:TAP 10 abc").error, "usage: TAP x y");
  EXPECT_STREQ(parse("LX:LONG x 1").error, "usage: LONG x y");
  EXPECT_NE(parse("LX:TAP 10 99999999").error, nullptr);  // beyond kIntArgLimit
}

TEST(DevProtocol, Swipe) {
  const Command c = parse("LX:SWIPE 1 2 3 4");
  EXPECT_EQ(c.verb, Verb::Swipe);
  EXPECT_EQ(c.x2, 3);
  EXPECT_EQ(c.y2, 4);
  EXPECT_NE(parse("LX:SWIPE 1 2 3").error, nullptr);
}

TEST(DevProtocol, ButtonsAndClamping) {
  Command c = parse("LX:BTN LEFT");
  EXPECT_EQ(c.verb, Verb::Button);
  EXPECT_EQ(c.button, ButtonName::Left);
  EXPECT_EQ(c.ms, config::kButtonDefaultMs);
  EXPECT_EQ(parse("LX:BTN NEXT").button, ButtonName::Right);
  EXPECT_EQ(parse("LX:BTN POWER 300").ms, 300);
  EXPECT_EQ(parse("LX:BTN RIGHT 1").ms, config::kButtonMinMs);
  EXPECT_EQ(parse("LX:BTN RIGHT 99999").ms, config::kButtonMaxMs);
  EXPECT_NE(parse("LX:BTN").error, nullptr);
  EXPECT_NE(parse("LX:BTN UP").error, nullptr);
  EXPECT_NE(parse("LX:BTN LEFT abc").error, nullptr);
  EXPECT_NE(parse("LX:BTN LEFT 1 2").error, nullptr);
}

TEST(DevProtocol, HomeVersusHomeHold) {
  EXPECT_EQ(parse("LX:HOME").verb, Verb::Home);
  EXPECT_EQ(parse("LX:HOME HOLD").verb, Verb::HomeHold);
  EXPECT_NE(parse("LX:HOME TAP").error, nullptr);
  EXPECT_STREQ(verbName(Verb::HomeHold), "HOME");
}

TEST(DevProtocol, Awake) {
  EXPECT_EQ(parse("LX:AWAKE 0").awake, AwakeMode::Off);
  EXPECT_EQ(parse("LX:AWAKE 1").awake, AwakeMode::On);
  EXPECT_EQ(parse("LX:AWAKE 2").awake, AwakeMode::Lease);
  EXPECT_NE(parse("LX:AWAKE 3").error, nullptr);
  EXPECT_NE(parse("LX:AWAKE").error, nullptr);
}

TEST(DevProtocol, UnknownAndMalformed) {
  EXPECT_STREQ(parse("LX:FLY").error, "unknown command");
  EXPECT_NE(parse("LX:").error, nullptr);
  EXPECT_NE(parse("LX:TAP 1 2 3 4 5 6 7").error, nullptr);       // too many tokens
  EXPECT_NE(parse("LX:TAPTAPTAPTAPTAPTAP 1 2").error, nullptr);  // over-long token
}

TEST(DevProtocol, LegacyScreenshot) {
  EXPECT_EQ(parse("CMD:SCREENSHOT").verb, Verb::LegacyScreenshot);
  EXPECT_EQ(parse("CMD: SCREENSHOT ").verb, Verb::LegacyScreenshot);  // upstream trimmed too
  EXPECT_EQ(parse("CMD:OTHER").verb, Verb::None);
  EXPECT_EQ(parse("CMD:OTHER").error, nullptr);
}

std::string feedAll(LineAssembler& a, const std::string& bytes, int* lines = nullptr) {
  std::string last;
  int n = 0;
  for (const char ch : bytes) {
    if (a.feed(ch)) {
      last = a.line();
      n++;
    }
  }
  if (lines) *lines = n;
  return last;
}

TEST(DevProtocol, AssemblerHandlesCrAndNul) {
  LineAssembler a;
  EXPECT_EQ(feedAll(a, std::string("LX:PI\0NG\r\n", 10)), "LX:PING");
}

TEST(DevProtocol, AssemblerSkipsEmptyLines) {
  LineAssembler a;
  int n = 0;
  feedAll(a, "\n\n\r\n", &n);
  EXPECT_EQ(n, 0);
}

TEST(DevProtocol, AssemblerLengthBoundary) {
  const size_t maxChars = config::kLineMax - 1;  // one byte is the terminator
  LineAssembler a;
  const std::string fits(maxChars, 'A');
  EXPECT_EQ(feedAll(a, fits + "\n"), fits);

  int n = 0;
  const std::string tooLong(maxChars + 1, 'B');
  feedAll(a, tooLong + "\n", &n);
  EXPECT_EQ(n, 0);  // dropped whole

  EXPECT_EQ(feedAll(a, "LX:PING\n"), "LX:PING");  // and the next line still works
}

}  // namespace
