// lexirise-client.md §4: analyze's entryMetaById/stateByEntryId and dictionary/lookup. Fixtures are
// synthetic (the public repo never holds real responses).

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/api/Responses.h"

using lexipoint::api::AnalyzeResult;
using lexipoint::api::LookupResult;
using lexipoint::api::parseAnalyze;
using lexipoint::api::parseLookup;
using lexipoint::api::ParseStatus;

namespace {

constexpr const char* kAnalyzeWithState =
    R"({"occurrences":[{"word":"食べた","lemma":"食べる","isWordLike":true,"charStart":0,"charEnd":3,)"
    R"("entryId":21,"lemmaEntryId":22}],"morphoPending":false,)"
    R"("entryMetaById":{"21":{"entryId":21,"transliteration":"tabeta","rank":310,)"
    R"("partOfSpeech":["verb","aux"],"tags":["x"]},"abc":{"transliteration":"skip"}},)"
    R"("stateByEntryId":{"22":{"saved_expression_id":901,"proficiency":2,"seen_count":7},)"
    R"("23":{"saved_expression_id":"se_x","proficiency":9},"24":{"proficiency":1}}})";

constexpr const char* kLookup =
    R"({"word":"食べる","transliteration":"taberu","rank":310,"frequency_score":0.9,)"
    R"("translation_status":"ready","system_tags":["kanji","JLPT-N5"],)"
    R"("translations":[{"translation":"to eat","part_of_speech":["verb"]},)"
    R"({"translation":"","part_of_speech":["verb"]},{"translation":"to live on","part_of_speech":[]},)"
    R"({"translation":"third kept? no","part_of_speech":["verb"]}],"breakdown":[{"c":"食"}]})";

}  // namespace

TEST(LookupParse, AnalyzeMetaAndState) {
  AnalyzeResult r;
  ASSERT_EQ(parseAnalyze(kAnalyzeWithState, r), ParseStatus::Ok);
  const auto* meta = r.metaFor(21);
  ASSERT_NE(meta, nullptr);
  EXPECT_EQ(meta->reading, "tabeta");
  EXPECT_EQ(meta->partOfSpeech, "verb");  // the first
  EXPECT_EQ(meta->rank, 310u);
  EXPECT_EQ(r.metaFor(22), nullptr);
  EXPECT_EQ(r.meta.size(), 1u);  // a key that isn't an id is skipped

  const auto* saved = r.stateFor(22);
  ASSERT_NE(saved, nullptr);
  EXPECT_EQ(saved->savedExpressionId, "901");  // a number, kept as text
  EXPECT_EQ(saved->proficiency, 2);
  EXPECT_EQ(saved->seenCount, 7u);
  const auto* stringId = r.stateFor(23);
  ASSERT_NE(stringId, nullptr);
  EXPECT_EQ(stringId->savedExpressionId, "se_x");
  EXPECT_EQ(stringId->proficiency, 0);  // out of range: ignored
  EXPECT_EQ(r.stateFor(24), nullptr);   // no saved id: not saved
  EXPECT_EQ(r.stateFor(21), nullptr);
}

TEST(LookupParse, Lookup) {
  LookupResult r;
  ASSERT_EQ(parseLookup(kLookup, r), ParseStatus::Ok);
  EXPECT_EQ(r.word, "食べる");
  EXPECT_EQ(r.reading, "taberu");
  EXPECT_EQ(r.level, "JLPT-N5");
  EXPECT_EQ(r.rank, 310u);
  EXPECT_FLOAT_EQ(r.frequency, 0.9f);
  EXPECT_FALSE(r.translationPending);
  // The first kMaxTranslations entries with a translation: the empty one is skipped, not counted.
  static_assert(lexipoint::config::kMaxTranslations == 2);
  ASSERT_EQ(r.senses.size(), 2u);
  EXPECT_EQ(r.senses[0].translation, "to eat");
  EXPECT_EQ(r.senses[0].partOfSpeech, "verb");
  EXPECT_EQ(r.senses[1].translation, "to live on");
  EXPECT_EQ(r.senses[1].partOfSpeech, "");
}

TEST(LookupParse, LookupLevelTags) {
  const auto level = [](const std::string& tags) {
    LookupResult r;
    EXPECT_EQ(parseLookup(R"({"word":"w","system_tags":[)" + tags + "]}", r), ParseStatus::Ok);
    return r.level;
  };
  EXPECT_EQ(level(R"("HSK-1")"), "HSK-1");
  EXPECT_EQ(level(R"("char","HSK-7+")"), "HSK-7+");
  EXPECT_EQ(level(R"("HSK-3","HSK-4")"), "HSK-3");  // the first
  for (const char* none : {R"("HSK-10")", R"("HSK-0")", R"("JLPT-N6")", R"("JLPT-N")", R"("hsk-1")", "1", ""}) {
    EXPECT_EQ(level(none), "") << none;
  }
}

TEST(LookupParse, LookupPendingAndLimits) {
  LookupResult r;
  ASSERT_EQ(parseLookup(R"({"word":"罕","translation_status":"pending","translations":[]})", r), ParseStatus::Ok);
  EXPECT_TRUE(r.translationPending);
  EXPECT_TRUE(r.senses.empty());

  // A long translation is cut at a character boundary.
  std::string longText;
  while (longText.size() < lexipoint::config::kMaxTranslationBytes + 10) longText += "あ";
  ASSERT_EQ(parseLookup(R"({"word":"w","translations":[{"translation":")" + longText + R"("}]})", r), ParseStatus::Ok);
  ASSERT_EQ(r.senses.size(), 1u);
  EXPECT_LE(r.senses[0].translation.size(), lexipoint::config::kMaxTranslationBytes);
  EXPECT_EQ(r.senses[0].translation.size() % 3, 0u);

  EXPECT_EQ(parseLookup(R"({"translations":[]})", r), ParseStatus::Malformed);  // no word
  EXPECT_EQ(parseLookup(R"({"word":""})", r), ParseStatus::Malformed);
  EXPECT_EQ(parseLookup(R"({"word":1})", r), ParseStatus::Malformed);
  EXPECT_EQ(parseLookup(R"({"word":"w")", r), ParseStatus::Malformed);  // truncated
}

TEST(LookupParse, ChineseLookup) {
  // 学习 (synthetic, shaped like a zh response: pinyin, an HSK tag among others).
  LookupResult r;
  ASSERT_EQ(parseLookup(R"({"word":"学习","transliteration":"xuéxí","translation_status":"ready",)"
                        R"("system_tags":["word","HSK-1"],"translations":[{"translation":"to study",)"
                        R"("part_of_speech":["verb","noun"]}]})",
                        r),
            ParseStatus::Ok);
  EXPECT_EQ(r.reading, "xuéxí");
  EXPECT_EQ(r.level, "HSK-1");
  ASSERT_EQ(r.senses.size(), 1u);
  EXPECT_EQ(r.senses[0].partOfSpeech, "verb");
}

TEST(LookupParse, HostileShapesDegradeQuietly) {
  AnalyzeResult a;
  const std::string occ = R"({"occurrences":[{"word":"x","isWordLike":true,"charStart":0,"charEnd":1,"entryId":1}],)";
  for (const char* maps : {
           R"("entryMetaById":[{"transliteration":"a"}],"stateByEntryId":[1,2])",         // arrays, not objects
           R"("entryMetaById":{"99999999999":{"transliteration":"a"},"-1":{"rank":1}})",  // not a uint32 id
           R"("entryMetaById":{"1":{"transliteration":5,"partOfSpeech":"verb","rank":-3}})",
           R"("stateByEntryId":{"1":{"saved_expression_id":true,"proficiency":1.5}})",
           R"("stateByEntryId":{"1":{"saved_expression_id":null,"proficiency":-1}})",
       }) {
    SCOPED_TRACE(maps);
    ASSERT_EQ(parseAnalyze(occ + maps + "}", a), ParseStatus::Ok);
    EXPECT_EQ(a.stateFor(1), nullptr);
    if (const auto* meta = a.metaFor(1)) {
      EXPECT_EQ(meta->reading, "");
      EXPECT_EQ(meta->partOfSpeech, "");
      EXPECT_EQ(meta->rank, 0u);
    }
    EXPECT_LE(a.meta.size(), 1u);
  }

  LookupResult r;
  ASSERT_EQ(parseLookup(R"({"word":"w","translations":{"translation":"x"},"system_tags":"HSK-1"})", r),
            ParseStatus::Ok);
  EXPECT_TRUE(r.senses.empty());
  EXPECT_EQ(r.level, "");
  ASSERT_EQ(parseLookup(R"({"word":"w","translations":[{"translation":7},"x",{"part_of_speech":{"a":"b"}}]})", r),
            ParseStatus::Ok);
  EXPECT_TRUE(r.senses.empty());
}

TEST(LookupParse, EntriesPastTheCapAreDroppedNotFatal) {
  std::string maps = R"({"occurrences":[{"word":"x","isWordLike":true,"charStart":0,"charEnd":1,"entryId":1}],)"
                     R"("entryMetaById":{"1":{"transliteration":"first"})";
  for (size_t i = 2; i <= lexipoint::config::kMaxEntries + 50; i++) {
    maps += ",\"" + std::to_string(i) + R"(":{"rank":1})";
  }
  maps += "}}";
  AnalyzeResult a;
  ASSERT_EQ(parseAnalyze(maps, a), ParseStatus::Ok);
  EXPECT_EQ(a.meta.size(), lexipoint::config::kMaxEntries);
  ASSERT_NE(a.metaFor(1), nullptr);
  EXPECT_EQ(a.metaFor(1)->reading, "first");
}

TEST(LookupParse, ALeadingNullTranslationDoesNotCostASense) {
  LookupResult r;
  ASSERT_EQ(parseLookup(R"({"word":"w","translations":[{"translation":null,"part_of_speech":["x"]},)"
                        R"({"part_of_speech":["noun"],"translation":"one"},{"translation":"two"},)"
                        R"({"translation":"three"}]})",
                        r),
            ParseStatus::Ok);
  ASSERT_EQ(r.senses.size(), 2u);
  EXPECT_EQ(r.senses[0].translation, "one");
  EXPECT_EQ(r.senses[0].partOfSpeech, "noun");  // part of speech before the translation: same sense
  EXPECT_EQ(r.senses[1].translation, "two");
}

TEST(LookupParse, FrequencyScoreOnlyInZeroToOne) {
  for (const auto& [body, expected] :
       std::vector<std::pair<const char*, float>>{{R"({"word":"x","frequency_score":1})", 1.0f},
                                                  {R"({"word":"x","frequency_score":0.25})", 0.25f},
                                                  {R"({"word":"x","frequency_score":1.5})", 0.0f},
                                                  {R"({"word":"x","frequency_score":-0.5})", 0.0f},
                                                  {R"({"word":"x","frequency_score":1e-3})", 0.0f},
                                                  {R"({"word":"x","frequency_score":"0.5"})", 0.0f},
                                                  {R"({"word":"x","frequency_score":null})", 0.0f}}) {
    LookupResult r;
    ASSERT_EQ(parseLookup(body, r), ParseStatus::Ok) << body;
    EXPECT_FLOAT_EQ(r.frequency, expected) << body;
  }
}

TEST(LookupParse, SaveResponse) {
  using lexipoint::api::parseSave;
  using lexipoint::api::SaveResult;
  SaveResult r;
  ASSERT_EQ(parseSave(R"({"result":{"text":"x","type":"word","status":"added","savedExpressionId":901},"item":{}})", r),
            ParseStatus::Ok);
  EXPECT_EQ(r.savedExpressionId, "901");
  ASSERT_EQ(parseSave(R"({"item":{"id":1},"result":{"savedExpressionId":"se_x"}})", r), ParseStatus::Ok);
  EXPECT_EQ(r.savedExpressionId, "se_x");
  for (const char* bad : {R"({"result":{}})", R"({"result":{"savedExpressionId":null}})", "{", ""}) {
    EXPECT_EQ(parseSave(bad, r), ParseStatus::Malformed) << bad;
  }
}
