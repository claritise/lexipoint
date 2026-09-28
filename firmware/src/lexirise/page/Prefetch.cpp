#if LEXIRISE

#include "Prefetch.h"

#include <Logging.h>

#include <algorithm>

#include "lexirise/api/AccessPolicy.h"
#include "lexirise/api/Requests.h"
#include "lexirise/util/Timing.h"
#include "lexirise/vocab/VocabMirror.h"

namespace lexipoint::page {
namespace {

// One analyze/text call for the page, streamed.
api::ApiResponse ask(api::LexiriseApi& api, const PageText& text, const bool fast, PageAnalysis& out,
                     const net::Abort abort) {
  PageReader reader(text.language, nullptr, text.units);
  api::ApiResponse response = api.analyzePage(api::analyzePageRequest(text.language, text.text, fast), reader, abort);
  if (!response.ok()) return response;
  if (reader.finish(out) != api::ParseStatus::Ok) response.error = api::ApiError::Malformed;
  return response;
}

// What the page's answer says about each word's entries, for the vocab mirror (vocab::addLiveState, as the card's
// analysis), known as of the page's analysis (`asOfS`; 0: unknown).
std::vector<vocab::LiveState> liveStatesOf(const PageAnalysis& page, const uint32_t asOfS) {
  const bool whole = !page.statesCut;
  std::vector<vocab::LiveState> live;
  live.reserve(page.occurrences.size() * 2);
  for (const Occ& o : page.occurrences) {
    if (!o.wordLike) continue;
    for (const uint32_t id : {o.lemmaEntryId, o.entryId}) {
      const State* s = id != 0 ? page.stateFor(id) : nullptr;
      std::optional<api::EntryState> saved;
      if (s) {
        api::EntryState state;
        state.savedExpressionId = page.str(s->savedId);
        state.proficiency = s->proficiency;
        saved = std::move(state);
      }
      vocab::addLiveState(live, page.language, id, saved ? &*saved : nullptr, whole);
    }
  }
  for (vocab::LiveState& l : live) l.asOfS = asOfS;
  return live;
}

}  // namespace

void PagePrefetcher::shown(const uint32_t spine, const uint32_t start, const unsigned long drawnMs) {
  if (known_ && spine == spine_ && start == start_) {
    // Drawn again: the dwell counts from this drawing. The same drawing, told again on the next pass, changes nothing
    // (a call given up for input restarted the dwell from its end: that holds).
    if (drawnMs != lastDrawnMs_) shownMs_ = drawnMs;
    lastDrawnMs_ = drawnMs;
    return;
  }
  known_ = true;
  spine_ = spine;
  start_ = start;
  shownMs_ = drawnMs;
  lastDrawnMs_ = drawnMs;
  done_[0] = done_[1] = false;
}

bool PagePrefetcher::due(const unsigned long nowMs, const Conditions& c) const {
  if (!known_ || (done_[0] && done_[1])) return false;
  if (!timing::reached(nowMs, shownMs_ + config::kPagePrefetchDwellMs)) return false;
  if (waitUntilMs_ && !timing::reached(nowMs, *waitUntilMs_)) return false;
  return c.wifiUp && !c.busy && c.usable && !c.blocked && budgetLeft(c.usedLastHour, c.rateLimit);
}

PagePrefetcher::Step PagePrefetcher::step(PageTexts& texts, const net::Abort abort) {
  Step out;
  // Input already there (a button still held, a finger down): no SD read, no call; the reader has it next pass.
  if (abort && abort()) return out;
  const int which = done_[0] ? 1 : 0;
  out.which = which;
  const std::optional<PageText> text = texts.textOf(which);
  if (!text || text->units == 0 || text->units > config::kPageMaxTextUnits) {
    done_[which] = true;
    out.kind = Step::Kind::NoText;
    return out;
  }
  const uint32_t hash = textHash(text->text);
  if (std::optional<PageAnalysis> cached = store_.read(text->key, text->language, text->units, hash)) {
    done_[which] = true;
    out.kind = Step::Kind::Cached;
    out.occurrences = cached->occurrences.size();
    out.refined = cached->refined;
    return out;
  }
  const unsigned long callStart = clock_();
  PageAnalysis page;
  api::ApiResponse response = ask(api_, *text, /*fast=*/false, page, abort);
  // Calls counted as the log shows them answered, with an HTTP status (page-smoke counts the same way): one given up
  // for input, or failing before an answer (no memory, no connection, a timeout), isn't.
  out.calls += response.status != 0 && response.error != api::ApiError::Cancelled ? 1 : 0;
  if (response.ok() && !page.morphoPending) {
    // Lexirise has refined this text before (the prefetch itself makes it, ~1.5-2.5 min later): its answer cuts
    // words into morphemes, so V1's word-level split puts them back. Without it the page isn't kept.
    PageAnalysis words;
    const api::ApiResponse fast = ask(api_, *text, /*fast=*/true, words, abort);
    out.calls += fast.status != 0 && fast.error != api::ApiError::Cancelled ? 1 : 0;
    if (fast.ok()) {
      page = mergeWholeWords(page, words);
    } else {
      response = fast;
    }
  }
  const unsigned long callEnd = clock_();
  // Stamped after the call (it may have set the clock): the time the page's answer was known.
  const uint64_t epochMs = static_cast<uint64_t>(wall_ ? wall_() : 0) * timing::kMsPerSecond;
  out.callMs = callEnd - callStart;
  out.error = response.error;
  if (response.error == api::ApiError::Cancelled) {
    out.kind = Step::Kind::Cancelled;
    shownMs_ = callEnd;  // the reader has input: the page must stay up a whole dwell again first
    return out;
  }
  page.textUnits = text->units;
  page.textHash = hash;
  page.analyzedMs = epochMs;
  if (response.error == api::ApiError::Malformed || (response.ok() && !fitsFile(page))) {
    // Unreadable or over a cap (a merge can pass one): asking again won't change it, not this time on this page.
    LOG_ERR(kLogTag, "page %u-%u: its answer can't be kept (%s)", unsigned(text->key.spine), unsigned(text->key.start),
            response.ok() ? "over a cap" : api::apiErrorName(response.error));
    out.kind = Step::Kind::Unusable;
    done_[which] = true;
    return out;
  }
  if (!response.ok()) {
    out.kind = Step::Kind::Failed;
    waitUntilMs_ = response.error == api::ApiError::RateLimited ? api::retryAtMs(response, callEnd)
                                                                : callEnd + config::kPageFailureWaitMs;
    return out;
  }
  waitUntilMs_.reset();
  out.kind = Step::Kind::Analyzed;
  out.occurrences = page.occurrences.size();
  out.refined = page.refined;
  out.written = store_.write(text->key, page);
  out.writeMs = clock_() - callEnd;
  if (!out.written) LOG_ERR(kLogTag, "page %u-%u not written", unsigned(text->key.spine), unsigned(text->key.start));
  done_[which] = true;
  // A live answer, as a card's (the live answer wins: VocabStore::record, no SD I/O here), for a mirror loaded already:
  // one not loaded yet keeps only the newest card answers (config::kVocabPendingMax), which a page's would push out.
  if (mirror_ && mirror_->loaded(page.language))
    mirror_->record(liveStatesOf(page, static_cast<uint32_t>(epochMs / timing::kMsPerSecond)));
  return out;
}

bool PagePass::ready(const Pass& pass, const unsigned long nowMs, PageStarts& starts) {
  // Only a page on screen and drawn counts (its dwell counts from its latest drawing: a card closed over it redraws
  // it); until then nothing is due.
  if (!pass.onScreen || pass.drawnMs == 0) return false;
  // The cheap gates first: nothing reads the section's file for a page that won't be analyzed (shown() later still
  // counts the dwell from the drawing).
  if (!pass.reader.usable || !pass.reader.wifiConnected) return false;
  if (pass.drawnMs != startForMs_) {
    if (pass.reader.rendering) return false;  // a render under way: next pass
    const std::optional<uint32_t> start = starts.startOf();
    if (!start) return false;
    start_ = *start;
    startForMs_ = pass.drawnMs;
  }
  prefetch_.shown(pass.spine, start_, pass.drawnMs);
  return prefetch_.due(nowMs, readerConditions(pass.reader));
}

}  // namespace lexipoint::page

#endif  // LEXIRISE
