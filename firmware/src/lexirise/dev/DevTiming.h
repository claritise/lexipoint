#pragma once

// LEXIPOINT dev harness timing state (pure, host-testable): the keep-awake lease and the synthetic
// button press. All times are millis() values; differences use unsigned arithmetic, so a millis()
// wrap (every ~49.7 days) is harmless. Tests: test/lexirise_dev/DevTimingTest.cpp.

#include <cstdint>

#include "DevConfig.h"

namespace lexipoint::dev {

enum class AwakeMode { Off = 0, On = 1, Lease = 2 };

// Keeps the device out of power saving and auto-sleep while a host is driving it.
class KeepAwake {
 public:
  void setMode(const AwakeMode mode) { mode_ = mode; }
  AwakeMode mode() const { return mode_; }
  void renew(const unsigned long nowMs) { lastMs_ = nowMs; }

  // hostPresent: a USB host is actually on the line (not a charger, not battery). In Lease mode the
  // device stays awake only while a host is present *and* has sent a command recently.
  bool active(const unsigned long nowMs, const bool hostPresent) const {
    switch (mode_) {
      case AwakeMode::On:
        return true;
      case AwakeMode::Off:
        return false;
      case AwakeMode::Lease:
      default:
        return hostPresent && nowMs - lastMs_ < config::kKeepAwakeLeaseMs;
    }
  }

 private:
  AwakeMode mode_ = AwakeMode::Lease;
  unsigned long lastMs_ = 0;
};

// One synthetic button press fed through the SDK's button hook: held until both the deadline has
// passed and the SDK has sampled it config::kButtonMinUpdates times (it needs agreeing samples).
class ButtonPress {
 public:
  bool busy() const { return mask_ != 0; }

  // Starts a press of the given button bit. False (and no change) while another press is held, so
  // the SDK never sees one button turn into another without a release.
  bool start(const uint8_t bit, const unsigned long nowMs, const unsigned long durationMs) {
    if (busy()) return false;
    mask_ = static_cast<uint8_t>(1u << bit);
    untilMs_ = nowMs + durationMs;
    updates_ = 0;
    return true;
  }

  // Called by the button hook on every SDK update; returns the mask to OR into the button state.
  uint8_t tick(const unsigned long nowMs) {
    if (!busy()) return 0;
    updates_++;
    if (static_cast<long>(nowMs - untilMs_) >= 0 && updates_ >= config::kButtonMinUpdates) mask_ = 0;
    return mask_;
  }

 private:
  uint8_t mask_ = 0;
  unsigned long untilMs_ = 0;
  unsigned updates_ = 0;
};

}  // namespace lexipoint::dev
