#pragma once

// LEXIPOINT: the web file manager's and WebDAV's "is this path off limits" rule. Pure and header-only
// (host-tested in test/lexirise_net/HiddenPathTest.cpp); HiddenPathHal.cpp supplies the SD card lookup.
// Not LEXIRISE-gated: upstream code uses it in every build.
//
// A path is hidden if any segment is: its name starts with '.'. On FAT the same entry can also be
// reached by its 8.3 short name, and a dot name's short name never starts with a dot (".lexirise" is
// also "LEXIRI~1"). Such a name is never valid 8.3, so SdFat (and every other FAT writer) gives it a
// "~N" tail: only segments containing '~' can be aliases, and only those are looked up on the card.

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace lexipoint::web {

// Given a path prefix ("/LEXIRI~1"), the entry's real (long) name, or nullopt if it doesn't exist.
using NameLookup = std::function<std::optional<std::string>(std::string_view prefix)>;

// True if a segment of `path` is hidden as typed ("/.lexirise/config.ini", "/a/.x/b", "/.."; not
// "/a.b/c"), or, given a lookup, is a short-name alias of a hidden entry. An alias that can't be
// resolved is refused too: that costs a 403 instead of a 404, never access.
inline bool isHiddenPath(const std::string_view path, const NameLookup& lookup = nullptr) {
  size_t start = 0;
  while (start < path.size()) {
    if (path[start] == '/') {
      start++;
      continue;
    }
    const size_t slash = path.find('/', start);
    const size_t end = slash == std::string_view::npos ? path.size() : slash;
    const std::string_view segment = path.substr(start, end - start);
    if (segment.front() == '.') return true;
    if (lookup && segment.find('~') != std::string_view::npos) {
      const std::optional<std::string> name = lookup(path.substr(0, end));
      if (!name || name->empty() || name->front() == '.') return true;
    }
    start = end + 1;
  }
  return false;
}

// The device's check against the SD card (HiddenPathHal.cpp).
bool isHiddenOnCard(const char* path);

}  // namespace lexipoint::web
