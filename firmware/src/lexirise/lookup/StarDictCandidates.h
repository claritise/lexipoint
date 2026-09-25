#pragma once

// What to look up in StarDict for a tap (lookup-flow.md §4). A Japanese/Chinese token is one character,
// but StarDict needs a word: try the longest run of word characters (text::chars::isJaZhWordChar) from
// the tapped one along the line (up to
// config::kStarDictMaxPrefixChars), then one shorter, … down to the character itself. A Latin token is
// the word. Pure; tests: test/lexirise_lookup.

#include <Utf8.h>

#include <string>
#include <vector>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/text/CharClass.h"
#include "lexirise/text/SentenceBuilder.h"

namespace lexipoint::lookup {

inline std::vector<std::string> starDictCandidates(const text::TextLine& line, const size_t token) {
  if (token >= line.tokens.size()) return {};
  // The run of word characters from the tap, one codepoint at a time.
  std::vector<std::string> chars;
  bool cjk = true;
  for (size_t t = token; t < line.tokens.size() && chars.size() < config::kStarDictMaxPrefixChars; t++) {
    const auto* p = reinterpret_cast<const unsigned char*>(line.tokens[t].c_str());
    bool stop = false;
    while (const uint32_t cp = utf8NextCodepoint(&p)) {
      if (!text::chars::isJaZhWordChar(cp)) {
        stop = true;
        break;
      }
      std::string ch;
      utf8AppendCodepoint(cp, ch);
      chars.push_back(std::move(ch));
      if (chars.size() >= config::kStarDictMaxPrefixChars) break;
    }
    if (t == token && chars.empty()) cjk = false;
    if (stop) break;
  }
  if (!cjk) return {line.tokens[token]};  // Latin (or a punctuation tap): the token is the word
  std::vector<std::string> out;
  for (size_t n = chars.size(); n >= 1; n--) {
    std::string candidate;
    for (size_t i = 0; i < n; i++) candidate += chars[i];
    out.push_back(std::move(candidate));
  }
  return out;
}

enum class ProbeResult { Found, NotFound, Error };

// Tries each candidate in order until one is found. A read error (anything but "not found") stops the
// probing: the caller reports it rather than masking it with a shorter word.
template <typename LookupFn>
ProbeResult probeStarDict(const std::vector<std::string>& candidates, LookupFn&& lookup) {
  for (const std::string& candidate : candidates) {
    const ProbeResult result = lookup(candidate);
    if (result != ProbeResult::NotFound) return result;
  }
  return ProbeResult::NotFound;
}

}  // namespace lexipoint::lookup
