#pragma once

// The wall clock as Lexipoint uses it: seconds since the epoch, or 0 while it isn't set (before
// config::kMinValidEpochS: the SNTP answer hasn't come). One home for the rule (V7b R5).

#include <cstdint>
#include <ctime>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::timing {

inline uint32_t epochOrZero(const int64_t t) {
  return t >= static_cast<int64_t>(config::kMinValidEpochS) && t <= static_cast<int64_t>(UINT32_MAX)
             ? static_cast<uint32_t>(t)
             : 0;
}
inline uint32_t epochNowS() { return epochOrZero(static_cast<int64_t>(time(nullptr))); }

}  // namespace lexipoint::timing
