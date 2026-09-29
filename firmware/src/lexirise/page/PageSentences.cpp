#include "PageSentences.h"

#include <Logging.h>

#include <string>

#include "lexirise/vocab/VocabMirror.h"

namespace lexipoint::page {

std::optional<DescribedPage> describePage(const text::PageModel& model, const text::BookLanguage& book,
                                          const Settings& settings, const std::string_view bookPath,
                                          const uint32_t spine, const uint32_t start) {
  // The text doesn't depend on the script (only a sentence's cuts do): the CJK superset's pieces join the same.
  std::optional<text::BuiltSentence> built = text::buildPageText(model, text::Script::Japanese);
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

std::optional<api::EntryState> stateOf(const vocab::LiveState& live) {
  if (!live.saved) return std::nullopt;
  api::EntryState state;
  state.savedExpressionId = std::to_string(live.savedId);
  state.proficiency = live.proficiency;
  return state;
}

}  // namespace

// One rule (V7b R5): each word's state is the newer of the mirror's (its time, vocab::mirrorOutranks) and the page's
// snapshot (its analysis time); a word the mirror doesn't hold is unsaved only once the mirror is complete as of a
// time after the page (SyncState::lastSyncS), else the snapshot stands. Before the mirror loads, the reader's own
// answers and writes waiting for it (VocabStore::pendingState) take the entry's place.
void applyMirrorStates(api::AnalyzeResult& sentence, const Language language, const uint64_t analyzedMs,
                       vocab::VocabStore& mirror) {
  const bool loaded = mirror.loaded(language);
  const vocab::SyncState sync = loaded ? mirror.syncState(language) : vocab::SyncState{};
  // At its cap (overflowed: it refused an entry) the mirror isn't complete whatever its time: absence keeps the
  // snapshot.
  const uint32_t lastSyncS = sync.overflowed ? 0 : sync.lastSyncS;
  const bool mirrorComplete = lastSyncS != 0 && analyzedMs != 0 && lastSyncS >= analyzedMs / timing::kMsPerSecond;
  for (const api::Occurrence& occ : sentence.occurrences) {
    for (const uint32_t id : {occ.entryId, occ.lemmaEntryId}) {
      if (id == 0) continue;
      if (!loaded) {
        const std::optional<vocab::LiveState> pending = mirror.pendingState(language, id);
        if (pending && vocab::mirrorOutranks(pending->asOfS, pending->own, analyzedMs))
          take(sentence, id, stateOf(*pending));
        continue;
      }
      const std::optional<vocab::Entry> entry = mirror.find(language, id);
      if (entry) {
        if (vocab::mirrorOutranks(entry->asOfS, entry->own, analyzedMs))
          take(sentence, id, vocab::savedStateOf(*entry));
      } else if (mirrorComplete) {
        sentence.state.erase(id);  // not in the account as of a time after the page
      }
    }
  }
}

}  // namespace lexipoint::page
