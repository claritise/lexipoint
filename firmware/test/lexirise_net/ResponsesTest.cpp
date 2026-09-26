// Fixtures are synthetic but follow the live shapes (lexirise-api-notes.md); the public repo never
// holds real responses, IDs or account data.

#include <gtest/gtest.h>

#include <string>
#include <vector>

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

TEST(ResponsesDecks, ListKeepsWhatFindsABooksDeck) {
  // GET /v1/decks: synthetic, with the fields the reference names (lexirise-api-notes.md, Decks).
  constexpr const char* kList =
      R"({"decks":[{"id":7,"title":"Lexipoint: Kokoro","deck_type":"dynamic","unit_type":"word",)"
      R"("rule_type":"user_tag_filter","user_tags":["book:kokoro"],"itemCount":3,"parent_deck_id":null},)"
      R"({"id":"d_8","title":"JLPT N5","deck_type":"snapshot","unit_type":"word","user_tags":null},)"
      R"({"title":"no id"}]})";
  std::vector<lexipoint::api::DeckSummary> decks;
  ASSERT_EQ(lexipoint::api::parseDeckList(kList, decks), ParseStatus::Ok);
  ASSERT_EQ(decks.size(), 2u);  // the one without an id is dropped
  EXPECT_EQ(decks[0].id, "7");
  EXPECT_EQ(decks[0].title, "Lexipoint: Kokoro");
  EXPECT_EQ(decks[0].deckType, "dynamic");
  EXPECT_EQ(decks[0].unitType, "word");
  EXPECT_EQ(decks[0].ruleType, "user_tag_filter");
  EXPECT_EQ(decks[0].userTags, std::vector<std::string>{"book:kokoro"});
  EXPECT_EQ(decks[1].id, "d_8");
  EXPECT_TRUE(decks[1].userTags.empty());

  ASSERT_EQ(lexipoint::api::parseDeckList(R"({"decks":[]})", decks), ParseStatus::Ok);
  EXPECT_TRUE(decks.empty());
  for (const char* bad : {R"({"success":false,"error":"x"})", R"({"decks":{}})", "[", ""}) {
    EXPECT_EQ(lexipoint::api::parseDeckList(bad, decks), ParseStatus::Malformed) << bad;
  }
}

TEST(ResponsesDecks, AListPastTheCapKeepsTheFirst) {
  std::string body = R"({"decks":[)";
  for (size_t i = 0; i < lexipoint::config::kMaxDecksListed + 5; i++) {
    if (i) body += ',';
    body += R"({"id":)" + std::to_string(i + 1) + R"(,"title":"t"})";
  }
  body += "]}";
  std::vector<lexipoint::api::DeckSummary> decks;
  ASSERT_EQ(lexipoint::api::parseDeckList(body, decks), ParseStatus::Ok);
  ASSERT_EQ(decks.size(), lexipoint::config::kMaxDecksListed);
  EXPECT_EQ(decks.back().id, std::to_string(lexipoint::config::kMaxDecksListed));
}

TEST(ResponsesDecks, ACreatedDecksId) {
  lexipoint::api::CreatedDeck created;
  ASSERT_EQ(lexipoint::api::parseCreatedDeck(
                R"({"success":true,"deck":{"id":12,"title":"Lexipoint: x","deck_type":"dynamic",)"
                R"("rule_type":"user_tag_filter"}})",
                created),
            ParseStatus::Ok);
  EXPECT_EQ(created.id, "12");
  EXPECT_EQ(created.deckType, "dynamic");
  EXPECT_EQ(created.ruleType, "user_tag_filter");
  for (const char* bad : {R"({"success":false,"error":"Deck not found"})", R"({"deck":{"id":""}})", "{"}) {
    EXPECT_EQ(lexipoint::api::parseCreatedDeck(bad, created), ParseStatus::Malformed) << bad;
  }
}

TEST(ResponsesDecks, OtherShapesAndCamelCaseAreRead) {
  // The live shapes are unseen: camelCase names, deckId, a `data` wrapper, a bare array, a flat answer.
  std::vector<lexipoint::api::DeckSummary> decks;
  ASSERT_EQ(lexipoint::api::parseDeckList(
                R"({"data":[{"deckId":"d_7","title":"Lexipoint: Kokoro","deckType":"dynamic","unitType":"word",)"
                R"("ruleType":"user_tag_filter","userTags":["book:kokoro"],"isStarred":false}]})",
                decks),
            ParseStatus::Ok);
  ASSERT_EQ(decks.size(), 1u);
  EXPECT_EQ(decks[0].id, "d_7");
  EXPECT_EQ(decks[0].deckType, "dynamic");
  EXPECT_EQ(decks[0].unitType, "word");
  EXPECT_EQ(decks[0].ruleType, "user_tag_filter");
  EXPECT_EQ(decks[0].userTags, std::vector<std::string>{"book:kokoro"});
  EXPECT_FALSE(decks[0].othersDeck());

  ASSERT_EQ(lexipoint::api::parseDeckList(R"([{"deck_id":5,"title":"T","starred":true},{"id":6,"title":"U",)"
                                          R"("isOwner":false}])",
                                          decks),
            ParseStatus::Ok);
  ASSERT_EQ(decks.size(), 2u);
  EXPECT_EQ(decks[0].id, "5");
  EXPECT_TRUE(decks[0].othersDeck());  // starred, ownership not said: someone else's
  EXPECT_TRUE(decks[1].othersDeck());  // not the user's

  lexipoint::api::CreatedDeck created;
  ASSERT_EQ(lexipoint::api::parseCreatedDeck(R"({"data":{"deckId":"d_9","deckType":"dynamic"}})", created),
            ParseStatus::Ok);
  EXPECT_EQ(created.id, "d_9");
  EXPECT_EQ(created.deckType, "dynamic");
  ASSERT_EQ(lexipoint::api::parseCreatedDeck(R"({"id":10,"title":"Lexipoint: x"})", created), ParseStatus::Ok);
  EXPECT_EQ(created.id, "10");
}

TEST(ResponsesDecks, AListNoEntryOfWhichCanBeReadIsMalformed) {
  // Entries with no id, or with an id but neither a title nor a type: a shape this doesn't read, never "no deck".
  std::vector<lexipoint::api::DeckSummary> decks;
  for (const char* bad : {R"({"decks":[{"name":"Lexipoint: Kokoro","deck":5}]})", R"({"decks":[{"id":5}]})",
                          R"({"decks":[{"id":"4/2","title":"x"}]})", R"([{"deck":{"id":5,"title":"x"}}])"}) {
    EXPECT_EQ(lexipoint::api::parseDeckList(bad, decks), ParseStatus::Malformed) << bad;
  }
}

TEST(ResponsesDecks, SaysWhetherTheListIsWhole) {
  std::vector<lexipoint::api::DeckSummary> decks;
  bool complete = false;
  ASSERT_EQ(lexipoint::api::parseDeckList(R"({"decks":[]})", decks, &complete), ParseStatus::Ok);
  EXPECT_TRUE(complete);
  ASSERT_EQ(lexipoint::api::parseDeckList(R"({"decks":[],"hasMore":false,"nextOffset":null})", decks, &complete),
            ParseStatus::Ok);
  EXPECT_TRUE(complete);
  ASSERT_EQ(lexipoint::api::parseDeckList(R"({"decks":[{"id":1,"title":"a"}],"totalCount":1})", decks, &complete),
            ParseStatus::Ok);
  EXPECT_TRUE(complete);
  for (const char* part :
       {R"({"decks":[],"hasMore":true})", R"({"data":[],"next_offset":50})", R"({"decks":[],"has_more":true})",
        R"({"decks":[{"id":1,"title":"a"}],"totalCount":7})", R"({"decks":[],"total_count":3})"}) {
    ASSERT_EQ(lexipoint::api::parseDeckList(part, decks, &complete), ParseStatus::Ok) << part;
    EXPECT_FALSE(complete) << part;
  }
  // An entry dropped for an id that can't go into a path (or none): it may be the book's deck, so not whole.
  for (const char* dropped : {R"({"decks":[{"id":1,"title":"a"},{"id":"4/2","title":"Lexipoint: Kokoro"}]})",
                              R"({"decks":[{"id":1,"title":"a"},{"title":"Lexipoint: Kokoro"}]})"}) {
    ASSERT_EQ(lexipoint::api::parseDeckList(dropped, decks, &complete), ParseStatus::Ok) << dropped;
    EXPECT_EQ(decks.size(), 1u);
    EXPECT_FALSE(complete) << dropped;
  }
  std::string body = R"({"decks":[)";  // cut at the cap
  for (size_t i = 0; i <= lexipoint::config::kMaxDecksListed; i++) {
    if (i) body += ',';
    body += R"({"id":)" + std::to_string(i + 1) + R"(,"title":"t"})";
  }
  body += "]}";
  ASSERT_EQ(lexipoint::api::parseDeckList(body, decks, &complete), ParseStatus::Ok);
  EXPECT_FALSE(complete);
}

TEST(ResponsesDecks, LanguageAndOwnershipAreRead) {
  std::vector<lexipoint::api::DeckSummary> decks;
  ASSERT_EQ(lexipoint::api::parseDeckList(R"({"decks":[{"id":1,"title":"a","language":"ja","starred":true,)"
                                          R"("is_owner":true},{"id":2,"title":"b","sourceLanguage":"zh"},)"
                                          R"({"id":3,"title":"c","lang":"ja","isOwned":false}]})",
                                          decks),
            ParseStatus::Ok);
  ASSERT_EQ(decks.size(), 3u);
  EXPECT_EQ(decks[0].language, "ja");
  EXPECT_EQ(decks[0].owned, true);
  EXPECT_FALSE(decks[0].othersDeck());  // the user's own, starred
  EXPECT_EQ(decks[1].language, "zh");
  EXPECT_EQ(decks[1].owned, std::nullopt);
  EXPECT_FALSE(decks[1].othersDeck());
  EXPECT_EQ(decks[2].language, "ja");
  EXPECT_TRUE(decks[2].othersDeck());
}

TEST(ResponsesDecks, AListOfSomethingElseIsntAnEmptyList) {
  std::vector<lexipoint::api::DeckSummary> decks;
  for (const char* bad : {"[1,2]", R"({"decks":["a"]})", R"({"data":[[1]]})"}) {
    EXPECT_EQ(lexipoint::api::parseDeckList(bad, decks), ParseStatus::Malformed) << bad;
  }
}

TEST(ResponsesAnalyze, ASavedWordsNotesAndTags) {
  // The documented shape (context-brief.md): user_tags as {id, name}; plain strings are read too.
  const std::string longNote(lexipoint::config::kMaxSavedNoteBytes + 30, 'a');
  const std::string body =
      std::string(R"({"occurrences":[{"word":"猫","isWordLike":true,"charStart":0,"charEnd":1,"entryId":5}],)") +
      R"("stateByEntryId":{"5":{"saved_expression_id":987,"entry_id":5,"proficiency":2,"seen_count":8,)"
      R"("notes":"猫が好き。","user_tags":[{"id":12,"name":"xteink"},{"id":13,"name":"book:kokoro"}],"images":null},)"
      R"("6":{"saved_expression_id":1,"proficiency":1,"notes":null,"user_tags":["book:x",""]},)"
      R"("7":{"saved_expression_id":2,"notes":")" +
      longNote + R"("}}})";
  AnalyzeResult r;
  ASSERT_EQ(parseAnalyze(body, r), ParseStatus::Ok);  // a long note never fails the analysis
  const auto* cat = r.stateFor(5);
  ASSERT_NE(cat, nullptr);
  EXPECT_EQ(cat->notes, "猫が好き。");
  EXPECT_EQ(cat->userTags, (std::vector<std::string>{"xteink", "book:kokoro"}));
  EXPECT_EQ(r.stateFor(6)->notes, "");  // null
  EXPECT_EQ(r.stateFor(6)->userTags, std::vector<std::string>{"book:x"});
  EXPECT_EQ(r.stateFor(7)->notes.size(), lexipoint::config::kMaxSavedNoteBytes);  // cut
  // As seen live (2026-09-26): no notes or user_tags at all.
  AnalyzeResult bare;
  ASSERT_EQ(
      parseAnalyze(R"({"occurrences":[{"word":"猫","isWordLike":true,"charStart":0,"charEnd":1,"entryId":5}],)"
                   R"("stateByEntryId":{"5":{"saved_expression_id":1,"entry_id":5,"proficiency":0,"seen_count":1}}})",
                   bare),
      ParseStatus::Ok);
  EXPECT_TRUE(bare.stateFor(5)->notes.empty());
  EXPECT_TRUE(bare.stateFor(5)->userTags.empty());
}

TEST(ResponsesAnalyze, ANoteIsCutAtACharacter) {
  std::string note;
  while (note.size() + 3 <= lexipoint::config::kMaxSavedNoteBytes + 1) note += "字";  // 3 bytes each, past the cap
  const std::string body =
      std::string(R"({"occurrences":[{"word":"字","isWordLike":true,"charStart":0,"charEnd":1,"entryId":5}],)") +
      R"("stateByEntryId":{"5":{"saved_expression_id":1,"notes":")" + note + R"("}}})";
  AnalyzeResult r;
  ASSERT_EQ(parseAnalyze(body, r), ParseStatus::Ok);
  const std::string& kept = r.stateFor(5)->notes;
  EXPECT_LE(kept.size(), lexipoint::config::kMaxSavedNoteBytes);
  EXPECT_EQ(kept.size() % 3, 0u);  // whole characters
}

TEST(ResponsesAnalyze, ASavedWordsTagsAreCappedAndAnOverLongOneSkipped) {
  std::string tags;
  for (size_t i = 0; i < lexipoint::config::kMaxSavedTags + 4; i++) {
    tags += (i ? "," : "") + std::string(R"({"name":"t)") + std::to_string(i) + R"("})";
  }
  const std::string longTag(lexipoint::config::kMaxTokenBytes + 1, 'x');
  const std::string body =
      std::string(R"({"occurrences":[{"word":"猫","isWordLike":true,"charStart":0,"charEnd":1,"entryId":5}],)") +
      R"("stateByEntryId":{"5":{"saved_expression_id":1,"user_tags":[{"name":")" + longTag + R"("},)" + tags + "]}}}";
  AnalyzeResult r;
  ASSERT_EQ(parseAnalyze(body, r), ParseStatus::Ok);  // an over-long tag never fails the answer
  const auto& kept = r.stateFor(5)->userTags;
  ASSERT_EQ(kept.size(), lexipoint::config::kMaxSavedTags);
  EXPECT_EQ(kept.front(), "t0");  // the long one skipped
}
