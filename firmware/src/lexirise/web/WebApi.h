#pragma once

// The /api/lexirise wire format (settings.md §1a-2), pure: request JSON → SettingsPatch, and the
// page's state → JSON. LexiriseWeb.cpp only glues these to the WebServer.
// Tests: test/lexirise_net/WebApiTest.cpp.

#include <string>
#include <string_view>
#include <vector>

#include "lexirise/api/KeyCheck.h"
#include "lexirise/settings/Settings.h"
#include "lexirise/settings/SettingsPatch.h"

namespace lexipoint::web {

// Reads a POST body. Returns nullptr on success, "json" for a body that isn't a JSON object, or the
// first field with the wrong type or value. Unknown fields are ignored; absent ones stay unset.
const char* parsePatch(std::string_view body, SettingsPatch& out);

// Everything the page shows. The key only ever appears masked (settings.md §2).
std::string stateJson(const Settings& settings, const api::KeyStatus& status,
                      const std::vector<std::string>& dictionaries);

// {"error":"<error>"[,"field":"<field>"]}
std::string errorJson(const char* error, const char* field = nullptr);

}  // namespace lexipoint::web
