#pragma once

// A tap's sentence from the page's analysis (C12, v0.2 V7b; docs/v0.2/page-annotations.md §1.1 "As built (V7b)"):
// when the page on screen was analyzed for its very text (PageStore), the card's request ① isn't sent: the sentence
// is the page's occurrences over its span (sliceSentence; measured: a sentence alone splits exactly as the page does),
// and its saved states come from the vocab mirror, which holds what the reader did on the device at once and what the
// account changed by its last sync; the page's own snapshot answers only for an entry the mirror can't speak for (not
// loaded yet, or a sync that hasn't reached the page's time). Tests: test/lexirise_page.

#include <optional>

#include "PageStore.h"
#include "Prefetch.h"
#include "lexirise/text/TapContext.h"

namespace lexipoint::vocab {
class VocabStore;
}

namespace lexipoint::page {

// A page as the reader lays it out, ready to send or to cut sentences from: its text (text::buildPageText), where
// it's kept, and the language a Lexirise lookup on it would use (the book's, or, for a book that doesn't say, the
// page's own text decides, as a sentence's does). nullopt: no text, or no language Lexirise is used for.
struct DescribedPage {
  PageText page;
  text::BuiltSentence built;
};
std::optional<DescribedPage> describePage(const text::PageModel& model, const text::BookLanguage& book,
                                          const Settings& settings, std::string_view bookPath, uint32_t spine,
                                          uint32_t start);

class SentenceSource {
 public:
  virtual ~SentenceSource() = default;
  // The sentence's analysis as ① would give it; nullopt: ask ①.
  virtual std::optional<api::AnalyzeResult> analysisOf(const text::TapContext& tap) = 0;
};

// The page word select shows: its key, its text as the card's sentences are cut from it, and the language it would be
// analyzed in. The file is read on the first sentence asked (never as the card opens), once.
class PageSentences final : public SentenceSource {
 public:
  PageSentences(PageStore& store, const PageKey& key, text::BuiltSentence pageText, Language language)
      : store_(store), key_(key), pageText_(std::move(pageText)), language_(language) {}
  void setMirror(vocab::VocabStore* mirror) { mirror_ = mirror; }
  std::optional<api::AnalyzeResult> analysisOf(const text::TapContext& tap) override;
  bool found() const { return page_.has_value(); }  // after the first sentence: the page was cached

 private:
  PageStore& store_;
  PageKey key_;
  text::BuiltSentence pageText_;
  Language language_;
  vocab::VocabStore* mirror_ = nullptr;
  bool read_ = false;
  std::optional<PageAnalysis> page_;
};

// The saved states of a sentence cut from a page analyzed at `analyzedMs` (ms since the epoch; 0: unknown): each
// word's state is the newer of the mirror's entry (known as of its asOfS: vocab::mirrorOutranks) and the page's
// snapshot; a word the mirror doesn't hold is unsaved once the mirror is complete as of a time after the page
// (SyncState::lastSyncS), else the snapshot stands. Before the mirror loads, the reader's pending answers and writes
// (VocabStore::pendingState) take the entry's place. With a time unknown, only the reader's own writes outrank the
// snapshot.
void applyMirrorStates(api::AnalyzeResult& sentence, Language language, uint64_t analyzedMs, vocab::VocabStore& mirror);

}  // namespace lexipoint::page
