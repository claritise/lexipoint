#pragma once

// Cross-site protection for /api/lexirise (settings.md §2). CrossPoint's web server allows
// every origin (enableCORS) and has no login, so any page open in a browser on the same WiFi could
// otherwise POST new Lexirise settings or read the account name. Browsers always send Origin on a
// cross-origin request; tools like curl and the dev harness send none. Pure, header-only; tests:
// test/lexirise_net/OriginTest.cpp.

#include <string_view>

namespace lexipoint::web {

// origin: the request's Origin header ("" when absent). host: its Host header ("192.168.1.5",
// "crosspoint.local:80"). A request is allowed with no Origin, or when Origin is http://<host>.
inline bool isSameOriginRequest(std::string_view origin, std::string_view host) {
  if (origin.empty()) return true;
  constexpr std::string_view kHttp = "http://";
  if (origin.substr(0, kHttp.size()) != kHttp || host.empty()) return false;
  origin.remove_prefix(kHttp.size());
  constexpr std::string_view kDefaultPort = ":80";
  const auto stripDefaultPort = [&](std::string_view s) {
    if (s.size() > kDefaultPort.size() && s.substr(s.size() - kDefaultPort.size()) == kDefaultPort) {
      s.remove_suffix(kDefaultPort.size());
    }
    return s;
  };
  const std::string_view a = stripDefaultPort(origin);
  const std::string_view b = stripDefaultPort(host);
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {  // host names are case-insensitive
    char x = a[i];
    char y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

}  // namespace lexipoint::web
