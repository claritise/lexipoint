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
#include "api/RequestWindow.h"
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
  // GET /v1/vocabulary/{id} (read-only: on the keep-alive session, resent once if it turned stale).
  api::ApiResponse savedItem(const net::Request& request) override { return send(request); }
  // GET /v1/vocabulary, a page streamed into `sink` (V7a): only while WiFi is up already (never a join: the mirror's
  // sync adds no radio time the reader didn't cause), else NoWifi without the network.
  api::ApiResponse vocabularyPage(const net::Request& request, net::BodySink& sink, net::Abort abort) override;
  // POST /v1/analyze/text for a whole page (V7b), streamed: as vocabularyPage, never a join and never counted as the
  // reader's WiFi use or TLS activity; `abort` gives it up in any of the call's waits.
  api::ApiResponse analyzePage(const net::Request& request, net::BodySink& sink, net::Abort abort) override;

  // WiFi up for something the reader asked for on the home screen (V7b's Sync Vocabulary): the saved network, as a
  // card joins. The only join outside a lookup's own calls; NoWifi / NoWifiSaved when it can't.
  api::ApiError joinForUser();
  // Whether the station is connected now (the page analysis goes only then).
  bool wifiConnected() { return wifi_.connected(); }
  // This reader's requests that reached Lexirise in the last hour, and the key's hourly limit (/v1/me's; the
  // measured config::kRateLimitDefault until it has answered): the page analysis's budget (V7b).
  unsigned requestsLastHour() { return window_.count(clock_()); }
  uint32_t rateLimit() const {
    return status_.me.rateLimitMax != 0 ? status_.me.rateLimitMax : config::kRateLimitDefault;
  }

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
  // Closes the session and gives WiFi back now if Lexipoint owns it (true: it did).
  bool releaseWifi();

 private:
  // `mayJoin` false: only over a station already connected (NoWifi otherwise, never a join).
  api::ApiResponse send(const net::Request& request, net::BodySink* sink = nullptr, bool mayJoin = true,
                        net::Abort abort = nullptr);
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
  api::RequestWindow window_;
};

// The device instance (LexiriseServiceHal.cpp; not linked into host tests).
LexiriseService& service();

}  // namespace lexipoint
