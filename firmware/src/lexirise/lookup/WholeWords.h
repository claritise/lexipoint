#pragma once

// Whole words for a sentence Lexirise returned already refined (v0.2 V1, lexirise-api-notes.md "The second
// analyze/text pass, measured"). A refined answer cuts words into grammatical morphemes (深深 → 深 · 深,
// 一边 → 一 · 边, 小さな → 小さ · な), and Lexirise caches it, so a sentence it has seen before comes back that way
// on the first call. Its `fast` answer still has the word-level split, without lemmas. Pure.
// Tests: test/lexirise_lookup/WholeWordsTest.cpp.

#include "lexirise/api/Responses.h"

namespace lexipoint::lookup {

// `refined` with `words`' split: for each word-level occurrence, the refined occurrence with exactly its span (it
// has the lemma); else the word, when Lexirise ranks it (a dictionary word the refined pass cut up); else the
// refined pieces that tile it (the refined pass split a bad token, 一日中雨 → 一日中 · 雨); else the word. Entry
// facts and saved states from both, the word-level answer's first. An empty `words` leaves `refined` as it is.
api::AnalyzeResult wholeWords(const api::AnalyzeResult& refined, api::AnalyzeResult words);

}  // namespace lexipoint::lookup
