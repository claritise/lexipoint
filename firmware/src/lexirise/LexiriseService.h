#pragma once

// The device's one Lexirise session: settings snapshot → WiFi → verified TLS → client. Used by the
// /lexirise web page (key check), the dev harness (LX:LEXI), and from P3 the lookup provider.
// Everything it touches is injected, so it is host-tested with fakes (test/lexirise_net/ServiceTest);
// LexiriseServiceHal.cpp wires the device instance. Main task only.

#include <cstdint>
#include <string>
#include <string_view>

#include "api/AccessPolicy.h"
#include "api/KeyCheck.h"
#include "api/LexiriseApi.h"
#include "api/LexiriseClient.h"
#include "net/Connection.h"
#include "net/WifiSession.h"
#include "settings/Settings.h"
#include "settings/SettingsStore.h"

namespace lexipoint {

class LexiriseService final : public api::LexiriseApi {
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
  // A new key or server: the cached status, and a rejection or back-off, no longer apply.
  void invalidateKeyStatus() {
    status_ = api::KeyStatus();
    access_.reset();
  }

  // Whether Lexirise may be asked now: not after a 401/403 (until reboot or a new key), not during a 429's
  // back-off. Lookups skip it then (StarDict answers), and writes fail at once.
  api::AccessPolicy::Block blocked() const { return access_.blocked(clock_()); }
  uint32_t retryInS() const { return access_.retryInS(clock_()); }
  // The block the reader hasn't been told about yet (then it has): word select shows its notice.
  api::AccessPolicy::Block takeUnannouncedBlock() { return access_.takeUnannounced(clock_()); }

  // POST /v1/analyze/text (and its word-level form) and /v1/dictionary/lookup (raw responses;
  // api::parseAnalyze / parseLookup read them). All share the keep-alive session, so a lookup's calls cost one
  // handshake.
  api::ApiResponse analyze(Language language, std::string_view text) override;
  api::ApiResponse analyzeWords(Language language, std::string_view text) override;
  api::ApiResponse lookup(Language language, std::string_view lemma) override;
  // A /v1/vocabulary write. One that isn't safe to resend (the save's POST, an upsert) starts on a fresh
  // session (lookup-flow.md §7): a reused keep-alive session that turned stale would fail it unretried.
  api::ApiResponse write(const net::Request& request) override;
  // A /v1/decks call: the creation (a POST) on a fresh session too, like a save.
  api::ApiResponse deck(const net::Request& request) override { return write(request); }

  // Main-loop tick: a queued key check, the idle TLS close, and the WiFi idle teardown.
  void tick();

  // While the live card is open, WiFi Lexipoint owns stays up between its calls (one per loop pass):
  // "Keep WiFi on after a lookup: Off" means once per card, not once per call. Released, the idle rule
  // counts from then (so Off turns it off at the next tick).
  void holdWifi(bool hold);

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
  api::AccessPolicy access_;
  bool checkPending_ = false;
  bool checking_ = false;       // inside checkKey(): its own send() doesn't queue another
  bool sessionActive_ = false;  // a call ran since the last close: the idle close is armed
  unsigned long lastCallMs_ = 0;
  bool wifiHeld_ = false;
  SettingsWatch wifiIdleWatch_;
  int wifiIdleMin_ = 0;
};

// The device instance (LexiriseServiceHal.cpp; not linked into host tests).
LexiriseService& service();

}  // namespace lexipoint
