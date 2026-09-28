#pragma once

// JSON numbers as Lexirise's answers carry them, read strictly (shared by Responses.cpp and the page analysis's
// streamed reader, page/PageAnalysis.cpp).

#include <cstdint>
#include <string_view>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/net/JsonReader.h"

namespace lexipoint::api {

// A JSON number that is a whole value in [0, UINT32_MAX] ("12", not "1.5", "-1" or "1e3").
inline bool toUint32(const json::Type type, const std::string_view text, uint32_t& out) {
  if (type != json::Type::Number || text.empty() || text.size() > 10) return false;
  uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + static_cast<uint64_t>(c - '0');
  }
  if (value > UINT32_MAX) return false;
  out = static_cast<uint32_t>(value);
  return true;
}

// A JSON number in [0, 1] ("0.83", "1", "0"): a frequency score. Anything else leaves `out` alone.
inline bool toUnitFloat(const json::Type type, const std::string_view text, float& out) {
  if (type != json::Type::Number || text.empty() || text.size() > config::kMaxScoreChars) return false;
  float value = 0;
  float scale = 0;  // 0 before the point, then 0.1, 0.01 …
  for (const char c : text) {
    if (c == '.' && scale == 0) {
      scale = 0.1f;
    } else if (c >= '0' && c <= '9') {
      if (scale == 0) {
        value = value * 10 + static_cast<float>(c - '0');
      } else {
        value += scale * static_cast<float>(c - '0');
        scale /= 10;
      }
    } else {
      return false;  // a sign or an exponent: not a score
    }
  }
  if (value > 1) return false;
  out = value;
  return true;
}

}  // namespace lexipoint::api
