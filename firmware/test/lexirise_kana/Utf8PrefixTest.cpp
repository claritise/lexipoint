// Whole characters of a UTF-8 string (text/Utf8Prefix.h).

#include <gtest/gtest.h>

#include "lexirise/text/Utf8Prefix.h"

using namespace lexipoint::text;

TEST(Utf8Prefix, PrefixesNeverSplitACharacter) {
  EXPECT_EQ(utf8Prefix("食べる", 4), "食");
  EXPECT_EQ(utf8Prefix("食べる", 6), "食べ");
  EXPECT_EQ(utf8Prefix("ab", 5), "ab");
  EXPECT_EQ(utf8FirstChars("食べる", 2), "食べ");
  EXPECT_EQ(utf8FirstChars("a", 3), "a");
}

TEST(Utf8Prefix, TheLastCharacter) {
  EXPECT_EQ(utf8LastChar("食べる"), "る");
  EXPECT_EQ(utf8WithoutLastChar("食べる"), "食べ");
  EXPECT_EQ(utf8LastChar("ab"), "b");
  EXPECT_EQ(utf8LastChar(""), "");
  EXPECT_EQ(utf8WithoutLastChar("る"), "");
  EXPECT_EQ(utf8LastChar("a\xF0\x9F\x98\x80"), "\xF0\x9F\x98\x80");  // four bytes
}

TEST(Utf8Prefix, Codepoints) {
  EXPECT_EQ(utf8FirstCodepoint("る"), 0x308Bu);
  EXPECT_EQ(utf8FirstCodepoint("A"), 0x41u);
  EXPECT_EQ(utf8FirstCodepoint("\xC3\xA9"), 0xE9u);
  EXPECT_EQ(utf8FirstCodepoint("\xF0\x9F\x98\x80"), 0x1F600u);
  EXPECT_EQ(utf8FirstCodepoint(std::string_view("るabc", 3)), 0x308Bu);  // a view: reads no further
  EXPECT_EQ(utf8FirstCodepoint(""), 0u);
  EXPECT_EQ(utf8FirstCodepoint("\x80"), 0xFFFDu);                               // not a start
  EXPECT_EQ(utf8FirstCodepoint("\xE3\x81"), 0xFFFDu);                           // cut short
  EXPECT_EQ(utf8FirstCodepoint(std::string_view("\xE3\x81\x82", 2)), 0xFFFDu);  // cut short by the view
  EXPECT_TRUE(isContinuationByte('\x80'));
  EXPECT_FALSE(isContinuationByte('a'));
}

TEST(Utf8Prefix, TheCharacterAtAUtf16Offset) {
  EXPECT_EQ(utf8CharAtUtf16("書けば、", 2), "ば");
  EXPECT_EQ(utf8CharAtUtf16("書けば", 0), "書");
  EXPECT_EQ(utf8CharAtUtf16("書けば", 3), "");                // past the end
  EXPECT_EQ(utf8CharAtUtf16("\xF0\x9F\x98\x80ば", 2), "ば");  // a non-BMP character counts 2
  EXPECT_EQ(utf8CharAtUtf16("\xF0\x9F\x98\x80ば", 1), "");    // inside it
}
