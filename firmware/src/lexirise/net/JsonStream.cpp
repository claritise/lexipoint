#if LEXIRISE

#include "JsonStream.h"

namespace lexipoint::json {
namespace {

constexpr uint32_t kReplacement = 0xFFFD;
bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool isDigit(const char c) { return c >= '0' && c <= '9'; }
bool isHighSurrogate(const uint32_t cp) { return cp >= 0xD800 && cp <= 0xDBFF; }
bool isLowSurrogate(const uint32_t cp) { return cp >= 0xDC00 && cp <= 0xDFFF; }

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

StreamReader::StreamReader(Visitor& visitor, const size_t maxStringBytes)
    : visitor_(visitor), maxString_(maxStringBytes) {
  containers_.reserve(config::kJsonMaxDepth);
  indices_.reserve(config::kJsonMaxDepth);
  text_.reserve(maxStringBytes);
}

bool StreamReader::fail(const Result why) {
  state_ = State::Failed;
  failure_ = why;
  return false;
}

bool StreamReader::feed(const std::string_view bytes) {
  for (const char c : bytes) {
    if (!step(c)) return false;
  }
  return state_ != State::Failed;
}

Result StreamReader::finish() {
  if (state_ == State::Number && !numberEnded()) return failure_;
  if (state_ == State::Done) return Result::Ok;
  return state_ == State::Failed ? failure_ : Result::Malformed;
}

void StreamReader::appendByte(const char c) {
  if (over_) return;
  if (text_.size() >= maxString_) {
    over_ = true;
    return;
  }
  text_ += c;
}

void StreamReader::appendCodepoint(const uint32_t cp) {
  if (cp < 0x80) {
    appendByte(static_cast<char>(cp));
  } else if (cp < 0x800) {
    appendByte(static_cast<char>(0xC0 | (cp >> 6)));
    appendByte(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    appendByte(static_cast<char>(0xE0 | (cp >> 12)));
    appendByte(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    appendByte(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    appendByte(static_cast<char>(0xF0 | (cp >> 18)));
    appendByte(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    appendByte(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    appendByte(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

void StreamReader::valueDone() {
  if (containers_.empty()) {
    state_ = State::Done;
    return;
  }
  path_.pop();  // the member or element that just ended
  state_ = State::AfterValue;
}

bool StreamReader::open(const bool isArray) {
  if (containers_.size() >= config::kJsonMaxDepth) return fail(Result::TooDeep);
  visitor_.onBegin(path_, isArray);
  containers_.push_back(isArray);
  indices_.push_back(0);
  state_ = isArray ? State::ArrayFirst : State::ObjectFirst;
  return true;
}

bool StreamReader::close(const bool isArray) {
  if (containers_.empty() || containers_.back() != isArray) return fail();
  containers_.pop_back();
  indices_.pop_back();
  visitor_.onEnd(path_, isArray);
  valueDone();
  return true;
}

bool StreamReader::startValue(const char c) {
  text_.clear();
  over_ = false;
  switch (c) {
    case '{':
      return open(false);
    case '[':
      return open(true);
    case '"':
      stringIsKey_ = false;
      highSurrogate_ = 0;
      state_ = State::String;
      return true;
    case 't':
      literal_ = "true";
      break;
    case 'f':
      literal_ = "false";
      break;
    case 'n':
      literal_ = "null";
      break;
    default:
      if (c != '-' && !isDigit(c)) return fail();
      num_ = Num::Sign;
      state_ = State::Number;
      if (!numberByte(c)) return fail();
      return true;
  }
  literalAt_ = 1;
  state_ = State::Literal;
  return true;
}

bool StreamReader::numberByte(const char c) {
  Num next = num_;
  switch (num_) {
    case Num::Sign:  // nothing yet, or a '-'
      if (c == '-' && text_.empty() && !over_) break;
      if (c == '0') {
        next = Num::Zero;
      } else if (isDigit(c)) {
        next = Num::Int;
      } else {
        return false;
      }
      break;
    case Num::Zero:
    case Num::Int:
      if (num_ == Num::Int && isDigit(c)) break;
      if (c == '.') {
        next = Num::Dot;
      } else if (c == 'e' || c == 'E') {
        next = Num::Exp;
      } else {
        return false;
      }
      break;
    case Num::Dot:
      if (!isDigit(c)) return false;
      next = Num::Frac;
      break;
    case Num::Frac:
      if (isDigit(c)) break;
      if (c != 'e' && c != 'E') return false;
      next = Num::Exp;
      break;
    case Num::Exp:
      if (c == '+' || c == '-') {
        next = Num::ExpSign;
      } else if (isDigit(c)) {
        next = Num::ExpDigits;
      } else {
        return false;
      }
      break;
    case Num::ExpSign:
    case Num::ExpDigits:
      if (!isDigit(c)) return false;
      next = Num::ExpDigits;
      break;
  }
  num_ = next;
  appendByte(c);
  return true;
}

bool StreamReader::numberEnded() {
  if (num_ != Num::Zero && num_ != Num::Int && num_ != Num::Frac && num_ != Num::ExpDigits) return fail();
  if (over_) {
    skipped_++;
  } else {
    visitor_.onValue(path_, Type::Number, text_);
  }
  valueDone();
  return true;
}

bool StreamReader::endString() {
  if (stringIsKey_) {
    if (over_) return fail();  // a key that long isn't one Lexirise sends
    state_ = State::Colon;
    return true;
  }
  if (over_) {
    skipped_++;
  } else {
    visitor_.onValue(path_, Type::String, text_);
  }
  valueDone();
  return true;
}

bool StreamReader::step(const char c) {
  switch (state_) {
    case State::Value:
      if (isSpace(c)) return true;
      return startValue(c);
    case State::ArrayFirst:
      if (isSpace(c)) return true;
      if (c == ']') return close(true);
      path_.pushIndex(0);
      return startValue(c);
    case State::ObjectFirst:
    case State::ObjectKey:
      if (isSpace(c)) return true;
      if (c == '}' && state_ == State::ObjectFirst) return close(false);
      if (c != '"') return fail();
      text_.clear();
      over_ = false;
      stringIsKey_ = true;
      highSurrogate_ = 0;
      state_ = State::String;
      return true;
    case State::Colon:
      if (isSpace(c)) return true;
      if (c != ':') return fail();
      path_.pushKeyCopy(text_);
      state_ = State::Value;
      return true;
    case State::AfterValue:
      if (isSpace(c)) return true;
      if (c == ',') {
        if (containers_.back()) {
          path_.pushIndex(++indices_.back());
          state_ = State::Value;
        } else {
          state_ = State::ObjectKey;
        }
        return true;
      }
      if (c == ']' || c == '}') return close(c == ']');
      return fail();
    case State::String:
      if (highSurrogate_ != 0 && c != '\\') {  // a high surrogate not followed by an escape: unpaired
        appendCodepoint(kReplacement);
        highSurrogate_ = 0;
      }
      if (c == '"') return endString();
      if (c == '\\') {
        state_ = State::Escape;
        return true;
      }
      if (static_cast<unsigned char>(c) < 0x20) return fail();
      appendByte(c);
      return true;
    case State::Escape: {
      if (c == 'u') {
        hex_ = 0;
        hexDigits_ = 0;
        state_ = State::Hex;
        return true;
      }
      if (highSurrogate_ != 0) {  // followed by another escape: unpaired
        appendCodepoint(kReplacement);
        highSurrogate_ = 0;
      }
      char decoded = 0;
      switch (c) {
        case '"':
        case '\\':
        case '/':
          decoded = c;
          break;
        case 'b':
          decoded = '\b';
          break;
        case 'f':
          decoded = '\f';
          break;
        case 'n':
          decoded = '\n';
          break;
        case 'r':
          decoded = '\r';
          break;
        case 't':
          decoded = '\t';
          break;
        default:
          return fail();
      }
      appendByte(decoded);
      state_ = State::String;
      return true;
    }
    case State::Hex: {
      const int v = hexValue(c);
      if (v < 0) return fail();
      hex_ = (hex_ << 4) | static_cast<uint32_t>(v);
      if (++hexDigits_ < 4) return true;
      state_ = State::String;
      uint32_t cp = hex_;
      if (highSurrogate_ != 0) {
        const uint32_t high = highSurrogate_;
        highSurrogate_ = 0;
        if (isLowSurrogate(cp)) {
          appendCodepoint(0x10000 + ((high - 0xD800) << 10) + (cp - 0xDC00));
          return true;
        }
        appendCodepoint(kReplacement);  // the high one unpaired; this escape is read on its own
      }
      if (isHighSurrogate(cp)) {
        highSurrogate_ = cp;  // its pair may follow
        return true;
      }
      if (isLowSurrogate(cp)) cp = kReplacement;
      appendCodepoint(cp);
      return true;
    }
    case State::Number:
      if (numberByte(c)) return true;
      if (!numberEnded()) return false;
      return step(c);  // the byte after the number, read in the state it left
    case State::Literal:
      if (c != literal_[literalAt_]) return fail();
      if (literal_[++literalAt_] != '\0') return true;
      visitor_.onValue(path_, literal_[0] == 'n' ? Type::Null : Type::Bool, literal_);
      valueDone();
      return true;
    case State::Done:
      if (!isSpace(c)) return fail();
      return true;
    case State::Failed:
      return false;
  }
  return fail();
}

}  // namespace lexipoint::json

#endif  // LEXIRISE
