#pragma once

// One page's analyze/text answer, compact (C12, v0.2 V7b; docs/v0.2/page-annotations.md §1.1 "V7b design" and "As
// built (V7b)"). A page's answer is 76-93 KB (measured, lexirise-api-notes.md "Page analysis (V7b), measured"), over
// what a response may hold, so it's streamed (PageReader, a net::BodySink) into sorted fixed-size arrays and one string
// pool: no std::map node or std::string per entry, so a page is a few large blocks (PSRAM on the device, past malloc's
// 4 KB threshold), never hundreds of small internal-RAM ones. Kept on the SD card per page (page/PageStore.h) in this
// shape (serializePage / parsePageFile). A tap on the page takes its sentence from here (sliceSentence) instead of
// asking analyze/text again. Pure; tests: test/lexirise_page.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lexirise/api/Responses.h"
#include "lexirise/net/Http.h"
#include "lexirise/net/JsonStream.h"
#include "lexirise/settings/Settings.h"

namespace lexipoint::page {

constexpr const char* kLogTag = "LXPAGE";  // the page analysis's log lines (every file of it)

// A string in the page's pool.
struct Str {
  uint32_t at = 0;
  uint16_t len = 0;
};

// An occurrence, as api::Occurrence (charStart/charEnd in UTF-16 units of the page's text).
struct Occ {
  uint32_t start = 0;
  uint32_t end = 0;
  uint32_t entryId = 0;
  uint32_t lemmaEntryId = 0;
  Str word;
  Str lemma;  // the word's when the server left it out (as api::parseAnalyze)
  Str reading;
  bool wordLike = false;
};

// entryMetaById[id], as api::EntryMeta.
struct Meta {
  uint32_t id = 0;
  uint32_t rank = 0;
  float frequency = 0;
  Str reading;
  Str partOfSpeech;
};

// stateByEntryId[id], as api::EntryState (without what analyze/text never carries).
struct State {
  uint32_t id = 0;
  Str savedId;
  uint8_t proficiency = 0;
  uint32_t seenCount = 0;
};

struct PageAnalysis {
  Language language = Language::Japanese;
  // A refined answer merged with its word-level split (V1's whole words, mergeWholeWords); false: a first pass.
  bool refined = false;
  uint32_t textUnits = 0;   // the page text sent, in UTF-16 units
  uint32_t textHash = 0;    // and its textHash(): a cached page is used only for the very same text
  uint64_t analyzedMs = 0;  // when it was asked (ms since the epoch; 0: the clock wasn't set)
  std::vector<Occ> occurrences;
  std::vector<Meta> meta;    // sorted by id
  std::vector<State> state;  // sorted by id
  std::string pool;

  std::string_view str(const Str& s) const { return std::string_view(pool).substr(s.at, s.len); }
  const Meta* metaFor(uint32_t id) const;
  const State* stateFor(uint32_t id) const;
  bool morphoPending = false;  // the answer's own flag (not kept in the file: `refined` says what it is)
  // The answer listed more saved states than config::kPageMaxEntries: an entry without one isn't known unsaved (as
  // the card's config::kMaxEntries). Not kept in the file (only the fresh answer's states go to the mirror).
  bool statesCut = false;
};

// FNV-1a 32 of the page's text (UTF-8): the cache's check that a file is this page's analysis.
uint32_t textHash(std::string_view text);

// An answer fed as it arrives. Keeps only the entries the page's occurrences name (the entry and the lemma entry).
// Malformed: not a JSON object with an `occurrences` array, an occurrence without a word or a span, or cut JSON;
// OverLimit: past config::kPageMaxOccurrences or config::kPageMaxPoolBytes.
class PageReader final : public net::BodySink {
 public:
  using Cancel = bool (*)();
  // `textUnits`: the page text's length, to reserve the arrays once (a page has at most one occurrence per unit).
  explicit PageReader(Language language, Cancel cancel = nullptr, size_t textUnits = 0);
  bool onBody(const char* data, size_t len) override;
  size_t maxBytes() const override { return config::kPageAnswerMaxBytes; }
  bool cancelled() const { return cancelled_; }
  api::ParseStatus finish(PageAnalysis& out);  // after the last byte

 private:
  class Visitor final : public json::Visitor {
   public:
    explicit Visitor(PageAnalysis& page) : page_(page) {}
    void onBegin(const json::Path& path, bool isArray) override;
    void onEnd(const json::Path& path, bool isArray) override;
    void onValue(const json::Path& path, json::Type type, std::string_view text) override;
    bool sawOccurrences = false;
    bool overLimit = false;
    bool malformed = false;
    struct Reserve {
      size_t occurrences = 0, meta = 0, state = 0, pool = 0;
    } reserve;  // each array's first reservation, made as its first element arrives

   private:
    Str add(std::string_view text);
    PageAnalysis& page_;
    bool inOcc_ = false;
    bool sawWord_ = false;
    bool sawLemma_ = false;
    bool sawStart_ = false;
    bool sawEnd_ = false;
    bool inMeta_ = false;
    bool inState_ = false;
  };
  PageAnalysis page_;
  Visitor visitor_;
  json::StreamReader reader_;
  Cancel cancel_ = nullptr;
  bool cancelled_ = false;
};

// A whole answer at once (tests).
api::ParseStatus parsePage(std::string_view body, Language language, PageAnalysis& out);

// V1's whole words at page scale (lookup::wholeWords' rule, v0.2 V1): for each word of the word-level (`fast`)
// answer, the refined token with the same span (it has the lemma), else the refined pieces when the word isn't ranked
// (一日中雨), else the word itself (a ranked whole word: 深深, 一边, 小さな). The word-level answer's facts and states
// win; the refined one fills in the pieces' entries. The result is `refined`, with the refined answer's text check.
PageAnalysis mergeWholeWords(const PageAnalysis& refined, const PageAnalysis& words);

// The sentence [start, end) of the page's text (UTF-16 units) as analyze/text would answer it alone (measured: a
// sentence sent alone splits exactly as the page does): the occurrences inside it, shifted to start at 0 (one that
// crosses its edges is left out), and the entries they name. nullopt: the span is past the page's text.
std::optional<api::AnalyzeResult> sliceSentence(const PageAnalysis& page, uint32_t start, uint32_t end);

// The file (docs/v0.1/settings.md §3, "The page cache"): a header with a CRC, the arrays, the pool. It doesn't keep
// `statesCut`: only a fresh answer's states go to the mirror, and a page read back is only sliced for a card. Only a
// page within every cap (fitsFile: what parsePageFile takes back, a pool place fitting its u16) is written:
// PageStore::write refuses the rest (a merge of two answers can pass one).
bool fitsFile(const PageAnalysis& page);
std::string serializePage(const PageAnalysis& page);
bool parsePageFile(std::string_view bytes, PageAnalysis& out);

// Every entry id the page's occurrences name (the surface and the lemma), sorted, once each.
std::vector<uint32_t> entriesOf(const PageAnalysis& page);

}  // namespace lexipoint::page
