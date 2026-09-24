#if LEXIRISE

#include "LexiriseWeb.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <WebServer.h>

#include <optional>
#include <string>
#include <vector>

#include "Origin.h"
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
constexpr int kHttpBadRequest = 400;
constexpr int kHttpForbidden = 403;
constexpr int kHttpServerError = 500;

// Same caching contract as CrossPointWebServer's static pages (ETag + no-cache revalidation).
void sendGzipped(WebServer& server, const char* data, const size_t len, const char* etag, const char* type) {
  if (server.header("If-None-Match") == etag) {
    server.sendHeader("ETag", etag);
    server.sendHeader("Cache-Control", "no-cache");
    server.send(304);
    return;
  }
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("ETag", etag);
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, type, data, len);
}

void sendError(WebServer& server, const int code, const char* error, const char* field = nullptr) {
  JsonDocument doc;
  doc["error"] = error;
  if (field) doc["field"] = field;
  String out;
  serializeJson(doc, out);
  server.send(code, kJson, out);
}

const char* readingName(const Reading reading) { return reading == Reading::Romaji ? "romaji" : "kana"; }

void writeStatus(JsonObject out, const api::KeyStatus& status) {
  out["state"] = api::keyStateName(status.state);
  if (status.state == api::KeyState::Connected) {
    out["name"] = status.me.name.c_str();
    out["plan"] = status.me.plan.c_str();
  } else if (status.error != api::ApiError::None) {
    out["error"] = api::apiErrorName(status.error);
  }
}

// The page's whole view. The key only ever leaves the device masked (settings.md §2).
void sendState(WebServer& server) {
  const Settings s = settingsStore().snapshot();
  JsonDocument doc;
  doc["enabled"] = s.enabled;
  doc["hasKey"] = s.hasApiKey();
  doc["key"] = s.hasApiKey() ? maskApiKey(s.apiKey).c_str() : "";
  JsonObject ja = doc["ja"].to<JsonObject>();
  ja["enabled"] = s.japanese.enabled;
  ja["reading"] = readingName(s.japaneseReading);
  ja["stardict"] = s.japanese.stardict.c_str();
  JsonObject zh = doc["zh"].to<JsonObject>();
  zh["enabled"] = s.chinese.enabled;
  zh["stardict"] = s.chinese.stardict.c_str();
  doc["defaultLanguage"] = languageCode(s.defaultLanguage);
  doc["tags"] = s.tags.c_str();
  doc["wifiIdleMin"] = s.wifiIdleMin;
  doc["baseUrl"] = s.baseUrl.c_str();

  JsonObject choices = doc["choices"].to<JsonObject>();
  JsonArray idle = choices["wifiIdleMin"].to<JsonArray>();
  for (const int minutes : config::kWifiIdleChoicesMin) idle.add(minutes);
  JsonArray dicts = choices["dictionaries"].to<JsonArray>();
  std::vector<DictionaryEntry> found;
  DictionaryRegistry::discover(found);
  for (const auto& d : found) dicts.add(d.name.c_str());

  writeStatus(doc["status"].to<JsonObject>(), service().keyStatus());
  String out;
  serializeJson(doc, out);
  server.send(200, kJson, out);
}

// JSON → SettingsPatch. Returns the offending field, or nullptr. Absent keys stay unset.
const char* readPatch(JsonVariantConst doc, SettingsPatch& p) {
  const auto readBool = [](JsonVariantConst v, std::optional<bool>& out) {
    if (v.isNull()) return true;
    if (!v.is<bool>()) return false;
    out = v.as<bool>();
    return true;
  };
  const auto readString = [](JsonVariantConst v, std::optional<std::string>& out) {
    if (v.isNull()) return true;
    if (!v.is<const char*>()) return false;
    out = std::string(v.as<const char*>());
    return true;
  };

  if (!readBool(doc["enabled"], p.enabled)) return "enabled";
  if (!readString(doc["key"], p.apiKey)) return "key";
  std::optional<bool> clearKey;
  if (!readBool(doc["clearKey"], clearKey)) return "clearKey";
  p.clearApiKey = clearKey.value_or(false);

  JsonVariantConst ja = doc["ja"];
  if (!ja.isNull()) {
    if (!ja.is<JsonObjectConst>()) return "ja";
    if (!readBool(ja["enabled"], p.japaneseEnabled)) return "ja.enabled";
    if (!readString(ja["stardict"], p.japaneseStardict)) return "ja.stardict";
    std::optional<std::string> reading;
    if (!readString(ja["reading"], reading)) return "ja.reading";
    if (reading) {
      if (*reading == "kana") {
        p.japaneseReading = Reading::Kana;
      } else if (*reading == "romaji") {
        p.japaneseReading = Reading::Romaji;
      } else {
        return "ja.reading";
      }
    }
  }
  JsonVariantConst zh = doc["zh"];
  if (!zh.isNull()) {
    if (!zh.is<JsonObjectConst>()) return "zh";
    if (!readBool(zh["enabled"], p.chineseEnabled)) return "zh.enabled";
    if (!readString(zh["stardict"], p.chineseStardict)) return "zh.stardict";
  }

  std::optional<std::string> language;
  if (!readString(doc["defaultLanguage"], language)) return "defaultLanguage";
  if (language) {
    if (*language == "ja") {
      p.defaultLanguage = Language::Japanese;
    } else if (*language == "zh") {
      p.defaultLanguage = Language::Chinese;
    } else {
      return "defaultLanguage";
    }
  }
  if (!readString(doc["tags"], p.tags)) return "tags";
  JsonVariantConst idle = doc["wifiIdleMin"];
  if (!idle.isNull()) {
    if (!idle.is<int>()) return "wifiIdleMin";
    p.wifiIdleMin = idle.as<int>();
  }
  if (!readString(doc["baseUrl"], p.baseUrl)) return "baseUrl";
  return nullptr;
}

// Every /api/lexirise call: writes could redirect or replace the key, and reads show the account name.
bool allowOrigin(WebServer& server) {
  if (isSameOriginRequest(server.header("Origin").c_str(), server.hostHeader().c_str())) return true;
  LOG_ERR(kLogTag, "Refused a cross-site request");
  sendError(server, kHttpForbidden, "cross-origin");
  return false;
}

void handlePost(WebServer& server) {
  if (!allowOrigin(server)) return;
  if (!server.hasArg("plain")) return sendError(server, kHttpBadRequest, "missing-body");
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) || !doc.is<JsonObject>()) {
    return sendError(server, kHttpBadRequest, "invalid-json");
  }
  SettingsPatch patch;
  if (const char* field = readPatch(doc.as<JsonVariantConst>(), patch)) {
    return sendError(server, kHttpBadRequest, "invalid", field);
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
    return sendError(server, kHttpBadRequest, "invalid", applied.field);
  }
  if (result == SettingsStore::UpdateResult::SaveFailed) return sendError(server, kHttpServerError, "save-failed");

  // A new key or server: the old status says nothing about it, so check again straight away.
  if (applied.keyChanged || serverChanged) {
    service().invalidateKeyStatus();
    if (hasKey) service().checkKey();
  }
  sendState(server);
}

void handleTest(WebServer& server) {
  if (!allowOrigin(server)) return;
  service().checkKey();
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
  server.on("/api/lexirise", HTTP_GET, [&server] {
    if (allowOrigin(server)) sendState(server);
  });
  server.on("/api/lexirise", HTTP_POST, [&server] { handlePost(server); });
  server.on("/api/lexirise/test", HTTP_POST, [&server] { handleTest(server); });
}

}  // namespace lexipoint::web

#endif  // LEXIRISE
