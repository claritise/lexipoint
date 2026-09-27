#pragma once

// Pure HTTP/1.1 pieces for the Lexirise client: base URL parsing, request serialisation and an
// incremental response parser (Content-Length, chunked, or close-delimited bodies, all bounded).
// No sockets here; the TLS transport feeds bytes in. Tests: test/lexirise_net/HttpTest.cpp.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::net {

struct Endpoint {
  std::string host;
  uint16_t port = config::kHttpsPort;
  std::string basePath;  // "" or "/prefix" (no trailing slash)
};

// Parses an https:// base URL ("https://host[:port][/prefix]"). Rejects any other scheme, userinfo,
// query, fragment, and hosts with characters outside [A-Za-z0-9.-].
bool parseBaseUrl(std::string_view url, Endpoint& out);

enum class Method { Get, Post, Patch, Put, Delete };
const char* methodName(Method method);

struct Request {
  Method method = Method::Get;
  std::string path;  // absolute, e.g. "/v1/me"
  std::string body;  // JSON; sent with Content-Type only when non-empty or the method carries one
  // Safe to send twice (a repeat changes nothing): only these are resent after a stale keep-alive
  // session. GET/PUT/DELETE always are; a POST opts in (analyze/text is read-only).
  bool idempotent = false;

  bool retryable() const {
    return idempotent || method == Method::Get || method == Method::Put || method == Method::Delete;
  }
};

// Serialises the request with Host, Authorization: Bearer, Accept, User-Agent, Content-Type and
// Content-Length. `apiKey` must already have passed isPlausibleApiKey (no CR/LF can reach a header).
std::string buildRequest(const Endpoint& endpoint, const Request& request, std::string_view apiKey,
                         std::string_view userAgent);

// Where a streamed body goes (a response too large to hold: V7a's vocabulary pages), piece by piece as it's
// decoded. False: stop (the body is unreadable); the response fails as Rejected.
class BodySink {
 public:
  virtual ~BodySink() = default;
  virtual bool onBody(const char* data, size_t len) = 0;
  virtual size_t maxBytes() const = 0;  // the whole body's cap: past it the response is malformed
};

// Incremental response parser. Feed it bytes as they arrive; it stops at the end of one response.
// noBody: the request was a HEAD, so the response has no body whatever its headers say.
class ResponseParser {
 public:
  enum class State { Headers, Body, Done, Error };
  enum class Failure { None, Malformed, HeadersTooLarge, BodyTooLarge, Truncated, Rejected };

  explicit ResponseParser(size_t maxBody = config::kHttpMaxBodyBytes, bool noBody = false)
      : maxBody_(maxBody), noBody_(noBody) {}

  // A 2xx body goes to `sink` as it comes, up to `maxBytes` in all, and is never held (body() stays empty); any
  // other status's body is kept as usual (maxBody), for the error it carries. Set before the first feed().
  void streamTo(BodySink* sink, size_t maxBytes) {
    sink_ = sink;
    streamMax_ = maxBytes;
  }

  // Consumes up to len bytes and returns how many it used (fewer only once Done or Error).
  size_t feed(const char* data, size_t len);

  // The peer closed the connection. Completes a close-delimited body; anything else is Truncated.
  void onClose();

  State state() const { return state_; }
  Failure failure() const { return failure_; }
  bool done() const { return state_ == State::Done; }

  int status() const { return status_; }
  const std::string& body() const { return body_; }
  std::string takeBody() { return std::move(body_); }
  // Whether the connection may carry another request (HTTP/1.1 without "Connection: close").
  bool keepAlive() const { return keepAlive_ && framing_ != Framing::UntilClose; }
  // Retry-After in seconds for a 429/503, clamped to [1, kRetryAfterMaxS]; 0 when absent/unparsable.
  uint32_t retryAfterSeconds() const { return retryAfterS_; }

 private:
  enum class Framing { None, Length, Chunked, UntilClose };
  enum class ChunkState { Size, Data, DataEnd, Trailer };

  void fail(Failure failure);
  bool takeLine(const char*& p, const char* end, std::string& line, bool& complete);
  bool onStatusLine(std::string_view line);
  bool onHeaderLine(std::string_view line);
  bool onHeadersEnd();
  size_t feedBody(const char* data, size_t len);
  bool appendBody(const char* data, size_t len);
  size_t bodyRoom() const;  // bytes the body may still take

  size_t maxBody_;
  bool noBody_ = false;
  State state_ = State::Headers;
  Failure failure_ = Failure::None;
  std::string line_;
  size_t headerBytes_ = 0;
  bool sawStatus_ = false;
  int interimResponses_ = 0;

  int status_ = 0;
  bool keepAlive_ = false;
  uint32_t retryAfterS_ = 0;
  Framing framing_ = Framing::None;
  size_t remaining_ = 0;  // Length: bytes left; Chunked: bytes left in the current chunk
  bool sawLength_ = false;
  ChunkState chunkState_ = ChunkState::Size;
  std::string body_;
  BodySink* sink_ = nullptr;
  size_t streamMax_ = 0;
  bool streaming_ = false;  // this response's body goes to sink_ (a 2xx)
  size_t streamed_ = 0;
};

}  // namespace lexipoint::net
