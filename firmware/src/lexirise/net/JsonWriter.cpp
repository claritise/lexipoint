#if LEXIRISE

#include "JsonWriter.h"

#include <cstdint>
#include <cstdio>

namespace lexipoint::net {
namespace {

constexpr const char* kReplacement = "\xEF\xBF\xBD";  // U+FFFD

// Length of the valid UTF-8 sequence starting at text[i], or 0 if it is invalid (overlong, surrogate,
// out of range, or truncated).
size_t validSequenceLength(const std::string_view text, const size_t i) {
  const auto byte = [&](const size_t k) { return static_cast<uint8_t>(text[k]); };
  const uint8_t b0 = byte(i);
  size_t len = 0;
  uint8_t lo = 0x80;
  uint8_t hi = 0xBF;
  if (b0 < 0x80) return 1;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    len = 2;
  } else if (b0 >= 0xE0 && b0 <= 0xEF) {
    len = 3;
    if (b0 == 0xE0) lo = 0xA0;  // overlong
    if (b0 == 0xED) hi = 0x9F;  // UTF-16 surrogates
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    len = 4;
    if (b0 == 0xF0) lo = 0x90;  // overlong
    if (b0 == 0xF4) hi = 0x8F;  // > U+10FFFF
  } else {
    return 0;
  }
  if (i + len > text.size()) return 0;
  if (byte(i + 1) < lo || byte(i + 1) > hi) return 0;
  for (size_t k = 2; k < len; k++) {
    if (byte(i + k) < 0x80 || byte(i + k) > 0xBF) return 0;
  }
  return len;
}

}  // namespace

void appendJsonString(std::string& out, const std::string_view text) {
  out.reserve(out.size() + text.size() + 2);
  out += '"';
  size_t i = 0;
  while (i < text.size()) {
    const char c = text[i];
    const auto u = static_cast<uint8_t>(c);
    if (u >= 0x80) {
      const size_t len = validSequenceLength(text, i);
      if (len == 0) {
        out += kReplacement;
        i++;
      } else {
        out.append(text.substr(i, len));
        i += len;
      }
      continue;
    }
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (u < 0x20 || u == 0x7F) {
          char escaped[7];
          snprintf(escaped, sizeof(escaped), "\\u%04x", u);
          out += escaped;
        } else {
          out += c;
        }
    }
    i++;
  }
  out += '"';
}

void JsonObject::key(const std::string_view key) {
  if (body_.size() > 1) body_ += ',';
  appendJsonString(body_, key);
  body_ += ':';
}

JsonObject& JsonObject::add(const std::string_view key, const std::string_view value) {
  this->key(key);
  appendJsonString(body_, value);
  return *this;
}

JsonObject& JsonObject::add(const std::string_view key, const long long value) {
  this->key(key);
  body_ += std::to_string(value);
  return *this;
}

JsonObject& JsonObject::add(const std::string_view key, const bool value) {
  this->key(key);
  body_ += value ? "true" : "false";
  return *this;
}

JsonObject& JsonObject::add(const std::string_view key, const std::vector<int>& values) {
  this->key(key);
  body_ += '[';
  for (size_t i = 0; i < values.size(); i++) {
    if (i > 0) body_ += ',';
    body_ += std::to_string(values[i]);
  }
  body_ += ']';
  return *this;
}

JsonObject& JsonObject::add(const std::string_view key, const JsonObject& nested) {
  this->key(key);
  body_ += nested.str();
  return *this;
}

JsonObject& JsonObject::addNull(const std::string_view key) {
  this->key(key);
  body_ += "null";
  return *this;
}

JsonObject& JsonObject::add(const std::string_view key, const std::vector<std::string>& values) {
  this->key(key);
  body_ += '[';
  for (size_t i = 0; i < values.size(); i++) {
    if (i > 0) body_ += ',';
    appendJsonString(body_, values[i]);
  }
  body_ += ']';
  return *this;
}

}  // namespace lexipoint::net

#endif  // LEXIRISE
