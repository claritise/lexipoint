#pragma once

// Minimal JSON writer for request bodies (lexirise-client.md §1: "manual JSON string escaping").
// Pure and host-testable: test/lexirise_net/JsonWriterTest.cpp.

#include <string>
#include <string_view>
#include <vector>

namespace lexipoint::net {

// Appends `text` as a quoted JSON string. Escapes quote, backslash and control characters, and
// replaces invalid UTF-8 with U+FFFD, so the output is always valid JSON in valid UTF-8.
void appendJsonString(std::string& out, std::string_view text);

// Builds one flat JSON object: JsonObject().add("a", "x").add("n", 1).str() == {"a":"x","n":1}.
class JsonObject {
 public:
  JsonObject& add(std::string_view key, std::string_view value);
  JsonObject& add(std::string_view key, const char* value) { return add(key, std::string_view(value)); }
  JsonObject& add(std::string_view key, long long value);
  JsonObject& add(std::string_view key, int value) { return add(key, static_cast<long long>(value)); }
  JsonObject& add(std::string_view key, bool value);
  JsonObject& add(std::string_view key, const std::vector<std::string>& values);  // array of strings
  JsonObject& add(std::string_view key, const std::vector<int>& values);          // array of numbers
  JsonObject& add(std::string_view key, const JsonObject& nested);                // nested object
  JsonObject& addNull(std::string_view key);

  // The finished object. The writer stays usable (more add() calls extend it).
  std::string str() const { return body_ + "}"; }

 private:
  void key(std::string_view key);
  std::string body_ = "{";
};

}  // namespace lexipoint::net
