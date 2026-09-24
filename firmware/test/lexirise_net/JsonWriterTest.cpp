#include <gtest/gtest.h>

#include <string>

#include "lexirise/net/JsonWriter.h"

using lexipoint::net::appendJsonString;
using lexipoint::net::JsonObject;

namespace {
std::string quoted(const std::string& text) {
  std::string out;
  appendJsonString(out, text);
  return out;
}
}  // namespace

TEST(JsonWriter, EscapesQuoteBackslashAndControls) {
  EXPECT_EQ(quoted("a\"b\\c"), R"("a\"b\\c")");
  EXPECT_EQ(quoted("l1\nl2\r\t"), R"("l1\nl2\r\t")");
  EXPECT_EQ(quoted(std::string("\x01\x1f\x7f", 3)), R"("\u0001\u001f\u007f")");
  EXPECT_EQ(quoted(std::string("a\0b", 3)), R"("a\u0000b")");
  EXPECT_EQ(quoted("/"), R"("/")");
}

TEST(JsonWriter, KeepsValidUtf8IncludingNonBmp) {
  EXPECT_EQ(quoted("東京へ行く"), "\"東京へ行く\"");
  EXPECT_EQ(quoted("𠮷野家"), "\"𠮷野家\"");  // 4-byte sequence
  EXPECT_EQ(quoted("é"), "\"é\"");
}

TEST(JsonWriter, ReplacesInvalidUtf8) {
  const std::string fffd = "\xEF\xBF\xBD";
  EXPECT_EQ(quoted("a\xFF"
                   "b"),
            "\"a" + fffd + "b\"");
  EXPECT_EQ(quoted("\xC0\xAF"), "\"" + fffd + fffd + "\"");                        // overlong '/'
  EXPECT_EQ(quoted("\xED\xA0\x80"), "\"" + fffd + fffd + fffd + "\"");             // UTF-16 surrogate
  EXPECT_EQ(quoted("\xE6\x9D"), "\"" + fffd + fffd + "\"");                        // truncated 東
  EXPECT_EQ(quoted("\xF4\x90\x80\x80"), "\"" + fffd + fffd + fffd + fffd + "\"");  // > U+10FFFF
}

TEST(JsonWriter, BuildsFlatObjects) {
  EXPECT_EQ(JsonObject().str(), "{}");
  const std::string body = JsonObject()
                               .add("text", "彼は\"走った\"")
                               .add("language", "ja")
                               .add("proficiency", 1)
                               .add("fast", false)
                               .add("tags", std::vector<std::string>{"xteink", "a\"b"})
                               .str();
  EXPECT_EQ(body, R"({"text":"彼は\"走った\"","language":"ja","proficiency":1,"fast":false,"tags":["xteink","a\"b"]})");
  EXPECT_EQ(JsonObject().add("tags", std::vector<std::string>{}).str(), R"({"tags":[]})");
}
