#pragma once

// Lexirise HTTP client (lexirise-client.md §1-2, offline-and-errors.md §1). Sends one request at a
// time over a Connection, reuses the session while the server keeps it alive, and maps every
// failure to one ApiError. Not thread-safe: one owner (the lookup worker) drives it.
// Tests: test/lexirise_net/LexiriseClientTest.cpp.

#include <cstdint>
#include <string>
#include <string_view>

#include "lexirise/net/Connection.h"
#include "lexirise/net/Http.h"

namespace lexipoint::api {

enum class ApiError {
  None,
  NotConfigured,  // no key, or an unusable base URL
  NoWifiSaved,    // no saved network: nothing to reach Lexirise with (StarDict answers unmarked)
  NoWifi,         // the join failed, or the radio is someone else's (hotspot): offline
  LowMemory,
  ClockNotSet,
  Network,  // connect failed or the connection dropped
  Tls,      // handshake or certificate verification failed
  Timeout,
  Unauthorized,  // 401 / 403: the provider disables itself until reboot
  RateLimited,   // 429: back off retryAfterS
  Server,        // 5xx
  Http,          // any other non-2xx
  Malformed,     // unparseable or over-limit response
  Cancelled,     // given up for the reader's input (the call's abort, V7b)
};

const char* apiErrorName(ApiError error);

struct ApiResponse {
  ApiError error = ApiError::None;
  int status = 0;  // HTTP status when one was received
  uint32_t retryAfterS = 0;
  std::string body;  // 2xx only
  // The whole request was written to the server, so it may have acted on it whatever the error (a read timeout,
  // a cut answer). False: it never left (no connection, a failed connect or write, a refusal before the network).
  bool sent = false;
  bool ok() const { return error == ApiError::None; }
};

// Maps an HTTP status (and Retry-After) to the error the provider acts on.
ApiError classifyStatus(int status);
constexpr int kHttpNotFound = 404;  // a deck deleted in Lexirise (ApiError::Http with this status)

class LexiriseClient {
 public:
  using Clock = unsigned long (*)();  // millis() on the device, a fake in tests

  LexiriseClient(net::Connection& connection, std::string userAgent, Clock clock)
      : connection_(connection), userAgent_(std::move(userAgent)), clock_(clock) {}

  // Points the client at a base URL and key. Closes the session if the endpoint changed. Returns
  // false (and the client stays unconfigured) for a non-https or malformed URL or an empty key.
  bool configure(std::string_view baseUrl, std::string_view apiKey);
  bool configured() const { return configured_; }

  // Sends one request. A reused keep-alive session that turns out to be stale (it fails before any
  // response byte) is reopened and a retryable() request sent once more; nothing else is retried. Once a
  // connection is open the whole request, retry included, gets config::kRequestDeadlineMs.
  // `sink`: a 2xx body is streamed to it (up to its maxBytes()) and never held; the response's body
  // stays empty. A sink that stops the body makes the response Malformed.
  // `abort` (optional, V7b) is asked between reads and in the connection's waits: true gives the call up (Cancelled,
  // the connection closed; never resent).
  ApiResponse send(const net::Request& request, net::BodySink* sink = nullptr, net::Abort abort = nullptr);

  void close() { connection_.close(); }
  void forgetSession() { connection_.forgetSession(); }

 private:
  enum class Attempt { Done, StaleSession };
  Attempt attempt(const net::Request& request, bool reused, ApiResponse& out, net::BodySink* sink);
  uint32_t readTimeout() const;

  net::Connection& connection_;
  std::string userAgent_;
  Clock clock_;
  unsigned long deadlineMs_ = 0;
  bool deadlineSet_ = false;
  net::Endpoint endpoint_;
  std::string apiKey_;
  bool configured_ = false;
};

}  // namespace lexipoint::api
