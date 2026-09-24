#pragma once

// The device's one Lexirise session: settings snapshot → WiFi → verified TLS → client. Used by the
// /lexirise web page (key check), the dev harness (LX:LEXI), and from P3 the lookup provider.
// Everything it touches is injected, so it is host-tested with fakes (test/lexirise_net/ServiceTest);
// LexiriseServiceHal.cpp wires the device instance. Main task only.

#include <cstdint>
#include <string>
#include <string_view>

#include "api/KeyCheck.h"
#include "api/LexiriseClient.h"
#include "net/Connection.h"
#include "net/WifiSession.h"
#include "settings/Settings.h"
#include "settings/SettingsStore.h"

namespace lexipoint {

class LexiriseService {
 public:
  using Clock = api::LexiriseClient::Clock;

  LexiriseService(SettingsStore& store, net::WifiControl& wifi, net::Connection& connection, std::string userAgent,
                  Clock clock);

  // GET /v1/me with the current key, now; the result is cached for keyStatus().
  api::KeyStatus checkKey();
  // The same, from the next tick(): web handlers queue it instead of blocking inside the HTTP request
  // (and so off WebServer::handleClient's stack). keyStatus() reads Checking until it has run.
  void requestKeyCheck();
  api::KeyStatus keyStatus() const { return status_; }
  // Queues a check if the cached status is stale (never checked, or offline last time) and there is a
  // key. The web page calls it when it opens; send() does it itself whenever WiFi comes up.
  void recheckIfStale();
  // The key or server changed: the cached status no longer applies.
  void invalidateKeyStatus() { status_ = api::KeyStatus(); }

  // POST /v1/analyze/text (the raw response; api::parseAnalyze reads it).
  api::ApiResponse analyze(Language language, std::string_view text);

  // Main-loop tick: a queued key check, the idle TLS close, and the WiFi idle teardown.
  void tick();

  // The activity on screen is about to change (ActivityManager hook, before the next onEnter).
  // `reading`: a reader activity is on screen or under it. Leaving reading gives WiFi back and closes
  // the TLS session, so the next activity never shares the radio with Lexipoint (net/WifiLease.h).
  void onActivityChanged(bool reading);
  // Closes the session and gives WiFi back now if Lexipoint owns it.
  void releaseWifi();

 private:
  api::ApiResponse send(const net::Request& request);
  void closeSession();

  SettingsStore& store_;
  net::WifiControl& wifi_;
  api::LexiriseClient client_;
  Clock clock_;
  api::KeyStatus status_;
  bool checkPending_ = false;
  bool checking_ = false;       // inside checkKey(): its own send() doesn't queue another
  bool sessionActive_ = false;  // a call ran since the last close: the idle close is armed
  unsigned long lastCallMs_ = 0;
  bool wifiIdleKnown_ = false;
  uint32_t wifiIdleRevision_ = 0;
  int wifiIdleMin_ = 0;
};

// The device instance (LexiriseServiceHal.cpp; not linked into host tests).
LexiriseService& service();

}  // namespace lexipoint
