#if LEXIRISE

#include "LexiriseWeb.h"

#include <Logging.h>
#include <WebServer.h>

#include <cstring>
#include <string>
#include <vector>

#include "Origin.h"
#include "WebApi.h"
#include "lexirise/LexiriseService.h"
#include "lexirise/settings/SettingsPatch.h"
#include "lexirise/settings/SettingsStore.h"
#include "lexirise/web/LexiriseNavJs.generated.h"
#include "lexirise/web/LexirisePageHtml.generated.h"
#include "util/DictionaryRegistry.h"

namespace lexipoint::web {
namespace {

constexpr const char* kLogTag = "LXWEB";
constexpr const char* kJson = "application/json";
constexpr int kHttpOk = 200;
constexpr int kHttpNotModified = 304;
constexpr int kHttpBadRequest = 400;
constexpr int kHttpForbidden = 403;
constexpr int kHttpServerError = 500;

// Same caching contract as CrossPointWebServer's static pages (ETag + no-cache revalidation).
void sendGzipped(WebServer& server, const char* data, const size_t len, const char* etag, const char* type) {
  if (server.header("If-None-Match") == etag) {
    server.sendHeader("ETag", etag);
    server.sendHeader("Cache-Control", "no-cache");
    server.send(kHttpNotModified);
    return;
  }
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("ETag", etag);
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(kHttpOk, type, data, len);
}

void sendJson(WebServer& server, const int code, const std::string& body) {
  server.sendHeader("Cache-Control", "no-store");  // it can hold the account name
  server.send(code, kJson, body.c_str());
}

void sendState(WebServer& server) {
  std::vector<DictionaryEntry> found;
  DictionaryRegistry::discover(found);
  std::vector<std::string> names;
  names.reserve(found.size());
  for (auto& d : found) names.push_back(std::move(d.name));
  sendJson(server, kHttpOk,
           stateJson(settingsStore().snapshot(), service().keyStatus(), names,
                     settingsStore().lastLoad() == LoadOutcome::Unreadable));
}

// Every /api/lexirise call: writes could replace the key or its server, and reads show the account
// name. Refuses other sites (Origin) and DNS-rebinding hosts (Host), see Origin.h.
bool allowRequest(WebServer& server) {
  const String host = server.hostHeader();
  if (isTrustedHost(host.c_str()) && isSameOriginRequest(server.header("Origin").c_str(), host.c_str())) return true;
  LOG_ERR(kLogTag, "Refused a cross-site request");
  sendJson(server, kHttpForbidden, errorJson("cross-origin"));
  return false;
}

void handleGet(WebServer& server) {
  if (!allowRequest(server)) return;
  // The page opening: a key saved while offline (hotspot) gets checked now the device may be online.
  if (server.arg("recheck") == "1") service().recheckIfStale();
  sendState(server);
}

void handlePost(WebServer& server) {
  if (!allowRequest(server)) return;
  SettingsPatch patch;
  const char* bad = server.hasArg("plain") ? parsePatch(server.arg("plain").c_str(), patch) : "json";
  if (bad) {
    return sendJson(server, kHttpBadRequest,
                    std::strcmp(bad, "json") == 0 ? errorJson("invalid-json") : errorJson("invalid", bad));
  }

  PatchResult applied;
  bool serverChanged = false;
  bool hasKey = false;
  const auto result = settingsStore().update([&](Settings& s) {
    const std::string previousBaseUrl = s.baseUrl;
    applied = applyPatch(s, patch);
    serverChanged = s.baseUrl != previousBaseUrl;
    hasKey = s.hasApiKey();
    return applied.ok;
  });
  if (result == SettingsStore::UpdateResult::Declined) {
    return sendJson(server, kHttpBadRequest, errorJson("invalid", applied.field));
  }
  if (result == SettingsStore::UpdateResult::SaveFailed) {
    return sendJson(server, kHttpServerError, errorJson("save-failed"));
  }

  // A new key or server: the old status says nothing about it. The check runs from the main loop,
  // not inside this request; the page polls until it settles.
  if (applied.keyChanged || serverChanged) {
    service().invalidateKeyStatus();
    if (hasKey) service().requestKeyCheck();
  }
  sendState(server);
}

void handleTest(WebServer& server) {
  if (!allowRequest(server)) return;
  service().requestKeyCheck();
  sendState(server);
}

}  // namespace

void registerRoutes(WebServer& server) {
  server.on("/lexirise", HTTP_GET, [&server] {
    sendGzipped(server, LexirisePageHtml, sizeof(LexirisePageHtml), LexirisePageHtmlETag, "text/html");
  });
  server.on("/lexirise/nav.js", HTTP_GET, [&server] {
    sendGzipped(server, LexiriseNavJs, sizeof(LexiriseNavJs), LexiriseNavJsETag, "application/javascript");
  });
  server.on("/api/lexirise", HTTP_GET, [&server] { handleGet(server); });
  server.on("/api/lexirise", HTTP_POST, [&server] { handlePost(server); });
  server.on("/api/lexirise/test", HTTP_POST, [&server] { handleTest(server); });
}

}  // namespace lexipoint::web

#endif  // LEXIRISE
