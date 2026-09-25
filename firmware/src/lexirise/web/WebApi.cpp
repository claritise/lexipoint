#if LEXIRISE

#include "WebApi.h"

#include <iterator>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/net/JsonReader.h"
#include "lexirise/net/JsonWriter.h"
#include "lexirise/settings/SettingsScreen.h"

namespace lexipoint::web {
namespace {

using json::Path;
using json::Type;

const char* readingName(const Reading reading) { return reading == Reading::Romaji ? "romaji" : "kana"; }

// The page's rows that can hide, by their data-when value (test_lexirise_page.py pins the two lists). A new
// row that can hide is one line here and its data-when there.
struct PageRow {
  const char* key;
  settings_screen::Row row;
};
constexpr PageRow kPageRows[] = {
    {"jaLookups", settings_screen::Row::JaLookups}, {"jaReading", settings_screen::Row::JaReading},
    {"zhLookups", settings_screen::Row::ZhLookups}, {"defaultLanguage", settings_screen::Row::DefaultLanguage},
    {"tags", settings_screen::Row::Tags},           {"wifiIdle", settings_screen::Row::WifiIdle},
};

// Which of them show: the device screen's own (settings_screen::visibleRows), so the page has no rule of its
// own to drift (P13).
net::JsonObject shownJson(const Settings& s) {
  const settings_screen::Rows rows = settings_screen::visibleRows(s);
  net::JsonObject out;
  for (const PageRow& pageRow : kPageRows) out.add(pageRow.key, settings_screen::shows(rows, pageRow.row));
  return out;
}

// Collects the top-level and one-level-nested ("ja"/"zh") scalars into the patch.
class PatchVisitor final : public json::Visitor {
 public:
  explicit PatchVisitor(SettingsPatch& out) : out_(out) {}
  const char* error = nullptr;
  bool rootIsObject = false;

  void onBegin(const Path& path, const bool isArray) override {
    if (path.depth() == 0) {
      rootIsObject = !isArray;
    } else if (path.depth() == 1 && (path.keyIs(0, "ja") || path.keyIs(0, "zh"))) {
      if (isArray) fail(path);  // the language groups must be objects
    } else if (path.depth() <= 2) {
      fail(path);  // a known field holding an object or array (unknown ones are ignored)
    }
  }

  void onValue(const Path& path, const Type type, const std::string_view text) override {
    if (error) return;
    if (path.depth() == 1) {
      top(path, type, text);
    } else if (path.depth() == 2 && path.keyIs(0, "ja")) {
      japanese(path, type, text);
    } else if (path.depth() == 2 && path.keyIs(0, "zh")) {
      chinese(path, type, text);
    }
  }

 private:
  // Records the first bad field, as a static string (it outlives the visitor). Unknown fields are
  // never errors.
  void fail(const Path& path) {
    if (error || path.depth() == 0 || path.isIndex(0)) return;
    static constexpr const char* kFields[] = {
        "enabled", "key", "clearKey",   "defaultLanguage", "tags",        "wifiIdleMin", "baseUrl",
        "ja",      "zh",  "ja.enabled", "ja.reading",      "ja.stardict", "zh.enabled",  "zh.stardict"};
    std::string name = path.at(0).key;
    if (path.depth() >= 2 && !path.isIndex(1)) name += "." + path.at(1).key;
    for (const char* field : kFields) {
      if (name == field) {
        error = field;
        return;
      }
    }
  }

  bool readBool(const Path& path, const Type type, const std::string_view text, std::optional<bool>& field) {
    if (type != Type::Bool) {
      fail(path);
      return false;
    }
    field = text == "true";
    return true;
  }

  bool readString(const Path& path, const Type type, const std::string_view text, std::optional<std::string>& field) {
    if (type != Type::String) {
      fail(path);
      return false;
    }
    field = std::string(text);
    return true;
  }

  void top(const Path& path, const Type type, const std::string_view text) {
    const std::string_view key = path.leaf();
    if (key == "ja" || key == "zh") return fail(path);  // must be objects
    if (key == "enabled") {
      readBool(path, type, text, out_.enabled);
    } else if (key == "key") {
      readString(path, type, text, out_.apiKey);
    } else if (key == "clearKey") {
      std::optional<bool> clear;
      if (readBool(path, type, text, clear)) out_.clearApiKey = *clear;
    } else if (key == "defaultLanguage") {
      std::optional<std::string> code;
      if (!readString(path, type, text, code)) return;
      if (*code == "ja") {
        out_.defaultLanguage = Language::Japanese;
      } else if (*code == "zh") {
        out_.defaultLanguage = Language::Chinese;
      } else {
        fail(path);
      }
    } else if (key == "tags") {
      readString(path, type, text, out_.tags);
    } else if (key == "wifiIdleMin") {
      // A small whole number; range is SettingsPatch's job.
      int value = 0;
      if (type != Type::Number || text.empty() || text.size() > 4) return fail(path);
      for (const char c : text) {
        if (c < '0' || c > '9') return fail(path);
        value = value * 10 + (c - '0');
      }
      out_.wifiIdleMin = value;
    } else if (key == "baseUrl") {
      readString(path, type, text, out_.baseUrl);
    }
  }

  void japanese(const Path& path, const Type type, const std::string_view text) {
    const std::string_view key = path.leaf();
    if (key == "enabled") {
      readBool(path, type, text, out_.japaneseEnabled);
    } else if (key == "stardict") {
      readString(path, type, text, out_.japaneseStardict);
    } else if (key == "reading") {
      std::optional<std::string> reading;
      if (!readString(path, type, text, reading)) return;
      if (*reading == "kana") {
        out_.japaneseReading = Reading::Kana;
      } else if (*reading == "romaji") {
        out_.japaneseReading = Reading::Romaji;
      } else {
        fail(path);
      }
    }
  }

  void chinese(const Path& path, const Type type, const std::string_view text) {
    const std::string_view key = path.leaf();
    if (key == "enabled") {
      readBool(path, type, text, out_.chineseEnabled);
    } else if (key == "stardict") {
      readString(path, type, text, out_.chineseStardict);
    }
  }

  SettingsPatch& out_;
};

net::JsonObject statusJson(const api::KeyStatus& status) {
  net::JsonObject out;
  out.add("state", api::keyStateName(status.state));
  if (status.state == api::KeyState::Connected) {
    out.add("name", status.me.name).add("plan", status.me.plan);
  } else if (status.error != api::ApiError::None) {
    out.add("error", api::apiErrorName(status.error));
  }
  return out;
}

}  // namespace

const char* parsePatch(const std::string_view body, SettingsPatch& out) {
  SettingsPatch patch;
  PatchVisitor visitor(patch);
  if (json::read(body, visitor) != json::Result::Ok || !visitor.rootIsObject) return "json";
  if (visitor.error) return visitor.error;
  out = std::move(patch);
  return nullptr;
}

std::string stateJson(const Settings& s, const api::KeyStatus& status, const std::vector<std::string>& dictionaries,
                      const bool settingsReset) {
  const std::vector<int> idleChoices(std::begin(config::kWifiIdleChoicesMin), std::end(config::kWifiIdleChoicesMin));
  return net::JsonObject()
      .add("enabled", s.enabled)
      .add("hasKey", s.hasApiKey())
      .add("key", s.hasApiKey() ? maskApiKey(s.apiKey) : std::string())
      .add("ja", net::JsonObject()
                     .add("enabled", s.japanese.enabled)
                     .add("reading", readingName(s.japaneseReading))
                     .add("stardict", s.japanese.stardict))
      .add("zh", net::JsonObject().add("enabled", s.chinese.enabled).add("stardict", s.chinese.stardict))
      .add("defaultLanguage", languageCode(s.defaultLanguage))
      .add("tags", s.tags)
      .add("wifiIdleMin", s.wifiIdleMin)
      .add("baseUrl", s.baseUrl)
      .add("defaultBaseUrl", config::kDefaultBaseUrl)
      .add("choices", net::JsonObject().add("wifiIdleMin", idleChoices).add("dictionaries", dictionaries))
      .add("status", statusJson(status))
      .add("settingsReset", settingsReset)
      .add("checkTimeoutS", static_cast<int>((config::kMaxCallMs + 999) / 1000))
      .add("shows", shownJson(s))
      .str();
}

std::string errorJson(const char* error, const char* field) {
  net::JsonObject out;
  out.add("error", error);
  if (field) out.add("field", field);
  return out.str();
}

}  // namespace lexipoint::web

#endif  // LEXIRISE
