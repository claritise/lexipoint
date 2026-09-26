#pragma once

// Lexirise request builders (lexirise-client.md §2). Pure. Tests: test/lexirise_net/RequestsTest.cpp.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lexirise/net/Http.h"
#include "lexirise/settings/Settings.h"

namespace lexipoint::api {

net::Request meRequest();

// POST /v1/analyze/text with the full (not "fast") analysis: v0.1 needs lemmas. Text over
// config::kMaxAnalyzeTextBytes is cut at a UTF-8 boundary.
net::Request analyzeRequest(Language language, std::string_view text);

// The same with `fast: true`: the word-level split and each word's entry, reading, rank and saved state, but no
// lemmas (v0.2 V1: it keeps whole words that a refined answer cuts into morphemes, lexirise-api-notes.md).
net::Request analyzeWordsRequest(Language language, std::string_view text);

// POST /v1/dictionary/lookup for a lemma (read-only, so safe to resend).
net::Request lookupRequest(Language language, std::string_view lemma);

// POST /v1/vocabulary (D9, lexirise-client.md §2): the lemma saved at a proficiency, with its first
// translation, the tags and the book's sentence as notes. An upsert that replaces tags, notes and
// proficiency (lexirise-api-notes.md), so it's never sent for a word already saved, and not retried.
struct SaveWord {
  Language language = Language::Japanese;
  std::string_view text;         // the lemma
  std::string_view translation;  // the first sense; empty: none sent
  std::string_view notes;        // the sentence
  int proficiency = 1;           // 1-4 (T L F K)
  std::vector<std::string> tags;
};
net::Request saveRequest(const SaveWord& word);

// PATCH /v1/vocabulary/{id} to a proficiency (0-4): a level change, and a level change's Undo.
// nullopt when `id` isn't a plain id (it goes into the path).
std::optional<net::Request> setProficiencyRequest(std::string_view id, int proficiency);

// Undoing a new save (popup-ui.md §3.2): DELETE only resets a dictionary word to unknown and keeps its
// notes, translation and tags, so they're cleared with a PATCH after it.
std::optional<net::Request> removeRequest(std::string_view id);
std::optional<net::Request> clearRequest(std::string_view id);

// A book's deck (C4, V3). GET /v1/decks?language=: the decks the user owns or starred in that language.
net::Request deckListRequest(Language language);
// GET /v1/decks/{id}, one item's page: whether the deck still exists (404 once deleted). nullopt for an id that
// isn't plain.
std::optional<net::Request> deckRequest(std::string_view id);
// POST /v1/decks: a dynamic deck of words filled by one tag (`user_tag_filter`). Not idempotent: a repeat makes
// a second deck.
struct NewDeck {
  Language language = Language::Japanese;
  std::string_view title;
  std::string_view tag;
};
net::Request createDeckRequest(const NewDeck& deck);

// An id as it may go into a request path (a saved expression's, a deck's): digits, letters, '-' and '_', at most
// `maxBytes`.
bool isPlainId(std::string_view id, size_t maxBytes);

// A request path for the log: a vocabulary item's or a deck's id is left out.
std::string loggablePath(std::string_view path);

// "Lexipoint/<ver> CrossPoint/<ver>".
std::string userAgent(std::string_view crossPointVersion);

}  // namespace lexipoint::api
