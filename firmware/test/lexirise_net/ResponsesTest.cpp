// Fixtures are synthetic but follow the live shapes (lexirise-api-notes.md); the public repo never
// holds real responses, IDs or account data.

#include <gtest/gtest.h>

#include <string>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/api/Responses.h"

using lexipoint::api::AnalyzeResult;
using lexipoint::api::MeInfo;
using lexipoint::api::parseAnalyze;
using lexipoint::api::parseMe;
using lexipoint::api::ParseStatus;

namespace {

constexpr const char* kMe =
    R"({"user":{"id":"u_x","email":"reader@example.com","name":"Reader","plan":"free","languages":["ja","zh"],)"
    R"("native_lang":"en"},"apiKey":{"id":"k_x","name":"device","start":"lx_A","lastRequest":"2026-09-24T00:00:00Z",)"
    R"("rateLimitMax":1200,"rateLimitTimeWindow":3600000}})";

// 彼は東京へ行った。 (lemma only where it differs, punctuation without a reading, undocumented extras).
constexpr const char* kAnalyze =
    R"({"occurrences":[)"
    R"({"word":"彼","isWordLike":true,"transliteration":"kare","charStart":0,"charEnd":1,"entryId":11,)"
    R"("lemmaEntryId":11,"lang":"ja","normalized":"彼","subTokenCount":1,"multipleReadings":["kare","ka"]},)"
    R"({"word":"東京","isWordLike":true,"transliteration":"toukyou","charStart":2,"charEnd":4,"entryId":12,)"
    R"("lemmaEntryId":12,"breakdown":[{"c":"東"}],"morphemes":[]},)"
    R"({"word":"行った","lemma":"行く","isWordLike":true,"transliteration":"itta","charStart":5,"charEnd":8,)"
    R"("entryId":13,"lemmaEntryId":14,"feats":{"Tense":"Past"}},)"
    R"({"word":"。","isWordLike":false,"charStart":8,"charEnd":9,"lang":"ja"}],)"
    R"("grammar":[],"grammarStates":{},"morphoPending":false,)"
    R"("entryMetaById":{"11":{"entryId":11,"transliteration":"kare","partOfSpeech":["pronoun"]}},)"
    R"("stateByEntryId":{}})";

}  // namespace

TEST(ResponsesMe, KeepsRateLimitsNameAndPlan) {
  MeInfo me;
  ASSERT_EQ(parseMe(kMe, me), ParseStatus::Ok);
  EXPECT_EQ(me.rateLimitMax, 1200u);
  EXPECT_EQ(me.rateLimitWindowMs, 3600000u);
  EXPECT_EQ(me.name, "Reader");
  EXPECT_EQ(me.plan, "free");
  MeInfo longName;
  ASSERT_EQ(parseMe(R"({"user":{"name":")" + std::string(65, 'n') + R"("},"apiKey":{"rateLimitMax":1}})", longName),
            ParseStatus::Ok);
  EXPECT_EQ(longName.name, "");
  EXPECT_EQ(parseMe(R"({"user":{}})", me), ParseStatus::Malformed);
  EXPECT_EQ(parseMe(R"({"apiKey":{"rateLimitMax":"1200"}})", me), ParseStatus::Malformed);
  EXPECT_EQ(parseMe("{\"apiKey\":", me), ParseStatus::Malformed);
}

TEST(ResponsesAnalyze, ParsesOccurrences) {
  AnalyzeResult r;
  ASSERT_EQ(parseAnalyze(kAnalyze, r), ParseStatus::Ok);
  ASSERT_EQ(r.occurrences.size(), 4u);
  EXPECT_EQ(r.occurrences[0].word, "彼");
  EXPECT_EQ(r.occurrences[0].lemma, "彼");  // omitted lemma falls back to the word
  EXPECT_EQ(r.occurrences[0].reading, "kare");
  EXPECT_EQ(r.occurrences[1].charStart, 2u);
  EXPECT_EQ(r.occurrences[1].charEnd, 4u);
  EXPECT_EQ(r.occurrences[2].lemma, "行く");
  EXPECT_EQ(r.occurrences[2].entryId, 13u);
  EXPECT_EQ(r.occurrences[2].lemmaEntryId, 14u);
  EXPECT_TRUE(r.occurrences[2].wordLike);
  EXPECT_FALSE(r.occurrences[3].wordLike);
  EXPECT_EQ(r.occurrences[3].reading, "");
  EXPECT_FALSE(r.morphoPending);
}

TEST(ResponsesAnalyze, KeyOrderDoesNotMatter) {
  AnalyzeResult r;
  ASSERT_EQ(parseAnalyze(R"({"morphoPending":true,"occurrences":[{"charEnd":1,"charStart":0,"word":"x"}]})", r),
            ParseStatus::Ok);
  EXPECT_TRUE(r.morphoPending);
  EXPECT_EQ(r.occurrences[0].word, "x");
}

TEST(ResponsesAnalyze, RejectsBrokenResponses) {
  AnalyzeResult r;
  EXPECT_EQ(parseAnalyze(R"({"grammar":[]})", r), ParseStatus::Malformed);  // no occurrences
  EXPECT_EQ(parseAnalyze(R"({"occurrences":[{"charStart":0,"charEnd":1}]})", r), ParseStatus::Malformed);  // no word
  EXPECT_EQ(parseAnalyze(R"({"occurrences":[{"word":"x","charStart":3,"charEnd":1}]})", r), ParseStatus::Malformed);
  EXPECT_EQ(parseAnalyze(R"({"occurrences":[{"word":"x","charStart":-1,"charEnd":1}]})", r), ParseStatus::Malformed);
  EXPECT_EQ(parseAnalyze(R"({"occurrences":[{"word":"x","charStart":0.5,"charEnd":1}]})", r), ParseStatus::Malformed);
  EXPECT_EQ(parseAnalyze(R"({"occurrences":[{"word":"x","charEnd":1}]})", r), ParseStatus::Malformed);    // no start
  EXPECT_EQ(parseAnalyze(R"({"occurrences":[{"word":"x","charStart":0}]})", r), ParseStatus::Malformed);  // no end
  const std::string truncated(kAnalyze, 120);
  EXPECT_EQ(parseAnalyze(truncated, r), ParseStatus::Malformed);
  EXPECT_TRUE(r.occurrences.empty());  // the output is untouched on failure
}

TEST(ResponsesAnalyze, UndocumentedFieldsMayBeMissingOrOddlyTyped) {
  AnalyzeResult r;
  ASSERT_EQ(parseAnalyze(R"({"occurrences":[{"word":"x","charStart":0,"charEnd":1,"transliteration":null,)"
                         R"("lemma":7,"breakdown":"?"}]})",
                         r),
            ParseStatus::Ok);
  EXPECT_EQ(r.occurrences[0].reading, "");
  EXPECT_EQ(r.occurrences[0].lemma, "x");
}

TEST(ResponsesAnalyze, LimitsAreEnforced) {
  std::string many = R"({"occurrences":[)";
  for (size_t i = 0; i <= lexipoint::config::kMaxOccurrences; i++) {
    many += (i ? "," : "") + std::string(R"({"word":"a","charStart":0,"charEnd":1})");
  }
  many += "]}";
  AnalyzeResult r;
  EXPECT_EQ(parseAnalyze(many, r), ParseStatus::OverLimit);
  const std::string longWord(lexipoint::config::kMaxTokenBytes + 1, 'a');
  EXPECT_EQ(parseAnalyze(R"({"occurrences":[{"word":")" + longWord + R"(","charStart":0,"charEnd":1}]})", r),
            ParseStatus::OverLimit);
}
