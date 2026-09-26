#if LEXIRISE

#include "BookSlug.h"

#include <cstdio>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::text {
namespace {

constexpr uint32_t kFnvOffsetBasis = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;
constexpr size_t kHashSlugBytes = 9;  // "h" + 8 hex digits

// A space or a control character.
bool isBlank(const char c) { return static_cast<unsigned char>(c) <= 0x20 || c == 0x7F; }

std::string_view trim(std::string_view s) {
  while (!s.empty() && isBlank(s.front())) s.remove_prefix(1);
  while (!s.empty() && isBlank(s.back())) s.remove_suffix(1);
  return s;
}

std::string hashSlug(const std::string_view key) {
  char out[kHashSlugBytes + 1];
  std::snprintf(out, sizeof(out), "h%08x", static_cast<unsigned>(fnv1a32(key)));
  return out;
}

}  // namespace

uint32_t fnv1a32(const std::string_view bytes) {
  uint32_t hash = kFnvOffsetBasis;
  for (const char c : bytes) {
    hash ^= static_cast<unsigned char>(c);
    hash *= kFnvPrime;
  }
  return hash;
}

bool isUntitled(const std::string_view title) { return trim(title).empty(); }

std::string bookSlug(std::string_view title, const std::string_view fallbackKey) {
  title = trim(title);
  if (title.empty()) return hashSlug(fallbackKey);

  std::string slug;
  slug.reserve(title.size() < config::kBookSlugMaxBytes ? title.size() : config::kBookSlugMaxBytes + 1);
  size_t alnum = 0;
  for (const char c : title) {
    if (slug.size() > config::kBookSlugMaxBytes) break;  // one byte past the cap shows whether a word was cut
    if (c >= 'A' && c <= 'Z') {
      slug.push_back(static_cast<char>(c - 'A' + 'a'));
    } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      slug.push_back(c);
    } else {
      if (!slug.empty() && slug.back() != '-') slug.push_back('-');
      continue;
    }
    alnum++;
  }
  if (slug.size() > config::kBookSlugMaxBytes) {
    const size_t dash = slug.rfind('-', config::kBookSlugMaxBytes);
    slug.resize(dash != std::string::npos && dash >= config::kBookSlugMaxBytes / 2 ? dash : config::kBookSlugMaxBytes);
  }
  while (!slug.empty() && slug.back() == '-') slug.pop_back();
  if (alnum < config::kBookSlugMinAlnum) return hashSlug(title);
  return slug;
}

std::string bookTag(const std::string_view title, const std::string_view fallbackKey) {
  return config::kBookTagPrefix + bookSlug(title, fallbackKey);
}

}  // namespace lexipoint::text

#endif  // LEXIRISE
