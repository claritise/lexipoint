#pragma once

// This reader's own Lexirise requests over the last hour (v0.2 V7b: the page analysis stops above
// config::kPageBudgetPercent of the key's hourly limit). Answers carry no rate-limit headers (measured,
// lexirise-api-notes.md "Page analysis (V7b), measured"), so the device can only count what it sent itself: the
// key's use elsewhere is unseen, and a 429 (AccessPolicy) stops everything anyway. Minute buckets. Pure; tests:
// test/lexirise_page/PrefetchTest.cpp.

#include <array>
#include <cstdint>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::api {

// Whether `used` requests are below `percent` of the hourly `limit` (the page analysis's budget, the home sync's stop).
inline bool belowPercent(const unsigned used, const uint32_t limit, const unsigned percent) {
  return static_cast<uint64_t>(used) * 100 < static_cast<uint64_t>(limit) * percent;
}

class RequestWindow {
 public:
  void add(const unsigned long nowMs) {
    advance(nowMs);
    buckets_[head_]++;
  }
  // Requests in the last config::kRateWindowMs (to the minute).
  unsigned count(const unsigned long nowMs) {
    advance(nowMs);
    unsigned total = 0;
    for (const uint16_t n : buckets_) total += n;
    return total;
  }

 private:
  static constexpr unsigned long kBucketMs = config::kRateWindowMs / config::kRateWindowBuckets;
  void advance(const unsigned long nowMs) {
    if (!started_) {
      started_ = true;
      headStartMs_ = nowMs;
      return;
    }
    const unsigned long elapsed = nowMs - headStartMs_;  // wraps with millis(): unsigned difference
    if (elapsed < kBucketMs) return;
    const unsigned long steps = elapsed / kBucketMs;
    const size_t clear = steps >= config::kRateWindowBuckets ? config::kRateWindowBuckets : static_cast<size_t>(steps);
    for (size_t i = 0; i < clear; i++) {
      head_ = (head_ + 1) % config::kRateWindowBuckets;
      buckets_[head_] = 0;
    }
    headStartMs_ += steps * kBucketMs;
  }
  std::array<uint16_t, config::kRateWindowBuckets> buckets_{};
  size_t head_ = 0;
  unsigned long headStartMs_ = 0;
  bool started_ = false;
};

}  // namespace lexipoint::api
