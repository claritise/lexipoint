#pragma once

// LEXIPOINT: the web file manager's and WebDAV's "is this path off limits" rule. Pure and header-only
// (host-tested in test/lexirise_net/HiddenPathTest.cpp); HiddenPathHal.cpp supplies the SD card lookup.
// Not LEXIRISE-gated: CrossPoint's code uses it in every build.
//
// A path is hidden if any segment is: its name starts with '.'. "Its name" is what SdFat opens, not
// what was typed: SdFat skips a segment's leading spaces and trims trailing dots and spaces (FatFile /
// ExFatFile::parsePathName), so "/ .lexirise" opens "/.lexirise" and is checked as that.
//
// On FAT the same entry can also be reached by its 8.3 short name, and a dot name's short name never
// starts with a dot (".lexirise" is also "LEXIRI~1"). Such a name isn't valid 8.3, so SdFat (which
// creates /.lexirise) and Windows give it a "~N" tail: segments containing '~' are looked up on the card
// and checked by their real name. (A folder recreated on a Linux vfat mount with "nonumtail" could get
// a tail-less short name; the firmware always creates it with SdFat.)

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

// A segment as SdFat parses it: leading spaces skipped, trailing dots and spaces trimmed.
inline std::string_view sdfatSegment(std::string_view segment) {
  while (!segment.empty() && segment.front() == ' ') segment.remove_prefix(1);
  while (!segment.empty() && (segment.back() == '.' || segment.back() == ' ')) segment.remove_suffix(1);
  return segment;
}

// True if a segment of `path` names a hidden entry ("/.lexirise/config.ini", "/a/.x/b", "/..",
// "/ .lexirise"; not "/a.b/c"), or, given a lookup, is a short-name alias of one. A segment that is
// only dots or spaces is refused too (SdFat can't open it, and it's never a real name).
inline bool isHiddenPath(const std::string_view path, const NameLookup& lookup = nullptr) {
  size_t start = 0;
  while (start < path.size()) {
    if (path[start] == '/') {
      start++;
      continue;
    }
    const size_t slash = path.find('/', start);
    const size_t end = slash == std::string_view::npos ? path.size() : slash;
    // Leading dots survive SdFat's trimming, so a dot name keeps its dot; "." and ".." trim to empty.
    const std::string_view segment = sdfatSegment(path.substr(start, end - start));
    if (segment.empty() || segment.front() == '.') return true;
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
