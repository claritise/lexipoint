#if LEXIRISE

#include "WifiSession.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include "Wait.h"
#include "WifiCredentialStore.h"

namespace lexipoint::net {
namespace {

constexpr const char* kLogTag = "LXW";

}  // namespace

WifiResult WifiSession::ensureUp() {
  switch (decideEnsureUp(WiFi.status() == WL_CONNECTED, WiFi.getMode() == WIFI_MODE_NULL)) {
    case WifiAction::UseExisting:
      touch();
      return WifiResult::Up;
    case WifiAction::Busy:
      LOG_INF(kLogTag, "WiFi is in use by something else, not touching it");
      return WifiResult::Busy;
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

  // Same recipe as WifiSelectionActivity::attemptConnection (credentials live in the store, not NVS).
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  if (credential->password.empty()) {
    WiFi.begin(credential->ssid.c_str());
  } else {
    WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
  }
  const unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < config::kWifiConnectMs) {
    pollWait(config::kWifiPollMs);
  }
  const bool up = WiFi.status() == WL_CONNECTED;
  // Ours from here, including a failed attempt's radio (tearDown turns it back off).
  lease_.acquired(millis());
  if (!up) {
    tearDown();
    LOG_INF(kLogTag, "WiFi failed after %lu ms", millis() - started);
    return WifiResult::Failed;
  }
  WiFi.setSleep(false);  // modem sleep shows up as HTTP timeouts (KOSync does the same)
  LOG_INF(kLogTag, "WiFi up in %lu ms", millis() - started);
  return WifiResult::Up;
}

void WifiSession::touch() { lease_.used(millis()); }

bool WifiSession::tick(const int idleMin) {
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
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  lease_.lost();
}

}  // namespace lexipoint::net

#endif  // LEXIRISE
