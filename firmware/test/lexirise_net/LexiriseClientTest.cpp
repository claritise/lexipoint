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

TEST(LexiriseClient, NonIdempotentPostIsNeverSentTwice) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  conn.reads = {ok("{}")};
  ASSERT_TRUE(client.send(kMe).ok());
  conn.reads = {FakeConnection::kClose, ok("{}")};
  const Request save{Method::Post, "/v1/vocabulary", "{}"};  // not idempotent
  EXPECT_EQ(client.send(save).error, ApiError::Network);
  EXPECT_EQ(conn.opens, 1);  // no second attempt
  ASSERT_EQ(conn.written.size(), 2u);
  // An idempotent POST is retried like a GET.
  Request analyze{Method::Post, "/v1/analyze/text", "{}"};
  analyze.idempotent = true;
  conn.reads = {ok("{}"), FakeConnection::kClose, ok("{}")};
  ASSERT_TRUE(client.send(analyze).ok());  // opens a session
  EXPECT_TRUE(client.send(analyze).ok());  // stale, retried
}

TEST(LexiriseClient, AnOverLimitBodyKeepsItsStatusForTheLog) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  conn.reads = {"HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(lexipoint::config::kHttpMaxBodyBytes + 1) +
                "\r\n\r\n{"};
  const auto r = client.send(kMe);
  EXPECT_EQ(r.error, ApiError::Malformed);
  EXPECT_EQ(r.status, 200);
}

TEST(LexiriseClient, SaysWhetherTheRequestLeft) {
  // A creation (POST /v1/decks) whose answer is lost may have happened only once the request was written.
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  const Request create{Method::Post, "/v1/decks", "{}"};
  for (const OpenError open : {OpenError::ConnectFailed, OpenError::Timeout, OpenError::TlsFailed}) {
    conn.openResults = {open};
    EXPECT_FALSE(client.send(create).sent) << static_cast<int>(open);
  }
  conn.failNextWrite = true;  // a fresh session that can't be written to
  const auto unwritten = client.send(create);
  EXPECT_EQ(unwritten.error, ApiError::Network);
  EXPECT_FALSE(unwritten.sent);

  conn.reads = {"HTTP/1.1 200 OK\r\n", FakeConnection::kStall};  // written, then the answer timed out
  const auto timedOut = client.send(create);
  EXPECT_EQ(timedOut.error, ApiError::Timeout);
  EXPECT_TRUE(timedOut.sent);
  conn.reads = {"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc", FakeConnection::kClose};  // cut short
  EXPECT_TRUE(client.send(create).sent);
  conn.reads = {ok("{}")};
  EXPECT_TRUE(client.send(create).sent);
  // A stale keep-alive session: written, then dropped before any answer. Not resent, and it may have arrived.
  conn.reads = {ok("{}")};
  ASSERT_TRUE(client.send(kMe).ok());
  conn.reads = {FakeConnection::kClose};
  const auto stale = client.send(create);
  EXPECT_EQ(stale.error, ApiError::Network);
  EXPECT_TRUE(stale.sent);
  // A GET on a stale session is retried: written, dropped, then the retry can't connect. It was sent once.
  conn.reads = {ok("{}")};
  ASSERT_TRUE(client.send(kMe).ok());
  conn.reads = {FakeConnection::kClose};
  conn.openResults = {OpenError::ConnectFailed};
  const auto retried = client.send(kMe);
  EXPECT_EQ(retried.error, ApiError::Network);
  EXPECT_TRUE(retried.sent);
}

namespace {

// Collects a streamed body (V7a), and can stop it after `stopAfter` bytes.
struct Collect : lexipoint::net::BodySink {
  std::string got;
  size_t pieces = 0;
  size_t stopAfter = SIZE_MAX;
  bool onBody(const char* data, size_t len) override {
    got.append(data, len);
    pieces++;
    return got.size() <= stopAfter;
  }
  size_t maxBytes() const override { return lexipoint::config::kVocabPageMaxBytes; }
};

}  // namespace

TEST(LexiriseClient, AStreamedBodyGoesToTheSinkAndIsNeverHeld) {
  FakeConnection conn;
  conn.maxBytesPerRead = 100;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  // Past the buffered limit (kHttpMaxBodyBytes), chunked, as a vocabulary page may come.
  const std::string body(lexipoint::config::kHttpMaxBodyBytes * 3, 'x');
  std::string chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
  for (size_t at = 0; at < body.size(); at += 5000) {
    const std::string piece = body.substr(at, 5000);
    char size[16];
    std::snprintf(size, sizeof(size), "%zx\r\n", piece.size());
    chunked += size + piece + "\r\n";
  }
  chunked += "0\r\n\r\n";
  conn.reads = {chunked, ok(std::string(10, 'y'))};
  Collect sink;
  const auto r = client.send(kMe, &sink);
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.body.empty());
  EXPECT_EQ(sink.got, body);
  EXPECT_GT(sink.pieces, 1u);
  // The session is kept, and a call without a sink buffers as before.
  const auto next = client.send(kMe);
  EXPECT_EQ(next.body, std::string(10, 'y'));
  EXPECT_EQ(conn.opens, 1);
}

TEST(LexiriseClient, AnErrorsBodyIsntStreamedAndASinkThatStopsMakesItMalformed) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  conn.reads = {"HTTP/1.1 429 Too Many\r\nRetry-After: 20\r\nContent-Length: 2\r\n\r\n{}"};
  Collect refused;
  const auto limited = client.send(kMe, &refused);
  EXPECT_EQ(limited.error, ApiError::RateLimited);
  EXPECT_EQ(limited.retryAfterS, 20u);
  EXPECT_TRUE(refused.got.empty());

  conn.reads = {ok(std::string(3000, 'z'))};
  Collect stops;
  stops.stopAfter = 1000;
  conn.maxBytesPerRead = 500;
  EXPECT_EQ(client.send(kMe, &stops).error, ApiError::Malformed);
  EXPECT_FALSE(conn.isOpen());  // the rest of the body is never read: the session goes
}

TEST(LexiriseClient, AStreamedBodyOverItsCapIsMalformed) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  client.configure(kBase, kKey);
  conn.reads = {"HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(lexipoint::config::kVocabPageMaxBytes + 1) +
                "\r\n\r\n"};
  Collect sink;
  EXPECT_EQ(client.send(kMe, &sink).error, ApiError::Malformed);
  EXPECT_TRUE(sink.got.empty());
}

// --- A call's abort (v0.2 V7b: the reader's input gives a page's call up) ---

namespace {
bool abortNow = false;
bool aborted() { return abortNow; }
}  // namespace

TEST(LexiriseClient, AnOpenGivenUpForInputIsCancelledAndSendsNothing) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  ASSERT_TRUE(client.configure(kBase, kKey));
  conn.openResults = {OpenError::Aborted};
  const auto r = client.send(kMe, nullptr, aborted);
  EXPECT_EQ(r.error, ApiError::Cancelled);
  EXPECT_FALSE(r.sent);
  EXPECT_TRUE(conn.written.empty());
}

TEST(LexiriseClient, InputAlreadyThereSendsNothing) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  ASSERT_TRUE(client.configure(kBase, kKey));
  abortNow = true;
  const auto r = client.send(kMe, nullptr, aborted);
  abortNow = false;
  EXPECT_EQ(r.error, ApiError::Cancelled);
  EXPECT_FALSE(r.sent);
  EXPECT_TRUE(conn.written.empty());
  EXPECT_FALSE(conn.isOpen());
}

TEST(LexiriseClient, AStaleSessionGivenUpIsntResent) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  ASSERT_TRUE(client.configure(kBase, kKey));
  conn.reads = {ok("{}")};
  ASSERT_TRUE(client.send(kMe).ok());               // the session stays open
  conn.reads = {FakeConnection::kClose, ok("{}")};  // stale: it closes before any byte
  static int asked = 0;
  asked = 0;
  // Input comes while the stale read fails: the retry on a fresh session must not go out.
  const auto r = client.send(kMe, nullptr, [] { return ++asked > 1; });
  EXPECT_EQ(r.error, ApiError::Cancelled);
  EXPECT_EQ(conn.written.size(), 2u);  // the first call's and the stale attempt's: no third
  EXPECT_FALSE(conn.isOpen());
}

TEST(LexiriseClient, TheAbortIsForgottenAfterTheCall) {
  FakeConnection conn;
  LexiriseClient client(conn, "UA", FakeClock::now);
  ASSERT_TRUE(client.configure(kBase, kKey));
  abortNow = true;
  client.send(kMe, nullptr, aborted);
  conn.reads = {ok("{}")};
  EXPECT_TRUE(client.send(kMe).ok());  // no abort given: the last one doesn't linger
  abortNow = false;
}
