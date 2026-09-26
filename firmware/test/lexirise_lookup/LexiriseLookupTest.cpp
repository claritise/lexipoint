// lookup-flow.md §5: the Lexirise lookup against a scripted API: which outcome, and what the card holds.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "lexirise/lookup/Fallback.h"
#include "lexirise/lookup/LexiriseLookup.h"

using lexipoint::Language;
using lexipoint::api::ApiError;
using lexipoint::api::ApiResponse;
using lexipoint::lookup::AnalyzedSentence;
using lexipoint::lookup::LookupCard;
using lexipoint::lookup::LookupOutcome;
using lexipoint::lookup::lookupWithLexirise;
using lexipoint::text::BuiltSentence;
using lexipoint::text::TapContext;

namespace {

class FakeApi final : public lexipoint::api::LexiriseApi {
 public:
  ApiResponse analyzeReply;
  ApiResponse wordsReply = [] {  // analyzeWords: offline unless a test scripts it (the refined answer stands)
    ApiResponse r;
    r.error = lexipoint::api::ApiError::Network;
    return r;
  }();
  ApiResponse lookupReply;
  std::vector<std::string> analyzed;
  std::vector<std::string> analyzedWords;
  std::vector<std::string> looked;

  ApiResponse analyze(Language, std::string_view sentence) override {
    analyzed.emplace_back(sentence);
    return analyzeReply;
  }
  ApiResponse analyzeWords(Language, std::string_view sentence) override {
    analyzedWords.emplace_back(sentence);
    return wordsReply;
  }
  ApiResponse lookup(Language, std::string_view lemma) override {
    looked.emplace_back(lemma);
    return lookupReply;
  }
  ApiResponse write(const lexipoint::net::Request&) override { return {}; }
  ApiResponse deck(const lexipoint::net::Request&) override { return {}; }
};

ApiResponse body(std::string text) {
  ApiResponse r;
  r.status = 200;
  r.body = std::move(text);
  return r;
}

ApiResponse failure(const ApiError error) {
  ApiResponse r;
  r.error = error;
  return r;
}

// 食べさせられた。 tapped on さ (offset 2).
TapContext tap(const uint32_t offset = 2, const Language language = Language::Japanese) {
  TapContext t;
  BuiltSentence s;
  s.text = "食べさせられた。";
  s.tapOffset = offset;
  s.tapLength = 1;
  t.sentence = s;
  t.language.language = language;
  t.language.detected = language;
  return t;
}

constexpr const char* kAnalyze =
    R"({"occurrences":[{"word":"食べさせられた","lemma":"食べる","isWordLike":true,)"
    R"("transliteration":"tabesaserareta","charStart":0,"charEnd":7,"entryId":31,"lemmaEntryId":32},)"
    R"({"word":"。","isWordLike":false,"charStart":7,"charEnd":8}],)"
    R"("entryMetaById":{"31":{"partOfSpeech":["verb"]}},"stateByEntryId":{}})";

constexpr const char* kLookup =
    R"({"word":"食べる","transliteration":"taberu","translation_status":"ready","system_tags":["JLPT-N5"],)"
    R"("translations":[{"translation":"to eat","part_of_speech":["verb"]}]})";

}  // namespace

TEST(LexiriseLookup, NoSentenceOrLanguageMeansLexiriseIsNotAsked) {
  FakeApi api;
  LookupCard card;
  TapContext noSentence = tap();
  noSentence.sentence.reset();
  EXPECT_EQ(lookupWithLexirise(api, noSentence, card).outcome, LookupOutcome::Unavailable);
  TapContext noLanguage = tap();
  noLanguage.language.language.reset();
  EXPECT_EQ(lookupWithLexirise(api, noLanguage, card).outcome, LookupOutcome::Unavailable);
  EXPECT_TRUE(api.analyzed.empty());
}

TEST(LexiriseLookup, AFullCard) {
  FakeApi api;
  api.analyzeReply = body(kAnalyze);
  api.lookupReply = body(kLookup);
  LookupCard card;
  const auto report = lookupWithLexirise(api, tap(), card);
  ASSERT_EQ(report.outcome, LookupOutcome::Card);
  EXPECT_EQ(report.error, ApiError::None);
  EXPECT_EQ(api.analyzed, std::vector<std::string>{"食べさせられた。"});
  EXPECT_EQ(api.looked, std::vector<std::string>{"食べる"});  // the lemma, not the surface
  EXPECT_EQ(card.surface, "食べさせられた");
  EXPECT_EQ(card.surfaceReading, "tabesaserareta");
  EXPECT_EQ(card.lemma, "食べる");
  EXPECT_EQ(card.headword(), "食べる");
  EXPECT_EQ(card.reading, "taberu");  // the lemma's
  EXPECT_EQ(card.partOfSpeech, "verb");
  EXPECT_EQ(card.level, "JLPT-N5");
  ASSERT_EQ(card.senses.size(), 1u);
  EXPECT_EQ(card.entryId, 31u);
  EXPECT_EQ(card.lemmaEntryId, 32u);
  EXPECT_FALSE(card.saved);
  EXPECT_FALSE(card.translationUnavailable);
  EXPECT_TRUE(card.complete);
}

TEST(LexiriseLookup, AnalyzeFailuresAreUnavailable) {
  for (const ApiError e : {ApiError::NoWifi, ApiError::Timeout, ApiError::Unauthorized, ApiError::RateLimited}) {
    FakeApi api;
    api.analyzeReply = failure(e);
    LookupCard card;
    const auto report = lookupWithLexirise(api, tap(), card);
    EXPECT_EQ(report.outcome, LookupOutcome::Unavailable);
    EXPECT_EQ(report.error, e);
    EXPECT_TRUE(api.looked.empty());
  }
  FakeApi api;
  api.analyzeReply = body("{\"occurrences\":");
  LookupCard card;
  const auto report = lookupWithLexirise(api, tap(), card);
  EXPECT_EQ(report.outcome, LookupOutcome::Unavailable);
  EXPECT_EQ(report.error, ApiError::Malformed);
}

TEST(LexiriseLookup, NoWordInTheSentenceIsNotFound) {
  FakeApi api;
  api.analyzeReply = body(R"({"occurrences":[{"word":"。","isWordLike":false,"charStart":0,"charEnd":1}]})");
  LookupCard card;
  EXPECT_EQ(lookupWithLexirise(api, tap(0), card).outcome, LookupOutcome::NotFound);
  EXPECT_TRUE(api.looked.empty());
}

TEST(LexiriseLookup, ALookupFailureStillShowsTheWord) {
  for (const ApiResponse& reply : {failure(ApiError::Timeout), body("not json")}) {
    FakeApi api;
    api.analyzeReply = body(kAnalyze);
    api.lookupReply = reply;
    LookupCard card;
    const auto report = lookupWithLexirise(api, tap(), card);
    ASSERT_EQ(report.outcome, LookupOutcome::Card);
    EXPECT_EQ(report.error, reply.ok() ? ApiError::Malformed : ApiError::Timeout);
    EXPECT_TRUE(card.translationUnavailable);
    EXPECT_EQ(card.lemma, "食べる");
    EXPECT_EQ(card.reading, "");  // not the surface's: 食べる must not read "tabesaserareta"
  }
}

TEST(LexiriseLookup, PendingTranslation) {
  FakeApi api;
  api.analyzeReply = body(kAnalyze);
  api.lookupReply = body(R"({"word":"食べる","translation_status":"pending","translations":[]})");
  LookupCard card;
  ASSERT_EQ(lookupWithLexirise(api, tap(), card).outcome, LookupOutcome::Card);
  EXPECT_TRUE(card.translationPending);
  EXPECT_FALSE(card.translationUnavailable);
}

TEST(LexiriseLookup, SavedStatePrefersTheLemma) {
  const auto analyze = [](const std::string& state) {
    return std::string(R"({"occurrences":[{"word":"食べた","lemma":"食べる","isWordLike":true,)"
                       R"("charStart":0,"charEnd":3,"entryId":41,"lemmaEntryId":42}],"stateByEntryId":{)") +
           state + "}}";
  };
  {
    FakeApi api;
    api.analyzeReply = body(analyze(R"("41":{"saved_expression_id":1},"42":{"saved_expression_id":2})"));
    api.lookupReply = body(kLookup);
    LookupCard card;
    lookupWithLexirise(api, tap(0), card);
    ASSERT_TRUE(card.saved);
    EXPECT_EQ(card.saved->savedExpressionId, "2");
  }
  {
    FakeApi api;
    api.analyzeReply = body(analyze(R"("41":{"saved_expression_id":1})"));
    api.lookupReply = body(kLookup);
    LookupCard card;
    lookupWithLexirise(api, tap(0), card);
    ASSERT_TRUE(card.saved);
    EXPECT_EQ(card.saved->savedExpressionId, "1");  // only the surface is saved
  }
}

TEST(LexiriseLookup, MissingLemmaEntryFallsBackToTheSurfaceEntry) {
  FakeApi api;
  api.analyzeReply =
      body(R"({"occurrences":[{"word":"学习","isWordLike":true,"charStart":0,"charEnd":2,"entryId":51}]})");
  api.lookupReply = body(R"({"word":"学习","translations":[{"translation":"to study"}]})");
  LookupCard card;
  ASSERT_EQ(lookupWithLexirise(api, tap(1, Language::Chinese), card).outcome, LookupOutcome::Card);
  EXPECT_EQ(card.language, Language::Chinese);
  EXPECT_EQ(card.lemmaEntryId, 51u);
  EXPECT_EQ(card.headword(), "学习");
  EXPECT_EQ(card.surface, card.headword());  // the dictionary form itself
}

TEST(LexiriseLookup, ReadingBeforeTheLookup) {
  // The lemma's own meta gives its reading when the lookup fails; a word that is its own lemma keeps
  // the surface reading.
  FakeApi api;
  api.analyzeReply = body(R"({"occurrences":[{"word":"食べた","lemma":"食べる","isWordLike":true,)"
                          R"("transliteration":"tabeta","charStart":0,"charEnd":3,"entryId":61,"lemmaEntryId":62}],)"
                          R"("entryMetaById":{"62":{"transliteration":"taberu"}}})");
  api.lookupReply = failure(ApiError::Network);
  LookupCard card;
  lookupWithLexirise(api, tap(0), card);
  EXPECT_EQ(card.reading, "taberu");
  EXPECT_EQ(card.surfaceReading, "tabeta");

  FakeApi same;
  same.analyzeReply = body(R"({"occurrences":[{"word":"猫","isWordLike":true,"transliteration":"neko",)"
                           R"("charStart":0,"charEnd":1,"entryId":63}]})");
  same.lookupReply = failure(ApiError::Network);
  lookupWithLexirise(same, tap(0), card);
  EXPECT_EQ(card.reading, "neko");
}

TEST(LexiriseLookup, AnEmptyLemmaLooksUpTheSurface) {
  FakeApi api;
  api.analyzeReply = body(R"({"occurrences":[{"word":"猫","lemma":"","isWordLike":true,)"
                          R"("charStart":0,"charEnd":1,"entryId":71}]})");
  api.lookupReply = body(R"({"word":"猫","translations":[{"translation":"cat"}]})");
  LookupCard card;
  ASSERT_EQ(lookupWithLexirise(api, tap(0), card).outcome, LookupOutcome::Card);
  EXPECT_EQ(api.looked, std::vector<std::string>{"猫"});
  EXPECT_EQ(card.headword(), "猫");
}

namespace {

// v0.2 V1: 这首歌深深地打动了我。 as Lexirise returns it refined (深深 cut into 深 · 深) and word-level (fast).
TapContext zhTap(const uint32_t offset) {
  TapContext t = tap(offset, Language::Chinese);
  t.sentence->text = "这首歌深深地打动了我。";
  return t;
}

std::string zhAnalyze(const bool pending) {
  return std::string(R"({"occurrences":[{"word":"这","isWordLike":true,"charStart":0,"charEnd":1,"entryId":1},)"
                     R"({"word":"首","isWordLike":true,"charStart":1,"charEnd":2,"entryId":2},)"
                     R"({"word":"歌","isWordLike":true,"charStart":2,"charEnd":3,"entryId":3},)"
                     R"({"word":"深","isWordLike":true,"charStart":3,"charEnd":4,"entryId":58,"lemmaEntryId":58},)"
                     R"({"word":"深","isWordLike":true,"charStart":4,"charEnd":5,"entryId":58,"lemmaEntryId":58},)"
                     R"({"word":"地","isWordLike":true,"charStart":5,"charEnd":6,"entryId":5},)"
                     R"({"word":"。","isWordLike":false,"charStart":10,"charEnd":11}],)"
                     R"("entryMetaById":{"58":{"transliteration":"shēn","rank":512}},"stateByEntryId":{},)"
                     R"("morphoPending":)") +
         (pending ? "true}" : "false}");
}

constexpr const char* kZhWords =
    R"({"occurrences":[{"word":"这","isWordLike":true,"charStart":0,"charEnd":1,"entryId":1},)"
    R"({"word":"首","isWordLike":true,"charStart":1,"charEnd":2,"entryId":2},)"
    R"({"word":"歌","isWordLike":true,"charStart":2,"charEnd":3,"entryId":3},)"
    R"({"word":"深深","isWordLike":true,"transliteration":"shēnshēn","charStart":3,"charEnd":5,"entryId":146},)"
    R"({"word":"地","isWordLike":true,"charStart":5,"charEnd":6,"entryId":5},)"
    R"({"word":"。","isWordLike":false,"charStart":10,"charEnd":11}],)"
    R"("entryMetaById":{"146":{"transliteration":"shēnshēn","partOfSpeech":["adverb"],"rank":4180}},)"
    R"("stateByEntryId":{"146":{"saved_expression_id":7,"proficiency":2}},"morphoPending":false})";

}  // namespace

TEST(WholeWordsLookup, ARefinedAnswerGetsTheWholeWordBack) {
  FakeApi api;
  api.analyzeReply = body(zhAnalyze(false));
  api.wordsReply = body(kZhWords);
  AnalyzedSentence sentence;
  size_t word = 0;
  ASSERT_EQ(lexipoint::lookup::analyzeTap(api, zhTap(4), sentence, word).outcome, LookupOutcome::Card);
  EXPECT_EQ(api.analyzedWords, std::vector<std::string>{"这首歌深深地打动了我。"});
  const LookupCard card = lexipoint::lookup::cardFor(sentence, word);
  EXPECT_EQ(card.surface, "深深");  // not 深
  EXPECT_EQ(card.headword(), "深深");
  EXPECT_EQ(card.entryId, 146u);
  EXPECT_EQ(card.lemmaEntryId, 146u);
  EXPECT_EQ(card.charStart, 3u);
  EXPECT_EQ(card.charEnd, 5u);
  EXPECT_EQ(card.reading, "shēnshēn");
  EXPECT_EQ(card.partOfSpeech, "adverb");
  EXPECT_EQ(card.rank, 4180u);
  EXPECT_TRUE(card.saved);               // the whole word's own saved state
  EXPECT_EQ(sentence.words.size(), 5u);  // stepping goes word by word: 这 首 歌 深深 地
}

TEST(WholeWordsLookup, AFirstPassAnswerAsksNothingMore) {
  FakeApi api;
  api.analyzeReply = body(zhAnalyze(true));  // morphoPending: the fast split already
  api.wordsReply = body(kZhWords);
  AnalyzedSentence sentence;
  size_t word = 0;
  ASSERT_EQ(lexipoint::lookup::analyzeTap(api, zhTap(4), sentence, word).outcome, LookupOutcome::Card);
  EXPECT_TRUE(api.analyzedWords.empty());
  EXPECT_EQ(lexipoint::lookup::cardFor(sentence, word).surface, "深");
}

TEST(WholeWordsLookup, WithoutTheWordLevelAnswerTheRefinedOneStands) {
  for (const ApiResponse& reply : {failure(ApiError::Network), failure(ApiError::RateLimited), body("{not json")}) {
    FakeApi api;
    api.analyzeReply = body(zhAnalyze(false));
    api.wordsReply = reply;
    AnalyzedSentence sentence;
    size_t word = 0;
    ASSERT_EQ(lexipoint::lookup::analyzeTap(api, zhTap(3), sentence, word).outcome, LookupOutcome::Card);
    EXPECT_EQ(api.analyzedWords.size(), 1u);
    EXPECT_EQ(lexipoint::lookup::cardFor(sentence, word).surface, "深");
  }
}

TEST(WholeWordsLookup, EitherPieceOfTheWordFindsIt) {
  for (const uint32_t offset : {3u, 4u}) {
    FakeApi api;
    api.analyzeReply = body(zhAnalyze(false));
    api.wordsReply = body(kZhWords);
    AnalyzedSentence sentence;
    size_t word = 0;
    ASSERT_EQ(lexipoint::lookup::analyzeTap(api, zhTap(offset), sentence, word).outcome, LookupOutcome::Card);
    EXPECT_EQ(lexipoint::lookup::cardFor(sentence, word).surface, "深深") << offset;
  }
}

TEST(LookupCard, HeadwordIsTheLemmaElseTheSurface) {
  LookupCard card;
  card.surface = "学习";
  EXPECT_EQ(card.headword(), "学习");
  card.surface = "食べた";
  card.lemma = "食べる";
  EXPECT_EQ(card.headword(), "食べる");
}

TEST(LexiriseLookup, NoLemmaEntryMeansNoLemmaReading) {
  // A lemma that differs, but no lemmaEntryId: the only meta is the surface's, which isn't the lemma's.
  FakeApi api;
  api.analyzeReply = body(R"({"occurrences":[{"word":"食べた","lemma":"食べる","isWordLike":true,)"
                          R"("transliteration":"tabeta","charStart":0,"charEnd":3,"entryId":81}],)"
                          R"("entryMetaById":{"81":{"transliteration":"tabeta"}}})");
  api.lookupReply = failure(ApiError::Timeout);
  LookupCard card;
  ASSERT_EQ(lookupWithLexirise(api, tap(0), card).outcome, LookupOutcome::Card);
  EXPECT_EQ(card.headword(), "食べる");
  EXPECT_EQ(card.reading, "");
  EXPECT_EQ(card.lemmaEntryId, 81u);  // Save falls back to the surface entry
}

namespace {

// 彼は本を読んだ。: four words and a full stop; 本 is saved (proficiency 3), 読んだ's lemma is 読む.
constexpr const char* kSentence = "彼は本を読んだ。";
constexpr const char* kAnalyzeSentence =
    R"({"occurrences":[)"
    R"({"word":"彼","isWordLike":true,"transliteration":"kare","charStart":0,"charEnd":1,"entryId":1},)"
    R"({"word":"は","isWordLike":true,"transliteration":"wa","charStart":1,"charEnd":2,"entryId":2},)"
    R"({"word":"本","isWordLike":true,"transliteration":"hon","charStart":2,"charEnd":3,"entryId":3},)"
    R"({"word":"を","isWordLike":true,"transliteration":"wo","charStart":3,"charEnd":4,"entryId":4},)"
    R"({"word":"読んだ","lemma":"読む","isWordLike":true,"transliteration":"yonda","charStart":4,"charEnd":7,)"
    R"("entryId":5,"lemmaEntryId":6},)"
    R"({"word":"。","isWordLike":false,"charStart":7,"charEnd":8}],)"
    R"("entryMetaById":{"3":{"transliteration":"hon","rank":120,"frequencyScore":0.61,"partOfSpeech":["noun"]},)"
    R"("5":{"transliteration":"yonda","rank":900},"6":{"transliteration":"yomu","rank":400,"partOfSpeech":["verb"]}},)"
    R"("stateByEntryId":{"3":{"saved_expression_id":77,"proficiency":3}}})";

TapContext sentenceTap(const uint32_t offset) {
  TapContext t = tap(offset);
  t.sentence->text = kSentence;
  return t;
}

}  // namespace

TEST(SentenceLookup, AnalyzeOnceThenEveryWordIsACardWithoutAsking) {
  FakeApi api;
  api.analyzeReply = body(kAnalyzeSentence);
  lexipoint::lookup::AnalyzedSentence sentence;
  size_t word = 99;
  ASSERT_EQ(lexipoint::lookup::analyzeTap(api, sentenceTap(2), sentence, word).outcome, LookupOutcome::Card);
  EXPECT_EQ(sentence.words.size(), 5u);  // the full stop isn't a word
  EXPECT_EQ(word, 2u);                   // 本
  EXPECT_TRUE(api.looked.empty());       // phase B hasn't run

  const LookupCard hon = lexipoint::lookup::cardFor(sentence, 2);
  EXPECT_EQ(hon.headword(), "本");
  EXPECT_EQ(hon.reading, "hon");
  EXPECT_EQ(hon.rank, 120u);
  EXPECT_FLOAT_EQ(hon.frequency, 0.61f);  // the bars match the rank before phase B
  EXPECT_EQ(hon.charStart, 2u);
  EXPECT_EQ(hon.charEnd, 3u);
  ASSERT_TRUE(hon.saved);
  EXPECT_EQ(hon.saved->savedExpressionId, "77");
  EXPECT_EQ(hon.saved->proficiency, 3);
  EXPECT_FALSE(hon.complete);

  const LookupCard yomu = lexipoint::lookup::cardFor(sentence, 4);  // a step right, twice
  EXPECT_EQ(yomu.headword(), "読む");
  EXPECT_EQ(yomu.reading, "yomu");   // the lemma's entry, not the surface's yonda
  EXPECT_EQ(yomu.partOfSpeech, "");  // the surface entry has none (inflected forms don't)
  EXPECT_EQ(yomu.rank, 400u);        // the lemma's rank
  EXPECT_EQ(yomu.charStart, 4u);
  EXPECT_EQ(yomu.charEnd, 7u);
  EXPECT_FALSE(yomu.saved);
  EXPECT_EQ(api.analyzed.size(), 1u);  // one analyze for the whole sentence
}

TEST(SentenceLookup, CompleteCardFillsPhaseBOrMarksItUnavailable) {
  FakeApi api;
  api.analyzeReply = body(kAnalyzeSentence);
  api.lookupReply =
      body(R"({"word":"読む","transliteration":"yomu","rank":350,"frequency_score":0.8,)"
           R"("system_tags":["JLPT-N5"],"translations":[{"translation":"to read","part_of_speech":["verb"]}]})");
  lexipoint::lookup::AnalyzedSentence sentence;
  size_t word = 0;
  lexipoint::lookup::analyzeTap(api, sentenceTap(5), sentence, word);
  EXPECT_EQ(word, 4u);
  LookupCard card = lexipoint::lookup::cardFor(sentence, word);
  EXPECT_EQ(lexipoint::lookup::completeCard(api, card), ApiError::None);
  EXPECT_EQ(api.looked, std::vector<std::string>{"読む"});
  EXPECT_TRUE(card.complete);
  EXPECT_EQ(card.rank, 350u);
  EXPECT_FLOAT_EQ(card.frequency, 0.8f);
  EXPECT_EQ(card.partOfSpeech, "verb");  // from the sense, the surface entry had none
  ASSERT_EQ(card.senses.size(), 1u);

  api.lookupReply = failure(ApiError::Timeout);
  LookupCard offline = lexipoint::lookup::cardFor(sentence, 2);
  EXPECT_EQ(lexipoint::lookup::completeCard(api, offline), ApiError::Timeout);
  EXPECT_TRUE(offline.complete);
  EXPECT_TRUE(offline.translationUnavailable);
  EXPECT_EQ(offline.headword(), "本");  // still a card
}

TEST(LexiriseLookup, LexiriseIsAskedOnlyWithASentenceALanguageAndAKey) {
  using lexipoint::lookup::asksLexirise;
  EXPECT_TRUE(asksLexirise(tap(), true));
  EXPECT_FALSE(asksLexirise(tap(), false));  // no key, off, or the language switched off: StarDict at once
  TapContext noSentence = tap();
  noSentence.sentence.reset();
  EXPECT_FALSE(asksLexirise(noSentence, true));
  TapContext noLanguage = tap();
  noLanguage.language.language.reset();
  EXPECT_FALSE(asksLexirise(noLanguage, true));
}

TEST(Fallback, TheGateSaysWhyLexiriseIsntAsked) {
  using lexipoint::lookup::Gate;
  using lexipoint::lookup::lexiriseGate;
  using Block = lexipoint::api::AccessPolicy::Block;
  const lexipoint::text::BookLanguage ja("ja", std::nullopt);
  lexipoint::Settings s;
  s.enabled = true;
  s.apiKey = "lx_TESTKEYtestkey0123456789";
  EXPECT_EQ(lexiriseGate(s, ja, Block::None), Gate::Ask);
  EXPECT_EQ(lexiriseGate(s, ja, Block::Rejected), Gate::Rejected);
  EXPECT_EQ(lexiriseGate(s, ja, Block::RateLimited), Gate::RateLimited);
  s.apiKey.clear();
  EXPECT_EQ(lexiriseGate(s, ja, Block::None), Gate::NoKey);
  s.enabled = false;
  EXPECT_EQ(lexiriseGate(s, ja, Block::None), Gate::Off);  // off beats "no key": nothing to say
}

TEST(Fallback, WhatTheUserIsToldWhenLexiriseDoesntAnswer) {
  using lexipoint::lookup::fallbackFor;
  using lexipoint::lookup::Notice;
  EXPECT_EQ(fallbackFor(ApiError::Unauthorized).notice, Notice::KeyRejected);
  EXPECT_EQ(fallbackFor(ApiError::RateLimited).notice, Notice::RateLimited);
  for (const ApiError e :
       {ApiError::NoWifi, ApiError::Network, ApiError::Timeout, ApiError::ClockNotSet, ApiError::Server}) {
    EXPECT_TRUE(fallbackFor(e).offline);
    EXPECT_EQ(fallbackFor(e).notice, Notice::None);
  }
  for (const ApiError e :
       {ApiError::NotConfigured, ApiError::Tls, ApiError::LowMemory, ApiError::Malformed, ApiError::Http}) {
    EXPECT_FALSE(fallbackFor(e).offline);
    EXPECT_EQ(fallbackFor(e).notice, Notice::None);
  }
}

TEST(Fallback, AGateSaysItsBlockOnceOrAlwaysWithoutStarDict) {
  using lexipoint::lookup::Gate;
  using lexipoint::lookup::gateFallback;
  using lexipoint::lookup::Notice;
  using Block = lexipoint::api::AccessPolicy::Block;
  // A wrong key found by the web page's check: the first lookup says so, later ones go quietly to StarDict.
  EXPECT_EQ(gateFallback(Gate::Rejected, false, Block::Rejected, true).notice, Notice::KeyRejected);
  EXPECT_EQ(gateFallback(Gate::Rejected, false, Block::None, true).notice, Notice::None);
  // No StarDict: the block says more than "No dictionary set", every time.
  EXPECT_EQ(gateFallback(Gate::RateLimited, false, Block::None, false).notice, Notice::RateLimited);
  EXPECT_EQ(gateFallback(Gate::NoKey, true, Block::None, true).notice, Notice::NoKey);
  EXPECT_EQ(gateFallback(Gate::NoKey, false, Block::None, true).notice, Notice::None);  // said this boot
  EXPECT_EQ(gateFallback(Gate::Off, false, Block::None, false).notice, Notice::None);
}

TEST(Fallback, ThePlanWordSelectCarriesOut) {
  using lexipoint::lookup::Fallback;
  using lexipoint::lookup::Notice;
  using lexipoint::lookup::planFallback;
  auto p = planFallback(Fallback{Notice::KeyRejected, false}, true);
  EXPECT_EQ(p.notice, Notice::KeyRejected);  // the notice, then StarDict
  EXPECT_TRUE(p.starDict);
  EXPECT_FALSE(p.noDictionary);
  p = planFallback(Fallback{Notice::RateLimited, false}, false);
  EXPECT_FALSE(p.starDict);  // the notice alone: it says more than "No dictionary set"
  EXPECT_FALSE(p.noDictionary);
  p = planFallback(Fallback{Notice::None, true}, true);
  EXPECT_TRUE(p.starDict);
  EXPECT_TRUE(p.offline);  // StarDict's title marked
  p = planFallback(Fallback{Notice::None, true}, false);
  EXPECT_TRUE(p.noDictionary);
  EXPECT_FALSE(p.offline);
}

TEST(Fallback, AnUnsentSaveSaysWhy) {
  using lexipoint::lookup::Notice;
  using lexipoint::lookup::noticeForUnsentSave;
  EXPECT_EQ(noticeForUnsentSave(ApiError::RateLimited), Notice::RateLimited);
  EXPECT_EQ(noticeForUnsentSave(ApiError::Unauthorized), Notice::KeyRejected);
  EXPECT_EQ(noticeForUnsentSave(ApiError::NoWifi), Notice::SaveFailed);
}
