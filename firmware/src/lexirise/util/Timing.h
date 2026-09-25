#pragma once

// millis() times: 32-bit on the device, wrapping after ~49.7 days. Compare them only through these, by
// their 32-bit difference, so a deadline set just before the wrap isn't taken as long past. Pure.

#include <cstdint>

namespace lexipoint::timing {

// `now` is at or past `deadline`.
inline bool reached(const unsigned long now, const unsigned long deadline) {
  return static_cast<int32_t>(static_cast<uint32_t>(now - deadline)) >= 0;
}

// `a` comes before `b`.
inline bool before(const unsigned long a, const unsigned long b) { return !reached(a, b); }

}  // namespace lexipoint::timing
