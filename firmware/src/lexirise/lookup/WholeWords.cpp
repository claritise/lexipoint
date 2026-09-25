#if LEXIRISE

#include "WholeWords.h"

#include <set>
#include <vector>

namespace lexipoint::lookup {
namespace {

// The refined occurrences exactly covering [start, end), in order; empty when they don't tile it (a piece that
// crosses the span's edge, or a gap).
std::vector<const api::Occurrence*> piecesOf(const api::AnalyzeResult& refined, const uint32_t start,
                                             const uint32_t end) {
  std::vector<const api::Occurrence*> pieces;
  uint32_t at = start;
  for (const api::Occurrence& piece : refined.occurrences) {
    if (piece.charEnd <= start || piece.charStart >= end) continue;
    if (piece.charStart != at || piece.charEnd > end) return {};
    pieces.push_back(&piece);
    at = piece.charEnd;
  }
  if (at != end) return {};
  return pieces;
}

}  // namespace

api::AnalyzeResult wholeWords(const api::AnalyzeResult& refined, api::AnalyzeResult words) {
  if (words.occurrences.empty()) return refined;
  std::set<uint32_t> ownEntries;  // the word-level answer's words: their saved state is its alone
  for (const api::Occurrence& word : words.occurrences) ownEntries.insert(word.entryId);
  std::vector<api::Occurrence> merged;
  merged.reserve(words.occurrences.size());
  for (const api::Occurrence& word : words.occurrences) {
    const std::vector<const api::Occurrence*> pieces = piecesOf(refined, word.charStart, word.charEnd);
    const api::EntryMeta* meta = words.metaFor(word.entryId);
    const bool ranked = meta != nullptr && meta->rank != 0;
    if (pieces.size() == 1) {
      merged.push_back(*pieces.front());  // the same token: the refined one has the lemma
    } else if (!pieces.empty() && !ranked) {
      // Cut up, but not a dictionary word Lexirise knows (一日中雨): the refined pieces are the better split.
      for (const api::Occurrence* piece : pieces) merged.push_back(*piece);
    } else {
      merged.push_back(word);  // a ranked whole word (深深, 一边, 小さな), or pieces that don't tile it
    }
  }
  words.occurrences = std::move(merged);
  // The word-level answer is at least as fresh as the (cached) refined one: its facts win, and it alone says
  // whether its own words are saved (a word unsaved since the refined answer was cached isn't shown as saved).
  // The refined answer fills in the rest (the pieces' entries).
  for (const auto& [id, meta] : refined.meta) words.meta.insert({id, meta});
  for (const auto& [id, state] : refined.state) {
    if (ownEntries.count(id) == 0) words.state.insert({id, state});
  }
  words.morphoPending = refined.morphoPending;
  return words;
}

}  // namespace lexipoint::lookup

#endif  // LEXIRISE
