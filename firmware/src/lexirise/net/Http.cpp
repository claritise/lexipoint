#if LEXIRISE

#include "Http.h"

#include <cctype>

namespace lexipoint::net {
namespace {

bool iequals(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

// Parses a whole non-negative decimal (no sign, no junk), refusing values above `max`.
bool parseDecimal(const std::string_view s, const uint64_t max, uint64_t& out) {
  if (s.empty()) return false;
  uint64_t value = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + static_cast<uint64_t>(c - '0');
    if (value > max) return false;
  }
  out = value;
  return true;
}

// Hex chunk size (at most 8 digits), ignoring chunk extensions (";name=value").
bool parseChunkSize(std::string_view line, size_t& out) {
  const size_t semi = line.find(';');
  if (semi != std::string_view::npos) line = line.substr(0, semi);
  line = trim(line);
  if (line.empty() || line.size() > 8) return false;
  size_t value = 0;
  for (const char c : line) {
    int digit = 0;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else {
      return false;
    }
    value = value * 16 + static_cast<size_t>(digit);
  }
  out = value;
  return true;
}

bool isHostChar(const char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
}

}  // namespace

bool parseBaseUrl(std::string_view url, Endpoint& out) {
  constexpr std::string_view kScheme = "https://";
  if (url.size() > config::kMaxBaseUrlLength) return false;
  if (url.substr(0, kScheme.size()) != kScheme) return false;
  url.remove_prefix(kScheme.size());
  if (url.find_first_of("?#@") != std::string_view::npos) return false;

  const size_t slash = url.find('/');
  std::string_view authority = url.substr(0, slash);
  std::string_view path = slash == std::string_view::npos ? std::string_view() : url.substr(slash);
  while (!path.empty() && path.back() == '/') path.remove_suffix(1);

  Endpoint result;
  const size_t colon = authority.find(':');
  if (colon != std::string_view::npos) {
    uint64_t port = 0;
    if (!parseDecimal(authority.substr(colon + 1), 65535, port) || port == 0) return false;
    result.port = static_cast<uint16_t>(port);
    authority = authority.substr(0, colon);
  }
  if (authority.empty() || authority.front() == '.' || authority.front() == '-') return false;
  for (const char c : authority) {
    if (!isHostChar(c)) return false;
  }
  for (const char c : path) {
    if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) return false;
  }
  result.host = std::string(authority);
  result.basePath = std::string(path);
  out = std::move(result);
  return true;
}

const char* methodName(const Method method) {
  switch (method) {
    case Method::Post:
      return "POST";
    case Method::Patch:
      return "PATCH";
    case Method::Put:
      return "PUT";
    case Method::Delete:
      return "DELETE";
    case Method::Get:
    default:
      return "GET";
  }
}

std::string buildRequest(const Endpoint& endpoint, const Request& request, const std::string_view apiKey,
                         const std::string_view userAgent) {
  const bool hasBody = !request.body.empty() || request.method == Method::Post || request.method == Method::Patch ||
                       request.method == Method::Put;
  std::string out;
  out.reserve(256 + request.path.size() + request.body.size());
  out += methodName(request.method);
  out += ' ';
  out += endpoint.basePath;
  out += request.path;
  out += " HTTP/1.1\r\nHost: ";
  out += endpoint.host;
  if (endpoint.port != config::kHttpsPort) {
    out += ':';
    out += std::to_string(endpoint.port);
  }
  out += "\r\nAuthorization: Bearer ";
  out += apiKey;
  out += "\r\nAccept: application/json\r\nUser-Agent: ";
  out += userAgent;
  out += "\r\n";
  if (hasBody) {
    out += "Content-Type: application/json\r\nContent-Length: ";
    out += std::to_string(request.body.size());
    out += "\r\n";
  }
  out += "\r\n";
  out += request.body;
  return out;
}

// --- ResponseParser ---

void ResponseParser::fail(const Failure failure) {
  state_ = State::Error;
  failure_ = failure;
  keepAlive_ = false;
}

// Accumulates bytes into `line` up to CRLF (a bare LF is accepted). Returns false on an overlong line.
bool ResponseParser::takeLine(const char*& p, const char* end, std::string& line, bool& complete) {
  complete = false;
  while (p < end) {
    const char c = *p++;
    if (c == '\n') {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      complete = true;
      return true;
    }
    if (line.size() >= config::kHttpMaxLineBytes) return false;
    line += c;
  }
  return true;
}

size_t ResponseParser::feed(const char* data, const size_t len) {
  const char* p = data;
  const char* end = data + len;
  while (p < end && state_ == State::Headers) {
    const char* start = p;
    bool complete = false;
    if (!takeLine(p, end, line_, complete)) {
      fail(Failure::HeadersTooLarge);
      return static_cast<size_t>(p - data);
    }
    headerBytes_ += static_cast<size_t>(p - start);
    if (headerBytes_ > config::kHttpMaxHeaderBytes) {
      fail(Failure::HeadersTooLarge);
      return static_cast<size_t>(p - data);
    }
    if (!complete) break;
    bool ok = true;
    if (!sawStatus_) {
      ok = onStatusLine(line_);
      sawStatus_ = true;
    } else if (line_.empty()) {
      if (status_ >= 100 && status_ < 200) {
        // Interim response (100 Continue): discard it and everything it said, parse the real one.
        *this = ResponseParser(maxBody_, noBody_);
      } else {
        ok = onHeadersEnd();
      }
    } else {
      ok = onHeaderLine(line_);
    }
    line_.clear();
    if (!ok) {
      if (state_ != State::Error) fail(Failure::Malformed);
      return static_cast<size_t>(p - data);
    }
  }
  if (p < end && state_ == State::Body) p += feedBody(p, static_cast<size_t>(end - p));
  return static_cast<size_t>(p - data);
}

bool ResponseParser::onStatusLine(const std::string_view line) {
  // "HTTP/1.x NNN reason"
  if (line.size() < 12 || line.substr(0, 7) != "HTTP/1." || line[8] != ' ') return false;
  uint64_t code = 0;
  if (!parseDecimal(line.substr(9, 3), 999, code) || code < 100) return false;
  if (line.size() > 12 && line[12] != ' ') return false;
  status_ = static_cast<int>(code);
  keepAlive_ = line[7] == '1';  // HTTP/1.1 defaults to persistent; 1.0 does not
  return true;
}

bool ResponseParser::onHeaderLine(const std::string_view line) {
  const size_t colon = line.find(':');
  if (colon == std::string_view::npos || colon == 0) return false;
  const std::string_view name = line.substr(0, colon);
  const std::string_view value = trim(line.substr(colon + 1));
  if (iequals(name, "content-length")) {
    uint64_t length = 0;
    if (!parseDecimal(value, UINT32_MAX, length)) return false;
    if (sawLength_ && length != remaining_) return false;  // conflicting lengths: request smuggling shape
    sawLength_ = true;
    remaining_ = static_cast<size_t>(length);
  } else if (iequals(name, "transfer-encoding")) {
    if (!iequals(value, "chunked")) return false;  // gzip etc. were never asked for
    framing_ = Framing::Chunked;
  } else if (iequals(name, "connection")) {
    if (iequals(value, "close")) keepAlive_ = false;
  } else if (iequals(name, "retry-after")) {
    uint64_t seconds = 0;
    if (parseDecimal(value, UINT32_MAX, seconds)) {  // HTTP-date form is ignored (falls back to default)
      if (seconds < 1) seconds = 1;
      if (seconds > config::kRetryAfterMaxS) seconds = config::kRetryAfterMaxS;
      retryAfterS_ = static_cast<uint32_t>(seconds);
    }
  }
  return true;
}

bool ResponseParser::onHeadersEnd() {
  if (framing_ == Framing::Chunked) {
    if (sawLength_) return false;  // both framings: refuse rather than guess
    remaining_ = 0;
    chunkState_ = ChunkState::Size;
  } else if (noBody_ || status_ == 204 || status_ == 304) {
    framing_ = Framing::None;
  } else if (sawLength_) {
    framing_ = Framing::Length;
    if (remaining_ > maxBody_) {
      fail(Failure::BodyTooLarge);
      return false;
    }
    body_.reserve(remaining_);
  } else {
    framing_ = Framing::UntilClose;
  }
  if (framing_ == Framing::None || (framing_ == Framing::Length && remaining_ == 0)) {
    state_ = State::Done;
  } else {
    state_ = State::Body;
  }
  return true;
}

bool ResponseParser::appendBody(const char* data, const size_t len) {
  if (body_.size() + len > maxBody_) {
    fail(Failure::BodyTooLarge);
    return false;
  }
  body_.append(data, len);
  return true;
}

size_t ResponseParser::feedBody(const char* data, const size_t len) {
  if (framing_ == Framing::UntilClose) {
    return appendBody(data, len) ? len : 0;
  }
  if (framing_ == Framing::Length) {
    const size_t take = len < remaining_ ? len : remaining_;
    if (!appendBody(data, take)) return 0;
    remaining_ -= take;
    if (remaining_ == 0) state_ = State::Done;
    return take;
  }

  // Chunked.
  const char* p = data;
  const char* end = data + len;
  while (p < end && state_ == State::Body) {
    switch (chunkState_) {
      case ChunkState::Size: {
        bool complete = false;
        if (!takeLine(p, end, line_, complete)) {
          fail(Failure::Malformed);
          break;
        }
        if (!complete) break;
        size_t size = 0;
        if (!parseChunkSize(line_, size)) {
          fail(Failure::Malformed);
          break;
        }
        if (size > maxBody_ - body_.size()) {
          fail(Failure::BodyTooLarge);
          break;
        }
        line_.clear();
        remaining_ = size;
        chunkState_ = size == 0 ? ChunkState::Trailer : ChunkState::Data;
        break;
      }
      case ChunkState::Data: {
        const size_t available = static_cast<size_t>(end - p);
        const size_t take = available < remaining_ ? available : remaining_;
        if (!appendBody(p, take)) break;
        p += take;
        remaining_ -= take;
        if (remaining_ == 0) chunkState_ = ChunkState::DataEnd;
        break;
      }
      case ChunkState::DataEnd: {
        bool complete = false;
        if (!takeLine(p, end, line_, complete)) {
          fail(Failure::Malformed);
          break;
        }
        if (!complete) break;
        if (!line_.empty()) {
          fail(Failure::Malformed);
          break;
        }
        chunkState_ = ChunkState::Size;
        break;
      }
      case ChunkState::Trailer: {
        bool complete = false;
        if (!takeLine(p, end, line_, complete)) {
          fail(Failure::HeadersTooLarge);
          break;
        }
        if (!complete) break;
        const bool last = line_.empty();
        line_.clear();
        if (last) state_ = State::Done;  // trailer fields themselves are ignored
        break;
      }
    }
  }
  return static_cast<size_t>(p - data);
}

void ResponseParser::onClose() {
  if (state_ == State::Body && framing_ == Framing::UntilClose) {
    state_ = State::Done;
    keepAlive_ = false;
    return;
  }
  if (state_ == State::Headers || state_ == State::Body) fail(Failure::Truncated);
  keepAlive_ = false;
}

}  // namespace lexipoint::net

#endif  // LEXIRISE
