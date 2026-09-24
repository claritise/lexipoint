#if LEXIRISE

#include "LexiriseService.h"

#include <Logging.h>

#include "api/Requests.h"
#include "net/WifiSession.h"
#include "settings/SettingsStore.h"

namespace lexipoint {
namespace {

constexpr const char* kLogTag = "LXS";

}  // namespace

LexiriseService::LexiriseService() : client_(connection_, api::userAgent(CROSSPOINT_VERSION)) {}

api::ApiResponse LexiriseService::send(const net::Request& request) {
  const Settings settings = settingsStore().snapshot();
  api::ApiResponse response;
  if (!client_.configure(settings.baseUrl, settings.apiKey)) {
    response.error = api::ApiError::NotConfigured;
    return response;
  }
  if (net::wifiSession().ensureUp() != net::WifiResult::Up) {
    client_.close();
    response.error = api::ApiError::Network;
    return response;
  }
  response = client_.send(request);
  net::wifiSession().touch();
  // Never log the body or the key: the status and error name are enough to diagnose.
  LOG_INF(kLogTag, "%s %s -> %d (%s)", net::methodName(request.method), request.path.c_str(), response.status,
          api::apiErrorName(response.error));
  return response;
}

api::KeyStatus LexiriseService::checkKey() {
  status_ = api::keyStatusFrom(send(api::meRequest()));
  if (status_.state == api::KeyState::Connected) {
    LOG_INF(kLogTag, "Key OK, rate limit %u per %u s", (unsigned)status_.me.rateLimitMax,
            (unsigned)(status_.me.rateLimitWindowMs / 1000));
  }
  return status_;
}

api::ApiResponse LexiriseService::analyze(const Language language, const std::string_view text) {
  return send(api::analyzeRequest(language, text));
}

void LexiriseService::tick() {
  // Re-read the idle setting only when the settings changed: no settings copy on every loop pass.
  const uint32_t revision = settingsStore().revision();
  if (!wifiIdleKnown_ || revision != wifiIdleRevision_) {
    wifiIdleMin_ = settingsStore().snapshot().wifiIdleMin;
    wifiIdleRevision_ = revision;
    wifiIdleKnown_ = true;
  }
  net::wifiSession().tick(wifiIdleMin_);
}

LexiriseService& service() {
  static LexiriseService instance;
  return instance;
}

}  // namespace lexipoint

#endif  // LEXIRISE
