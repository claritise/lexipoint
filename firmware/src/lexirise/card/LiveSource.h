#pragma once

// The live card's source (P5): the tapped sentence analyzed once, then each word's dictionary entry as
// the card lands on it (lookup-flow.md §5-6). Stepped past its last word, the card goes on into the page's
// next sentence (P9): extend() adds it, it is analyzed like the first, and its words follow at the end, so
// every word keeps its index. The network is behind api::LexiriseApi. The activity runs
// one blocking call per loop pass (fetch(): outside RenderLock, it changes nothing render() reads), then
// applies the answer under the lock (apply()), so each phase is drawn before the next call starts
// (phases A and B, popup-ui.md §2). Pure; tests: test/lexirise_card.

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "CardController.h"
#include "CardSource.h"
#include "ReaderScene.h"
#include "lexirise/deck/BookDeck.h"
#include "lexirise/lookup/LexiriseLookup.h"

namespace lexipoint::card {

// How a failed call is told on the card (CardSource.h CallFailure).
CallFailure callFailure(api::ApiError error);

class LiveSource final : public CardSource {
 public:
  // The sentence after one on the page (text::describeNextSentence over word select's page); none at its end.
  using NextSentence = std::function<text::TapContext(const text::TapContext& current)>;

  // `tap` must have a sentence and a language (a Lexirise lookup: lookup::lexiriseConfigured). `tags` go on
  // every word saved from it (settings: tags). Without `next`, the card stops at the sentence's ends.
  LiveSource(api::LexiriseApi& api, text::TapContext tap, ReaderPage page, std::vector<std::string> tags = {},
             NextSentence next = nullptr);

  enum class Advance {
    Idle,         // nothing to fetch
    Changed,      // an answer arrived: redraw (a next sentence that failed too: the card stays on its word,
                  // with a toast via extendFailure(), or quietly at the page's end)
    NotFound,     // Lexirise found no word in the tapped sentence: the card closes, word select says so
    Unavailable,  // no answer for the tapped sentence (error()): the card closes, StarDict answers
  };
  // One answer from the network, not yet applied.
  struct Fetched {
    enum class Kind : uint8_t { None, Analysis, Entry, Write } kind = Kind::None;
    lookup::LookupReport report;  // Analysis
    lookup::AnalyzedSentence analysis;
    size_t tapped = 0;
    size_t sentence = 0;  // Analysis: which (0: the tapped one)
    int index = 0;        // Entry: the word, and its card with phase B filled in
    lookup::LookupCard card;
    api::ApiError error = api::ApiError::None;
    std::string savedExpressionId;  // Write: a new save's id (empty for a level change or a removal)
    bool clearFailed = false;       // Write: removed (DELETE), but its notes and tags weren't cleared
    bool saveRetry = false;         // Entry: the lookup again, for a save, after it failed once
    std::string unreadable;         // a response we couldn't read: its start, for the log
    uint32_t retryAfterS = 0;       // Write refused with 429: seconds until Lexirise may be asked
  };
  // A write Lexirise refused or didn't answer: the word goes back to `back.to` (what Lexirise has; its
  // `from` is what the user had set), why, and for a 429 how long until it may be asked again.
  struct FailedWrite {
    LevelChange back;
    api::ApiError error = api::ApiError::None;
    uint32_t retryAfterS = 0;
  };

  // A level the user set (CardController's LevelChange), queued in order and sent one per fetch() once
  // its readyAtMs has come (an Undo toast's window): a new word is saved (POST, D9; after its phase B,
  // which gives the translation, retried once if it failed), a saved one changes level (PATCH), a removed
  // one is deleted (DELETE), and cleared too (PATCH notes/tags/translation) when this card saved it
  // (popup-ui.md §3.2): an item the user made in the app keeps what they wrote. A change to a word with
  // one still waiting merges into it (T then Undo in the window: nothing is sent at all).
  void queue(const LevelChange& change);
  bool hasPendingWrites() const { return !writes_.empty(); }
  // A write Lexirise refused or didn't answer: the word goes back to `back.to` (FailedWrite), and its later
  // changes are dropped (they were built on it). Taken by CardSession after apply().
  std::optional<FailedWrite> takeFailedWrite();

  // The book's deck (C4, V3; deck/BookDeck.h): a new save carrying its tag marks the deck wanted in the store
  // (memory only), and the deck's next step waits for no write to be queued. The call and its answer stay out of
  // the card's state: fetchDeck() and applyDeck() run outside RenderLock (CardSession::shouldFetchDeck says
  // when). `store` outlives the card; setBookDeck reads its file (word select, before the card opens).
  void setBookDeck(deck::BookDeck bookDeck, deck::DeckStore& store);
  bool hasDeckWork() const { return deckDue().has_value(); }
  deck::DeckCall fetchDeck();  // one call (network I/O) for the book deck's next step; step None when there's none
  void applyDeck(const deck::DeckCall& call);

  bool hasWork(unsigned long nowMs) const;  // fetch() would call the network
  // At most one call, in this order: the tapped sentence's analysis; the focused word's lookup (and a
  // save's, when a save waits for its word's translation); a next sentence the card waits for; the next
  // write that is ready. `closing`: only what the queued writes need, all of them now (no analysis).
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
  bool extend(unsigned long nowMs) override;
  bool extending() const override { return loadingSentence().value_or(0) > 0; }
  std::optional<CallFailure> extendFailure() const override { return extendFailure_; }
  bool tick(unsigned long) override { return false; }  // answers come through advance()
  std::optional<unsigned long> nextDueMs() const override { return std::nullopt; }

  PageScene scene(int index, bool highlight, const TextMetrics& metrics, int highlightCodepoints) const override;

 private:
  struct Sentence {
    text::TapContext tap;
    bool analyzed = false;
  };

  api::LexiriseApi& api_;
  std::vector<Sentence> sentences_;  // [0] the tapped one, then each one extend() added, in page order
  ReaderPage page_;
  NextSentence next_;
  bool pageEnded_ = false;  // no sentence after the last (the page ends, or it can't be sent to Lexirise)
  std::optional<CallFailure> extendFailure_;
  // A sentence with no word in it the card went past: a later extend() starts after it, not before it (a next
  // sentence that failed after it would otherwise ask about it again).
  std::optional<text::TapContext> resumeAfter_;
  std::vector<lookup::LookupCard> cards_;
  std::vector<size_t> sentenceOf_;  // per word: its sentence
  std::vector<CardWord> words_;
  int start_ = 0;
  int focused_ = 0;
  api::ApiError error_ = api::ApiError::None;
  std::vector<std::string> tags_;
  std::deque<LevelChange> writes_;
  std::optional<FailedWrite> failedWrite_;
  std::vector<std::string> createdIds_;  // items this card saved (their removal clears them too)
  std::vector<bool> saveRetried_;        // per word: its lookup was retried for a save
  struct DueDeck {
    size_t language;  // index in kLanguages (and deckKeys_)
    deck::DeckStep step;
  };
  std::optional<deck::BookDeck> bookDeck_;
  deck::DeckStore* decks_ = nullptr;
  bool savesCarryBookTag_ = false;
  std::vector<std::string> deckKeys_;  // the book deck's store key per language, in kLanguages order

  // A new save went through in `language`: when it carried the book tag, its deck is wanted.
  void savedWithBookTag(Language language);
  std::optional<DueDeck> deckDue() const;  // the book deck's next step, once no write is queued

  // Sends one write (a save, a level change, or a removal's two calls) for `card`.
  api::ApiResponse send(const LevelChange& change, const lookup::LookupCard& card, std::string& newId,
                        bool& clearFailed) const;
  // The word whose lookup fetch() runs next; -1: none. A save's lookup waits for its change to be ready.
  int lookupDue(unsigned long nowMs, bool closing) const;
  bool writeReady(unsigned long nowMs, bool closing) const;
  bool needsLookupForSave(int word) const;
  Fetched analysis(size_t sentence) const;  // the call analyzing one sentence
  // An analysis that brought words: they join the card at the end (the tapped sentence also sets the start).
  Advance addWords(Fetched& fetched);
  // A later sentence's analysis that brought none: the one after is tried (no word in it) or the card stays
  // where it is (no answer: extendFailure()).
  Advance laterSentenceFailed(const Fetched& fetched);
  // The first sentence Lexirise can be asked about after `current` on the page (sentences with no language, a
  // line of dots or English in a book that doesn't say, are passed over); none at the page's end.
  std::optional<text::TapContext> nextAskable(const text::TapContext& current) const;
  // The sentence being analyzed (the first one not yet); none when all are.
  std::optional<size_t> loadingSentence() const;
  // The sentence a word is in; past the known words, the one loading (its first character in phase 0).
  const text::BuiltSentence& sentenceFor(int index) const;
};

}  // namespace lexipoint::card
