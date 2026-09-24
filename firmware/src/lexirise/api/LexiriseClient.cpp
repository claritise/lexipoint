#if LEXIRISE

#include "LexiriseClient.h"

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::api {
namespace {

ApiError fromOpenError(const net::OpenError error) {
  switch (error) {
    case net::OpenError::None:
      return ApiError::None;
    case net::OpenError::LowMemory:
      return ApiError::LowMemory;
    case net::OpenError::ClockNotSet:
      return ApiError::ClockNotSet;
    case net::OpenError::TlsFailed:
      return ApiError::Tls;
    case net::OpenError::Timeout:
      return ApiError::Timeout;
    case net::OpenError::ConnectFailed:
    default:
      return ApiError::Network;
  }
}

}  // namespace

const char* apiErrorName(const ApiError error) {
  switch (error) {
    case ApiError::None:
      return "ok";
    case ApiError::NotConfigured:
      return "not-configured";
    case ApiError::NoWifi:
      return "no-wifi";
    case ApiError::LowMemory:
      return "low-memory";
    case ApiError::ClockNotSet:
      return "clock-not-set";
    case ApiError::Network:
      return "network";
    case ApiError::Tls:
      return "tls";
    case ApiError::Timeout:
      return "timeout";
    case ApiError::Unauthorized:
      return "unauthorized";
    case ApiError::RateLimited:
      return "rate-limited";
    case ApiError::Server:
      return "server";
    case ApiError::Http:
      return "http";
    case ApiError::Malformed:
    default:
      return "malformed";
  }
}

ApiError classifyStatus(const int status) {
  if (status >= 200 && status < 300) return ApiError::None;
  if (status == 401 || status == 403) return ApiError::Unauthorized;
  if (status == 429) return ApiError::RateLimited;
  if (status >= 500 && status < 600) return ApiError::Server;
  return ApiError::Http;
}

bool LexiriseClient::configure(const std::string_view baseUrl, const std::string_view apiKey) {
  net::Endpoint endpoint;
  if (apiKey.empty() || !net::parseBaseUrl(baseUrl, endpoint)) {
    connection_.close();
    configured_ = false;
    apiKey_.clear();
    return false;
  }
  if (!configured_ || endpoint.host != endpoint_.host || endpoint.port != endpoint_.port) connection_.close();
  endpoint_ = std::move(endpoint);
  apiKey_.assign(apiKey);
  configured_ = true;
  return true;
}

ApiResponse LexiriseClient::send(const net::Request& request) {
  ApiResponse response;
  if (!configured_) {
    response.error = ApiError::NotConfigured;
    return response;
  }
  deadlineSet_ = false;
  const bool reused = connection_.isOpen();
  if (attempt(request, reused, response) == Attempt::StaleSession) {
    response = ApiResponse();
    if (request.retryable()) {
      attempt(request, false, response);
    } else {
      response.error = ApiError::Network;  // it may have reached the server: never sent twice
    }
  }
  return response;
}

// The read wait: kHttpTimeoutMs, or less when the request's deadline is nearer (0 once it's past).
uint32_t LexiriseClient::readTimeout() const {
  const long left = static_cast<long>(deadlineMs_ - clock_());
  if (left <= 0) return 0;
  return static_cast<unsigned long>(left) < config::kHttpTimeoutMs ? static_cast<uint32_t>(left)
                                                                   : config::kHttpTimeoutMs;
}

LexiriseClient::Attempt LexiriseClient::attempt(const net::Request& request, const bool reused, ApiResponse& out) {
  if (!reused) {
    const net::OpenError openError = connection_.open(endpoint_);
    if (openError != net::OpenError::None) {
      out.error = fromOpenError(openError);
      return Attempt::Done;
    }
  }
  // The request's budget starts once a connection is up (opening has its own bounds). The retry on a
  // fresh session keeps whatever the stale attempt left.
  if (!deadlineSet_) {
    deadlineMs_ = clock_() + config::kRequestDeadlineMs;
    deadlineSet_ = true;
  }

  const std::string wire = net::buildRequest(endpoint_, request, apiKey_, userAgent_);
  if (!connection_.writeAll(wire.data(), wire.size())) {
    connection_.close();
    if (reused) return Attempt::StaleSession;
    out.error = ApiError::Network;
    return Attempt::Done;
  }

  net::ResponseParser parser;
  char buffer[config::kHttpReadChunkBytes];
  bool gotBytes = false;
  while (!parser.done() && parser.state() != net::ResponseParser::State::Error) {
    const uint32_t timeout = readTimeout();
    const int n = timeout == 0 ? 0 : connection_.read(buffer, sizeof(buffer), timeout);
    if (n == 0) {
      connection_.close();
      out.error = ApiError::Timeout;
      return Attempt::Done;
    }
    if (n < 0) {
      if (!gotBytes && reused) {
        connection_.close();
        return Attempt::StaleSession;
      }
      parser.onClose();
      break;
    }
    gotBytes = true;
    const size_t used = parser.feed(buffer, static_cast<size_t>(n));
    // Bytes past the end of the response: the server pipelined something we never asked for.
    if (parser.done() && used < static_cast<size_t>(n)) {
      connection_.close();
      out.error = ApiError::Malformed;
      return Attempt::Done;
    }
  }

  if (!parser.done()) {
    connection_.close();
    out.error = parser.failure() == net::ResponseParser::Failure::Truncated ? ApiError::Network : ApiError::Malformed;
    return Attempt::Done;
  }
  if (!parser.keepAlive()) connection_.close();

  out.status = parser.status();
  out.error = classifyStatus(out.status);
  if (out.error == ApiError::RateLimited) {
    out.retryAfterS = parser.retryAfterSeconds() != 0 ? parser.retryAfterSeconds() : config::kRetryAfterDefaultS;
  }
  if (out.ok()) out.body = parser.takeBody();
  return Attempt::Done;
}

}  // namespace lexipoint::api

#endif  // LEXIRISE
