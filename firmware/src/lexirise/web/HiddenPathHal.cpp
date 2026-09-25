// LEXIPOINT: HiddenPath.h's card lookup. Not LEXIRISE-gated (the file manager and WebDAV use it in
// every build).

#include <HalStorage.h>

#include <string>

#include "HiddenPath.h"

namespace lexipoint::web {
namespace {

NameLookupResult lookupOnCard(const std::string_view prefix) {
  const std::string path(prefix);
  if (!Storage.exists(path.c_str())) return {};  // Missing
  HalFile entry = Storage.open(path.c_str());
  if (!entry) return {NameLookupResult::Kind::Unreadable, {}};
  return readEntryName([&entry](char* name, const size_t size) { return entry.getName(name, size); });
}

}  // namespace

bool isHiddenOnCard(const char* path) { return isHiddenPath(path, lookupOnCard); }

}  // namespace lexipoint::web
