#pragma once

// When Lexirise may be asked at all (offline-and-errors.md §1, §3): a 401/403 turns it off until reboot
// or a new key (lookups go straight to StarDict, saves fail at once), a 429 backs it off for its
// Retry-After. A key check (/v1/me) may still probe a rejected key, and a good answer lifts the
// rejection. Pure; LexiriseService keeps one. Tests: test/lexirise_net/AccessPolicyTest.cpp.

#include <cstdint>

#include "LexiriseClient.h"
#include "lexirise/LexiriseConfig.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::api {

class AccessPolicy {
 public:
  enum class Block : uint8_t { None, Rejected, RateLimited };

  // What a response says about the key and the rate limit.
  void observe(const ApiResponse& response, const unsigned long nowMs) {
    if (response.error == ApiError::Unauthorized) {
      if (!rejected_) unannounced_ = true;  // new: the reader hasn't been told (a key check may have found it)
      rejected_ = true;                     // (a second 401 before anyone was told keeps it untold)
    } else if (response.error == ApiError::RateLimited) {
      unannounced_ = true;
      const uint32_t s = response.retryAfterS != 0 ? response.retryAfterS : config::kRetryAfterDefaultS;
      backoffUntilMs_ = nowMs + (s < config::kRetryAfterMaxS ? s : config::kRetryAfterMaxS) * 1000UL;
      backingOff_ = true;
    } else if (response.status >= 200 && response.status < 300) {
      rejected_ = false;  // answered with this key: it works
    }
  }

  // A new key or server: nothing learned about the old one applies.
  void reset() { *this = AccessPolicy(); }

  // A rejection or a rate limit the reader hasn't been told about yet (word select's notice), once.
  Block takeUnannounced(const unsigned long nowMs) {
    const Block b = unannounced_ ? blocked(nowMs) : Block::None;
    unannounced_ = false;
    return b;
  }

  Block blocked(const unsigned long nowMs) const {
    if (rejected_) return Block::Rejected;
    if (!backingOff_) return Block::None;
    if (!timing::reached(nowMs, backoffUntilMs_)) return Block::RateLimited;
    backingOff_ = false;  // over: forget it, or 2^31 ms on (the signed compare) it'd read as not reached
    return Block::None;
  }

  // Whole seconds left in the back-off (rounded up); 0 when there's none.
  uint32_t retryInS(const unsigned long nowMs) const {
    if (blocked(nowMs) != Block::RateLimited) return 0;
    const uint32_t leftMs = static_cast<uint32_t>(backoffUntilMs_ - nowMs);  // 32-bit: millis() wraps
    return (leftMs + 999U) / 1000U;
  }

  // The response a blocked call gets, without touching the network.
  ApiResponse refusal(const unsigned long nowMs) const {
    ApiResponse r;
    const Block b = blocked(nowMs);
    r.error = b == Block::Rejected ? ApiError::Unauthorized : ApiError::RateLimited;
    r.retryAfterS = retryInS(nowMs);
    return r;
  }

 private:
  bool rejected_ = false;
  bool unannounced_ = false;
  mutable bool backingOff_ = false;  // cleared by blocked() once the back-off has passed
  unsigned long backoffUntilMs_ = 0;
};

}  // namespace lexipoint::api
