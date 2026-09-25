#pragma once

// What the card shows (popup-ui.md §1-2) and the state it's in. Pure data; the bench fills it from
// fixtures (BenchFixtures), P5 from a lookup. Tests: test/lexirise_card.

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "CardMetrics.h"
#include "DisplayList.h"
#include "lexirise/LexiriseConfig.h"
#include "lexirise/settings/Settings.h"

namespace lexipoint::card {

// T L F K, as Lexirise's proficiency levels (tracked … known); None: not saved.
enum class Level : int8_t { None = -1, Tracked = 0, Learning = 1, Fresh = 2, Known = 3 };

struct CharInfo {
  std::string character;
  std::string reading;  // kana (ja) or pinyin (zh)
  std::string romaji;   // ja only
  std::string gloss;
};

struct FormInfo {
  std::string form;
  std::string label;  // "te-form: “troublesome, and…”"
};

// A sentence with one span marked (underlined, or inverted in the Context tab's own sentence).
struct MarkedText {
  std::string text;
  size_t markStart = 0;  // bytes
  size_t markLength = 0;
};

// Why phase B brought no meaning (offline-and-errors.md §1): the meaning row says it.
enum class NoMeaning : uint8_t {
  Offline,      // Lexirise couldn't be reached: "offline"
  KeyRejected,  // 401/403
  RateLimited,  // 429
  Unavailable,  // anything else (a bad answer, TLS, low memory): "meaning unavailable"
};

struct CardWord {
  Language language = Language::Japanese;
  std::string reading;  // kana (ja; romaji when it couldn't be converted) or pinyin (zh)
  std::string romaji;   // ja: the API's reading
  std::string word;     // the lemma
  std::string badge;    // "N1" / "HSK 4"; empty: none
  std::string surface;  // ja: the form in the sentence, when it isn't the lemma
  std::string conjugation;
  std::string partOfSpeech;
  std::vector<std::string> senses;
  uint32_t rank = 0;    // 0: unknown
  float frequency = 0;  // frequency_score, 0-1
  std::vector<CharInfo> chars;
  std::vector<FormInfo> forms;
  std::optional<MarkedText> metBefore;  // a sentence from another book
  std::string metBeforeBook;
  bool examplesOnlyTraditional = false;      // Lexirise's only examples are Traditional (a Simplified book)
  std::string traditionalForm;               // the word in Traditional characters, for that note (選擇)
  NoMeaning noMeaning = NoMeaning::Offline;  // Phase::Unanswered: why
};

// One line of the page, for the strips: its tokens at their x positions (relative to the line's start).
struct StripToken {
  std::string text;
  int x = 0;
  int width = 0;
};
struct StripLine {
  std::vector<StripToken> tokens;
  int activeFirst = -1;  // the active word's tokens [activeFirst, activeLast]
  int activeLast = -1;
  // Where the word starts in its first token and ends in its last, in codepoints (P11: a glued token such as
  // 话。 holds more than the word, and only the word is inverted). kToTokenEnd: to the last token's end.
  static constexpr uint32_t kToTokenEnd = UINT32_MAX;
  uint32_t activeStartCp = 0;
  uint32_t activeEndCp = kToTokenEnd;
  int lineNumber = 1;  // 1-based
  int lineCount = 1;
};

enum class View : uint8_t { Card, Expanded };
enum class Phase : uint8_t {
  Pending,             // 0: the tapped character and …
  Analyzed,            // A: the word, reading, form, POS, state
  Complete,            // B: + translation, rank, badge
  TranslationPending,  // B′: + "translation pending"
  Unanswered,          // B failed: the word without its meaning, and why (CardWord::noMeaning)
};
enum class ReadingMode : uint8_t { Kana, Romaji };

struct CardState {
  View view = View::Card;
  Phase phase = Phase::Complete;
  int tab = 0;
  Level level = Level::None;
  ReadingMode reading = ReadingMode::Kana;
  std::string pendingText;         // phase 0: the tapped character
  std::string toast;               // empty: none
  bool toastUndo = false;          // the toast offers Undo (a save): it's a touch target
  std::optional<Rect> wordOnPage;  // the active word's box on the page (D17: the card-view strip)
  StripLine strip;                 // the active word's page line
  MarkedText contextSentence;      // this book's sentence, the word marked
  int pageNumber = 0;              // "This book · p. 84"; 0: none
};

// The card's words; P6 fills them from I18n (popup-ui.md §4), the host keeps the English.
struct CardStrings {
  const char* levels[metrics::kLevelCells] = {"T", "L", "F", "K"};
  const char* levelNames[metrics::kLevelCells] = {"tracked", "learning", "fresh", "known"};
  const char* notSaved = "not saved";
  const char* bands[config::kRankBands] = {"very common", "common", "uncommon", "rare"};
  const char* translationPending = "translation pending";
  const char* tabsJa[5] = {"Meaning", "Examples", "Context", "Kanji", "Form"};
  const char* tabsZh[4] = {"Meaning", "Examples", "Context", "Chars"};
  const char* noExamples = "No Lexirise examples for this word.";
  // "Lexirise's only example is Traditional (選擇), hidden in a Simplified book."
  const char* onlyTraditional = "Lexirise's only example is Traditional";
  const char* hiddenInSimplified = ", hidden in a Simplified book.";
  const char* separator = " \xC2\xB7 ";  // " · ": "This book · p. 84", "Met before · 活着"
  const char* fromYourReading = " From your reading:";
  const char* thisBook = "This book";
  const char* page = "p.";
  const char* metBefore = "Met before";
  const char* firstTime = "First time you've met this word.";
  const char* notInflected = "Not inflected here: this is the dictionary form.";
  const char* actionUndo = "Undo save";
  const char* actions[3] = {"Save the sentence as a card", "Ignore this word", "Look up later"};
  const char* line = "line";
  // Toasts (the reference's): "Saved as learning  ·  Undo", "Now fresh  ·  Undo", "Readings: romaji".
  const char* savedAs = "Saved as ";
  const char* now = "Now ";
  const char* undoSuffix = "  \xC2\xB7  Undo";
  const char* readings = "Readings: ";
  const char* kana = "kana";
  const char* romaji = "romaji";
  const char* removed = "Removed from Lexirise";
  const char* saveFailed = "Save failed";
  const char* retrySuffix = "  \xC2\xB7  Retry";
  const char* keyRejected = "Lexirise key rejected";
  const char* rateLimitedTryIn = "Rate limited: try in ";
  const char* seconds = " s";
  const char* retrying = "Trying again\xE2\x80\xA6";
  const char* offline = "offline";  // the meaning row when phase B couldn't reach Lexirise
  const char* rateLimited = "Lexirise: rate limited";
  const char* nextSentenceFailed = "Couldn't load the next sentence";  // a step past the end, offline (P9)
  const char* meaningUnavailable = "meaning unavailable";
  const char* notYet = "Not in this version yet";
  const char* actionDone[3] = {"Sentence saved as a card", "Ignored: won't be marked again", "Flagged for later"};
};

// The tabs for a language, ⋯ last.
int tabCount(Language language);
bool isActionsTab(Language language, int tab);
// The rank word's band (popup-ui.md §1, languages.md §6): config::kRankBandLimitsJa / Zh.
int rankBand(uint32_t rank, Language language);
// ⌈frequency × 5⌉, at least 1.
int filledBars(float frequency);

}  // namespace lexipoint::card
