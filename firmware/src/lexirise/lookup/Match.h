#pragma once

// Which analyzed occurrence a tap is on (lookup-flow.md §5 ②). Pure; tests: test/lexirise_lookup.

#include <cstdint>
#include <optional>
#include <vector>

#include "lexirise/api/Responses.h"

namespace lexipoint::lookup {

// The word-like occurrence covering the tap's start (charStart <= tapOffset < charEnd). A tap on
// punctuation or a gap takes the nearest word-like occurrence: the closer of the one ending before the
// tap and the one starting after it, the earlier on a tie. nullopt when the sentence has no word at all.
inline std::optional<size_t> matchOccurrence(const std::vector<api::Occurrence>& occurrences,
                                             const uint32_t tapOffset) {
  std::optional<size_t> before;
  std::optional<size_t> after;
  for (size_t i = 0; i < occurrences.size(); i++) {
    const api::Occurrence& occ = occurrences[i];
    if (!occ.wordLike) continue;
    if (occ.charStart <= tapOffset && tapOffset < occ.charEnd) return i;
    if (occ.charEnd <= tapOffset) {
      if (!before || occ.charEnd > occurrences[*before].charEnd) before = i;
    } else if (!after || occ.charStart < occurrences[*after].charStart) {
      after = i;
    }
  }
  if (!before) return after;
  if (!after) return before;
  const uint32_t leftGap = tapOffset - occurrences[*before].charEnd + 1;  // +1: charEnd is exclusive
  const uint32_t rightGap = occurrences[*after].charStart - tapOffset;
  return rightGap < leftGap ? after : before;
}

}  // namespace lexipoint::lookup
