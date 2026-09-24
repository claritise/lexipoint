#if LEXIRISE

#include "JsonReader.h"

#include <cstdint>

namespace lexipoint::json {

bool Path::matches(const std::initializer_list<std::string_view> pattern) const {
  if (pattern.size() != depth_) return false;
  size_t i = 0;
  for (const std::string_view want : pattern) {
    const Segment& seg = segments_[i++];
    if (want == "[]") {
      if (seg.index < 0) return false;
    } else if (want == "*") {
      if (seg.index >= 0) return false;
    } else if (seg.index >= 0 || seg.key != want) {
      return false;
    }
  }
  return true;
}

void Path::pushKey(std::string&& key) {
  if (depth_ == segments_.size()) segments_.emplace_back();
  segments_[depth_].key = std::move(key);
  segments_[depth_].index = -1;
  depth_++;
}

void Path::pushIndex(const int index) {
  if (depth_ == segments_.size()) segments_.emplace_back();
  segments_[depth_].key.clear();
  segments_[depth_].index = index;
  depth_++;
}

namespace {

void appendUtf8(std::string& out, const uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

class Reader {
 public:
  Reader(const std::string_view doc, Visitor& visitor) : doc_(doc), visitor_(visitor) {}

  Result run() {
    skipSpace();
    if (!value(0)) return tooDeep_ ? Result::TooDeep : Result::Malformed;
    skipSpace();
    return pos_ == doc_.size() ? Result::Ok : Result::Malformed;
  }

 private:
  bool atEnd() const { return pos_ >= doc_.size(); }
  char peek() const { return doc_[pos_]; }

  void skipSpace() {
    while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) pos_++;
  }

  bool expect(const char c) {
    skipSpace();
    if (atEnd() || peek() != c) return false;
    pos_++;
    return true;
  }

  bool value(const size_t depth) {
    skipSpace();
    if (atEnd()) return false;
    switch (peek()) {
      case '{':
        return object(depth);
      case '[':
        return array(depth);
      case '"': {
        std::string text;
        if (!string(text)) return false;
        visitor_.onValue(path_, Type::String, text);
        return true;
      }
      case 't':
        return literal("true", Type::Bool);
      case 'f':
        return literal("false", Type::Bool);
      case 'n':
        return literal("null", Type::Null);
      default:
        return number();
    }
  }

  bool object(const size_t depth) {
    if (depth >= kMaxDepth) {
      tooDeep_ = true;
      return false;
    }
    pos_++;  // '{'
    visitor_.onBegin(path_, false);
    skipSpace();
    if (!atEnd() && peek() == '}') {
      pos_++;
      visitor_.onEnd(path_, false);
      return true;
    }
    while (true) {
      skipSpace();
      std::string key;
      if (atEnd() || peek() != '"' || !string(key)) return false;
      if (!expect(':')) return false;
      path_.pushKey(std::move(key));
      const bool ok = value(depth + 1);
      path_.pop();
      if (!ok) return false;
      skipSpace();
      if (atEnd()) return false;
      if (peek() == ',') {
        pos_++;
        continue;
      }
      if (peek() != '}') return false;
      pos_++;
      visitor_.onEnd(path_, false);
      return true;
    }
  }

  bool array(const size_t depth) {
    if (depth >= kMaxDepth) {
      tooDeep_ = true;
      return false;
    }
    pos_++;  // '['
    visitor_.onBegin(path_, true);
    skipSpace();
    if (!atEnd() && peek() == ']') {
      pos_++;
      visitor_.onEnd(path_, true);
      return true;
    }
    for (int index = 0;; index++) {
      path_.pushIndex(index);
      const bool ok = value(depth + 1);
      path_.pop();
      if (!ok) return false;
      skipSpace();
      if (atEnd()) return false;
      if (peek() == ',') {
        pos_++;
        continue;
      }
      if (peek() != ']') return false;
      pos_++;
      visitor_.onEnd(path_, true);
      return true;
    }
  }

  bool literal(const std::string_view word, const Type type) {
    if (doc_.substr(pos_, word.size()) != word) return false;
    pos_ += word.size();
    visitor_.onValue(path_, type, word);
    return true;
  }

  bool digits() {
    const size_t start = pos_;
    while (!atEnd() && peek() >= '0' && peek() <= '9') pos_++;
    return pos_ > start;
  }

  bool number() {
    const size_t start = pos_;
    if (!atEnd() && peek() == '-') pos_++;
    if (atEnd()) return false;
    if (peek() == '0') {
      pos_++;
    } else if (!digits()) {
      return false;
    }
    if (!atEnd() && peek() == '.') {
      pos_++;
      if (!digits()) return false;
    }
    if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
      pos_++;
      if (!atEnd() && (peek() == '+' || peek() == '-')) pos_++;
      if (!digits()) return false;
    }
    visitor_.onValue(path_, Type::Number, doc_.substr(start, pos_ - start));
    return true;
  }

  bool hex4(uint32_t& out) {
    if (pos_ + 4 > doc_.size()) return false;
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
      const char c = doc_[pos_++];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
    }
    out = value;
    return true;
  }

  // Reads a string starting at the opening quote. Raw bytes pass through (the transport gives UTF-8);
  // escapes are decoded, and a lone surrogate becomes U+FFFD.
  bool string(std::string& out) {
    pos_++;  // '"'
    while (true) {
      if (atEnd()) return false;
      const char c = doc_[pos_++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return false;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (atEnd()) return false;
      const char e = doc_[pos_++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          out += e;
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u': {
          uint32_t cp = 0;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            uint32_t low = 0;
            if (doc_.substr(pos_, 2) == "\\u") {
              const size_t save = pos_;
              pos_ += 2;
              if (!hex4(low)) return false;
              if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
              } else {
                pos_ = save;  // not a pair: the next escape is read on its own
                cp = 0xFFFD;
              }
            } else {
              cp = 0xFFFD;
            }
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
          }
          appendUtf8(out, cp);
          break;
        }
        default:
          return false;
      }
    }
  }

  std::string_view doc_;
  Visitor& visitor_;
  size_t pos_ = 0;
  bool tooDeep_ = false;
  Path path_;
};

}  // namespace

Result read(const std::string_view document, Visitor& visitor) { return Reader(document, visitor).run(); }

}  // namespace lexipoint::json

#endif  // LEXIRISE
