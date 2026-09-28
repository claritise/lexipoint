#pragma once

// Whether a finger is on the screen right now, from the touch controller's interrupt line alone (v0.2 V7b R1): a
// blocking Lexirise call gives way to it (input/InputAbort.h). Which level the line rests at isn't assumed (the board
// config's polarity, the SDK's pin mode and the controller's mode all bear on it): it's learned from the level seen
// whenever the reader knows no finger is down (idle()), and anything else is a touch. A line that won't settle (it
// changes while no finger is down, config::kTouchLineFlipsMax times within config::kTouchLineFlipWindowMs) is
// never trusted again this boot, so it can't make every call give up. Pure; tests: test/lexirise_page.

#include <cstdint>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::input {

class TouchLine {
 public:
  // The line's level with no finger down (the reader's debounced input says so). `level` -1: no line.
  void idle(const int level, const unsigned long nowMs) {
    if (level < 0 || ignored_) return;
    if (learned_ && level != idleLevel_) {
      if (flips_ == 0 || !timing::reached(nowMs, firstFlipMs_ + config::kTouchLineFlipWindowMs)) {
        if (flips_ == 0) firstFlipMs_ = nowMs;
        if (++flips_ >= config::kTouchLineFlipsMax) {
          ignored_ = true;
          justIgnored_ = true;
          return;
        }
      } else {
        flips_ = 1;
        firstFlipMs_ = nowMs;
      }
    }
    justLearned_ = !learned_;
    learned_ = true;
    idleLevel_ = level;
  }
  // A finger on the screen: the line away from its idle level (never before that's learned, nor once ignored).
  bool active(const int level) const { return level >= 0 && learned_ && !ignored_ && level != idleLevel_; }

  bool learned() const { return learned_; }
  bool ignored() const { return ignored_; }
  int idleLevel() const { return idleLevel_; }
  // Once each, for the log: the idle level was just learned / the line was just given up on.
  bool takeLearned() {
    const bool was = justLearned_;
    justLearned_ = false;
    return was;
  }
  bool takeIgnored() {
    const bool was = justIgnored_;
    justIgnored_ = false;
    return was;
  }

 private:
  bool learned_ = false;
  bool ignored_ = false;
  bool justLearned_ = false;
  bool justIgnored_ = false;
  int idleLevel_ = 0;
  unsigned flips_ = 0;
  unsigned long firstFlipMs_ = 0;
};

}  // namespace lexipoint::input
