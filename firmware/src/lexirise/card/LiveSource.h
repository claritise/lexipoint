#pragma once

// The live card's source (P5): the tapped sentence analyzed once, then each word's dictionary entry as
// the card lands on it (lookup-flow.md §5-6). The network is behind api::LexiriseApi. The activity runs
// one blocking call per loop pass (fetch(): outside RenderLock, it changes nothing render() reads), then
// applies the answer under the lock (apply()), so each phase is drawn before the next call starts
// (phases A and B, popup-ui.md §2). Pure; tests: test/lexirise_card.

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "CardController.h"
#include "CardSource.h"
#include "ReaderScene.h"
#include "lexirise/lookup/LexiriseLookup.h"

namespace lexipoint::card {

class LiveSource final : public CardSource {
 public:
  // `tap` must have a sentence and a language (a Lexirise lookup: lookup::lexiriseUsable). `tags` go on
  // every word saved from it (settings: tags).
  LiveSource(api::LexiriseApi& api, text::TapContext tap, ReaderPage page, std::vector<std::string> tags = {});

  enum class Advance {
    Idle,         // nothing to fetch
    Changed,      // an answer arrived: redraw
    NotFound,     // Lexirise found no word in the sentence: the card closes, word select says so
    Unavailable,  // no answer (error()): the card closes, word select falls back to StarDict
  };
  // One answer from the network, not yet applied.
  struct Fetched {
    enum class Kind : uint8_t { None, Analysis, Entry, Write } kind = Kind::None;
    lookup::LookupReport report;  // Analysis
    lookup::AnalyzedSentence analysis;
    size_t tapped = 0;
    int index = 0;  // Entry: the word, and its card with phase B filled in
    lookup::LookupCard card;
    api::ApiError error = api::ApiError::None;
    std::string savedExpressionId;  // Write: a new save's id (empty for a level change or a removal)
    bool clearFailed = false;       // Write: removed (DELETE), but its notes and tags weren't cleared
    bool saveRetry = false;         // Entry: the lookup again, for a save, after it failed once
  };

  // A level the user set (CardController's LevelChange), queued in order and sent one per fetch() once
  // its readyAtMs has come (an Undo toast's window): a new word is saved (POST, D9; after its phase B,
  // which gives the translation, retried once if it failed), a saved one changes level (PATCH), a removed
  // one is deleted (DELETE), and cleared too (PATCH notes/tags/translation) when this card saved it
  // (popup-ui.md §3.2): an item the user made in the app keeps what they wrote. A change to a word with
  // one still waiting merges into it (T then Undo in the window: nothing is sent at all).
  void queue(const LevelChange& change);
  bool hasPendingWrites() const { return !writes_.empty(); }
  // A write Lexirise refused or didn't answer: the word goes back to `level`, and its later changes are
  // dropped (they were built on it). Taken by the activity after apply().
  std::optional<LevelChange> takeFailedWrite();

  bool hasWork(unsigned long nowMs) const;  // fetch() would call the network
  // At most one call: the analysis; then the focused word's lookup; then the next write that is ready
  // (with its word's lookup first, when a save still waits for it). `closing`: only what the queued
  // writes need, all of them now.
  Fetched fetch(unsigned long nowMs, bool closing = false) const;
  Advance apply(Fetched fetched);  // under RenderLock on the device
  Advance advance(const unsigned long nowMs = 0) { return apply(fetch(nowMs)); }
  api::ApiError error() const { return error_; }

  int wordCount() const override { return static_cast<int>(words_.size()); }
  int startWord() const override { return start_; }
  const CardWord& word(const int index) const override { return words_[index]; }
  Level savedLevel(int index) const override;
  Phase phase(int index) const override;
  std::string pendingText() const override;
  int pageNumber() const override { return page_.pageNumber; }
  std::vector<int> sameWord(int index) const override;

  void open(unsigned long) override {}
  void focus(const int index, unsigned long) override { focused_ = index; }
  bool tick(unsigned long) override { return false; }  // answers come through advance()
  std::optional<unsigned long> nextDueMs() const override { return std::nullopt; }

  PageScene scene(int index, bool highlight, const TextMetrics& metrics, int highlightCodepoints) const override;

 private:
  api::LexiriseApi& api_;
  text::TapContext tap_;
  ReaderPage page_;
  bool analyzed_ = false;
  lookup::AnalyzedSentence analysis_;
  std::vector<lookup::LookupCard> cards_;
  std::vector<CardWord> words_;
  int start_ = 0;
  int focused_ = 0;
  api::ApiError error_ = api::ApiError::None;
  std::vector<std::string> tags_;
  std::deque<LevelChange> writes_;
  std::optional<LevelChange> failedWrite_;
  std::vector<std::string> createdIds_;  // items this card saved (their removal clears them too)
  std::vector<bool> saveRetried_;        // per word: its lookup was retried for a save

  // Sends one write (a save, a level change, or a removal's two calls) for `card`.
  api::ApiResponse send(const LevelChange& change, const lookup::LookupCard& card, std::string& newId,
                        bool& clearFailed) const;
  // The word whose lookup fetch() runs next; -1: none. A save's lookup waits for its change to be ready.
  int lookupDue(unsigned long nowMs, bool closing) const;
  bool writeReady(unsigned long nowMs, bool closing) const;
  bool needsLookupForSave(int word) const;
};

}  // namespace lexipoint::card
