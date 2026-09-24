// LEXIPOINT: HiddenPath.h's card lookup. Not LEXIRISE-gated (the file manager and WebDAV use it in
// every build).

#include <HalStorage.h>

#include <string>

#include "HiddenPath.h"

namespace lexipoint::web {
namespace {

// Only the first character matters to the rule, so a short buffer is enough even for long names.
constexpr size_t kNameProbeBytes = 16;

std::optional<std::string> longNameOnCard(const std::string_view prefix) {
  const std::string path(prefix);
  HalFile entry = Storage.open(path.c_str());
  if (!entry) return std::nullopt;
  char name[kNameProbeBytes] = {};
  const size_t len = entry.getName(name, sizeof(name));
  return std::string(name, len < sizeof(name) ? len : sizeof(name) - 1);
}

}  // namespace

bool isHiddenOnCard(const char* path) { return isHiddenPath(path, longNameOnCard); }

}  // namespace lexipoint::web
