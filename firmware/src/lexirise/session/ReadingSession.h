#pragma once

// The reading session (C1, C7, v0.2 V6; docs/v0.2/00-overview.md "V6 design", "As built (V6)"): what the reader did
// since the book was opened, in RAM only, for the summary the home screen draws once after the book closes:
// `3 saved · 11 looked up` / `1,204 words in Japanese`. Saved: saves kept (words and sentence cards; an Undo takes one
// back); looked up: cards opened by a tap on the page (a side-button step isn't one). The word count is the account's
// (GET /v1/vocabulary's totalCount, asked once per session on an idle card), then counted up and down here with each
// word saved and taken back. A session starts when a book opens and ends when it closes; the summary is kept only when
// the home screen comes next and the session looked something up. Sleep ends it with no summary (the book closes on
// its way to the sleep screen). Main task only. Pure; tests: test/lexirise_card/ReadingSessionTest.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lexirise/settings/Settings.h"

namespace lexipoint::session {

struct Summary {
  unsigned saved = 0;
  unsigned lookedUp = 0;
  Language language = Language::Japanese;
  std::optional<uint32_t> words;  // the account's words in `language`; none: the count never came
};

class ReadingSession {
 public:
  void bookOpened();  // a new session (the last one's summary, if it wasn't taken, is dropped)
  // The screen coming next is the home screen (ActivityManager::goHome): a book closing now leaves its summary.
  void homeNext() { homeNext_ = true; }
  void bookClosed();
  // The summary, once, for the home screen as it opens; none when the session looked nothing up or the book closed
  // to another screen.
  std::optional<Summary> takeSummary();
  bool active() const { return active_; }

  // A card opened by a tap on the page showed a word in `language` (the session's language: the first such card's).
  void lookedUp(Language language);
  // A save that went through (a word's POST, a sentence card's): counted, and its id kept so its Undo takes it back
  // (the first config::kSessionSavesMax of them).
  void saved(Language language, std::string_view id, bool word);
  // A save that's gone (its DELETE went through or found it gone, or a PATCH found it deleted in the app): uncounted
  // when it was one of this session's.
  void removed(std::string_view id);

  // The language whose word count is still wanted (GET /v1/vocabulary?limit=1 on an idle card); none: none is.
  std::optional<Language> countWanted() const;
  void countFetched(Language language, uint32_t totalCount);

  // A sentence card this session saved, by its text (the same sentence saved again is a PATCH, not a second POST
  // that would replace the item: lexirise-api-notes.md); none when it wasn't, or was taken back.
  std::optional<std::string> sentenceId(Language language, std::string_view text) const;
  // A sentence card's save went through (after saved()): its text remembered for sentenceId().
  void sentenceSaved(Language language, std::string_view text, std::string_view id);

 private:
  struct Saved {
    std::string id;
    Language language;
    bool word;
    uint32_t sentenceHash;  // a sentence card's text, FNV-1a 32 (0 for a word)
  };
  bool active_ = false;
  bool homeNext_ = false;
  unsigned saved_ = 0;
  unsigned lookedUp_ = 0;
  std::optional<Language> language_;
  std::optional<uint32_t> words_;
  std::vector<Saved> saves_;
  std::optional<Summary> summary_;
};

// The summary's two lines (the second empty when the count never came), from the I18n formats: "%s saved · %s looked
// up", "%s words in Japanese" (and the singular), numbers with thousands separators.
struct SummaryFormats {
  const char* counts = "%s saved \xC2\xB7 %s looked up";
  const char* wordsJa = "%s words in Japanese";
  const char* wordJa = "1 word in Japanese";
  const char* wordsZh = "%s words in Chinese";
  const char* wordZh = "1 word in Chinese";
};
std::string countsLine(const Summary& summary, const SummaryFormats& formats = {});
std::string wordsLine(const Summary& summary, const SummaryFormats& formats = {});
std::string groupedNumber(uint32_t n);  // 1204 → "1,204"

// The device's session (a function-local static).
ReadingSession& readingSession();

}  // namespace lexipoint::session
