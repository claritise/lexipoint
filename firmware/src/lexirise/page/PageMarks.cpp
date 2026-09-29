#include "PageMarks.h"

#include <algorithm>
#include <string>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint::page {

Mark markFor(const WordState& word) {
  if (word.ignored || word.suspended) return Mark::None;
  return markForLevel(word.saved, word.proficiency);
}

namespace {

// Entry `id`'s saved state: the mirror's when it speaks, else the page's snapshot. nullopt: not saved.
std::optional<WordState> savedOf(const PageAnalysis& page, const uint32_t id, const MarkSources& sources) {
  if (id == 0) return std::nullopt;
  const MirrorSays says = sources.mirror(id);
  if (says.verdict == MirrorSays::Verdict::Unsaved) return std::nullopt;
  if (says.verdict == MirrorSays::Verdict::Saved) {
    WordState out;
    out.saved = true;
    out.proficiency = says.state.proficiency;
    out.suspended = says.suspended;
    return out;
  }
  const State* snapshot = page.stateFor(id);
  if (!snapshot || snapshot->savedId.len == 0) return std::nullopt;
  WordState out;
  out.saved = true;
  out.proficiency = snapshot->proficiency;
  return out;
}

}  // namespace

WordState wordStateOf(const PageAnalysis& page, const Occ& occ, const MarkSources& sources) {
  const uint32_t key = occ.lemmaEntryId != 0 ? occ.lemmaEntryId : occ.entryId;  // lookup::entryKeyOf
  std::optional<WordState> state = savedOf(page, key, sources);
  if (!state && key != occ.entryId) state = savedOf(page, occ.entryId, sources);
  WordState out = state.value_or(WordState{});
  const std::optional<IgnoredKey> ignoredKey = ignoredKeyFor(page.language, key, page.str(occ.lemma));
  out.ignored = ignoredKey && sources.ignored(*ignoredKey);
  return out;
}

std::vector<SpanMark> pageMarks(const PageAnalysis& page, const MarkSources& sources) {
  std::vector<SpanMark> out;
  out.reserve(page.occurrences.size());
  for (const Occ& occ : page.occurrences) {
    if (!occ.wordLike || occ.end <= occ.start) continue;
    const Mark mark = markFor(wordStateOf(page, occ, sources));
    if (mark != Mark::None) out.push_back({occ.start, occ.end, mark});
  }
  return out;
}

std::vector<MarkRun> markRuns(const card::ReaderPage& page, const text::BuiltSentence& pageText,
                              const std::vector<SpanMark>& marks, const card::TextMetrics& metrics) {
  std::vector<MarkRun> out;
  out.reserve(marks.size());
  for (const SpanMark& mark : marks) {
    // One run per line: the word's characters on it (each its own token) and the gaps between them.
    std::vector<MarkRun> lines;
    for (const card::PieceBox& piece : card::pieceBoxes(page, pageText, mark.start, mark.end, metrics)) {
      const auto line =
          std::find_if(lines.begin(), lines.end(), [&piece](const MarkRun& run) { return run.box.y == piece.box.y; });
      if (line == lines.end()) {
        lines.push_back({piece.box, mark.mark});
        continue;
      }
      const int right = std::max(line->box.right(), piece.box.right());
      line->box.x = std::min(line->box.x, piece.box.x);
      line->box.w = right - line->box.x;
    }
    out.insert(out.end(), lines.begin(), lines.end());
  }
  return out;
}

void markFills(const MarkRun& run, const int ascender, const int belowBaseline, std::vector<card::Rect>& out) {
  const int left = run.box.x + config::kMarkInset;
  const int right = run.box.right() - config::kMarkInset;
  const int y = run.box.y + ascender + belowBaseline;
  if (run.mark == Mark::New) {
    if (right > left) out.push_back({left, y, right - left, config::kMarkThickness});
    return;
  }
  if (run.mark != Mark::Learning) return;
  for (int x = left; x + config::kMarkDot <= right; x += config::kMarkDotPitch) {
    out.push_back({x, y, config::kMarkDot, config::kMarkDot});
  }
}

}  // namespace lexipoint::page
