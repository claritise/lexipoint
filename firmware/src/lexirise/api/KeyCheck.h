#pragma once

// The result of checking the API key with GET /v1/me (lexirise-client.md §5, settings.md §1a): what
// the web page's status line and the device settings show. Pure.
// Tests: test/lexirise_net/KeyCheckTest.cpp.

#include <string>

#include "LexiriseClient.h"
#include "Responses.h"

namespace lexipoint::api {

enum class KeyState {
  Unchecked,  // no check yet this boot
  Checking,   // a check is queued or running (the web page polls until it settles)
  NoKey,
  Connected,
  Rejected,  // 401 / 403
  Offline,   // no WiFi, no route, timeout, or the clock couldn't be set
  Error,     // TLS failure, 5xx, rate limit, or a response we couldn't read
};

const char* keyStateName(KeyState state);  // stable tokens for the web API: "connected", "rejected", ...

struct KeyStatus {
  KeyState state = KeyState::Unchecked;
  ApiError error = ApiError::None;
  MeInfo me;  // valid when Connected
};

// Interprets a /v1/me response.
KeyStatus keyStatusFrom(const ApiResponse& response);

}  // namespace lexipoint::api
