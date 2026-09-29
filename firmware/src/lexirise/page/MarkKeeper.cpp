#include "MarkKeeper.h"

#include <Logging.h>
#include <Memory.h>

#include <algorithm>

namespace lexipoint::page {

void MarkKeeper::resetLocked() {
  slots_.clear();
  cardMarks_.drop();
  wanted_ = {};
  onScreen_.reset();
  gate_ = MarkGate{};
}

void MarkKeeper::open(const bool settingsShow, const bool bookOn, const std::vector<Language>& languages) {
  settingsShow_ = settingsShow;
  bookOn_ = bookOn;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    resetLocked();
    visibility_ = MarkVisibility{};
    visibility_.update(settingsShow, bookOn);
    languages_ = {};
    for (const Language language : languages) languages_[languageSlot(language)] = true;
  }
  if (drawn()) loadBookSources();
}

void MarkKeeper::loadBookSources() {
  if (!loader_) return;
  std::array<bool, std::size(kLanguages)> languages{};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    languages = languages_;
  }
  for (const Language language : kLanguages) {
    if (languages[languageSlot(language)]) loader_->load(language);
  }
}

void MarkKeeper::setBookOn(const bool on) {
  if (bookShowsMarks(settingsShow_.load(), on)) loadBookSources();  // before the render task follows the row
  bookOn_ = on;
}

bool MarkKeeper::due(const uint32_t book, const int spine, const int page, const unsigned long drawnMs,
                     const bool settingsShow) {
  // Mark words on the page turned on with a book open (not reachable from the reader now): its sources, before the
  // render task follows the setting.
  if (!drawn() && bookShowsMarks(settingsShow, bookOn_.load())) loadBookSources();
  settingsShow_ = settingsShow;
  if (visibility_.update(settingsShow, bookOn_.load())) {
    std::lock_guard<std::mutex> lock(mutex_);
    resetLocked();  // shown again, or hidden: every page afresh
  }
  if (!visibility_.shown()) return false;  // a book with Page marks off reads nothing
  gate_.drawn(book, spine, page, drawnMs);
  return gate_.due() >= 0;
}

void MarkKeeper::reload(const PageKey& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  slots_.reload(key);
  if (const std::optional<size_t> at = slotOfKey(wanted_, key)) gate_.reload(static_cast<int>(*at));
}

void MarkKeeper::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  resetLocked();
}

void MarkKeeper::dropCardMarks() {
  std::lock_guard<std::mutex> lock(mutex_);
  cardMarks_.drop();
}

void MarkKeeper::read(PageTexts& texts) {
  const int slot = gate_.due();
  if (slot < 0) return;
  gate_.done(slot);
  const auto at = static_cast<size_t>(slot);
  const unsigned long start = now();
  const std::optional<PageText> text = texts.textOf(kMarkPageSlots[at].offset);  // the section's page, under the lock
  if (!text) return;
  const PageId id{text->key, text->language, text->units, textHash(text->text)};
  const int onScreen = texts.pageIndex();
  const int page = onScreen + kMarkPageSlots[at].offset;
  if (at == 0) {
    if (onScreen_ && !(*onScreen_ == id)) {  // drawn again with another text (a reflow on the same index): all anew
      gate_.rearm();
      gate_.done(0);
    }
    onScreen_ = id;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    wanted_[at] = text->key;
    if (at == 0) slots_.onScreen(text->key.spine, onScreen);
    if (slots_.have(id)) return;  // kept: no analysis-file read
  }
  std::optional<PageAnalysis> analysis = store_.read(text->key, text->language, text->units, id.hash);
  // A few hundred bytes plus its arrays (the occurrences and the pool in PSRAM past malloc's 4 KB threshold; the
  // entries and states, smaller, in internal RAM, as V7b's).
  std::unique_ptr<PageAnalysis> kept;
  if (analysis) kept = makeUniqueNoThrow<PageAnalysis>(std::move(*analysis));
  const bool found = kept != nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    slots_.put(id, text->key.spine, page, std::move(kept));
  }
  LOG_DBG(kLogTag, "marks: %s page %u-%u %s in %lu ms", kMarkPageSlots[at].name, static_cast<unsigned>(text->key.spine),
          static_cast<unsigned>(text->key.start), found ? "kept" : "not analyzed", now() - start);
}

const PageAnalysis* MarkKeeper::analysisLocked(const PageKey& key, const uint32_t units, const uint32_t hash,
                                               const int spine, const int pageIndex, bool* read) {
  if (read) *read = false;
  const PageAnalysis* analysis = slots_.find(key, units, hash);
  if (analysis || pageIndex < 0 || slots_.knownNotAnalyzed(key, units, hash) || !slots_.peekDue(key, hash)) {
    return analysis;  // kept, not the reader's page, known not analyzed, or read as drawn already
  }
  // Drawn before the loop kept it (a book's first page, a jump, a fast turn, a reflow): its file, once, as it's drawn,
  // if it's in one of the book's languages.
  const unsigned long start = now();
  std::optional<PageAnalysis> peeked = store_.peek(key, units, hash);
  if (peeked && !languages_[languageSlot(peeked->language)]) peeked.reset();
  const bool found = peeked.has_value();
  if (read) *read = true;
  const Language language = found ? peeked->language : Language::Japanese;  // not analyzed: any (MarkSlots::have)
  slots_.peeked(PageId{key, language, units, hash}, static_cast<uint32_t>(spine), pageIndex,
                found ? makeUniqueNoThrow<PageAnalysis>(std::move(*peeked)) : nullptr);
  LOG_DBG(kLogTag, "marks: page %u-%u read as drawn: %s in %lu ms", static_cast<unsigned>(key.spine),
          static_cast<unsigned>(key.start), found ? "kept" : "not analyzed", now() - start);
  return slots_.find(key, units, hash);
}

}  // namespace lexipoint::page
