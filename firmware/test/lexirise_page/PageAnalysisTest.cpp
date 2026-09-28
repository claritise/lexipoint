// v0.2 V7b: one page's analysis, compact (page/PageAnalysis.h). Synthetic answers shaped like the measured ones
// (lexirise-api-notes.md "Page analysis (V7b), measured"): never a raw answer (RawPages below reads those from the
// gitignored research folder when told where).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "lexirise/lookup/WholeWords.h"
#include "lexirise/net/JsonWriter.h"
#include "lexirise/page/PageAnalysis.h"

using lexipoint::Language;
using lexipoint::api::AnalyzeResult;
using lexipoint::api::Occurrence;
using lexipoint::api::ParseStatus;
using namespace lexipoint::page;

namespace {

Occurrence occ(const char* word, uint32_t start, uint32_t end, uint32_t entry, uint32_t lemmaEntry = 0,
               bool wordLike = true, const char* lemma = nullptr) {
  Occurrence o;
  o.word = word;
  o.lemma = lemma ? lemma : word;
  o.reading = std::string(word) + "-r";
  o.charStart = start;
  o.charEnd = end;
  o.entryId = entry;
  o.lemmaEntryId = lemmaEntry;
  o.wordLike = wordLike;
  return o;
}

// An answer's JSON, in the measured key order (occurrences first) or with the maps first.
std::string answer(const AnalyzeResult& r, const bool mapsFirst = false, const bool omitLemmas = true) {
  const auto occurrences = [&] {
    std::string s = "\"occurrences\":[";
    for (size_t i = 0; i < r.occurrences.size(); i++) {
      const Occurrence& o = r.occurrences[i];
      if (i > 0) s += ",";
      s += "{\"entryId\":" + std::to_string(o.entryId) + ",\"word\":\"" + o.word + "\"";
      if (!omitLemmas || o.lemma != o.word) s += ",\"lemma\":\"" + o.lemma + "\"";
      if (o.lemmaEntryId != 0) s += ",\"lemmaEntryId\":" + std::to_string(o.lemmaEntryId);
      s += ",\"isWordLike\":" + std::string(o.wordLike ? "true" : "false") + ",\"transliteration\":\"" + o.reading +
           "\",\"charStart\":" + std::to_string(o.charStart) + ",\"charEnd\":" + std::to_string(o.charEnd) + "}";
    }
    return s + "]";
  };
  const auto maps = [&] {
    std::string s = "\"entryMetaById\":{";
    bool first = true;
    for (const auto& [id, m] : r.meta) {
      if (!first) s += ",";
      first = false;
      s += "\"" + std::to_string(id) + "\":{\"transliteration\":\"" + m.reading + "\",\"partOfSpeech\":[\"" +
           m.partOfSpeech + "\",\"x\"],\"rank\":" + std::to_string(m.rank) + ",\"frequencyScore\":0.5}";
    }
    s += "},\"stateByEntryId\":{";
    first = true;
    for (const auto& [id, st] : r.state) {
      if (!first) s += ",";
      first = false;
      s += "\"" + std::to_string(id) + "\":{\"saved_expression_id\":" + st.savedExpressionId +
           ",\"proficiency\":" + std::to_string(st.proficiency) + ",\"seen_count\":3,\"images\":[]}";
    }
    return s + "}";
  };
  const std::string flag = std::string("\"morphoPending\":") + (r.morphoPending ? "true" : "false");
  return mapsFirst ? "{" + maps() + ",\"grammar\":[]," + flag + "," + occurrences() + "}"
                   : "{" + occurrences() + ",\"grammar\":[],\"grammarStates\":{}," + flag + "," + maps() + "}";
}

// 他一边吃饭。 refined: 他 · 一 · 边 · 吃饭 · 。 (吃饭 with its lemma); the wholeWords test's fixture.
AnalyzeResult refinedSentence() {
  AnalyzeResult r;
  r.occurrences = {occ("他", 0, 1, 10), occ("一", 1, 2, 11), occ("边", 2, 3, 12), occ("吃饭", 3, 5, 13, 14),
                   occ("。", 5, 6, 0, 0, false)};
  r.meta[11].rank = 4;
  r.meta[11].reading = "yi";
  r.meta[13].rank = 900;
  r.meta[99].rank = 1;  // named by no occurrence: not kept
  r.state[12].savedExpressionId = "7";
  r.state[12].proficiency = 2;
  r.morphoPending = false;
  return r;
}
// Its fast answer: 他 · 一边 · 吃饭 · 。, no lemmas.
AnalyzeResult wordsSentence() {
  AnalyzeResult w;
  w.occurrences = {occ("他", 0, 1, 10), occ("一边", 1, 3, 20), occ("吃饭", 3, 5, 13), occ("。", 5, 6, 0, 0, false)};
  w.meta[20].rank = 1092;
  w.meta[20].partOfSpeech = "adverb";
  w.state[20].savedExpressionId = "9";
  w.state[20].proficiency = 3;
  w.morphoPending = true;
  return w;
}

PageAnalysis parsed(const AnalyzeResult& r, const uint32_t units = 6) {
  PageAnalysis page;
  EXPECT_EQ(parsePage(answer(r), Language::Chinese, page), ParseStatus::Ok);
  page.textUnits = units;
  return page;
}

// Two AnalyzeResults equal in everything a card reads.
void expectSame(const AnalyzeResult& a, const AnalyzeResult& b) {
  ASSERT_EQ(a.occurrences.size(), b.occurrences.size());
  for (size_t i = 0; i < a.occurrences.size(); i++) {
    const Occurrence& x = a.occurrences[i];
    const Occurrence& y = b.occurrences[i];
    EXPECT_EQ(x.word, y.word) << i;
    EXPECT_EQ(x.lemma, y.lemma) << i;
    EXPECT_EQ(x.reading, y.reading) << i;
    EXPECT_EQ(x.entryId, y.entryId) << i;
    EXPECT_EQ(x.lemmaEntryId, y.lemmaEntryId) << i;
    EXPECT_EQ(x.charStart, y.charStart) << i;
    EXPECT_EQ(x.charEnd, y.charEnd) << i;
    EXPECT_EQ(x.wordLike, y.wordLike) << i;
  }
  for (const Occurrence& o : a.occurrences) {
    for (const uint32_t id : {o.entryId, o.lemmaEntryId}) {
      const auto* ma = a.metaFor(id);
      const auto* mb = b.metaFor(id);
      ASSERT_EQ(ma == nullptr, mb == nullptr) << id;
      if (ma) {
        EXPECT_EQ(ma->rank, mb->rank) << id;
        EXPECT_EQ(ma->reading, mb->reading) << id;
        EXPECT_EQ(ma->partOfSpeech, mb->partOfSpeech) << id;
      }
      const auto* sa = a.stateFor(id);
      const auto* sb = b.stateFor(id);
      ASSERT_EQ(sa == nullptr, sb == nullptr) << id;
      if (sa) {
        EXPECT_EQ(sa->savedExpressionId, sb->savedExpressionId) << id;
        EXPECT_EQ(sa->proficiency, sb->proficiency) << id;
      }
    }
  }
}

}  // namespace

// --- reading an answer ---

TEST(PageAnalysis, AnAnswerIsReadIntoTheCompactPage) {
  const PageAnalysis page = parsed(refinedSentence());
  ASSERT_EQ(page.occurrences.size(), 5u);
  EXPECT_EQ(page.str(page.occurrences[3].word), "吃饭");
  EXPECT_EQ(page.occurrences[3].lemmaEntryId, 14u);
  EXPECT_EQ(page.str(page.occurrences[0].lemma), "他");  // the server left the lemma out: the word
  EXPECT_FALSE(page.occurrences[4].wordLike);
  ASSERT_TRUE(page.metaFor(11));
  EXPECT_EQ(page.str(page.metaFor(11)->reading), "yi");
  EXPECT_EQ(page.metaFor(11)->rank, 4u);
  EXPECT_FLOAT_EQ(page.metaFor(11)->frequency, 0.5f);
  EXPECT_FALSE(page.metaFor(99));  // no occurrence names it
  ASSERT_TRUE(page.stateFor(12));
  EXPECT_EQ(page.str(page.stateFor(12)->savedId), "7");
  EXPECT_EQ(page.stateFor(12)->proficiency, 2);
  EXPECT_FALSE(page.morphoPending);
}

TEST(PageAnalysis, KeyOrderAndPieceSizesDontMatter) {
  const std::string body = answer(refinedSentence());
  PageAnalysis whole;
  ASSERT_EQ(parsePage(body, Language::Chinese, whole), ParseStatus::Ok);
  PageAnalysis mapsFirst;
  ASSERT_EQ(parsePage(answer(refinedSentence(), true), Language::Chinese, mapsFirst), ParseStatus::Ok);
  EXPECT_EQ(serializePage(mapsFirst), serializePage(whole));
  for (const size_t piece : {1u, 7u, 1024u}) {
    PageReader reader(Language::Chinese);
    for (size_t at = 0; at < body.size(); at += piece) {
      ASSERT_TRUE(reader.onBody(body.data() + at, std::min(piece, body.size() - at)));
    }
    PageAnalysis streamed;
    ASSERT_EQ(reader.finish(streamed), ParseStatus::Ok);
    EXPECT_EQ(serializePage(streamed), serializePage(whole)) << piece;
  }
}

TEST(PageAnalysis, WhatIsntAnAnswerIsRefused) {
  PageAnalysis page;
  EXPECT_EQ(parsePage("[]", Language::Japanese, page), ParseStatus::Malformed);
  EXPECT_EQ(parsePage("{}", Language::Japanese, page), ParseStatus::Malformed);  // no occurrences
  const std::string body = answer(refinedSentence());
  EXPECT_EQ(parsePage(body.substr(0, body.size() / 2), Language::Japanese, page), ParseStatus::Malformed);  // cut
  EXPECT_EQ(parsePage(R"({"occurrences":[{"entryId":1,"charStart":0,"charEnd":1}]})", Language::Japanese, page),
            ParseStatus::Malformed);  // no word
  EXPECT_EQ(parsePage(R"({"occurrences":[{"word":"a","charStart":2,"charEnd":1}]})", Language::Japanese, page),
            ParseStatus::Malformed);  // a span backwards
  EXPECT_EQ(parsePage(R"({"occurrences":[{"word":"a","charStart":0}]})", Language::Japanese, page),
            ParseStatus::Malformed);                                                         // no end
  EXPECT_EQ(parsePage(R"({"occurrences":[]})", Language::Japanese, page), ParseStatus::Ok);  // an empty page is one
}

TEST(PageAnalysis, PastItsCapsAnAnswerIsOverLimit) {
  std::string body = "{\"occurrences\":[";
  for (size_t i = 0; i <= lexipoint::config::kPageMaxOccurrences; i++) {
    if (i > 0) body += ",";
    body += "{\"word\":\"a\",\"charStart\":" + std::to_string(i) + ",\"charEnd\":" + std::to_string(i + 1) + "}";
  }
  body += "]}";
  PageAnalysis page;
  EXPECT_EQ(parsePage(body, Language::Japanese, page), ParseStatus::OverLimit);
}

TEST(PageAnalysis, ACancelledAnswerIsGivenUp) {
  static bool stop = false;
  PageReader reader(Language::Japanese, [] { return stop; });
  const std::string body = answer(refinedSentence());
  ASSERT_TRUE(reader.onBody(body.data(), 10));
  stop = true;
  EXPECT_FALSE(reader.onBody(body.data() + 10, 10));
  EXPECT_TRUE(reader.cancelled());
  PageAnalysis page;
  EXPECT_NE(reader.finish(page), ParseStatus::Ok);
  stop = false;
}

// --- V1's whole words, at page scale ---

TEST(PageAnalysis, TheMergeIsV1sWholeWordsRule) {
  PageAnalysis refined = parsed(refinedSentence());
  const PageAnalysis words = parsed(wordsSentence());
  refined.textHash = 42;
  const PageAnalysis merged = mergeWholeWords(refined, words);
  EXPECT_TRUE(merged.refined);
  EXPECT_EQ(merged.textHash, 42u);
  const auto sentence = sliceSentence(merged, 0, 6);
  ASSERT_TRUE(sentence);
  AnalyzeResult r = refinedSentence();
  AnalyzeResult w = wordsSentence();
  for (auto& o : r.occurrences) o.reading = std::string(o.word) + "-r";
  expectSame(*sentence, lexipoint::lookup::wholeWords(r, w));
  ASSERT_EQ(sentence->occurrences.size(), 4u);
  EXPECT_EQ(sentence->occurrences[1].word, "一边");           // a ranked whole word
  EXPECT_EQ(sentence->occurrences[2].lemmaEntryId, 14u);      // the same token: the refined one, with its lemma
  EXPECT_EQ(sentence->stateFor(20)->savedExpressionId, "9");  // the word-level answer's state
  EXPECT_EQ(sentence->occurrences[1].reading, "一边-r");
}

TEST(PageAnalysis, AnUnrankedWordKeepsTheRefinedPieces) {
  AnalyzeResult r;  // 一日中雨: refined 一日中 · 雨, fast 一日中雨 unranked (the known bad merge)
  r.occurrences = {occ("一日中", 0, 3, 1), occ("雨", 3, 4, 2)};
  AnalyzeResult w;
  w.occurrences = {occ("一日中雨", 0, 4, 3)};
  const PageAnalysis merged = mergeWholeWords(parsed(r, 4), parsed(w, 4));
  ASSERT_EQ(merged.occurrences.size(), 2u);
  EXPECT_EQ(merged.str(merged.occurrences[0].word), "一日中");
}

// --- a sentence of it ---

TEST(PageAnalysis, ASentenceIsItsOccurrencesShiftedWithTheirEntries) {
  const PageAnalysis page = parsed(refinedSentence());
  const auto s = sliceSentence(page, 1, 5);  // 一边吃饭
  ASSERT_TRUE(s);
  ASSERT_EQ(s->occurrences.size(), 3u);
  EXPECT_EQ(s->occurrences[0].word, "一");
  EXPECT_EQ(s->occurrences[0].charStart, 0u);
  EXPECT_EQ(s->occurrences[2].charEnd, 4u);
  EXPECT_TRUE(s->metaFor(11));
  EXPECT_FALSE(s->metaFor(10));   // 他 is outside it
  EXPECT_TRUE(s->morphoPending);  // a first pass (not `refined`)
  EXPECT_EQ(s->stateFor(12)->proficiency, 2);
  const auto cut = sliceSentence(page, 4, 6);  // starts inside 吃饭: it's left out, 。 kept
  ASSERT_TRUE(cut);
  ASSERT_EQ(cut->occurrences.size(), 1u);
  EXPECT_EQ(cut->occurrences[0].word, "。");
  EXPECT_FALSE(sliceSentence(page, 2, 7));  // past the page's text
  EXPECT_FALSE(sliceSentence(page, 3, 2));
}

// --- the file ---

TEST(PageFile, RoundTripsEverything) {
  PageAnalysis page = mergeWholeWords(parsed(refinedSentence()), parsed(wordsSentence()));
  page.textHash = textHash("他一边吃饭。");
  page.analyzedMs = 1790000000123ULL;
  const std::string bytes = serializePage(page);
  PageAnalysis back;
  ASSERT_TRUE(parsePageFile(bytes, back));
  EXPECT_EQ(serializePage(back), bytes);
  EXPECT_TRUE(back.refined);
  EXPECT_EQ(back.language, Language::Chinese);
  EXPECT_EQ(back.textUnits, 6u);
  EXPECT_EQ(back.analyzedMs, 1790000000123ULL);
  expectSame(*sliceSentence(back, 0, 6), *sliceSentence(page, 0, 6));
}

TEST(PageFile, AnythingButWhatItWroteIsRefused) {
  const PageAnalysis page = parsed(refinedSentence());
  const std::string bytes = serializePage(page);
  PageAnalysis back;
  EXPECT_FALSE(parsePageFile(bytes.substr(0, bytes.size() - 1), back));  // cut
  EXPECT_FALSE(parsePageFile(bytes + "x", back));                        // too long
  std::string flipped = bytes;
  flipped[bytes.size() - 2] ^= 0x01;  // the pool: the CRC catches it
  EXPECT_FALSE(parsePageFile(flipped, back));
  std::string version = bytes;
  version[4] = 9;
  EXPECT_FALSE(parsePageFile(version, back));
  EXPECT_FALSE(parsePageFile("LXPA", back));
  EXPECT_FALSE(parsePageFile(std::string(64, '\0'), back));
}

TEST(PageFile, TheTextHashIsFnv1a) {
  EXPECT_EQ(textHash(""), 2166136261u);
  EXPECT_EQ(textHash("a"), 0xE40C292Cu);
  EXPECT_NE(textHash("他一边吃饭。"), textHash("他一边吃饭"));
}

// --- measured answers (the gitignored research folder; skipped without it) ---
//
//   LEXIPOINT_RAW_PAGES=<folder> ctest --test-dir build/test -R RawPages --output-on-failure
//
// with `page-<lang>-first.json`, `page-<lang>-refined.json` and `page-<lang>-refined-fast.json` bodies
// (tools/lexirise/probe_v7b.py's answers, one per file).

namespace {

std::string readAll(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

}  // namespace

TEST(RawPages, MeasuredPageAnswersParseAndMerge) {
  const char* dir = std::getenv("LEXIPOINT_RAW_PAGES");
  if (dir == nullptr) GTEST_SKIP() << "LEXIPOINT_RAW_PAGES not set";
  int pages = 0;
  for (const auto& [code, language] : {std::pair{"ja", Language::Japanese}, std::pair{"zh", Language::Chinese}}) {
    const std::filesystem::path base(dir);
    const auto first = base / (std::string("page-") + code + "-first.json");
    if (!std::filesystem::exists(first)) continue;
    const std::string body = readAll(first);
    PageAnalysis whole;
    ASSERT_EQ(parsePage(body, language, whole), ParseStatus::Ok) << code;
    PageReader reader(language);
    for (size_t at = 0; at < body.size(); at += 1024) {
      ASSERT_TRUE(reader.onBody(body.data() + at, std::min<size_t>(1024, body.size() - at)));
    }
    PageAnalysis streamed;
    ASSERT_EQ(reader.finish(streamed), ParseStatus::Ok);
    EXPECT_EQ(serializePage(streamed), serializePage(whole));
    const std::string file = serializePage(whole);
    std::printf("%s first: %zu occurrences, %zu entries, %zu states, pool %zu B, file %zu B (answer %zu B)\n", code,
                whole.occurrences.size(), whole.meta.size(), whole.state.size(), whole.pool.size(), file.size(),
                body.size());
    const auto refinedPath = base / (std::string("page-") + code + "-refined.json");
    const auto fastPath = base / (std::string("page-") + code + "-refined-fast.json");
    if (std::filesystem::exists(refinedPath) && std::filesystem::exists(fastPath)) {
      PageAnalysis refined;
      PageAnalysis fast;
      ASSERT_EQ(parsePage(readAll(refinedPath), language, refined), ParseStatus::Ok);
      ASSERT_EQ(parsePage(readAll(fastPath), language, fast), ParseStatus::Ok);
      const PageAnalysis merged = mergeWholeWords(refined, fast);
      std::printf("%s refined %zu occurrences, merged %zu (first pass %zu), file %zu B\n", code,
                  refined.occurrences.size(), merged.occurrences.size(), whole.occurrences.size(),
                  serializePage(merged).size());
    }
    pages++;
  }
  EXPECT_GT(pages, 0) << "no page-*-first.json in " << dir;
}

TEST(PageAnalysis, TheMergeCarriesAStatesCut) {
  PageAnalysis refined = parsed(refinedSentence());
  PageAnalysis words = parsed(wordsSentence());
  EXPECT_FALSE(mergeWholeWords(refined, words).statesCut);
  words.statesCut = true;
  EXPECT_TRUE(mergeWholeWords(refined, words).statesCut);
  words.statesCut = false;
  refined.statesCut = true;
  EXPECT_TRUE(mergeWholeWords(refined, words).statesCut);
}
