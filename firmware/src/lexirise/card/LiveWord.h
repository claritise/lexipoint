#pragma once

// A lookup's answer as the card shows it (popup-ui.md §1, languages.md §3): the reading in kana, the
// level as a badge, T L F K from Lexirise's proficiency. Pure; tests: test/lexirise_card.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "CardModel.h"
#include "lexirise/lookup/LookupCard.h"
#include "lexirise/settings/BookTags.h"

namespace lexipoint::card {

// A sentence as the card has it, and whether the page or the length cap cut it at either end.
struct SentenceText {
  std::string_view text;
  bool cutAtStart = false;
  bool cutAtEnd = false;
};

// The sentence a word is in on the page, and whether the page or the length cap cut it at its end (then what
// follows its last word isn't known). `whole`: for "Met before", the sentence as the card has it: this cut alone,
// or when the cap cut the sentence into several, the consecutive cuts joined, then each cut; each with its cut flags.
struct PageSentence {
  std::string_view text;
  bool cutAtEnd = false;
  std::vector<SentenceText> whole;

  // A sentence the card has in one piece.
  static PageSentence single(const std::string_view text, const bool cutAtEnd = false, const bool cutAtStart = false) {
    return {text, cutAtEnd, {{text, cutAtStart, cutAtEnd}}};
  }
};

// The page's character right after the word: "" when the sentence ends there; nullopt when that isn't known (the
// word's place unknown, or the sentence cut right after it).
std::optional<std::string_view> nextOnPage(const lookup::LookupCard& card, const PageSentence& sentence);

// A Japanese word's form, named (C16): the surface line's name and the Form tab's steps.
struct FormName {
  std::string surface;  // what it was worked out for: the form ...
  std::string word;     // ... and the dictionary form
  std::string conjugation;
  std::vector<FormInfo> forms;  // empty for the dictionary form itself
};
FormName formNameOf(const lookup::LookupCard& card, const PageSentence& sentence);

// The card's word. `sentence`: the one it's in on the page (for its form's name and "Met before"); `titles`: V2's
// book-tag record, read as the card opens (for "Met before"'s book title); `named`: its form's name worked out before
// (phase A, or outside RenderLock), reused when it's for the same form. Not filled from the API: the Kanji/Chars tab
// (Lexirise's breakdown needs a lookup per character).
CardWord cardWord(const lookup::LookupCard& card, const PageSentence& sentence = {},
                  const BookTagList* titles = nullptr, const FormName* named = nullptr);

// Where a saved word was met before (C14): the sentence it was saved with (its notes), the word underlined in it,
// and the book's title when one of its tags is a book tag this device recorded (V2); a book saved on another device
// gives the sentence alone. nullopt when it isn't saved, was saved without a sentence, or was saved from any of
// `here` (the page's sentence, or a cut of it: sameSentence); the same sentence again isn't a new context, another
// sentence of this book is.
struct MetBefore {
  MarkedText sentence;
  std::string book;  // empty: not recorded here
};
std::optional<MetBefore> metBefore(const lookup::LookupCard& card, std::span<const SentenceText> here,
                                   const BookTagList* titles);

// Whether a saved sentence (a word's note) is the page's own: the page's cut flags are trusted and its text never
// guessed at; only a note near the cap (kCutNoteMinPercentOfCap) is taken to be a cut. The decision table and its
// known limits: docs/v0.2/00-overview.md C14 "As built (V4)".
bool sameSentence(std::string_view saved, const SentenceText& here);

// Where the lookup is: A until phase B has run, then B, B′ while the server still translates, or
// Unanswered when B failed (CardWord::noMeaning says why).
Phase phaseOf(const lookup::LookupCard& card);

// Why phase B brought no meaning, from its error: the meaning row's words.
NoMeaning noMeaningFor(api::ApiError error);

// "JLPT-N1" → "N1", "HSK-4" → "HSK 4", "HSK-7+" → "HSK 7+"; anything else: none.
std::string badgeFor(std::string_view level);

// Lexirise's proficiency (1-4 saved; 0 unknown) as T L F K, and back.
Level levelOf(const std::optional<api::EntryState>& saved);
int proficiencyOf(Level level);

}  // namespace lexipoint::card
