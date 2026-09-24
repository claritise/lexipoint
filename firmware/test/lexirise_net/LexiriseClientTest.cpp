#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <vector>

#include "lexirise/api/LexiriseClient.h"

using lexipoint::api::ApiError;
using lexipoint::api::LexiriseClient;
using lexipoint::net::Connection;
using lexipoint::net::Endpoint;
using lexipoint::net::Method;
using lexipoint::net::OpenError;
using lexipoint::net::Request;

namespace {

constexpr const char* kKey = "lx_TESTKEYtestkey0123456789";
constexpr const char* kBase = "https://api.lexirise.app";

// Scripted connection: each read() pops the next chunk; kClose means the peer closed, kStall a
// timeout. open() results can be queued too.
class FakeConnection : public Connection {
 public:
  static constexpr const char* kClose = "\x01<close>";
  static constexpr const char* kStall = "\x01<stall>";

  std::deque<std::string> reads;
  std::deque<OpenError> openResults;
  bool failNextWrite = false;
  bool open_ = false;
  int opens = 0;
  int closes = 0;
  std::vector<std::string> written;

  OpenError open(const Endpoint&) override {
    opens++;
    OpenError result = OpenError::None;
    if (!openResults.empty()) {
      result = openResults.front();
      openResults.pop_front();
    }
    open_ = result == OpenError::None;
    return result;
  }
  bool isOpen() override { return open_; }
  bool writeAll(const char* data, size_t len) override {
    if (failNextWrite) {
      failNextWrite = false;
      open_ = false;
      return false;
    }
    written.emplace_back(data, len);
    return true;
  }
  int read(char* buffer, size_t capacity, uint32_t) override {
    if (reads.empty() || reads.front() == kClose) {
      if (!reads.empty()) reads.pop_front();
      open_ = false;
      return -1;
    }
    if (reads.front() == kStall) {
      reads.pop_front();
      return 0;
    }
    std::string& chunk = reads.front();
    const size_t n = std::min(capacity, chunk.size());
    chunk.copy(buffer, n);
    chunk.erase(0, n);
    if (chunk.empty()) reads.pop_front();
    return static_cast<int>(n);
  }
  void close() override {
    if (open_) closes++;
    open_ = false;
  }
};

std::string ok(const std::string& body, const std::string& extra = "") {
  return "HTTP/1.1 200 OK\r\n" + extra + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

const Request kMe{Method::Get, "/v1/me", ""};

}  // namespace

TEST(LexiriseClient, RefusesToSendUnconfigured) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA");
  EXPECT_EQ(client.send(kMe).error, ApiError::NotConfigured);
  EXPECT_FALSE(client.configure("http://api.lexirise.app", kKey));
  EXPECT_FALSE(client.configure(kBase, ""));
  EXPECT_EQ(client.send(kMe).error, ApiError::NotConfigured);
  EXPECT_EQ(conn.opens, 0);
}

TEST(LexiriseClient, SendsAndReusesKeepAliveSession) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA");
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
  LexiriseClient client(conn, "UA");
  client.configure(kBase, kKey);
  conn.reads = {ok("{}", "Connection: close\r\n"), ok("{}")};
  EXPECT_TRUE(client.send(kMe).ok());
  EXPECT_FALSE(conn.isOpen());
  EXPECT_TRUE(client.send(kMe).ok());
  EXPECT_EQ(conn.opens, 2);
}

TEST(LexiriseClient, StaleReusedSessionIsRetriedOnce) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA");
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
  LexiriseClient client(conn, "UA");
  client.configure(kBase, kKey);
  conn.reads = {FakeConnection::kClose, ok("{}")};
  EXPECT_EQ(client.send(kMe).error, ApiError::Network);
  EXPECT_EQ(conn.opens, 1);
}

TEST(LexiriseClient, MapsOpenErrors) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA");
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
  LexiriseClient client(conn, "UA");
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
  LexiriseClient client(conn, "UA");
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
  LexiriseClient client(conn, "UA");
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
  LexiriseClient client(conn, "UA");
  client.configure(kBase, kKey);
  conn.reads = {ok("{}")};
  client.send(kMe);
  ASSERT_TRUE(conn.isOpen());
  client.configure(kBase, "lx_OTHERkeyOTHERkey000000");  // same host: session kept, new key used
  EXPECT_TRUE(conn.isOpen());
  client.configure("https://staging.example.com", kKey);
  EXPECT_FALSE(conn.isOpen());
}
