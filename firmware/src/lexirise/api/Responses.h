#pragma once

// Lexirise response models and parsers (lexirise-client.md §2, §4). Pure; fixtures are synthetic
// (the public repo never holds real responses). Tests: test/lexirise_net/ResponsesTest.cpp.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lexipoint::api {

enum class ParseStatus { Ok, Malformed, OverLimit };

// GET /v1/me. Keeps the key's rate-limit numbers, and the display name and plan for the web page's
// "Connected as <name> (<plan>)". The email is never read, and name/plan are never logged.
struct MeInfo {
  std::string name;  // empty when missing or over config::kMaxDisplayFieldBytes
  std::string plan;
  uint32_t rateLimitMax = 0;
  uint32_t rateLimitWindowMs = 0;
};
ParseStatus parseMe(std::string_view body, MeInfo& out);

// POST /v1/analyze/text. charStart/charEnd are UTF-16 code units (lexirise-api-notes.md).
struct Occurrence {
  std::string word;
  std::string lemma;    // falls back to word when the server omits it (it does when they're equal)
  std::string reading;  // "transliteration": romaji or pinyin; empty for punctuation
  uint32_t entryId = 0;
  uint32_t lemmaEntryId = 0;
  uint32_t charStart = 0;
  uint32_t charEnd = 0;
  bool wordLike = false;
};

// entryMetaById[id]: the dictionary facts about one entry (the surface form's; not the lemma's).
struct EntryMeta {
  std::string reading;       // transliteration
  std::string partOfSpeech;  // the first one
  uint32_t rank = 0;         // 0: unknown
  float frequency = 0;       // frequencyScore, 0-1 (the card's bars); 0: unknown
};

// stateByEntryId[id]: the reader's own state for an entry, present only once it is saved.
struct EntryState {
  std::string savedExpressionId;  // as sent (a number or a string); empty: not saved
  int proficiency = 0;            // 0-4
  uint32_t seenCount = 0;
  std::string notes;                  // the sentence it was saved with, cut to config::kMaxSavedNoteBytes (C14)
  std::vector<std::string> userTags;  // user_tags' names (objects {id, name} or plain strings), the first kMaxSavedTags
};

struct AnalyzeResult {
  std::vector<Occurrence> occurrences;
  std::map<uint32_t, EntryMeta> meta;
  std::map<uint32_t, EntryState> state;
  bool morphoPending = false;

  const EntryMeta* metaFor(uint32_t entryId) const;
  const EntryState* stateFor(uint32_t entryId) const;
};
ParseStatus parseAnalyze(std::string_view body, AnalyzeResult& out);

// POST /v1/dictionary/lookup: the lemma's entry.
struct Sense {
  std::string translation;
  std::string partOfSpeech;  // the first one
};

struct LookupResult {
  std::string word;
  std::string reading;        // transliteration
  std::vector<Sense> senses;  // the first config::kMaxTranslations
  std::string level;          // "JLPT-N5" / "HSK-1" … / "HSK-7+" from system_tags; empty when on no list
  uint32_t rank = 0;
  float frequency = 0;              // frequency_score, 0-1 (the card's bars); 0: unknown
  bool translationPending = false;  // translation_status isn't "ready" (a rare word's first lookup)
};
ParseStatus parseLookup(std::string_view body, LookupResult& out);

// POST /v1/vocabulary: result.savedExpressionId (a number or a string), for a later PATCH / DELETE.
struct SaveResult {
  std::string savedExpressionId;
};
ParseStatus parseSave(std::string_view body, SaveResult& out);

// A book's deck (C4, V3). The live shapes are unseen (lexirise-api-notes.md, Decks), so the parsers take the
// reference's snake_case names and their camelCase forms, and `id`, `deckId` or `deck_id`.
struct DeckSummary {
  std::string id;  // as sent (a number or a string)
  std::string title;
  std::string deckType;  // "snapshot" / "dynamic"; empty: not sent
  std::string unitType;  // "word" / "sentence"
  std::string ruleType;  // "user_tag_filter" / "saved_vocab_query"
  std::vector<std::string> userTags;
  std::string language;       // "ja" / "zh" when the entry says; empty: not said
  std::optional<bool> owned;  // when the entry says whose it is
  bool starred = false;
  // Another user's deck, never the book's: as its ownership says, else a starred one (the list holds the decks the
  // user owns or starred).
  bool othersDeck() const { return owned ? !*owned : starred; }
};
// GET /v1/decks: the list under `decks` or `data`, or a bare array. The first config::kMaxDecksListed entries are
// read; one without a usable id is skipped. Malformed when there are entries but none is readable (an id, and a
// title or a type): a shape this can't read must never look like "the book has no deck". `complete` (optional):
// whether this is the whole list: every entry read (none cut at the cap or skipped as unreadable), and no sign of
// another page (a true `hasMore` / `has_more`, a `nextOffset` / `next_offset` that isn't null, a `totalCount` /
// `total_count` above the entries that came).
ParseStatus parseDeckList(std::string_view body, std::vector<DeckSummary>& out, bool* complete = nullptr);

// POST /v1/decks: the new deck, under `deck` or `data`, or at the top. Its type and rule when sent.
struct CreatedDeck {
  std::string id;
  std::string deckType;
  std::string ruleType;
};
ParseStatus parseCreatedDeck(std::string_view body, CreatedDeck& out);

}  // namespace lexipoint::api
