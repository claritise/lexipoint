#pragma once

// Test doubles shared by the lexirise_* host suites: a fake millis(), an in-memory SD card with
// SdFat's rename semantics, a scripted network connection, and a scripted WiFi.

#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "lexirise/net/Connection.h"
#include "lexirise/net/WifiSession.h"
#include "lexirise/settings/SettingsStore.h"

namespace lexipoint::fakes {

// A millis() tests move by hand. Reads can advance it (FakeConnection::msPerRead).
struct FakeClock {
  static unsigned long nowMs;
  static unsigned long now() { return nowMs; }
};
inline unsigned long FakeClock::nowMs = 0;

// In-memory card with SdFat's rename semantics and one-shot failure injection.
class FakeFiles : public SettingsFiles {
 public:
  std::map<std::string, std::string> files;
  std::string failWriteOf;   // path whose next write fails
  std::string failRenameTo;  // destination whose next rename fails
  bool failReads = false;    // every read reports an I/O error
  int writes = 0;

  ReadStatus read(const char* path, size_t maxBytes, std::string& out) override {
    const auto it = files.find(path);
    if (it == files.end()) return ReadStatus::Missing;
    if (failReads) return ReadStatus::Error;
    if (it->second.size() > maxBytes) return ReadStatus::TooLarge;
    out = it->second;
    return ReadStatus::Ok;
  }
  bool write(const char* path, std::string_view content) override {
    writes++;
    if (failWriteOf == path) {
      failWriteOf.clear();
      files[path] = "[acc";  // torn
      return false;
    }
    files[path] = std::string(content);
    return true;
  }
  bool exists(const char* path) override { return files.count(path) != 0; }
  bool remove(const char* path) override { return files.erase(path) != 0; }
  bool rename(const char* from, const char* to) override {
    if (failRenameTo == to) {
      failRenameTo.clear();
      return false;
    }
    const auto it = files.find(from);
    if (it == files.end() || files.count(to) != 0) return false;
    files[to] = it->second;
    files.erase(it);
    return true;
  }
  bool ensureDir(const char*) override { return true; }
};

// Scripted connection: each read() pops the next chunk; kClose means the peer closed, kStall a
// timeout. open() results can be queued too. Every read advances FakeClock by msPerRead.
class FakeConnection : public net::Connection {
 public:
  static constexpr const char* kClose = "\x01<close>";
  static constexpr const char* kStall = "\x01<stall>";

  std::deque<std::string> reads;
  std::deque<net::OpenError> openResults;
  bool failNextWrite = false;
  bool open_ = false;
  int opens = 0;
  int closes = 0;
  unsigned long msPerRead = 0;
  size_t maxBytesPerRead = SIZE_MAX;
  std::vector<std::string> written;

  net::OpenError open(const net::Endpoint&) override {
    opens++;
    net::OpenError result = net::OpenError::None;
    if (!openResults.empty()) {
      result = openResults.front();
      openResults.pop_front();
    }
    open_ = result == net::OpenError::None;
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
    FakeClock::nowMs += msPerRead;
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
    const size_t n = std::min({capacity, chunk.size(), maxBytesPerRead});
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

// Scripted WiFi.
class FakeWifi : public net::WifiControl {
 public:
  net::WifiResult result = net::WifiResult::Up;
  bool owned = false;  // what release()/tick() report giving back
  int ensures = 0;
  int touches = 0;
  int releases = 0;
  bool expireOnNextTick = false;

  net::WifiResult ensureUp() override {
    ensures++;
    return result;
  }
  void touch() override { touches++; }
  bool tick(int) override {
    if (!expireOnNextTick || !owned) return false;
    expireOnNextTick = false;
    owned = false;
    return true;
  }
  bool release() override {
    releases++;
    const bool was = owned;
    owned = false;
    return was;
  }
};

inline std::string httpOk(const std::string& body, const std::string& extraHeaders = "") {
  return "HTTP/1.1 200 OK\r\n" + extraHeaders + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

}  // namespace lexipoint::fakes
