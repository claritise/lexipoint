#if LEXIRISE

#include "ReleaseVersion.h"

#include <tuple>

namespace lexipoint::ota {
namespace {

constexpr std::string_view kLexiMarker = "-lexi.";  // scripts/lexipoint/release_tag.py LEXI_MARKER
constexpr int kMaxVersionComponent = 1000000;       // past this it isn't a version number

// A release candidate: "rc" right after the numbers (upstream's "1.6.5rc") or as a "-rc" part
// ("-lexi.2-rc+abc1234"), never inside another word ("-src").
bool isCandidate(const std::string_view rest) {
  return rest.substr(0, 2) == "rc" || rest.find("-rc") != std::string_view::npos;
}

// Reads a non-negative number at `pos`, advancing it; false when there's no digit there.
bool readNumber(const std::string_view text, size_t& pos, int& out) {
  const size_t start = pos;
  long value = 0;
  while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
    value = value * 10 + (text[pos] - '0');
    if (value > kMaxVersionComponent) return false;
    pos++;
  }
  out = static_cast<int>(value);
  return pos > start;
}

}  // namespace

std::optional<ReleaseVersion> parseReleaseVersion(std::string_view text) {
  if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
  ReleaseVersion v;
  size_t pos = 0;
  if (!readNumber(text, pos, v.major) || pos >= text.size() || text[pos++] != '.') return std::nullopt;
  if (!readNumber(text, pos, v.minor) || pos >= text.size() || text[pos++] != '.') return std::nullopt;
  if (!readNumber(text, pos, v.patch)) return std::nullopt;
  std::string_view rest = text.substr(pos);
  if (rest.substr(0, kLexiMarker.size()) == kLexiMarker) {
    size_t lexiPos = kLexiMarker.size();
    if (!readNumber(rest, lexiPos, v.lexi)) {
      if (lexiPos < rest.size() && rest[lexiPos] >= '0' && rest[lexiPos] <= '9') return std::nullopt;  // too big
      v.lexi = 0;  // "-lexi." with no number
    }
    rest.remove_prefix(lexiPos);
  }
  v.rc = isCandidate(rest);
  return v;
}

bool isNewerRelease(const std::string_view latest, const std::string_view current) {
  const auto l = parseReleaseVersion(latest);
  if (!l || l->lexi == 0 || l->rc) return false;  // upstream's, or not a release: never offered
  const auto c = parseReleaseVersion(current);
  if (!c) return true;  // an unversioned build: any release is newer
  const auto key = [](const ReleaseVersion& v) { return std::tie(v.major, v.minor, v.patch, v.lexi); };
  if (key(*l) != key(*c)) return key(*l) > key(*c);
  return c->rc;  // the release a candidate led to
}

}  // namespace lexipoint::ota

#endif  // LEXIRISE
