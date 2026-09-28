// v0.2 V7b R7: the binary files' shared helpers (util/ByteOrder.h, util/Crc32.h): little-endian fields, CRC-32 and
// FNV-1a 32 against known values.

#include <gtest/gtest.h>

#include <string>

#include "lexirise/util/ByteOrder.h"
#include "lexirise/util/Crc32.h"

using namespace lexipoint::bytes;

TEST(ByteOrder, LittleEndianRoundTrips) {
  std::string b(14, '\0');
  put16(b, 0, 0xBEEF);
  put32(b, 2, 0x01020304u);
  put64(b, 6, 0x1122334455667788ULL);
  EXPECT_EQ(static_cast<uint8_t>(b[0]), 0xEF);
  EXPECT_EQ(static_cast<uint8_t>(b[2]), 0x04);
  EXPECT_EQ(static_cast<uint8_t>(b[6]), 0x88);
  EXPECT_EQ(get16(b, 0), 0xBEEF);
  EXPECT_EQ(get32(b, 2), 0x01020304u);
  EXPECT_EQ(get64(b, 6), 0x1122334455667788ULL);
  EXPECT_EQ(get8(b, 1), 0xBE);
}

TEST(Crc32, KnownValues) {
  EXPECT_EQ(crc32("123456789"), 0xCBF43926u);      // the standard check value
  EXPECT_EQ(crc32("1234", "56789"), 0xCBF43926u);  // in parts, the same
  EXPECT_EQ(crc32(""), 0u);
}

TEST(Fnv1a, KnownValuesAndParts) {
  EXPECT_EQ(Fnv1a().value(), 2166136261u);
  EXPECT_EQ(Fnv1a().add("a").value(), 0xE40C292Cu);
  EXPECT_EQ(Fnv1a().add("foo").value(), 0xA9F37ED7u);
  EXPECT_EQ(Fnv1a().add("fo").add("o").value(), Fnv1a().add("foo").value());
}
