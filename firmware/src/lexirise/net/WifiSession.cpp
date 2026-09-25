#if LEXIRISE

#include "WifiSession.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>

#include "Wait.h"
#include "WifiCredentialStore.h"

namespace lexipoint::net {
namespace {

constexpr const char* kLogTag = "LXW";

}  // namespace

WifiResult WifiSession::ensureUp() {
  switch (decideEnsureUp(WiFi.status() == WL_CONNECTED, WiFi.getMode() == WIFI_MODE_NULL, lease_.owned())) {
    case WifiAction::UseExisting:  // (tick() has remembered where it is, whoever made it)
      touch();
      return WifiResult::Up;
    case WifiAction::Busy:
      LOG_INF(kLogTag, "WiFi is in use by something else, not touching it");
      return WifiResult::Busy;
    case WifiAction::Rejoin:
      LOG_INF(kLogTag, "Our WiFi link dropped, joining again");
      tearDown();
      break;
    case WifiAction::Join:
    default:
      break;
  }

  WIFI_STORE.loadFromFile();  // cheap, and picks up a network added since boot
  std::optional<WifiCredential> credential;
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  if (!last.empty()) credential = WIFI_STORE.findCredential(last);
  if (!credential) credential = WIFI_STORE.getCredentialAt(0);
  if (!credential) {
    LOG_INF(kLogTag, "No saved WiFi network");
    return WifiResult::NotConfigured;
  }

  const unsigned long started = millis();
  const bool up = join(
      hints_, credential->ssid,
      [&](const WifiHint* hint, const uint32_t scanUntilMs) {
        const AttemptStep result = attempt(credential->ssid, credential->password, hint, scanUntilMs, started);
        if (hint && result != AttemptStep::Up) {
          LOG_INF(kLogTag, "Direct join to channel %ld failed after %lu ms%s", static_cast<long>(hint->channel),
                  millis() - started, result == AttemptStep::OutOfTime ? " (no address)" : ", scanning");
        }
        return result;
      },
      [this] { stopRadio(); }, [&] { return static_cast<uint32_t>(millis() - started); });
  // Ours from here, including a failed attempt's radio (tearDown turns it back off).
  lease_.acquired(millis());
  if (!up) {
    tearDown();
    LOG_INF(kLogTag, "WiFi failed after %lu ms", millis() - started);
    return WifiResult::Failed;
  }
  WiFi.setSleep(false);  // modem sleep shows up as HTTP timeouts (KOSync does the same)
  rememberConnection();
  LOG_INF(kLogTag, "WiFi up in %lu ms (channel %ld)", millis() - started, static_cast<long>(WiFi.channel()));
  return WifiResult::Up;
}

static_assert(wl::kIdle == WL_IDLE_STATUS && wl::kNoSsid == WL_NO_SSID_AVAIL && wl::kConnected == WL_CONNECTED &&
                  wl::kConnectFailed == WL_CONNECT_FAILED,
              "WifiHint.h's wl:: values mirror wl_status_t");

AttemptStep WifiSession::attempt(const std::string& ssid, const std::string& password, const WifiHint* hint,
                                 const uint32_t scanUntilMs, const unsigned long joinStarted) {
  // Same recipe as WifiSelectionActivity::attemptConnection (credentials live in the store, not NVS); with a
  // hint, the channel and access point are given, so no scan.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setScanMethod(hint ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  const char* pass = password.empty() ? nullptr : password.c_str();
  if (hint) {
    WiFi.begin(ssid.c_str(), pass, hint->channel, hint->bssid.data());
  } else {
    WiFi.begin(ssid.c_str(), pass);
  }
  while (true) {
    const AttemptStep step =
        attemptStep(hint != nullptr, linkStateOf(WiFi.status()), millis() - joinStarted, scanUntilMs);
    if (step != AttemptStep::Wait) return step;
    pollWait(config::kWifiPollMs);
  }
}

void WifiSession::stopRadio() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  const unsigned long started = millis();
  while (WiFi.status() != WL_STOPPED && millis() - started < config::kWifiStopWaitMs) {
    pollWait(config::kWifiPollMs);
  }
  if (WiFi.status() != WL_STOPPED) LOG_INF(kLogTag, "Radio not reported stopped after %lu ms", millis() - started);
}

void WifiSession::rememberConnection() {
  WifiHint hint;
  hint.ssid = WiFi.SSID().c_str();
  if (const uint8_t* bssid = WiFi.BSSID()) std::copy(bssid, bssid + hint.bssid.size(), hint.bssid.begin());
  hint.channel = WiFi.channel();
  hints_.remember(hint);
}

void WifiSession::touch() { lease_.used(millis()); }

bool WifiSession::tick(const int idleMin) {
  // Every loop pass the card isn't holding WiFi (LexiriseService::tick), whatever is on screen: a connection the
  // web server or KOSync made is remembered too. (While the card holds it, the radio is Lexipoint's own, which
  // ensureUp remembers itself.)
  if (watch_.newlyConnected(WiFi.status() == WL_CONNECTED)) rememberConnection();
  if (!lease_.expired(millis(), idleMin)) return false;
  LOG_INF(kLogTag, "WiFi idle, turning it off");
  tearDown();
  return true;
}

bool WifiSession::release() {
  if (!lease_.owned()) return false;
  tearDown();
  return true;
}

void WifiSession::tearDown() {
  stopRadio();
  lease_.lost();
}

}  // namespace lexipoint::net

#endif  // LEXIRISE
