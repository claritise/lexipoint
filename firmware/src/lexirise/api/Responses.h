#pragma once

// Lexirise response models and parsers (lexirise-client.md §2, §4). Pure; fixtures are synthetic
// (the public repo never holds real responses). Tests: test/lexirise_net/ResponsesTest.cpp.

#include <cstdint>
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

struct AnalyzeResult {
  std::vector<Occurrence> occurrences;
  bool morphoPending = false;
};
ParseStatus parseAnalyze(std::string_view body, AnalyzeResult& out);

}  // namespace lexipoint::api
