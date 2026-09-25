#if LEXIRISE

#include "LexiriseService.h"

#include <Logging.h>

#include <utility>

#include "api/Requests.h"
#include "net/WifiLease.h"

namespace lexipoint {
namespace {

constexpr const char* kLogTag = "LXS";

}  // namespace

LexiriseService::LexiriseService(SettingsStore& store, net::WifiControl& wifi, net::Connection& connection,
                                 std::string userAgent, const Clock clock)
    : store_(store), wifi_(wifi), client_(connection, std::move(userAgent), clock), clock_(clock) {}

api::ApiResponse LexiriseService::send(const net::Request& request) {
  // A rejected key (401/403) or a rate limit (429) is honoured without the network (offline-and-errors.md
  // §1, §3); only the key check may probe a rejected key.
  const unsigned long now = clock_();  // one reading: the block and its refusal must agree
  const api::AccessPolicy::Block block = access_.blocked(now);
  if (block == api::AccessPolicy::Block::RateLimited || (block == api::AccessPolicy::Block::Rejected && !checking_)) {
    return access_.refusal(now);
  }
  const Settings settings = store_.snapshot();
  api::ApiResponse response;
  if (!client_.configure(settings.baseUrl, settings.apiKey)) {
    response.error = api::ApiError::NotConfigured;
    return response;
  }
  if (const net::WifiResult wifi = wifi_.ensureUp(); wifi != net::WifiResult::Up) {
    closeSession();
    response.error = wifi == net::WifiResult::NotConfigured ? api::ApiError::NoWifiSaved : api::ApiError::NoWifi;
    return response;
  }
  if (!checking_) recheckIfStale();  // online now: a key saved while offline gets checked next tick
  response = client_.send(request);
  access_.observe(response, clock_());
  // A lookup or save that finds the key rejected tells the web page too (it'd still say "Connected").
  if (!checking_ && response.error == api::ApiError::Unauthorized) status_ = api::keyStatusFrom(response);
  wifi_.touch();
  sessionActive_ = true;
  lastCallMs_ = clock_();
  // Never log the body or the key: the status and error name are enough to diagnose.
  LOG_INF(kLogTag, "%s %s -> %d (%s)", net::methodName(request.method), api::loggablePath(request.path).c_str(),
          response.status, api::apiErrorName(response.error));
  return response;
}

void LexiriseService::closeSession() {
  client_.close();
  sessionActive_ = false;
}

api::KeyStatus LexiriseService::checkKey() {
  checkPending_ = false;
  checking_ = true;
  status_ = api::keyStatusFrom(send(api::meRequest()));
  checking_ = false;
  if (status_.state == api::KeyState::Connected) {
    LOG_INF(kLogTag, "Key OK, rate limit %u per %u s", static_cast<unsigned>(status_.me.rateLimitMax),
            static_cast<unsigned>(status_.me.rateLimitWindowMs / 1000));
  }
  return status_;
}

void LexiriseService::recheckIfStale() {
  const bool stale = status_.state == api::KeyState::Unchecked || status_.state == api::KeyState::Offline;
  if (stale && !checkPending_ && store_.snapshot().hasApiKey()) requestKeyCheck();
}

void LexiriseService::requestKeyCheck() {
  checkPending_ = true;
  status_ = api::KeyStatus();
  status_.state = api::KeyState::Checking;
}

api::ApiResponse LexiriseService::analyze(const Language language, const std::string_view text) {
  return send(api::analyzeRequest(language, text));
}

api::ApiResponse LexiriseService::analyzeWords(const Language language, const std::string_view text) {
  LOG_INF(kLogTag, "analyze: the word-level split (fast), after a refined answer");  // tells the two calls apart
  return send(api::analyzeWordsRequest(language, text));
}

api::ApiResponse LexiriseService::lookup(const Language language, const std::string_view lemma) {
  return send(api::lookupRequest(language, lemma));
}

api::ApiResponse LexiriseService::write(const net::Request& request) {
  if (!request.retryable()) closeSession();
  return send(request);
}

void LexiriseService::tick() {
  if (checkPending_) checkKey();

  if (sessionActive_ && clock_() - lastCallMs_ >= config::kTlsIdleCloseMs) closeSession();

  // Re-read the idle setting only when the settings changed: no settings copy on every loop pass.
  const uint32_t revision = store_.revision();
  if (!wifiIdleKnown_ || revision != wifiIdleRevision_) {
    wifiIdleMin_ = store_.snapshot().wifiIdleMin;
    wifiIdleRevision_ = revision;
    wifiIdleKnown_ = true;
  }
  if (!wifiHeld_ && wifi_.tick(wifiIdleMin_)) closeSession();
}

void LexiriseService::holdWifi(const bool hold) {
  if (wifiHeld_ && !hold) wifi_.touch();  // idle from now, not from the card's last call
  wifiHeld_ = hold;
}

void LexiriseService::onActivityChanged(const bool reading) {
  if (!reading) releaseWifi();
}

void LexiriseService::releaseWifi() {
  closeSession();
  wifi_.release();
}

}  // namespace lexipoint

#endif  // LEXIRISE
