// lookup-flow.md §5: the Lexirise lookup against a scripted API: which outcome, and what the card holds.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "lexirise/lookup/LexiriseLookup.h"

using lexipoint::Language;
using lexipoint::api::ApiError;
using lexipoint::api::ApiResponse;
using lexipoint::lookup::LookupCard;
using lexipoint::lookup::LookupOutcome;
using lexipoint::lookup::lookupWithLexirise;
using lexipoint::text::BuiltSentence;
using lexipoint::text::TapContext;

namespace {

class FakeApi final : public lexipoint::api::LexiriseApi {
 public:
  ApiResponse analyzeReply;
  ApiResponse lookupReply;
  std::vector<std::string> analyzed;
  std::vector<std::string> looked;

  ApiResponse analyze(Language, std::string_view sentence) override {
    analyzed.emplace_back(sentence);
    return analyzeReply;
  }
  ApiResponse lookup(Language, std::string_view lemma) override {
    looked.emplace_back(lemma);
    return lookupReply;
  }
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
  const std::string text = card.plainText();
  EXPECT_NE(text.find("食べる  taberu · verb · JLPT-N5"), std::string::npos) << text;
  EXPECT_NE(text.find("(食べさせられた)"), std::string::npos) << text;
  EXPECT_NE(text.find("1. to eat"), std::string::npos) << text;
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
    EXPECT_NE(card.plainText().find("(meaning unavailable)"), std::string::npos);
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
  EXPECT_NE(card.plainText().find("pending"), std::string::npos);
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
  EXPECT_EQ(card.plainText().find("("), std::string::npos);  // surface == headword: no second line
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

TEST(LookupCard, PlainText) {
  LookupCard card;
  card.surface = "学习";
  EXPECT_EQ(card.headword(), "学习");
  EXPECT_EQ(card.plainText(), "学习\n\n");  // nothing known: the word alone
  card.lemma = "学习";
  card.reading = "xuéxí";
  card.level = "HSK-1";
  card.saved = lexipoint::api::EntryState{"9", 1, 2};
  card.senses = {{"to study", "verb"}, {"learning", "noun"}};
  EXPECT_EQ(card.plainText(), "学习  xuéxí · HSK-1 · saved\n\n1. to study\n2. learning\n");
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

TEST(LexiriseLookup, ChainStep) {
  using lexipoint::lookup::chainStep;
  using lexipoint::lookup::ChainStep;
  for (const bool starDict : {false, true}) {
    EXPECT_EQ(chainStep(LookupOutcome::Card, starDict), ChainStep::ShowCard);
    EXPECT_EQ(chainStep(LookupOutcome::NotFound, starDict), ChainStep::ShowNotFound);  // final: no StarDict
  }
  EXPECT_EQ(chainStep(LookupOutcome::Unavailable, true), ChainStep::RunStarDict);
  EXPECT_EQ(chainStep(LookupOutcome::Unavailable, false), ChainStep::NoDictionary);
}
