#pragma once

// Synthetic GET /v1/vocabulary pages (v0.2 V7a) in the measured shape (lexirise-api-notes.md "V7's foundations"):
// every field an item carries, with an embedded dictionary_entry that has its own ids, rank and look-alike fields the
// mirror must never take, long translations and notes. No real data: the public repo holds none. And a fake account
// that pages its items newest change first, as the list does with sortId=updated_at&sortDesc=true.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "FakeApi.h"

namespace lexipoint::fakes {

// ms since the epoch as Lexirise writes it: "2026-09-24T06:11:34.058Z".
inline std::string isoTime(const uint64_t ms) {
  int64_t days = static_cast<int64_t>(ms / 86400000ULL);
  const uint64_t inDay = ms % 86400000ULL;
  days += 719468;
  const int64_t era = days / 146097;
  const int64_t doe = days - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t y = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const int64_t m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2 ? 1 : 0;
  char out[32];
  std::snprintf(out, sizeof(out), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", static_cast<int>(y), static_cast<int>(m),
                static_cast<int>(d), static_cast<int>(inDay / 3600000), static_cast<int>(inDay / 60000 % 60),
                static_cast<int>(inDay / 1000 % 60), static_cast<int>(inDay % 1000));
  return out;
}

constexpr uint64_t kSept2026Ms = 1790208000000ULL;  // 2026-09-24T00:00:00Z

struct FakeVocabItem {
  uint32_t savedId = 0;
  uint32_t entryId = 0;
  int proficiency = 1;
  bool suspended = false;
  uint64_t updatedMs = kSept2026Ms;
  std::string unitType = "word";
  std::optional<uint64_t> nextReviewMs;
};

// One item as the list sends it (~2-7 KB with `pad` bytes of translation).
inline std::string vocabItemJson(const FakeVocabItem& item, const size_t pad = 600) {
  const std::string id = std::to_string(item.savedId);
  const std::string entry = std::to_string(item.entryId);
  const std::string updated = isoTime(item.updatedMs);
  const std::string review = item.nextReviewMs ? "\"" + isoTime(*item.nextReviewMs) + "\"" : "null";
  const std::string longText(pad, 'x');
  std::string j = "{";
  j += "\"id\":" + id + ",\"user_id\":\"u-synthetic\",\"expression_text\":\"\\u5c71\",\"source_lang\":\"ja\",";
  j += "\"dictionary_id\":" + entry + ",\"custom_translation\":null,";
  j +=
      "\"notes\":\"" + std::string(400, 'n') + "\",\"learnt_at\":null,\"created_at\":\"" + isoTime(kSept2026Ms) + "\",";
  j += "\"updated_at\":\"" + updated + "\",\"source_segment_id\":null,\"is_ai_suggested\":false,";
  j += "\"proficiency\":" + std::to_string(item.proficiency) + ",\"seen_count\":3,\"first_seen_at\":null,";
  j += "\"last_seen_at\":null,\"next_review_at\":" + review + ",\"fsrs_stability\":2.5,\"fsrs_difficulty\":null,";
  j += "\"fsrs_reps\":0,\"fsrs_lapses\":0,\"fsrs_state\":0,\"last_review_at\":null,";
  j += std::string("\"suspended\":") + (item.suspended ? "true" : "false") + ",\"buried_until\":null,";
  j += "\"proficiency_source\":\"manual\",\"source_chapter_id\":null,\"images\":null,\"introduced_at\":null,";
  j +=
      "\"introduced_by_session_id\":null,\"context_lemma_entry_id\":null,\"audio_urls\":[\"https://x.invalid/a.mp3\"],";
  j += "\"custom_translation_lang\":null,\"video_urls\":[],\"unit_type\":\"" + item.unitType + "\",";
  j += "\"sentence_text\":null,\"source_start_time\":null,\"source_end_time\":null,\"source_page_index\":null,";
  j += "\"dictionary_entry_word\":\"\\u5c71\",\"dictionary_entry_lang\":\"ja\",\"dictionary_entry_status\":"
       "\"completed\",";
  j += "\"dictionary_entry_morphology\":null,\"dictionary_entry_rank\":730,\"dictionary_entry_system_tags\":[\"JLPT-"
       "N5\"],";
  // The embedded entry: its own id, and fields named like the item's, never to be taken.
  j += "\"dictionary_entry\":{\"id\":999" + entry + ",\"dictionary_id\":7,\"proficiency\":4,\"suspended\":true,";
  j += "\"updated_at\":\"2099-01-01T00:00:00.000Z\",\"unit_type\":\"sentence\",\"word\":\"\\u5c71\",\"lang\":\"ja\",";
  j += "\"translations\":[{\"target_lang\":\"en\",\"translation\":\"" + longText + "\",\"examples\":null,";
  j += "\"part_of_speech\":[\"noun\"],\"labels\":null},{\"target_lang\":\"en\",\"translation\":\"mountain\"}],";
  j += "\"translation_status\":\"ready\",\"transliteration\":\"yama\",\"rank\":730,\"frequency_score\":0.48,";
  j += "\"system_tags\":[\"JLPT-N5\"],\"subTokenCount\":1,\"breakdown\":[{\"text\":\"\\u5c71\",\"entryId\":5,";
  j += "\"isWordLike\":true}]},";
  j += "\"user_tags\":[{\"id\":1,\"name\":\"xteink\"}],\"translation_status\":\"ready\",\"translation_target\":\"en\"}";
  return j;
}

inline std::string vocabPageJson(const std::vector<FakeVocabItem>& items, const std::optional<uint32_t> nextOffset,
                                 const uint32_t totalCount, const size_t pad = 600) {
  std::string j = "{\"items\":[";
  for (size_t i = 0; i < items.size(); i++) {
    if (i > 0) j += ',';
    j += vocabItemJson(items[i], pad);
  }
  j += "],\"totalCount\":" + std::to_string(totalCount) + ",\"languageCount\":" + std::to_string(totalCount);
  j += ",\"nextOffset\":" + (nextOffset ? std::to_string(*nextOffset) : std::string("null"));
  j += ",\"availableTags\":[\"xteink\"]}";
  return j;
}

// A fake account: its items, paged newest change first by the request's offset and limit.
struct FakeVocabAccount {
  std::vector<FakeVocabItem> items;
  std::vector<uint32_t> offsets;  // every page asked for

  static uint32_t param(const std::string& path, const std::string& name) {
    const size_t at = path.find(name + "=");
    return at == std::string::npos ? 0 : static_cast<uint32_t>(std::stoul(path.substr(at + name.size() + 1)));
  }

  api::ApiResponse answer(const net::Request& request) {
    std::vector<FakeVocabItem> sorted = items;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const FakeVocabItem& a, const FakeVocabItem& b) { return a.updatedMs > b.updatedMs; });
    const uint32_t offset = param(request.path, "offset");
    const uint32_t limit = param(request.path, "limit");
    offsets.push_back(offset);
    std::vector<FakeVocabItem> page;
    for (uint32_t i = offset; i < sorted.size() && i < offset + limit; i++) page.push_back(sorted[i]);
    const std::optional<uint32_t> next =
        offset + limit < sorted.size() ? std::optional<uint32_t>(offset + limit) : std::nullopt;
    return apiOk(vocabPageJson(page, next, static_cast<uint32_t>(sorted.size()), 64));
  }

  // Serves `api`'s vocabulary pages.
  void serve(FakeApi& api) {
    api.vocabServer = [this](const net::Request& request) { return answer(request); };
  }
};

}  // namespace lexipoint::fakes
