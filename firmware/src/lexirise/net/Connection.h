#pragma once

// The byte pipe the Lexirise client talks HTTP over. On the device it is TlsConnection (verified
// wolfSSL); host tests script a fake one. Keeping the client behind this interface is what makes
// its keep-alive, retry and error mapping testable.

#include <cstddef>
#include <cstdint>

#include "Http.h"

namespace lexipoint::net {

enum class OpenError {
  None,
  LowMemory,      // pre-flight: not enough internal heap for a TLS session
  ClockNotSet,    // certificate dates can't be checked yet (no NTP time)
  ConnectFailed,  // DNS or TCP
  TlsFailed,      // handshake failed, including certificate or hostname verification
  Timeout,
  Aborted,  // given up for the reader's input (Connection::setAbort)
};

// Asked in every wait of a call (v0.2 V7b: a page analysis gives way to the reader's input): true gives the call up.
using Abort = bool (*)();

class Connection {
 public:
  virtual ~Connection() = default;

  virtual OpenError open(const Endpoint& endpoint) = 0;
  // Whether a previously opened session is still usable for another request.
  virtual bool isOpen() = 0;
  // Writes everything or fails (and the connection is then closed).
  virtual bool writeAll(const char* data, size_t len) = 0;
  // Returns >0 bytes read, 0 when nothing arrived within timeoutMs, or -1 when the peer closed or
  // the session failed (the connection is then closed).
  virtual int read(char* buffer, size_t capacity, uint32_t timeoutMs) = 0;
  virtual void close() = 0;

  // The abort for the next call (nullptr: none); aborted() says whether it gave the call up.
  void setAbort(const Abort abort) {
    abort_ = abort;
    aborted_ = false;
  }
  bool aborted() const { return aborted_; }
  // True, and remembered, once the abort asks to give up: asked by the client between reads and by the connection
  // in its own waits (the TCP connect itself can't be: it blocks inside the SDK).
  bool checkAbort() {
    if (!aborted_ && abort_ != nullptr && abort_()) aborted_ = true;
    return aborted_;
  }

 private:
  Abort abort_ = nullptr;
  bool aborted_ = false;
};

}  // namespace lexipoint::net
