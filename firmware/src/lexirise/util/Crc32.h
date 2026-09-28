#pragma once

// CRC-32 (IEEE, reflected), a nibble at a time: 16 table entries in flash. Over two parts, as the binary files check
// a header's first bytes and everything after it. Pure; tests: test/lexirise_page/UtilTest.cpp.

#include <cstdint>
#include <string_view>

namespace lexipoint::bytes {

inline constexpr uint32_t kCrcNibbles[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                             0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                             0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};

inline uint32_t crc32(const std::string_view a, const std::string_view b = {}) {
  uint32_t crc = 0xFFFFFFFF;
  for (const std::string_view part : {a, b}) {
    for (const char c : part) {
      crc ^= static_cast<uint8_t>(c);
      crc = (crc >> 4) ^ kCrcNibbles[crc & 0x0F];
      crc = (crc >> 4) ^ kCrcNibbles[crc & 0x0F];
    }
  }
  return ~crc;
}

// FNV-1a 32, fed in parts (the page text's check, the page index's checksum).
class Fnv1a {
 public:
  Fnv1a& add(const std::string_view part) {
    for (const char c : part) {
      hash_ ^= static_cast<uint8_t>(c);
      hash_ *= 16777619u;
    }
    return *this;
  }
  uint32_t value() const { return hash_; }

 private:
  uint32_t hash_ = 2166136261u;
};

}  // namespace lexipoint::bytes
