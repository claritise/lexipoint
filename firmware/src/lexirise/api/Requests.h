#pragma once

// Lexirise request builders (lexirise-client.md §2). Pure. Tests: test/lexirise_net/RequestsTest.cpp.

#include <string>
#include <string_view>

#include "lexirise/net/Http.h"
#include "lexirise/settings/Settings.h"

namespace lexipoint::api {

net::Request meRequest();

// POST /v1/analyze/text with the full (not "fast") analysis: v0.1 needs lemmas. Text over
// config::kMaxAnalyzeTextBytes is cut at a UTF-8 boundary.
net::Request analyzeRequest(Language language, std::string_view text);

// "Lexipoint/<ver> CrossPoint/<ver>".
std::string userAgent(std::string_view crossPointVersion);

}  // namespace lexipoint::api
