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
#include <string>
#include <string_view>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::web {

// What the card says about a path prefix ("/LEXIRI~1").
struct NameLookupResult {
  enum class Kind {
    Missing,     // no such entry: it can't be an alias (the request 404s, or creates a new name)
    Found,       // `name` is the entry's real (long) name
    Unreadable,  // it exists but its name couldn't be read: refused, to be safe
  };
  Kind kind = Kind::Missing;
  std::string name;
};
using NameLookup = std::function<NameLookupResult(std::string_view prefix)>;

// True if a segment of `path` is hidden as typed ("/.lexirise/config.ini", "/a/.x/b", "/.."; not
// "/a.b/c"), or, given a lookup, is a short-name alias of a hidden entry.
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
      const NameLookupResult entry = lookup(path.substr(0, end));
      if (entry.kind == NameLookupResult::Kind::Unreadable) return true;
      if (entry.kind == NameLookupResult::Kind::Found && (entry.name.empty() || entry.name.front() == '.')) {
        return true;
      }
    }
    start = end + 1;
  }
  return false;
}

// Reads an open entry's long name through a getName(buffer, size)-style call (SdFat's FsFile::getName
// returns 0 and an empty name when the buffer is too small, so the buffer fits the longest FAT name).
inline NameLookupResult readEntryName(const std::function<size_t(char*, size_t)>& getName) {
  std::string buffer(config::kMaxFatNameBytes + 1, '\0');
  const size_t len = getName(buffer.data(), buffer.size());
  if (len == 0 || len >= buffer.size()) return {NameLookupResult::Kind::Unreadable, {}};
  buffer.resize(len);
  return {NameLookupResult::Kind::Found, std::move(buffer)};
}

// The device's check against the SD card (HiddenPathHal.cpp).
bool isHiddenOnCard(const char* path);

}  // namespace lexipoint::web
