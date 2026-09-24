#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <vector>

#include "Fakes.h"
#include "lexirise/api/LexiriseClient.h"

using lexipoint::api::ApiError;
using lexipoint::api::LexiriseClient;
using lexipoint::net::Endpoint;
using lexipoint::net::Method;
using lexipoint::net::OpenError;
using lexipoint::net::Request;

namespace {

constexpr const char* kKey = "lx_TESTKEYtestkey0123456789";
constexpr const char* kBase = "https://api.lexirise.app";

using lexipoint::fakes::FakeClock;
using lexipoint::fakes::FakeConnection;
std::string ok(const std::string& body, const std::string& extra = "") { return lexipoint::fakes::httpOk(body, extra); }

const Request kMe{Method::Get, "/v1/me", ""};

}  // namespace

TEST(LexiriseClient, RefusesToSendUnconfigured) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  EXPECT_EQ(client.send(kMe).error, ApiError::NotConfigured);
  EXPECT_FALSE(client.configure("http://api.lexirise.app", kKey));
  EXPECT_FALSE(client.configure(kBase, ""));
  EXPECT_EQ(client.send(kMe).error, ApiError::NotConfigured);
  EXPECT_EQ(conn.opens, 0);
}

TEST(LexiriseClient, SendsAndReusesKeepAliveSession) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  ASSERT_TRUE(client.configure(kBase, kKey));
  conn.reads = {ok("{\"a\":1}"), ok("{\"b\":2}")};
  auto first = client.send(kMe);
  ASSERT_TRUE(first.ok());
  EXPECT_EQ(first.body, "{\"a\":1}");
  auto second = client.send({Method::Post, "/v1/analyze/text", "{}"});
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(second.body, "{\"b\":2}");
  EXPECT_EQ(conn.opens, 1);
  ASSERT_EQ(conn.written.size(), 2u);
  EXPECT_NE(conn.written[0].find("Authorization: Bearer lx_TESTKEY"), std::string::npos);
}

TEST(LexiriseClient, ConnectionCloseEndsTheSession) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  conn.reads = {ok("{}", "Connection: close\r\n"), ok("{}")};
  EXPECT_TRUE(client.send(kMe).ok());
  EXPECT_FALSE(conn.isOpen());
  EXPECT_TRUE(client.send(kMe).ok());
  EXPECT_EQ(conn.opens, 2);
}

TEST(LexiriseClient, StaleReusedSessionIsRetriedOnce) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  conn.reads = {ok("{}")};
  ASSERT_TRUE(client.send(kMe).ok());
  // The server dropped the idle session: the next request sees an immediate close, then succeeds.
  conn.reads = {FakeConnection::kClose, ok("{\"fresh\":1}")};
  const auto r = client.send(kMe);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.body, "{\"fresh\":1}");
  EXPECT_EQ(conn.opens, 2);
  // A write failure on a reused session is retried the same way.
  conn.failNextWrite = true;
  conn.open_ = true;
  conn.reads = {ok("{}")};
  EXPECT_TRUE(client.send(kMe).ok());
  EXPECT_EQ(conn.opens, 3);
}

TEST(LexiriseClient, FreshSessionFailuresAreNotRetried) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  conn.reads = {FakeConnection::kClose, ok("{}")};
  EXPECT_EQ(client.send(kMe).error, ApiError::Network);
  EXPECT_EQ(conn.opens, 1);
}

TEST(LexiriseClient, MapsOpenErrors) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  const std::pair<OpenError, ApiError> cases[] = {{OpenError::LowMemory, ApiError::LowMemory},
                                                  {OpenError::ClockNotSet, ApiError::ClockNotSet},
                                                  {OpenError::ConnectFailed, ApiError::Network},
                                                  {OpenError::TlsFailed, ApiError::Tls},
                                                  {OpenError::Timeout, ApiError::Timeout}};
  for (const auto& [open, api] : cases) {
    conn.openResults = {open};
    EXPECT_EQ(client.send(kMe).error, api);
  }
}

TEST(LexiriseClient, MapsStatuses) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  const std::pair<const char*, ApiError> cases[] = {
      {"HTTP/1.1 401 Unauthorized\r\nContent-Length: 2\r\n\r\n{}", ApiError::Unauthorized},
      {"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n", ApiError::Unauthorized},
      {"HTTP/1.1 500 Oops\r\nContent-Length: 0\r\n\r\n", ApiError::Server},
      {"HTTP/1.1 404 Nope\r\nContent-Length: 0\r\n\r\n", ApiError::Http},
      {"HTTP/1.1 201 Created\r\nContent-Length: 2\r\n\r\n{}", ApiError::None}};
  for (const auto& [wire, error] : cases) {
    conn.reads = {wire};
    const auto r = client.send(kMe);
    EXPECT_EQ(r.error, error) << wire;
    if (error != ApiError::None) EXPECT_TRUE(r.body.empty());  // error bodies are never kept
  }
}

TEST(LexiriseClient, RateLimitCarriesRetryAfter) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  conn.reads = {"HTTP/1.1 429 Slow\r\nRetry-After: 42\r\nContent-Length: 0\r\n\r\n"};
  auto r = client.send(kMe);
  EXPECT_EQ(r.error, ApiError::RateLimited);
  EXPECT_EQ(r.retryAfterS, 42u);
  conn.reads = {"HTTP/1.1 429 Slow\r\nContent-Length: 0\r\n\r\n"};
  EXPECT_EQ(client.send(kMe).retryAfterS, lexipoint::config::kRetryAfterDefaultS);
}

TEST(LexiriseClient, TimeoutTruncationAndGarbage) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  conn.reads = {"HTTP/1.1 200 OK\r\n", FakeConnection::kStall};
  EXPECT_EQ(client.send(kMe).error, ApiError::Timeout);
  EXPECT_FALSE(conn.isOpen());
  conn.reads = {"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc", FakeConnection::kClose};
  EXPECT_EQ(client.send(kMe).error, ApiError::Network);
  conn.reads = {"SSH-2.0-OpenSSH\r\n\r\n"};
  EXPECT_EQ(client.send(kMe).error, ApiError::Malformed);
  conn.reads = {ok("{}") + "HTTP/1.1 200 OK\r\n"};  // bytes nobody asked for
  EXPECT_EQ(client.send(kMe).error, ApiError::Malformed);
  EXPECT_FALSE(conn.isOpen());
}

TEST(LexiriseClient, ChangingEndpointClosesTheSession) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  conn.reads = {ok("{}")};
  client.send(kMe);
  ASSERT_TRUE(conn.isOpen());
  client.configure(kBase, "lx_OTHERkeyOTHERkey000000");  // same host: session kept, new key used
  EXPECT_TRUE(conn.isOpen());
  client.configure("https://staging.example.com", kKey);
  EXPECT_FALSE(conn.isOpen());
}

TEST(LexiriseClient, TricklingServerHitsTheRequestDeadline) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  // One byte per read, 1 s per read: a 40-byte response would take 40 s.
  conn.reads = {ok(std::string(20, 'x'))};
  conn.maxBytesPerRead = 1;
  conn.msPerRead = 1000;
  const unsigned long started = FakeClock::nowMs;
  EXPECT_EQ(client.send(kMe).error, ApiError::Timeout);
  EXPECT_LE(FakeClock::nowMs - started, lexipoint::config::kRequestDeadlineMs + 1000);
  EXPECT_FALSE(conn.isOpen());
}

TEST(LexiriseClient, StaleRetryKeepsTheSameDeadline) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  conn.reads = {ok("{}")};
  ASSERT_TRUE(client.send(kMe).ok());
  // The stale attempt burns most of the budget before the close; the retry only gets what's left.
  conn.msPerRead = lexipoint::config::kRequestDeadlineMs - 500;
  conn.reads = {FakeConnection::kClose, "HTTP/1.1 200 OK\r\n", "Content-Length: 2\r\n\r\n{}"};
  EXPECT_EQ(client.send(kMe).error, ApiError::Timeout);
}
