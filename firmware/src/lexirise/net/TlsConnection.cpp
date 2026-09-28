#if LEXIRISE && defined(FREEINK_NET_WOLFSSL)

#include "TlsConnection.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_sntp.h>
#include <time.h>
#include <wolfssl/error-ssl.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#ifdef HAVE_SESSION_TICKET
#include <wolfssl/internal.h>  // WOLFSSL_SESSION::ticketLen (V7c)
#endif

#include <algorithm>
#include <cstring>

#include "TlsSession.h"
#include "TrustAnchors.h"
#include "Wait.h"
#include "lexirise/LexiriseConfig.h"
#include "network/HttpDownloader.h"  // MIN_TLS_FREE_HEAP / MIN_TLS_MAX_ALLOC: the shared TLS pre-flight

namespace lexipoint::net {
namespace {

constexpr const char* kLogTag = "LXT";

// wolfSSL I/O over the WiFiClient, as in the SDK's SecureClient: a dead transport must surface as
// CONN_CLOSE (not WANT_*), or a handshake spins until its deadline instead of failing fast.
int ioSend(WOLFSSL*, char* buf, const int sz, void* ctx) {
  auto* transport = static_cast<WiFiClient*>(ctx);
  const int n = transport->write(reinterpret_cast<const uint8_t*>(buf), sz);
  if (n <= 0) return transport->connected() ? WOLFSSL_CBIO_ERR_WANT_WRITE : WOLFSSL_CBIO_ERR_CONN_CLOSE;
  return n;
}

int ioRecv(WOLFSSL*, char* buf, const int sz, void* ctx) {
  auto* transport = static_cast<WiFiClient*>(ctx);
  if (transport->available() == 0) {
    return transport->connected() ? WOLFSSL_CBIO_ERR_WANT_READ : WOLFSSL_CBIO_ERR_CONN_CLOSE;
  }
  const int n = transport->read(reinterpret_cast<uint8_t*>(buf), sz);
  return n > 0 ? n : WOLFSSL_CBIO_ERR_WANT_READ;
}

// Only wolfSSL_get_error() codes: the CBIO_* callback codes collide with fatal wolfCrypt errors.
bool isWantIo(const int err) { return err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE; }

bool pastDeadline(const uint32_t deadline) { return static_cast<int32_t>(millis() - deadline) >= 0; }

bool loadRoot(WOLFSSL_CTX* ctx, const char* pem, const char* name) {
  const int rc = wolfSSL_CTX_load_verify_buffer(ctx, reinterpret_cast<const unsigned char*>(pem),
                                                static_cast<long>(strlen(pem)), WOLFSSL_FILETYPE_PEM);
  if (rc != WOLFSSL_SUCCESS) LOG_ERR(kLogTag, "Loading %s failed: %d", name, rc);
  return rc == WOLFSSL_SUCCESS;
}

void releaseSession(void* session) { wolfSSL_SESSION_free(static_cast<WOLFSSL_SESSION*>(session)); }

// The handshake's error rejected the server's certificate (chain, signature, dates, host name): retrying won't help.
bool certificateRejected(const int err) {
  switch (err) {
    case ASN_NO_SIGNER_E:
    case ASN_BEFORE_DATE_E:
    case ASN_AFTER_DATE_E:
    case ASN_SIG_CONFIRM_E:
    case ASN_SIG_HASH_E:
    case ASN_SIG_KEY_E:
    case DOMAIN_NAME_MISMATCH:
    case VERIFY_CERT_ERROR:
      return true;
    default:
      return false;
  }
}

}  // namespace

bool ensureClock(Connection* aborts) {
  if (time(nullptr) >= config::kMinValidEpochS) return true;
  if (!esp_sntp_enabled()) configTzTime("UTC0", config::kNtpServerPrimary, config::kNtpServerSecondary);
  const uint32_t deadline = millis() + config::kNtpWaitMs;
  while (!pastDeadline(deadline)) {
    if (time(nullptr) >= config::kMinValidEpochS) return true;
    if (aborts && aborts->checkAbort()) return false;  // the reader's input: the call gives up (open: Aborted)
    pollWait(config::kClockPollMs);
  }
  LOG_ERR(kLogTag, "Clock not set (no NTP answer in %u ms)", (unsigned)config::kNtpWaitMs);
  return false;
}

OpenError TlsConnection::open(const Endpoint& endpoint) {
  close();
  const uint32_t started = millis();

  if (!ensureClock(this)) return checkAbort() ? OpenError::Aborted : OpenError::ClockNotSet;
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR(kLogTag, "Pre-flight: free %u, max block %u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    return OpenError::LowMemory;
  }

  // One open's budget, as config::kMaxCallMs counts it: a TCP connect and a handshake. The fallback to a full handshake
  // (V7c) fits inside it or isn't tried.
  const uint32_t deadline = millis() + config::kTlsOpenBudgetMs;
  const OpenError error = openResuming(
      sessions_, endpoint.host, endpoint.port, [&](void* session) { return handshake(endpoint, session, deadline); },
      [&] { return static_cast<int32_t>(deadline - millis()) >= static_cast<int32_t>(config::kTlsFallbackMinMs); });
  if (error != OpenError::None) return error;
  auto* ssl = static_cast<WOLFSSL*>(ssl_);
  LOG_INF(kLogTag, "Verified %s (%s, %s, %s) in %lu ms, free heap %u", endpoint.host.c_str(), wolfSSL_get_version(ssl),
          wolfSSL_get_cipher(ssl), wolfSSL_session_reused(ssl) ? "resumed" : "full",
          (unsigned long)(millis() - started), (unsigned)ESP.getFreeHeap());
  return OpenError::None;
}

Attempted TlsConnection::handshake(const Endpoint& endpoint, void* session, const uint32_t openDeadline) {
  teardown();
  Attempted out;
  if (checkAbort()) return {OpenError::Aborted, false, false};
  // Each step gets its own kHttpTimeoutMs, but never past the open's deadline (the fallback's share is what's left).
  const auto within = [openDeadline](const uint32_t ms) {
    const int32_t left = static_cast<int32_t>(openDeadline - millis());
    return left <= 0 ? 1u : std::min(ms, static_cast<uint32_t>(left));
  };
  if (!transport_.connect(endpoint.host.c_str(), endpoint.port, static_cast<int32_t>(within(config::kHttpTimeoutMs)))) {
    LOG_ERR(kLogTag, "TCP connect to %s:%u failed", endpoint.host.c_str(), (unsigned)endpoint.port);
    out.error = OpenError::ConnectFailed;
    return out;
  }

  auto* ctx = wolfSSL_CTX_new(wolfSSLv23_client_method());
  if (!ctx) {
    teardown();
    out.error = OpenError::LowMemory;
    return out;
  }
  ctx_ = ctx;
  wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER, nullptr);
  if (!loadRoot(ctx, kIsrgRootX1, "ISRG Root X1") || !loadRoot(ctx, kIsrgRootX2, "ISRG Root X2")) {
    teardown();
    out.error = OpenError::TlsFailed;
    return out;
  }
  wolfSSL_SetIORecv(ctx, ioRecv);
  wolfSSL_SetIOSend(ctx, ioSend);

  auto* ssl = wolfSSL_new(ctx);
  if (!ssl) {
    teardown();
    out.error = OpenError::LowMemory;
    return out;
  }
  ssl_ = ssl;
  wolfSSL_SetIOReadCtx(ssl, &transport_);
  wolfSSL_SetIOWriteCtx(ssl, &transport_);
  const auto hostLen = static_cast<unsigned short>(endpoint.host.size());
  if (wolfSSL_UseSNI(ssl, WOLFSSL_SNI_HOST_NAME, endpoint.host.c_str(), hostLen) != WOLFSSL_SUCCESS ||
      wolfSSL_check_domain_name(ssl, endpoint.host.c_str()) != WOLFSSL_SUCCESS) {
    teardown();
    out.error = OpenError::TlsFailed;
    return out;
  }
#if defined(WOLFSSL_TLS13) && defined(HAVE_CURVE25519)
  wolfSSL_UseKeyShare(ssl, WOLFSSL_ECC_X25519);  // see SecureClient.cpp: P-256 keygen OOMs at reading-session heap
#endif
#ifdef HAVE_MAX_FRAGMENT
  wolfSSL_UseMaxFragment(ssl, WOLFSSL_MFL_2_11);  // 2KB records: no ~17KB receive buffer
#endif
#ifdef HAVE_SESSION_TICKET
  // V7c: the last session with this host (SessionKeeper). wolfSSL refuses one past its ticket's lifetime: a full
  // handshake then, as it is when the server doesn't take it.
  if (session) {
    out.offered = wolfSSL_set_session(ssl, static_cast<WOLFSSL_SESSION*>(session)) == WOLFSSL_SUCCESS;
    out.refused = !out.offered;
    if (out.refused) {
      LOG_INF(kLogTag, "Session with %s not offered (refused by wolfSSL: expired or unusable)", endpoint.host.c_str());
    }
  }
#else
  (void)session;
#endif

  const uint32_t deadline = millis() + within(config::kHttpTimeoutMs);  // the handshake's own budget, inside the open's
  int ret;
  while ((ret = wolfSSL_connect(ssl)) != WOLFSSL_SUCCESS) {
    const int err = wolfSSL_get_error(ssl, ret);
    if (!isWantIo(err)) {
      // e.g. -188 ASN_NO_SIGNER_E (untrusted chain), -322 DOMAIN_NAME_MISMATCH, -150/-151 dates.
      LOG_ERR(kLogTag, "Handshake with %s failed%s: %d, free heap %u", endpoint.host.c_str(),
              out.offered ? " (resuming)" : "", err, (unsigned)ESP.getFreeHeap());
      out.certificateRejected = certificateRejected(err);
      teardown();
      out.error = OpenError::TlsFailed;
      return out;
    }
    if (pastDeadline(deadline)) {
      LOG_ERR(kLogTag, "Handshake with %s timed out", endpoint.host.c_str());
      teardown();
      out.error = OpenError::Timeout;
      return out;
    }
    if (checkAbort()) {
      teardown();
      out.error = OpenError::Aborted;
      return out;
    }
    pollWait(config::kIoPollMs);
  }
  connected_ = true;
  host_ = endpoint.host;
  port_ = endpoint.port;
#ifdef HAVE_SESSION_TICKET
  // When the session's ticket was last seen, as the handshake ends: a NewSessionTicket read later changes it (close()).
  ticketSeenAtOpen_ = ssl->session ? static_cast<int64_t>(ssl->session->ticketSeen) : 0;
#endif
  return out;
}

bool TlsConnection::isOpen() { return connected_ && transport_.connected(); }

bool TlsConnection::writeAll(const char* data, const size_t len) {
  if (!connected_) return false;
  auto* ssl = static_cast<WOLFSSL*>(ssl_);
  const uint32_t deadline = millis() + config::kHttpTimeoutMs;
  size_t sent = 0;
  while (sent < len) {
    const int n = wolfSSL_write(ssl, data + sent, static_cast<int>(len - sent));
    if (n > 0) {
      sent += static_cast<size_t>(n);
      continue;
    }
    if (!isWantIo(wolfSSL_get_error(ssl, n)) || pastDeadline(deadline) || checkAbort()) {
      close();
      return false;
    }
    pollWait(config::kIoPollMs);
  }
  return true;
}

int TlsConnection::read(char* buffer, const size_t capacity, const uint32_t timeoutMs) {
  if (!connected_) return -1;
  auto* ssl = static_cast<WOLFSSL*>(ssl_);
  const uint32_t deadline = millis() + timeoutMs;
  while (true) {
    const int n = wolfSSL_read(ssl, buffer, static_cast<int>(capacity));
    if (n > 0) return n;
    const int err = wolfSSL_get_error(ssl, n);
    if (!isWantIo(err)) {
      if (err != WOLFSSL_ERROR_ZERO_RETURN) LOG_ERR(kLogTag, "Read failed: %d", err);
      close();
      return -1;
    }
    if (pastDeadline(deadline)) return 0;
    if (checkAbort()) {
      close();
      return -1;
    }
    pollWait(config::kIoPollMs);
  }
}

TlsConnection::TlsConnection() : sessions_(releaseSession) {}

void TlsConnection::close() {
#ifdef HAVE_SESSION_TICKET
  if (ssl_) {
    if (connected_) {  // V7c: a completed handshake's session, with a ticket read on it, for the next one
      auto* ssl = static_cast<WOLFSSL*>(ssl_);
      // The session's own ticket length (wolfSSL's session struct: no public getter in this build, and
      // wolfSSL_get_SessionTicket reports 0 for a ticket larger than the buffer it's given).
      const size_t ticketBytes = ssl->session ? ssl->session->ticketLen : 0;
      const bool ticketArrived = ssl->session && static_cast<int64_t>(ssl->session->ticketSeen) != ticketSeenAtOpen_;
      sessions_.keep(host_, port_, keepOnClose(ticketBytes, ticketArrived) ? wolfSSL_get1_session(ssl) : nullptr);
    }
  }
#endif
  teardown();
}

void TlsConnection::teardown() {
  if (ssl_) {
    wolfSSL_free(static_cast<WOLFSSL*>(ssl_));
    ssl_ = nullptr;
  }
  if (ctx_) {
    wolfSSL_CTX_free(static_cast<WOLFSSL_CTX*>(ctx_));
    ctx_ = nullptr;
  }
  transport_.stop();
  connected_ = false;
}

}  // namespace lexipoint::net

#endif  // LEXIRISE && FREEINK_NET_WOLFSSL
