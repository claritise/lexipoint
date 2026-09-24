#pragma once

// LEXIPOINT: the web file manager's "is this path off limits" rule, shared with nothing Arduino so it
// is host-testable (test/lexirise_net/HiddenPathTest.cpp). Header-only and not LEXIRISE-gated: the
// file manager uses it in every build.

#include <string_view>

namespace lexipoint::web {

// True if any segment of the path is hidden (starts with '.'), the same rule as
// WebDAVHandler::isProtectedPath. "/.lexirise/config.ini", "/a/.x/b" and "/.." are hidden; "/a.b/c" is not.
inline bool isHiddenWebPath(const std::string_view path) {
  size_t start = 0;
  while (start < path.size()) {
    if (path[start] == '/') {
      start++;
      continue;
    }
    if (path[start] == '.') return true;
    const size_t slash = path.find('/', start);
    if (slash == std::string_view::npos) break;
    start = slash + 1;
  }
  return false;
}

}  // namespace lexipoint::web
