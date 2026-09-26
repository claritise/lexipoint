#pragma once

// A Lexirise deck per book (C4, V3): a dynamic deck of words filled by the book's tag (`book:<slug>`, C2), so
// saves never touch it. The work is per book and per boot, kept in the store, not in a card: a save carrying the
// tag marks the book's deck wanted, and any card open in that book, once idle with its writes sent, takes the next
// step, one call at a time (CardSession::shouldFetchDeck): a recorded deck is checked once per boot (a 404 forgets
// it); else the user's decks are listed and the book's reused (made on another device, or before a reinstall);
// else, after a readable and whole list without it, one is created: at most one creation reaches Lexirise per book
// per boot, so a creation whose answer was lost is found by a later boot's list, not made again. Any other failure
// waits for the next tagged save. An undone save still leaves the deck wanted (it may be made empty: it fills as
// words are saved). The ids live in /.lexirise/decks.ini, one `<language>:<slug>=<deck id>` line per deck, newest
// last. Pure parts plus the store; tests: test/lexirise_deck.

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lexirise/api/LexiriseApi.h"
#include "lexirise/api/Responses.h"
#include "lexirise/settings/BookTags.h"
#include "lexirise/settings/SafeFile.h"
#include "lexirise/settings/Settings.h"

namespace lexipoint::deck {

// The open book's deck: the tag that fills it and its title.
struct BookDeck {
  std::string slug;   // the book tag is config::kBookTagPrefix + slug
  std::string title;  // config::kDeckTitlePrefix + the book's title
  std::string tag() const;
};

// Whether book decks are made: Lexirise, Tag with book title and Deck per book on.
bool deckAllowed(const Settings& settings);

// The book's deck while deckAllowed(); nullopt otherwise. The title is
// the one the book tag's record holds (`titles`), else the book's own (text::isUntitled: its file name).
std::optional<BookDeck> bookDeckFor(const Settings& settings, std::string_view title, std::string_view path,
                                    BookTagStore& titles);

// The record's key for a book's deck in one language: "<ja|zh>:<slug>".
std::string deckKey(Language language, std::string_view slug);

// The user's own deck for the book in `language` among `decks` (GET /v1/decks in that language): a dynamic word deck
// on its tag, else a deck with its title whose type and unit are its own or aren't said (and whose tags, when
// listed, hold the book's). A deck of someone else's (DeckSummary::othersDeck), or one that says the other language
// (the ja and zh decks of a book share title and tag), never counts; a language it can't read doesn't exclude it.
// nullopt: none.
std::optional<std::string> findBookDeck(const std::vector<api::DeckSummary>& decks, const BookDeck& deck,
                                        Language language);

// What is sent next for one book deck.
enum class DeckStep : uint8_t { None, Check, List, Create };

// One book deck, in one language, as the store knows it.
struct DeckState {
  std::optional<std::string> id;  // recorded
  bool checked = false;           // found on Lexirise since boot
  bool wanted = false;            // a save carrying the book tag went through, and there's work left
  bool listedNone = false;        // a readable, whole list since boot, and not there: the next step creates it
  bool createdThisBoot = false;   // a creation reached Lexirise this boot (sent): never another until reboot
};

// A step's answer: the call's error (and HTTP status: a 404), the deck id it gave (List: the book's deck found;
// Create: the new deck's), and for a List or a Create whether its body could be read.
struct DeckAnswer {
  api::ApiError error = api::ApiError::None;
  int status = 0;
  std::optional<std::string> deckId;
  bool unreadable = false;    // a 2xx body this couldn't read
  bool sent = false;          // the request reached Lexirise (api::ApiResponse::sent)
  bool listComplete = false;  // List: the whole list (api::parseDeckList's `complete`)
};

// A save carrying the book tag went through: work to do unless the deck is recorded and checked this boot.
void want(DeckState& state);
DeckStep nextStep(const DeckState& state);
// The state after `step`'s answer. A checked or found deck is done; a 404 forgets it; a list that isn't readable and
// whole never counts as "not there"; a creation that was sent counts as this boot's one, whatever came back (a
// lost answer is found by a later boot's list); one that never left (offline, a failed connect or write) and any
// other failure leave the work for the next tagged save.
void answer(DeckState& state, DeckStep step, const DeckAnswer& answer);

struct DeckEntry {
  std::string key;
  std::string id;
  bool operator==(const DeckEntry& o) const { return key == o.key && id == o.id; }
};
using DeckList = std::vector<DeckEntry>;

// Lines that aren't `<ja|zh>:<slug>=<id>` (a plain id) are skipped; a key listed twice keeps its last line.
DeckList parseDecks(std::string_view text);
std::string serializeDecks(const DeckList& list);
// Sets a key's id, moving it to the end, then forgets the oldest past config::kDecksMax / kDecksMaxBytes.
// False (nothing changed) for a key or id that can't be a line.
bool setDeckIn(DeckList& list, std::string_view key, std::string_view id);
void forgetDeckIn(DeckList& list, std::string_view key);

// One deck call and its answer, sent outside RenderLock: the card's state never holds it.
struct DeckCall {
  std::string key;
  DeckStep step = DeckStep::None;
  DeckAnswer answer;
};
// Sends `step` for `deck` in `language` (Check needs the recorded `id`) and reads the answer. An unreadable body's
// head is logged, so the device check shows the real shape.
DeckCall sendDeckStep(api::LexiriseApi& api, const BookDeck& deck, Language language, DeckStep step,
                      const std::optional<std::string>& id);

// The device's decks.ini, and each book deck's work since boot (memory only). Safe across tasks.
class DeckStore {
 public:
  explicit DeckStore(SettingsFiles& files) : files_(files) {}

  void load();  // reads the file once (word select opening a card: not under RenderLock)
  DeckState state(std::string_view key);
  void want(std::string_view key);  // memory only
  DeckStep next(std::string_view key);
  bool anyWanted();  // cheap: whether any book deck has work (a card checks it every idle pass)
  // Applies an answer. A change to the recorded id is saved; memory changes even when the save
  // fails, so a failed write can't make the same call again this boot. False: not saved.
  bool apply(const DeckCall& call);

 private:
  struct Work {
    std::string key;
    DeckState state;
  };
  void loadLocked();                            // requires mutex_
  DeckState stateLocked(std::string_view key);  // requires mutex_; never adds to work_
  // requires mutex_: the key's work, added if new (only want() and a real step's answer add one).
  Work& workLocked(std::string_view key);

  SettingsFiles& files_;
  std::mutex mutex_;
  bool loaded_ = false;
  DeckList list_;
  // The book decks wanted since boot: past config::kDeckWorkMax, only ones merely checked are dropped, so the
  // list can outgrow it by the books with work left or a creation sent this boot (one or two per tagged book).
  std::vector<Work> work_;
};

// The device-wide store over the SD card (SettingsFilesHal.cpp; not linked into host tests).
DeckStore& deckStore();

}  // namespace lexipoint::deck
