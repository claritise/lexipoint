#pragma once

// TLS session resumption's policy (C21, v0.2 V7c; docs/v0.2/00-overview.md C21 "V7c design"): the newest session with
// the API's host is kept after a connection closes and offered to the next handshake with the same host, which then
// skips the certificate chain and its verification (the server hands out tickets and accepts them: measured,
// docs/reference/lexirise-api-notes.md "Caches (V7c), measured"). A server that doesn't take it answers with a full
// handshake in the same exchange. In RAM only (never on SD: it resumes an authenticated session); a restart loses it.
// The session is an opaque handle (a WOLFSSL_SESSION* on the device, net/TlsConnection.cpp), released through the
// function given. Pure; tests: test/lexirise_net/TlsSessionTest.cpp.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "Connection.h"

namespace lexipoint::net {

class SessionKeeper {
 public:
  using Release = void (*)(void* session);
  explicit SessionKeeper(const Release release) : release_(release) {}
  ~SessionKeeper() { drop(); }
  SessionKeeper(const SessionKeeper&) = delete;
  SessionKeeper& operator=(const SessionKeeper&) = delete;

  // The session to offer a handshake with `host`:`port`; nullptr: none (a full handshake). One kept for another host
  // or port is dropped first.
  void* offer(const std::string_view host, const uint16_t port) {
    if (session_ && (host != host_ || port != port_)) drop();
    return session_;
  }
  // As a connection with `host`:`port` that completed its handshake closes: its session (a reference the keeper now
  // owns; nullptr when it had no ticket) replaces the one kept, which is released (the same object twice holds two
  // references: one goes).
  void keep(const std::string_view host, const uint16_t port, void* session) {
    drop();
    session_ = session;
    if (session_) {
      host_.assign(host);
      port_ = port;
    }
  }
  void drop() {
    if (session_ && release_) release_(session_);
    session_ = nullptr;
    host_.clear();
    port_ = 0;
  }
  bool holds() const { return session_ != nullptr; }

 private:
  Release release_;
  void* session_ = nullptr;
  std::string host_;
  uint16_t port_ = 0;
};

// Whether a closing connection's session is kept: it holds a ticket (the API's are 32 bytes, measured; wolfSSL keeps a
// longer one on the heap, so any size), and that ticket came on this connection. In wolfSSL 5.7.2 a resumed
// connection works on its own copy of the session it was offered (SetupPskKey calls HaveUniqueSessionObj, which
// duplicates a session the keeper also holds), and its handshake writes a new resumption secret into that copy
// (tls13.c's client Finished), while the copy's ticket changes only when a NewSessionTicket is read (SetTicket): a
// copy closed before that (a call given up right after its handshake) holds the old ticket with the new secret, which
// can't resume, so it isn't kept (the next open is a full handshake).
inline bool keepOnClose(const size_t ticketBytes, const bool ticketArrivedHere) {
  return ticketBytes > 0 && ticketArrivedHere;
}

// One attempt's outcome: its error; whether a session was really offered; whether wolfSSL refused the one given (past
// its ticket's lifetime); whether the server's certificate was rejected (a resumed handshake verifies none, so the
// server did a full handshake: another full one would be rejected the same way). A failure before the session could be
// offered (the TCP connect, memory, input before it) is none of them.
struct Attempted {
  OpenError error = OpenError::None;
  bool offered = false;
  bool refused = false;
  bool certificateRejected = false;
};

// One open with resumption. `attempt(session)` runs the TCP connect and the handshake offering `session` (nullptr: a
// full handshake) and says how it went. The keeper owns `session` throughout: `attempt` tears down without keeping
// anything (TlsConnection::teardown), so the pointer it was given stays valid.
// - A session wolfSSL refused to offer is dropped.
// - A handshake that offered the session and didn't end in a connection drops it: failed, timed out, or given up for
//   the reader's input mid-way. Conservative: the handshake worked on its own copy (HaveUniqueSessionObj), so the kept
//   session wasn't modified; a session that failed once isn't offered again. (A session wolfSSL refused, above, was
//   attached to the connection itself, shared: it's dropped then too.)
// - A full handshake that fails or times out drops it too (the next one is full anyway).
// - One that failed (OpenError::TlsFailed) having offered a session, with no certificate rejected, is tried once more
//   without it, so a stale session never fails a call (a timeout isn't: it would double the wait).
// Anything before the session could be offered (the clock, memory, the TCP connect, input before the handshake) keeps
// it.
// `timeLeft()`: whether one open's budget still has room for the fallback (TlsConnection::open: at least
// config::kTlsFallbackMinMs before its config::kTlsOpenBudgetMs deadline, so one open never takes longer than a full
// one could: the call's bound, config::kMaxCallMs, counts one TCP connect and one handshake). Past it the fallback
// isn't tried and the call fails as it did; the session is dropped, so the next open is full.
template <typename Attempt, typename TimeLeft>
OpenError openResuming(SessionKeeper& keeper, const std::string_view host, const uint16_t port, Attempt&& attempt,
                       TimeLeft&& timeLeft) {
  void* given = keeper.offer(host, port);
  const Attempted first = attempt(given);
  const OpenError error = first.error;
  if (first.refused || (first.offered && error != OpenError::None) || error == OpenError::TlsFailed ||
      error == OpenError::Timeout) {
    keeper.drop();
  }
  if (first.offered && error == OpenError::TlsFailed && !first.certificateRejected && timeLeft()) {
    return attempt(nullptr).error;
  }
  return error;
}

}  // namespace lexipoint::net
