#pragma once

// A push JSON reader for bodies too large to hold (v0.2 V7a: a GET /v1/vocabulary page, ~6.8 KB per item with its
// embedded dictionary entry: lexirise-api-notes.md "V7's foundations"). The bytes are fed as they arrive and each
// value goes to the same json::Visitor, with the same paths, begins and ends, in the same order, as json::read gives
// for the whole document, and the same documents are rejected (malformed, truncated, deeper than
// config::kJsonMaxDepth). One difference: a string or a number longer than `maxStringBytes` is walked (and checked)
// but not kept, so it never reaches the visitor (skippedStrings() counts them); a key that long is malformed. Nothing
// else is held: the path, one key and one value at a time, their buffers reused, so memory doesn't grow with the body.
// Tests: test/lexirise_net/JsonStreamTest.cpp.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "JsonReader.h"
#include "lexirise/LexiriseConfig.h"

namespace lexipoint::json {

class StreamReader {
 public:
  explicit StreamReader(Visitor& visitor, size_t maxStringBytes = config::kJsonStreamMaxStringBytes);

  // The next bytes. False once the document is known bad (stop feeding: the rest can't make it good).
  bool feed(std::string_view bytes);
  // After the last byte: Ok when exactly one whole value came, with only whitespace around it.
  Result finish();
  size_t skippedStrings() const { return skipped_; }

 private:
  enum class State : uint8_t {
    Value,        // a value must come (the top, after ':' or an array's ',')
    ArrayFirst,   // after '[': a value or ']'
    ObjectFirst,  // after '{': a key or '}'
    ObjectKey,    // after an object's ',': a key
    Colon,        // after a key
    AfterValue,   // inside a container: ',' or its closer
    String,
    Escape,  // after '\'
    Hex,     // inside \uXXXX
    Number,
    Literal,
    Done,  // the top value is whole: only whitespace may follow
    Failed,
  };

  bool step(char c);  // one byte; false: failed
  bool startValue(char c);
  bool open(bool isArray);
  bool close(bool isArray);
  void valueDone();  // a value (scalar or container) ended: back to its container, or Done
  bool endString();
  bool numberByte(char c);  // false: c isn't part of the number (it ended, or it's malformed: numberEnded())
  bool numberEnded();       // the number is whole (emitted) or malformed
  void appendCodepoint(uint32_t cp);
  void appendByte(char c);
  bool fail(Result why = Result::Malformed);

  Visitor& visitor_;
  size_t maxString_;
  State state_ = State::Value;
  Result failure_ = Result::Malformed;
  Path path_;
  std::vector<bool> containers_;  // per open container: true for an array
  std::vector<int> indices_;      // per open container: the array's element being read (objects: unused)
  std::string text_;              // the string, key or number being read (reused)
  bool stringIsKey_ = false;
  bool over_ = false;  // text_ passed maxString_: walked, not kept
  uint32_t hex_ = 0;
  int hexDigits_ = 0;
  uint32_t highSurrogate_ = 0;  // a \uD800-\uDBFF waiting for its pair
  enum class Num : uint8_t { Sign, Zero, Int, Dot, Frac, Exp, ExpSign, ExpDigits };
  Num num_ = Num::Sign;
  const char* literal_ = nullptr;
  size_t literalAt_ = 0;
  size_t skipped_ = 0;
};

}  // namespace lexipoint::json
