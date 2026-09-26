#pragma once

// A book's tag (C2): config::kBookTagPrefix + a slug of its title, the same on every device. Tag names can't be
// deleted from a Lexirise account, so the slug is plain ASCII and one per book. Pure; tests:
// test/lexirise_language/BookSlugTest.cpp.

#include <cstdint>
#include <string>
#include <string_view>

namespace lexipoint::text {

// The title ASCII-folded: ASCII letters lower-cased and digits kept; every other run of bytes (spaces,
// punctuation, accented and CJK letters) one '-', none at either end. Past config::kBookSlugMaxBytes it's cut
// at a '-' in its second half, else there. With fewer than config::kBookSlugMinAlnum letters and digits (活着,
// 変身) it's "h" + the 8 hex digits of fnv1a32 of the title trimmed at both ends instead; an untitled book
// hashes `fallbackKey` (its path).
std::string bookSlug(std::string_view title, std::string_view fallbackKey);

// No title to go by: empty, or only spaces and control characters.
bool isUntitled(std::string_view title);

// config::kBookTagPrefix + bookSlug(title, fallbackKey).
std::string bookTag(std::string_view title, std::string_view fallbackKey);

// FNV-1a, 32 bits, over the bytes.
uint32_t fnv1a32(std::string_view bytes);

}  // namespace lexipoint::text
