#pragma once

// Verified TLS for the Lexirise client (lexirise-client.md §1). The SDK's SecureClient never verifies
// (every CrossPoint caller uses setInsecure()), and an API key must not travel over an unverified
// channel, so this is a small wolfSSL client of our own: ISRG roots pinned (TrustAnchors.h), peer
// verification on, hostname checked, SNI sent. It reuses the SDK's heap measures (X25519 key share,
// 2KB max fragment), and resumes the last session with the host (V7c: TlsSession.h). Device-only; the client logic
// above it is tested through a fake Connection, the resumption policy on its own.

#include <WiFiClient.h>

#include <string>

#include "Connection.h"
#include "TlsSession.h"

namespace lexipoint::net {

class TlsConnection final : public Connection {
 public:
  TlsConnection();
  ~TlsConnection() override { close(); }
  TlsConnection(const TlsConnection&) = delete;
  TlsConnection& operator=(const TlsConnection&) = delete;

  OpenError open(const Endpoint& endpoint) override;
  bool isOpen() override;
  bool writeAll(const char* data, size_t len) override;
  int read(char* buffer, size_t capacity, uint32_t timeoutMs) override;
  void close() override;
  void forgetSession() override { sessions_.drop(); }

 private:
  // The TCP connect and the handshake, offering `session` (nullptr: a full handshake).
  // `openDeadline`: the open's budget ends then (the TCP connect and the handshake each stop by it).
  Attempted handshake(const Endpoint& endpoint, void* session, uint32_t openDeadline);
  // Frees the connection without keeping its session (close() keeps it first when it may be resumed).
  void teardown();
  WiFiClient transport_;
  void* ctx_ = nullptr;  // WOLFSSL_CTX*
  void* ssl_ = nullptr;  // WOLFSSL*
  bool connected_ = false;
  std::string host_;  // the open connection's host and port
  uint16_t port_ = 0;
  int64_t ticketSeenAtOpen_ = 0;  // the session's ticket time as the handshake ended (V7c: a new one read since?)
  SessionKeeper sessions_;        // V7c: the last session, for resumption (RAM only)
};

// Makes sure the system clock is plausible (NTP, bounded wait). Needs WiFi up.
// `aborts` (optional): its call's abort is asked in the wait (up to config::kNtpWaitMs).
bool ensureClock(Connection* aborts = nullptr);

}  // namespace lexipoint::net
