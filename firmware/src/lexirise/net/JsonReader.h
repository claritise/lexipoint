#pragma once

// Strict JSON reader for Lexirise response bodies (lexirise-client.md §4). The body is already
// buffered and bounded by the HTTP layer, so this walks it in place and reports every scalar with
// its path. Unlike lib/JsonParser/StreamingJsonParser it decodes \u escapes (surrogate pairs too),
// has no token-length limit that could misattribute a value to the previous key, and rejects
// malformed, truncated or over-deep (config::kJsonMaxDepth) documents. Tests: test/lexirise_net/JsonReaderTest.cpp.

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::json {

// Where a value sits: one segment per enclosing container, e.g. occurrences[3].word is
// {key "occurrences"}, {index 3}, {key "word"}.
class Path {
 public:
  struct Segment {
    std::string key;  // object member name (empty for array elements)
    int index = -1;   // array element index (-1 for object members)
  };

  size_t depth() const { return depth_; }
  const Segment& at(size_t i) const { return segments_[i]; }
  bool keyIs(size_t i, std::string_view key) const {
    return i < depth_ && segments_[i].index < 0 && segments_[i].key == key;
  }
  bool isIndex(size_t i) const { return i < depth_ && segments_[i].index >= 0; }
  int index(size_t i) const { return i < depth_ ? segments_[i].index : -1; }
  // The last segment's key ("" for an array element).
  std::string_view leaf() const {
    return depth_ == 0 ? std::string_view() : std::string_view(segments_[depth_ - 1].key);
  }

  // Matches the whole path against a pattern of keys, where "[]" matches any array index and "*"
  // any object key: path.matches({"occurrences", "[]", "word"}).
  bool matches(std::initializer_list<std::string_view> pattern) const;

  // Internal (used by the reader).
  void pushKey(std::string&& key);
  void pushIndex(int index);
  void pop() { depth_--; }

 private:
  std::vector<Segment> segments_;  // reused across pushes: depth_ is the live size
  size_t depth_ = 0;
};

enum class Type { String, Number, Bool, Null };

class Visitor {
 public:
  virtual ~Visitor() = default;
  // text: the decoded string, the number's literal text, "true"/"false", or "null".
  virtual void onValue(const Path& path, Type type, std::string_view text) = 0;
  // Called when an object or array opens/closes at `path` (the path of the container itself).
  virtual void onBegin(const Path& /*path*/, bool /*isArray*/) {}
  virtual void onEnd(const Path& /*path*/, bool /*isArray*/) {}
};

enum class Result { Ok, Malformed, TooDeep };

// Walks one JSON document (surrounding whitespace allowed, nothing else after it).
Result read(std::string_view document, Visitor& visitor);

}  // namespace lexipoint::json
