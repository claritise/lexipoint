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

void WifiSession::registerEvents() {
  if (eventsRegistered_) return;
  eventsRegistered_ = true;
  // Runs on the network event task: only touches atomics, the main task acts on them in tick().
  WiFi.onEvent([this](const arduino_event_id_t event, arduino_event_info_t) {
    switch (phase_.load()) {
      case Phase::Connecting:
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) phase_.store(Phase::Idle);
        return;
      case Phase::Disconnecting:
        if (event == ARDUINO_EVENT_WIFI_STA_STOP) phase_.store(Phase::Idle);
        return;
      case Phase::Idle:
      default:
        if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED || event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED ||
            event == ARDUINO_EVENT_WIFI_STA_STOP) {
          foreignEvent_.store(true);
        }
        return;
    }
  });
}

// Bounded: if a terminal event never comes (driver quirk), give up waiting and treat the transition
// as over, so later foreign events are still seen.
void WifiSession::waitForPhaseIdle() {
  const unsigned long started = millis();
  while (phase_.load() != Phase::Idle && millis() - started < config::kWifiEventSettleMs) pollWait(config::kWifiPollMs);
  phase_.store(Phase::Idle);
}

WifiResult WifiSession::ensureUp() {
  registerEvents();
  if (WiFi.status() == WL_CONNECTED) {
    touch();
    return WifiResult::Up;
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
  phase_.store(Phase::Connecting);
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
  if (up) {
    waitForPhaseIdle();
    WiFi.setSleep(false);  // modem sleep shows up as HTTP timeouts (KOSync does the same)
    foreignEvent_.store(false);
    lease_.acquired(millis());
  } else {
    tearDown();
  }
  LOG_INF(kLogTag, up ? "WiFi up in %lu ms" : "WiFi failed after %lu ms", millis() - started);
  return up ? WifiResult::Up : WifiResult::Failed;
}

void WifiSession::touch() { lease_.used(millis()); }

void WifiSession::tick(const int idleMin) {
  if (foreignEvent_.exchange(false)) lease_.lost();
  if (lease_.expired(millis(), idleMin)) {
    LOG_INF(kLogTag, "WiFi idle, turning it off");
    tearDown();
  }
}

void WifiSession::release() {
  if (foreignEvent_.exchange(false)) lease_.lost();
  if (lease_.owned()) tearDown();
}

void WifiSession::tearDown() {
  phase_.store(Phase::Disconnecting);  // our own disconnect is not a foreign event
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  waitForPhaseIdle();
  foreignEvent_.store(false);
  lease_.lost();
}

WifiSession& wifiSession() {
  static WifiSession session;
  return session;
}

}  // namespace lexipoint::net

#endif  // LEXIRISE
