#include "PageSentences.h"

#include <Logging.h>

#include <string>

#include "lexirise/vocab/VocabMirror.h"

namespace lexipoint::page {

std::optional<DescribedPage> describePage(const text::PageModel& model, const text::BookLanguage& book,
                                          const Settings& settings, const std::string_view bookPath,
                                          const uint32_t spine, const uint32_t start) {
  std::optional<text::BuiltSentence> built = text::pageTextOf(model);
  if (!built) return std::nullopt;
  const text::LanguageDecision decision = book.decide(book.dependsOnSentence() ? built->text : std::string(), settings);
  if (!decision.language) return std::nullopt;
  DescribedPage out;
  out.page.key = PageKey{bookKey(bookPath), spine, start};
  out.page.language = *decision.language;
  out.page.units = text::utf16Length(built->text);
  out.page.text = built->text;
  out.built = std::move(*built);
  return out;
}

std::optional<api::AnalyzeResult> PageSentences::analysisOf(const text::TapContext& tap) {
  if (!tap.sentence || !tap.language.language || *tap.language.language != language_) return std::nullopt;
  if (!read_) {
    read_ = true;
    page_ = store_.read(key_, language_, text::utf16Length(pageText_.text), textHash(pageText_.text));
    LOG_INF(kLogTag, "card: page %u-%u %s", static_cast<unsigned>(key_.spine), static_cast<unsigned>(key_.start),
            page_ ? "analyzed: no analyze/text for its sentences" : "not analyzed");
  }
  if (!page_) return std::nullopt;
  const std::optional<uint32_t> start = text::pageOffsetOf(pageText_, *tap.sentence);
  if (!start) return std::nullopt;
  std::optional<api::AnalyzeResult> out = sliceSentence(*page_, *start, *start + text::utf16Length(tap.sentence->text));
  if (out && mirror_) applyMirrorStates(*out, language_, page_->analyzedMs, *mirror_);
  return out;
}

namespace {

// A sentence's entry `id` as the mirror says it: saved (`state`) or not (nullopt).
void take(api::AnalyzeResult& sentence, const uint32_t id, std::optional<api::EntryState> state) {
  if (!state) {
    sentence.state.erase(id);
    return;
  }
  const api::EntryState* snapshot = sentence.stateFor(id);
  state->seenCount = snapshot ? snapshot->seenCount : 0;
  sentence.state[id] = std::move(*state);
}

MirrorSays saysOf(const std::optional<api::EntryState>& state, const bool suspended) {
  MirrorSays out;
  if (!state) {
    out.verdict = MirrorSays::Verdict::Unsaved;
    return out;
  }
  out.verdict = MirrorSays::Verdict::Saved;
  out.state = *state;
  out.suspended = suspended;
  return out;
}

std::optional<api::EntryState> stateOf(const vocab::LiveState& live) {
  if (!live.saved) return std::nullopt;
  api::EntryState state;
  state.savedExpressionId = std::to_string(live.savedId);
  state.proficiency = live.proficiency;
  return state;
}

}  // namespace

MirrorView::MirrorView(const Language language, const uint64_t analyzedMs, vocab::VocabStore& mirror)
    : language_(language), analyzedMs_(analyzedMs), mirror_(mirror), loaded_(mirror.loaded(language)) {
  if (!loaded_) return;
  const vocab::SyncState sync = mirror.syncState(language);
  // At its cap (overflowed: it refused an entry) the mirror isn't complete whatever its time: absence keeps the
  // snapshot.
  const uint32_t lastSyncS = sync.overflowed ? 0 : sync.lastSyncS;
  complete_ = lastSyncS != 0 && analyzedMs != 0 && lastSyncS >= analyzedMs / timing::kMsPerSecond;
}

MirrorSays MirrorView::says(const uint32_t id) const {
  if (id == 0) return {};
  if (!loaded_) {
    const std::optional<vocab::LiveState> pending = mirror_.pendingState(language_, id);
    if (pending && vocab::mirrorOutranks(pending->asOfS, pending->own, analyzedMs_)) {
      return saysOf(stateOf(*pending), false);
    }
    return {};
  }
  const std::optional<vocab::Entry> entry = mirror_.find(language_, id);
  if (entry) {
    if (vocab::mirrorOutranks(entry->asOfS, entry->own, analyzedMs_))
      return saysOf(vocab::savedStateOf(*entry), entry->suspended);
    return {};
  }
  if (complete_) return saysOf(std::nullopt, false);  // not in the account as of a time after the page
  return {};
}

// One rule (V7b R5): each word's state is the newer of the mirror's (its time, vocab::mirrorOutranks) and the page's
// snapshot (its analysis time); a word the mirror doesn't hold is unsaved only once the mirror is complete as of a
// time after the page (SyncState::lastSyncS), else the snapshot stands. Before the mirror loads, the reader's own
// answers and writes waiting for it (VocabStore::pendingState) take the entry's place.
void applyMirrorStates(api::AnalyzeResult& sentence, const Language language, const uint64_t analyzedMs,
                       vocab::VocabStore& mirror) {
  const MirrorView view(language, analyzedMs, mirror);
  for (const api::Occurrence& occ : sentence.occurrences) {
    for (const uint32_t id : {occ.entryId, occ.lemmaEntryId}) {
      const MirrorSays says = view.says(id);
      if (says.verdict == MirrorSays::Verdict::Saved) take(sentence, id, says.state);
      if (says.verdict == MirrorSays::Verdict::Unsaved) take(sentence, id, std::nullopt);
    }
  }
}

}  // namespace lexipoint::page
