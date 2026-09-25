#pragma once

// Every tunable of the LEXIPOINT dev harness in one place. Keep lxctl.py's host-side timeouts in
// step with the timing values here (scripts/lexipoint/lxctl.py, "Protocol constants").

#include <cstddef>

namespace lexipoint::dev::config {

// Serial line reader.
constexpr size_t kLineMax = 192;       // longest accepted command line, including the terminator
constexpr size_t kMaxTokens = 6;       // verb + up to 5 arguments
constexpr size_t kTokenMax = 16;       // longest single token, including the terminator
constexpr long kIntArgLimit = 100000;  // |numeric argument| sanity bound (coordinates, ms)

// Gesture timing reported to the UI (contact durations the SDK classifier would have measured).
constexpr unsigned long kTapHeldMs = 120;    // > MappedInputManager's 90 ms touch-down select delay
constexpr unsigned long kLongHeldMs = 800;   // > the 700 ms reader long-press threshold
constexpr unsigned long kSwipeHeldMs = 200;  // < the SDK's 700 ms swipe window
// Mirrors the SDK's private InputManager::TOUCH_SWIPE_MIN_PX: a swipe must travel this far on at least
// one axis (InputManager.cpp, classifier), so shorter injected swipes are rejected as unrealistic.
constexpr int kSwipeMinPx = 60;

// Synthetic buttons. The SDK needs two agreeing debounced samples, and an idle loop samples every
// 50 ms, so presses are held for a floor of time *and* a floor of update() calls.
constexpr int kButtonMinMs = 120;
constexpr int kButtonMaxMs = 5000;
constexpr int kButtonDefaultMs = 150;
constexpr unsigned kButtonMinUpdates = 3;

// Keep-awake: while the host keeps sending commands (any command renews the lease), the device stays
// out of power saving and auto-sleep. With no host, it sleeps normally once the lease runs out.
constexpr unsigned long kKeepAwakeLeaseMs = 10UL * 60UL * 1000UL;

// LX:SYNC: reply once the input queue is drained, buttons are released and no render has run for a
// few consecutive polls; give up after the timeout.
constexpr unsigned kSyncSettleFrames = 3;
constexpr unsigned kSyncStablePolls = 3;
constexpr unsigned long kSyncTimeoutMs = 15000;

// Screenshots: total time allowed to push the 48 KB frame to a slow host.
constexpr unsigned long kShotWriteDeadlineMs = 3000;

// LX:SELFTEST coordinate grid: an odd stride hits every row/column parity cheaply.
constexpr int kSelfTestStride = 7;
constexpr int kSelfTestMaxMisses = 3;  // misses printed before summarising

// LX:REBOOT: let the reply leave the USB FIFO before restarting.
constexpr unsigned long kRebootFlushMs = 50;

// LX:LEXI: the P1 gate's client checks. Fixed sample sentences (a command line can't carry much
// text); the soak runs n analyze calls and reports heap after each (no monotonic loss allowed).
constexpr int kLexiSoakMax = 50;
constexpr const char* kLexiSampleJa = "彼は東京へ行った。";
constexpr const char* kLexiSampleZh = "我们明天去北京看朋友。";

// Gesture queue: a few whole gestures ahead of the device.
constexpr size_t kQueueCapacity = 24;
constexpr size_t kMaxGestureFrames = 4;

}  // namespace lexipoint::dev::config
