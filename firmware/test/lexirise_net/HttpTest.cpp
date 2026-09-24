#include <gtest/gtest.h>

#include <string>

#include "lexirise/net/Http.h"

using lexipoint::net::buildRequest;
using lexipoint::net::Endpoint;
using lexipoint::net::Method;
using lexipoint::net::parseBaseUrl;
using lexipoint::net::Request;
using lexipoint::net::ResponseParser;
namespace config = lexipoint::config;

namespace {

// Feeds `wire` in pieces of `step` bytes (1 exercises every split point).
ResponseParser parse(const std::string& wire, const size_t step = 1, const size_t maxBody = config::kHttpMaxBodyBytes) {
  ResponseParser parser(maxBody);
  size_t at = 0;
  while (at < wire.size() && !parser.done() && parser.state() != ResponseParser::State::Error) {
    const size_t len = std::min(step, wire.size() - at);
    at += parser.feed(wire.data() + at, len);
  }
  return parser;
}

}  // namespace

TEST(HttpBaseUrl, AcceptsHttpsHostsPortsAndPrefixes) {
  Endpoint e;
  ASSERT_TRUE(parseBaseUrl("https://api.lexirise.app", e));
  EXPECT_EQ(e.host, "api.lexirise.app");
  EXPECT_EQ(e.port, 443);
  EXPECT_EQ(e.basePath, "");
  ASSERT_TRUE(parseBaseUrl("https://staging.example.com:8443/api/", e));
  EXPECT_EQ(e.host, "staging.example.com");
  EXPECT_EQ(e.port, 8443);
  EXPECT_EQ(e.basePath, "/api");
}

TEST(HttpBaseUrl, RejectsEverythingElse) {
  Endpoint e;
  for (const char* bad : {"http://api.lexirise.app", "api.lexirise.app", "https://", "https://user@host",
                          "https://host?x=1", "https://host#f", "https://host:0", "https://host:70000", "https://host:",
                          "https://ho st", "https://-host", "https://host/a b", "https://host\r\nX: y", "ftp://host"}) {
    EXPECT_FALSE(parseBaseUrl(bad, e)) << bad;
  }
  EXPECT_FALSE(parseBaseUrl("https://" + std::string(config::kMaxBaseUrlLength, 'a'), e));
}

TEST(HttpRequest, SerialisesGetWithoutBodyHeaders) {
  Endpoint e;
  parseBaseUrl("https://api.lexirise.app", e);
  const std::string wire = buildRequest(e, {Method::Get, "/v1/me", ""}, "lx_KEY", "Lexipoint/1");
  EXPECT_EQ(wire,
            "GET /v1/me HTTP/1.1\r\nHost: api.lexirise.app\r\nAuthorization: Bearer lx_KEY\r\n"
            "Accept: application/json\r\nUser-Agent: Lexipoint/1\r\n\r\n");
}

TEST(HttpRequest, SerialisesPostWithPrefixPortAndLength) {
  Endpoint e;
  parseBaseUrl("https://h.example:8443/p", e);
  const std::string body = R"({"text":"東京"})";  // 12 bytes: length is bytes, not characters
  const std::string wire = buildRequest(e, {Method::Post, "/v1/analyze/text", body}, "lx_KEY", "UA");
  EXPECT_EQ(wire.rfind("POST /p/v1/analyze/text HTTP/1.1\r\nHost: h.example:8443\r\n", 0), 0u);
  EXPECT_NE(
      wire.find("Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body),
      std::string::npos);
  // An empty POST still says so.
  EXPECT_NE(buildRequest(e, {Method::Post, "/x", ""}, "k", "UA").find("Content-Length: 0\r\n"), std::string::npos);
}

TEST(HttpResponse, ContentLengthAtEverySplitPoint) {
  const std::string wire =
      "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\n\r\n{\"ok\":true}";
  for (size_t step : {size_t{1}, size_t{2}, size_t{7}, wire.size()}) {
    const ResponseParser p = parse(wire, step);
    ASSERT_TRUE(p.done()) << step;
    EXPECT_EQ(p.status(), 200);
    EXPECT_EQ(p.body(), "{\"ok\":true}");
    EXPECT_TRUE(p.keepAlive());
  }
}

TEST(HttpResponse, StopsAtTheEndOfOneResponse) {
  const std::string first = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi";
  ResponseParser p;
  const std::string wire = first + "HTTP/1.1 200 OK\r\n";
  EXPECT_EQ(p.feed(wire.data(), wire.size()), first.size());
  EXPECT_TRUE(p.done());
}

TEST(HttpResponse, ChunkedWithExtensionsAndTrailers) {
  const std::string wire =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
      "5;ext=1\r\nhello\r\n1A\r\n abcdefghijklmnopqrstuvwxy\r\n0\r\nX-Trailer: t\r\n\r\n";
  for (size_t step : {size_t{1}, size_t{3}, wire.size()}) {
    const ResponseParser p = parse(wire, step);
    ASSERT_TRUE(p.done()) << step;
    EXPECT_EQ(p.body(), "hello abcdefghijklmnopqrstuvwxy");
  }
}

TEST(HttpResponse, CloseDelimitedBodyEndsOnClose) {
  ResponseParser p = parse("HTTP/1.0 200 OK\r\n\r\npartial");
  EXPECT_EQ(p.state(), ResponseParser::State::Body);
  p.onClose();
  EXPECT_TRUE(p.done());
  EXPECT_EQ(p.body(), "partial");
  EXPECT_FALSE(p.keepAlive());
}

TEST(HttpResponse, TruncatedBodyIsAnError) {
  ResponseParser p = parse("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc");
  p.onClose();
  EXPECT_EQ(p.state(), ResponseParser::State::Error);
  EXPECT_EQ(p.failure(), ResponseParser::Failure::Truncated);
  ResponseParser q = parse("HTTP/1.1 200 OK\r\nContent-Le");
  q.onClose();
  EXPECT_EQ(q.failure(), ResponseParser::Failure::Truncated);
}

TEST(HttpResponse, ConnectionCloseAndHttp10DisableKeepAlive) {
  EXPECT_FALSE(parse("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n").keepAlive());
  EXPECT_FALSE(parse("HTTP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n").keepAlive());
  EXPECT_TRUE(parse("HTTP/1.1 204 No Content\r\n\r\n").done());
}

TEST(HttpResponse, LimitsAreEnforced) {
  EXPECT_EQ(parse("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n", 64, 10).failure(),
            ResponseParser::Failure::BodyTooLarge);
  EXPECT_EQ(parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n8\r\n12345678\r\n8\r\n", 64, 10).failure(),
            ResponseParser::Failure::BodyTooLarge);
  EXPECT_EQ(parse("HTTP/1.1 200 OK\r\n\r\n" + std::string(20, 'x'), 64, 10).failure(),
            ResponseParser::Failure::BodyTooLarge);
  EXPECT_EQ(parse("HTTP/1.1 200 OK\r\nX: " + std::string(config::kHttpMaxLineBytes, 'a') + "\r\n\r\n").failure(),
            ResponseParser::Failure::HeadersTooLarge);
  std::string many = "HTTP/1.1 200 OK\r\n";
  while (many.size() <= config::kHttpMaxHeaderBytes) many += "X-Pad: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n";
  EXPECT_EQ(parse(many + "\r\n", 512).failure(), ResponseParser::Failure::HeadersTooLarge);
}

TEST(HttpResponse, MalformedInputsAreRejected) {
  for (const char* bad :
       {"HTTP/2 200 OK\r\n\r\n", "HTTP/1.1 20 OK\r\n\r\n", "HTTP/1.1 abc OK\r\n\r\n", "garbage\r\n\r\n",
        "HTTP/1.1 200 OK\r\nNoColon\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 3\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 2\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nabX\r\n"}) {
    EXPECT_EQ(parse(bad).state(), ResponseParser::State::Error) << bad;
  }
}

TEST(HttpResponse, InterimResponseIsSkippedWithItsHeaders) {
  const ResponseParser p =
      parse("HTTP/1.1 100 Continue\r\nContent-Length: 99\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
  ASSERT_TRUE(p.done());
  EXPECT_EQ(p.status(), 200);
  EXPECT_EQ(p.body(), "ok");
}

TEST(HttpResponse, RetryAfterIsParsedAndClamped) {
  EXPECT_EQ(parse("HTTP/1.1 429 Too Many\r\nRetry-After: 30\r\nContent-Length: 0\r\n\r\n").retryAfterSeconds(), 30u);
  EXPECT_EQ(parse("HTTP/1.1 429 X\r\nRetry-After: 0\r\nContent-Length: 0\r\n\r\n").retryAfterSeconds(), 1u);
  EXPECT_EQ(parse("HTTP/1.1 429 X\r\nRetry-After: 999999\r\nContent-Length: 0\r\n\r\n").retryAfterSeconds(),
            config::kRetryAfterMaxS);
  EXPECT_EQ(parse("HTTP/1.1 429 X\r\nRetry-After: Wed, 21 Oct 2026 07:28:00 GMT\r\nContent-Length: 0\r\n\r\n")
                .retryAfterSeconds(),
            0u);
}

TEST(HttpResponse, HeadResponseHasNoBody) {
  ResponseParser p(config::kHttpMaxBodyBytes, /*noBody=*/true);
  const std::string wire = "HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\n";
  p.feed(wire.data(), wire.size());
  EXPECT_TRUE(p.done());
}
