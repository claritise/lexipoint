#pragma once

// Verified TLS for the Lexirise client (lexirise-client.md §1). The SDK's SecureClient never verifies
// (every CrossPoint caller uses setInsecure()), and an API key must not travel over an unverified
// channel, so this is a small wolfSSL client of our own: ISRG roots pinned (TrustAnchors.h), peer
// verification on, hostname checked, SNI sent. It reuses the SDK's heap measures (X25519 key share,
// 2KB max fragment). Device-only; the client logic above it is tested through a fake Connection.

#include <WiFiClient.h>

#include "Connection.h"

namespace lexipoint::net {

class TlsConnection final : public Connection {
 public:
  TlsConnection() = default;
  ~TlsConnection() override { close(); }
  TlsConnection(const TlsConnection&) = delete;
  TlsConnection& operator=(const TlsConnection&) = delete;

  OpenError open(const Endpoint& endpoint) override;
  bool isOpen() override;
  bool writeAll(const char* data, size_t len) override;
  int read(char* buffer, size_t capacity, uint32_t timeoutMs) override;
  void close() override;

 private:
  WiFiClient transport_;
  void* ctx_ = nullptr;  // WOLFSSL_CTX*
  void* ssl_ = nullptr;  // WOLFSSL*
  bool connected_ = false;
};

// Makes sure the system clock is plausible (NTP, bounded wait). Needs WiFi up.
bool ensureClock();

}  // namespace lexipoint::net
