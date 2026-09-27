#pragma once

// One page of GET /v1/vocabulary (v0.2 V7a, the vocab mirror), read as it streams: each item keeps only what the
// mirror needs, and everything else (the embedded `dictionary_entry` with its own ids and ranks, translations,
// notes, media URLs: ~6.8 KB an item, lexirise-api-notes.md "V7's foundations") is walked and dropped. The field
// names are the ones measured live (snake_case, lexirise-api-notes.md "V7's foundations"): `id` (the saved
// expression's id, what stateByEntryId calls saved_expression_id), `dictionary_id` (the entry a save targets,
// stateByEntryId's key), `proficiency`, `suspended`, `next_review_at`, `updated_at`, `unit_type`. Only an item's own
// top-level fields are read: `dictionary_entry.id` and the like are never taken. Pure; tests:
// test/lexirise_net/VocabPageTest.cpp.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "Responses.h"
#include "lexirise/net/Http.h"
#include "lexirise/net/JsonStream.h"

namespace lexipoint::api {

// An ISO 8601 UTC time as Lexirise sends it ("2026-09-24T06:11:34.058Z"; an offset "+09:00" is taken too), in ms
// since the epoch. nullopt: not one.
std::optional<uint64_t> parseIsoTimeMs(std::string_view text);

struct VocabItem {
  uint32_t savedId = 0;      // `id`; 0: missing or not a whole number
  uint32_t entryId = 0;      // `dictionary_id`; 0: missing
  int proficiency = -1;      // 0-4; -1: missing or out of range
  bool suspended = false;    // only JSON true
  uint32_t nextReviewS = 0;  // `next_review_at`, seconds since the epoch; 0: none (null) or unreadable
  uint64_t updatedMs = 0;    // `updated_at`; 0: unreadable
  bool word = true;          // `unit_type` is "word" (or not said); a sentence card isn't a word
  // A word the mirror can keep: its ids and level were read.
  bool usable() const { return word && savedId != 0 && entryId != 0 && proficiency >= 0; }
};

struct VocabPage {
  std::vector<VocabItem> items;           // every item, in the order they came (newest change first when so sorted)
  std::optional<uint32_t> nextOffset;     // null or missing: the last page
  std::optional<uint32_t> totalCount;     // the words (sentence cards not counted: measured)
  std::optional<uint32_t> languageCount;  // every item the list pages through, sentence cards too (measured)
  // How many items the list paged holds: languageCount (sentence cards too, as the list has them). totalCount counts
  // words only, so a sentence card deleted would move the list without moving it: not a count of this list.
  std::optional<uint32_t> listCount() const { return languageCount; }
  uint64_t newestMs = 0;  // the latest updated_at among the items (the first, newest-first)
};

// A page, fed as its body arrives (net::BodySink). At most config::kVocabListLimitMax items (the API's own cap): more
// is OverLimit. Malformed: not a JSON object with an `items` array, or broken or cut JSON.
class VocabPageReader final : public net::BodySink {
 public:
  // `cancel` (optional) is asked before each piece of the body: true stops the page there (the reader has input to
  // handle); cancelled() then says so.
  using Cancel = bool (*)();
  explicit VocabPageReader(Cancel cancel = nullptr);
  bool onBody(const char* data, size_t len) override;
  size_t maxBytes() const override { return config::kVocabPageMaxBytes; }
  bool cancelled() const { return cancelled_; }
  ParseStatus finish(VocabPage& out);  // after the last byte
  size_t skippedStrings() const { return reader_.skippedStrings(); }

 private:
  class Visitor final : public json::Visitor {
   public:
    explicit Visitor(VocabPage& page) : page_(page) {}
    void onBegin(const json::Path& path, bool isArray) override;
    void onEnd(const json::Path& path, bool isArray) override;
    void onValue(const json::Path& path, json::Type type, std::string_view text) override;
    bool sawObject = false;
    bool sawItems = false;
    bool overLimit = false;

   private:
    bool inItem_ = false;
    VocabPage& page_;
  };
  VocabPage page_;
  Visitor visitor_;
  json::StreamReader reader_;
  Cancel cancel_ = nullptr;
  bool cancelled_ = false;
};

// A whole body at once (tests, and any page already held).
ParseStatus parseVocabPage(std::string_view body, VocabPage& out);

}  // namespace lexipoint::api
