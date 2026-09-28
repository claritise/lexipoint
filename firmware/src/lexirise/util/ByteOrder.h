#pragma once

// Little-endian fields in a byte string: the binary files' one home for them (the vocab mirror, the page cache and
// its index). Pure; tests: test/lexirise_page/UtilTest.cpp.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lexipoint::bytes {

inline void put16(std::string& out, const size_t at, const uint16_t v) {
  out[at] = static_cast<char>(v & 0xFF);
  out[at + 1] = static_cast<char>(v >> 8);
}
inline void put32(std::string& out, const size_t at, const uint32_t v) {
  for (size_t i = 0; i < 4; i++) out[at + i] = static_cast<char>((v >> (8 * i)) & 0xFF);
}
inline void put64(std::string& out, const size_t at, const uint64_t v) {
  for (size_t i = 0; i < 8; i++) out[at + i] = static_cast<char>((v >> (8 * i)) & 0xFF);
}
inline uint8_t get8(const std::string_view in, const size_t at) { return static_cast<uint8_t>(in[at]); }
inline uint16_t get16(const std::string_view in, const size_t at) {
  return static_cast<uint16_t>(get8(in, at) | (get8(in, at + 1) << 8));
}
inline uint32_t get32(const std::string_view in, const size_t at) {
  uint32_t v = 0;
  for (size_t i = 0; i < 4; i++) v |= static_cast<uint32_t>(get8(in, at + i)) << (8 * i);
  return v;
}
inline uint64_t get64(const std::string_view in, const size_t at) {
  uint64_t v = 0;
  for (size_t i = 0; i < 8; i++) v |= static_cast<uint64_t>(get8(in, at + i)) << (8 * i);
  return v;
}

}  // namespace lexipoint::bytes
