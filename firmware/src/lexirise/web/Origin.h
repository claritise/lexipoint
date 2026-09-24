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

// DNS-rebinding guard: a hostile domain pointed at the device's IP would send a matching Origin and
// Host, so /api/lexirise also requires the Host to be how the device is actually reached: an IPv4
// literal (its LAN or hotspot address) or an mDNS "<name>.local", each with an optional port.
inline bool isTrustedHost(std::string_view host) {
  const size_t colon = host.rfind(':');
  if (colon != std::string_view::npos) {
    const std::string_view port = host.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    for (const char c : port) {
      if (c < '0' || c > '9') return false;
    }
    host = host.substr(0, colon);
  }
  if (host.empty()) return false;

  // IPv4: four decimal parts, each 0-255.
  int parts = 0;
  int value = -1;
  bool ipv4 = true;
  for (const char c : host) {
    if (c == '.') {
      if (value < 0) ipv4 = false;
      parts++;
      value = -1;
    } else if (c >= '0' && c <= '9') {
      value = (value < 0 ? 0 : value * 10) + (c - '0');
      if (value > 255) ipv4 = false;
    } else {
      ipv4 = false;
    }
  }
  if (ipv4 && parts == 3 && value >= 0) return true;

  constexpr std::string_view kMdns = ".local";
  if (host.size() <= kMdns.size()) return false;
  std::string_view suffix = host.substr(host.size() - kMdns.size());
  for (size_t i = 0; i < suffix.size(); i++) {
    const char c = suffix[i] >= 'A' && suffix[i] <= 'Z' ? static_cast<char>(suffix[i] - 'A' + 'a') : suffix[i];
    if (c != kMdns[i]) return false;
  }
  const std::string_view name = host.substr(0, host.size() - kMdns.size());
  for (const char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) return false;  // one label only: "evil.example.local" style names are refused too
  }
  return name.front() != '-';
}

}  // namespace lexipoint::web
