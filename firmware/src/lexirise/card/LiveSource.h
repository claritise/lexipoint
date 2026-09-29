#pragma once

// The live card's source (P5): the tapped sentence analyzed once, then each word's dictionary entry as
// the card lands on it (lookup-flow.md §5-6). Stepped past its last word, the card goes on into the page's
// next sentence (P9): extend() adds it, it is analyzed like the first, and its words follow at the end, so
// every word keeps its index. A saved word's "Met before" (C14) comes from its item (GET /v1/vocabulary/{id}), asked
// once the card is on it and its phase B has run, once per item on the card (a refusal is asked again later). The
// words the reader ignored (C17, V5: a list on the SD card, never Lexirise) come from the store, loaded as the card
// opens, and this card's own ignores on top. The vocab mirror (C13, V7a: vocab/VocabMirror.h) takes what the card
// learns (each analysis's saved states, the live answer winning, and the card's own writes), and an idle card syncs
// it a page at a time. The network is behind api::LexiriseApi. The activity runs one blocking
// call per loop pass (fetch(): outside RenderLock; it changes nothing render() reads, and of the source's own state
// only itemRetryAtMs_, a refusal's passed retry time forgotten: mutable, on the loop task like every call), then
// applies the answer under the lock (apply()), so each phase is drawn before the next call starts (phases A and B,
// popup-ui.md §2). Pure; tests: test/lexirise_card.

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CardController.h"
#include "CardSource.h"
#include "LiveWord.h"
#include "ReaderScene.h"
#include "lexirise/deck/BookDeck.h"
#include "lexirise/lookup/LexiriseLookup.h"
#include "lexirise/lookup/LookupCache.h"
#include "lexirise/page/PageSentences.h"
#include "lexirise/settings/IgnoredWords.h"
#include "lexirise/vocab/VocabMirror.h"

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
    enum class Kind : uint8_t { None, Analysis, Entry, Write, Item } kind = Kind::None;
    lookup::LookupReport report;  // Analysis
    size_t tapped = 0;
    size_t sentence = 0;  // Analysis: which (0: the tapped one)
    int index = 0;        // Entry: the word, and its card with phase B filled in; Item: the word
    lookup::LookupCard card;
    api::ApiError error = api::ApiError::None;
    // Write: a new save's id (empty for a level change or a removal); Item: the item's.
    std::string savedExpressionId;
    api::SavedItem item;  // Item: what came (empty when the call failed)
    // Item: refused (a 429 or a rejected key, usually by AccessPolicy without the network): not kept, asked again
    // retryAfterS (else the default) after apply()'s time, when the answer came.
    bool refused = false;
    bool clearFailed = false;  // Write: removed (DELETE), but its notes and tags weren't cleared
    bool saveRetry = false;    // Entry: the lookup again, for a save, after it failed once
    // Entry: the lemma cache's read before the call (V7c; Outcome::Off without a cache), and the answer to keep when
    // the call brought one it may keep.
    lookup::CacheReadLog cacheRead;
    std::optional<lookup::CachedLookup> toCache;
    std::string unreadable;                 // a response we couldn't read: its start, for the log
    uint32_t retryAfterS = 0;               // Write or Item refused with 429: seconds until Lexirise may be asked
    std::vector<lookup::LookupCard> cards;  // Analysis: each word's card ...
    std::vector<FormName> names;            // ... and its form's name, worked out here, not under RenderLock
    // Analysis: what it says of each word's entries (saved or not), for the vocab mirror (V7a); empty without one.
    std::vector<vocab::LiveState> live;
    // A cut continuing the one before it: that cut's last word, named again now its next character is known.
    int renamed = -1;
    FormName renamedName;
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
  // V2's book-tag record, for "Met before"'s book titles (C14). Read here, as the card opens (not under RenderLock);
  // `titles` outlives the card.
  void setBookTitles(BookTagStore& titles);
  // The words the reader ignored (C17, V5): `store` (outliving the card) is loaded here, as the card opens (not under
  // RenderLock), and then only asked from memory; the card's own ignores (setIgnored) are kept here until the
  // activity has written them to `store` outside the lock, and after (a failed write takes one back).
  void setIgnoredWords(IgnoredWordStore& store);
  IgnoredWordStore* ignoredStore() const { return ignoredStore_; }  // none: the card can't ignore (CardSession)
  // Word `index`'s key in the ignore list (its language and entry key, lookup::entryKeyOf, else its dictionary form);
  // none when it has neither usable.
  std::optional<IgnoredKey> ignoreKey(int index) const;
#if LEXIPOINT_DEV_HARNESS
  // Dev builds: a clock (millis) to log how long a sentence's forms take to name, and the stack left:
  // "[LXCARD] names <n> words <ms> ms, stack <bytes> B free".
  using Clock = unsigned long (*)();
  void setClock(const Clock clock) { clock_ = clock; }
#endif
  bool hasDeckWork() const { return deckDue().has_value(); }

  // The vocab mirror (C13, V7a). `store` outlives the card; nothing is read as the card opens. The card's saved state
  // still comes from analyze/text; the mirror takes it: each analysis's states (for every word's lemma and surface
  // entries; an entry the answer doesn't list isn't saved, unless the answer was cut at config::kMaxEntries), except an
  // entry this card wrote (its write is newer), and each write that went through. Taken into the mirror's memory by
  // recordMirror(); its file is read and written only on an idle card or as the card closes (flushMirror), never before
  // a redraw.
  void setVocabMirror(vocab::VocabStore& store);
  // The page's analysis (C12, V7b): a sentence it holds is taken from it and request ① isn't sent (its states from
  // the mirror, page::PageSentences); none, or a sentence it can't give, asks ① as before.
  void setSentenceSource(std::unique_ptr<page::SentenceSource> source) { pageSentences_ = std::move(source); }
  // A page of the mirror's sync is due (CardSession::shouldFetchVocab says when the card is idle enough): the card
  // reached Lexirise (its words came, and its last call didn't fail), no write is queued, it has fetched fewer than
  // config::kVocabPagesPerCard, and the store has a page due within its budget. `epochS`: the wall clock (time()).
  bool hasVocabWork(unsigned long nowMs, uint32_t epochS) const;
  // Network I/O, outside RenderLock; `cancel` (optional) gives the page up when the reader has input (sendPage), and
  // asked first, a button already held gives it up before any request is sent.
  std::optional<vocab::PageCall> fetchVocab(unsigned long nowMs, uint32_t epochS,
                                            api::VocabPageReader::Cancel cancel = nullptr);
  // V7b: the mirror's probe as the card settles (VocabStore::takeCardProbe; CardSession::shouldProbeVocab says when):
  // the same conditions as a page (hasVocabWork), not counted in the card's share of pages. Network I/O, outside
  // RenderLock; its answer goes through applyVocab().
  bool hasProbeWork(unsigned long nowMs) const;
  std::optional<vocab::PageCall> fetchProbe(unsigned long nowMs, api::VocabPageReader::Cancel cancel = nullptr);
  // After a page changed the mirror (PageApplied::changedEntries): each word on the card whose saved state the mirror
  // now says otherwise takes it (not an entry this card wrote: its write is newer), rebuilt as a save's answer is;
  // returns the words changed (the controller takes their levels). Under RenderLock.
  std::vector<int> takeMirrorChanges(const std::vector<uint32_t>& changedEntries);
  // SD I/O, outside RenderLock.
  vocab::PageApplied applyVocab(const vocab::PageCall& call, unsigned long nowMs, uint32_t epochS);
  // What apply() learned for the mirror since the last call, into the store's memory (no SD I/O: after every answer,
  // under RenderLock or not; a language not loaded yet keeps it until it is).
  void recordMirror();
  // The mirror's file wants reading (this card's language isn't loaded yet) or writing (memory changed).
  bool mirrorFlushDue() const;
  // SD I/O on an idle card (CardSession::shouldFlushFiles) or as the card closes, outside RenderLock but for a close
  // without end() (sleep, the stack cleared: under the lock exitActivity holds, V7c R9): records, then
  // `load`s this card's language (once per boot), then writes what changed. True: the file was read or written (or
  // its write tried: lastFlushWriteFailed() says whether that failed).
  bool flushMirror(bool load);
  bool lastFlushWriteFailed() const { return lastFlushWriteFailed_; }  // the last flushMirror()'s write failed
  // The lemma cache (C21, V7c; lookup/LookupCache.h): phase B reads it before its call (one bucket: a hit makes no
  // call); an answer the call brought is kept in memory (the newest config::kLookupPendingMax) and written by
  // flushLookups(), never before phase B is drawn. `cache` outlives the card; none: phase B always calls.
  void setLookupCache(lookup::LookupCache& cache) { lookupCache_ = &cache; }
  size_t pendingLookups() const { return lookupWrites_.size(); }
  // Answers wait for the cache's file, and no write of them failed on this card (that one waits for the close).
  bool lookupFlushDue() const;
  // SD I/O on an idle card or as the card closes (`closing`: tried even after a failure), as flushMirror()'s: outside
  // RenderLock but for a close without end().
  struct LookupsFlushed {
    size_t answers = 0;  // answers written or tried (0: nothing to do)
    bool written = false;
  };
  LookupsFlushed flushLookups(bool closing);
  deck::DeckCall fetchDeck();  // one call (network I/O) for the book deck's next step; step None when there's none
  void applyDeck(const deck::DeckCall& call);

  bool hasWork(unsigned long nowMs) const;  // fetch() would call the network
  // At most one call, in this order: the tapped sentence's analysis; the focused word's lookup (and a
  // save's, when a save waits for its word's translation); a next sentence the card waits for; the focused saved
  // word's item ("Met before", after its phase B, never while a sentence loads); the next write that is ready.
  // `closing`: only what the queued writes need, all of them now (no analysis, no item).
  Fetched fetch(unsigned long nowMs, bool closing = false) const;
  // Under RenderLock on the device. Changed: the words or their phases changed (the controller syncs). Whatever
  // it answers, a change to what the focused word shows (its "Met before" once its item came, a copy's after a write,
  // an earlier cut's word once the next cut joins, a lookup that changed the word on screen) is told by
  // takeShownChanged(); what's drawn is the CardWord and the controller's state, so a lookup that changes neither
  // isn't drawn again.
  // `nowMs`: when the answer came (after the call): a refusal's retry time starts then, as AccessPolicy's back-off.
  Advance apply(Fetched fetched, unsigned long nowMs);
  // Whether the focused word was rebuilt showing something new since the last call (then cleared): draw it.
  bool takeShownChanged();
  Advance advance(const unsigned long nowMs = 0) { return apply(fetch(nowMs), nowMs); }
  api::ApiError error() const { return error_; }

  int wordCount() const override { return static_cast<int>(words_.size()); }
  int startWord() const override { return start_; }
  const CardWord& word(const int index) const override { return words_[index]; }
  Level savedLevel(int index) const override;
  bool ignored(int index) const override;  // on the reader's ignore list (never Lexirise's `suspended`)
  bool setIgnored(int index, bool ignored) override;
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
  bool shownChanged_ = false;
#if LEXIPOINT_DEV_HARNESS
  Clock clock_ = nullptr;
#endif
  api::ApiError error_ = api::ApiError::None;
  std::vector<std::string> tags_;
  std::deque<LevelChange> writes_;
  std::optional<FailedWrite> failedWrite_;
  std::vector<std::string> createdIds_;  // items this card saved (their removal clears them too)
  std::vector<bool> saveRetried_;        // per word: its lookup was retried for a save
  // A saved word's item, asked once on this card whatever came: its sentence (the notes, else sentence_text) and tags;
  // both empty when it had none or the call failed ("First time you've met this word.").
  struct Item {
    std::string id;
    std::string sentence;
    std::vector<std::string> tags;
  };
  std::vector<Item> items_;
  const Item* itemFor(const std::string& id) const;
  // The item fetched for word `index`'s saved state, copied into it (its notes and tags); false when there's none.
  bool fillFromItem(int index);
  // The word whose item fetch() asks for next; -1: none (only the focused saved word, after its phase B, once).
  int itemDue(unsigned long nowMs, bool closing) const;
  Fetched savedItem(int index) const;  // the call for a word's item
  // After a refusal: no item asked before this; cleared by itemDue() once reached.
  mutable std::optional<unsigned long> itemRetryAtMs_;
  bool ours(const std::string& id) const;  // saved on this card (its sentence and tags known: never asked)
  struct DueDeck {
    size_t language;  // index in kLanguages (and deckKeys_)
    deck::DeckStep step;
  };
  std::optional<deck::BookDeck> bookDeck_;
  deck::DeckStore* decks_ = nullptr;
  bool savesCarryBookTag_ = false;
  std::vector<std::string> deckKeys_;         // the book deck's store key per language, in kLanguages order
  BookTagList titles_;                        // V2's record as the card opened (empty: none)
  IgnoredWordStore* ignoredStore_ = nullptr;  // the reader's ignore list (none: nothing is ignored)
  // This card's ignores and their Undos, newest last (a handful): they win over the store.
  std::vector<std::pair<IgnoredKey, bool>> ignoredHere_;
  // The page's analysis (none: every sentence asks ①). A read-once cache (it reads the page's file on the first
  // sentence asked), so it's asked from the const fetch() and changes behind the pointer: the source's own state only,
  // nothing apply() or the render task reads.
  std::unique_ptr<page::SentenceSource> pageSentences_;
  lookup::LookupCache* lookupCache_ = nullptr;      // the lemma cache (none: phase B always calls)
  std::vector<lookup::CachedLookup> lookupWrites_;  // answers for it, newest last, until flushLookups()
  bool lookupWriteFailed_ = false;                  // not tried again on this card's idle windows (the close does)
  vocab::VocabStore* vocab_ = nullptr;              // the vocab mirror (none: nothing kept or synced)
  std::optional<Language> vocabLanguage_;           // the tapped sentence's: the mirror this card syncs
  unsigned vocabPages_ = 0;                         // pages this card fetched
  bool mirrorWriteFailed_ = false;                  // the mirror's file couldn't be written on this card
  bool lastFlushWriteFailed_ = false;               // ...by the last flushMirror() (the log says so)
  std::vector<vocab::LiveState> mirrorUpdates_;     // apply()'s, until recordMirror()
  // Entries this card wrote (a save, a level, a removal): a later analysis's state for them may predate the write.
  std::vector<std::pair<Language, uint32_t>> writtenEntries_;
  // Queues `state` for the mirror; `ownWrite`: from this card's write (later analyses don't override it).
  void toMirror(const vocab::LiveState& state, bool ownWrite);
  // cardWord with its sentence and the book titles; the form's name kept from the word as built so far.
  // `named`: its form's name worked out already (else the one shown now is reused when it's for the same form).
  CardWord wordFor(int index, const FormName* named = nullptr) const;
  PageSentence pageSentence(size_t sentence) const;
  // words_[index] built again (wordFor); a change on the focused word sets shownChanged_.
  void rebuild(int index, const FormName* named = nullptr);
  // The consecutive sentences_ that are cuts of one long sentence with `sentence` (first, last).
  std::pair<size_t, size_t> cutChain(size_t sentence) const;
  // analysis(): when `sentence` continues a cut sentence, the last word of the cut before, named again (f.renamed).
  void renameLastWordBefore(size_t sentence, Fetched& f) const;

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
