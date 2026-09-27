#pragma once

// millis() times: 32-bit on the device, wrapping after ~49.7 days. Compare them only through these, by
// their 32-bit difference, so a deadline set just before the wrap isn't taken as long past. Pure.

#include <cstdint>

namespace lexipoint::timing {

// One type per unit: milliseconds as millis() gives them (unsigned long), seconds signed (a UTC offset subtracts).
constexpr unsigned long kMsPerSecond = 1000UL;
constexpr unsigned long kMsPerMinute = 60UL * kMsPerSecond;
constexpr unsigned long kMsPerHour = 60UL * kMsPerMinute;
constexpr int64_t kSecondsPerMinute = 60;
constexpr int64_t kSecondsPerHour = 60 * kSecondsPerMinute;
constexpr int64_t kSecondsPerDay = 24 * kSecondsPerHour;

// `now` is at or past `deadline`.
inline bool reached(const unsigned long now, const unsigned long deadline) {
  return static_cast<int32_t>(static_cast<uint32_t>(now - deadline)) >= 0;
}

// `a` comes before `b`.
inline bool before(const unsigned long a, const unsigned long b) { return !reached(a, b); }

}  // namespace lexipoint::timing
