#include <gtest/gtest.h>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/api/Requests.h"

using lexipoint::Language;
using lexipoint::api::analyzeRequest;
using lexipoint::api::analyzeWordsRequest;
using lexipoint::api::meRequest;
using lexipoint::net::Method;

TEST(Requests, Me) {
  const auto r = meRequest();
  EXPECT_EQ(r.method, Method::Get);
  EXPECT_EQ(r.path, "/v1/me");
  EXPECT_TRUE(r.body.empty());
}

TEST(Requests, AnalyzeBodyIsEscapedJson) {
  const auto r = analyzeRequest(Language::Chinese, "他说：\"走\"");
  EXPECT_EQ(r.method, Method::Post);
  EXPECT_EQ(r.path, "/v1/analyze/text");
  EXPECT_EQ(r.body, R"({"text":"他说：\"走\"","language":"zh"})");
  EXPECT_TRUE(r.retryable());
  EXPECT_NE(analyzeRequest(Language::Japanese, "x").body.find(R"("language":"ja")"), std::string::npos);
}

TEST(Requests, TheWordLevelAnalyzeIsTheSameWithFast) {
  // v0.2 V1: the same text and language, plus fast: true (the word-level split, no lemmas).
  const auto full = analyzeRequest(Language::Chinese, "这首歌深深地打动了我。");
  const auto words = analyzeWordsRequest(Language::Chinese, "这首歌深深地打动了我。");
  EXPECT_EQ(words.method, Method::Post);
  EXPECT_EQ(words.path, full.path);
  EXPECT_TRUE(words.retryable());
  EXPECT_NE(words.body.find(R"("fast":true)"), std::string::npos);
  EXPECT_EQ(full.body.find("fast"), std::string::npos);  // the full analysis never sends it: it needs lemmas
  EXPECT_NE(words.body.find(R"("language":"zh")"), std::string::npos);
  EXPECT_NE(words.body.find("深深"), std::string::npos);
}

TEST(Requests, AnalyzeTextIsCutAtACharacterBoundary) {
  // 3-byte characters: the cut must not land inside one.
  std::string text;
  while (text.size() < lexipoint::config::kMaxAnalyzeTextBytes + 10) text += "東";
  const auto body = analyzeRequest(Language::Japanese, text).body;
  const size_t start = body.find("\"text\":\"") + 8;
  const size_t end = body.find("\",\"language\"");
  const size_t kept = end - start;
  EXPECT_LE(kept, lexipoint::config::kMaxAnalyzeTextBytes);
  EXPECT_EQ(kept % 3, 0u);
  EXPECT_EQ(body.find("\xEF\xBF\xBD"), std::string::npos);  // no replacement characters
}

TEST(Requests, UserAgent) {
  EXPECT_EQ(lexipoint::api::userAgent("1.6.5"),
            std::string("Lexipoint/") + lexipoint::config::kLexipointVersion + " CrossPoint/1.6.5");
}

TEST(Requests, LookupIsARetryablePostOfTheLemma) {
  const auto r = lexipoint::api::lookupRequest(Language::Japanese, "食べる");
  EXPECT_EQ(r.method, Method::Post);
  EXPECT_EQ(r.path, "/v1/dictionary/lookup");
  EXPECT_EQ(r.body, R"({"text":"食べる","language":"ja"})");
  EXPECT_TRUE(r.retryable());
  std::string lemma;
  while (lemma.size() < lexipoint::config::kMaxTokenBytes + 10) lemma += "学";
  const auto body = lexipoint::api::lookupRequest(Language::Chinese, lemma).body;
  const size_t kept = body.find("\",\"language\"") - (body.find("\"text\":\"") + 8);
  EXPECT_LE(kept, lexipoint::config::kMaxTokenBytes);
  EXPECT_EQ(kept % 3, 0u);
}

TEST(Requests, SaveIsTheD9PayloadAndNeverRetried) {
  lexipoint::api::SaveWord word;
  word.language = Language::Japanese;
  word.text = "煩わしい";
  word.translation = "troublesome";
  word.notes = "いつも煩わしくて困る。";
  word.proficiency = 2;
  word.tags = {"xteink", "book:x"};
  const auto r = lexipoint::api::saveRequest(word);
  EXPECT_EQ(r.method, Method::Post);
  EXPECT_EQ(r.path, "/v1/vocabulary");
  EXPECT_EQ(r.body, R"({"language":"ja","text":"煩わしい","mode":"word","translation":"troublesome","proficiency":2,)"
                    R"("tags":["xteink","book:x"],"notes":"いつも煩わしくて困る。"})");
  EXPECT_FALSE(r.retryable());  // an upsert that replaces: a stale-session resend could overwrite
  word.translation = "";
  word.notes = "";
  word.tags = {};
  EXPECT_EQ(lexipoint::api::saveRequest(word).body,
            R"({"language":"ja","text":"煩わしい","mode":"word","proficiency":2,"tags":[]})");
}

TEST(Requests, VocabularyItemRequests) {
  const auto level = lexipoint::api::setProficiencyRequest("901", 3);
  ASSERT_TRUE(level);
  EXPECT_EQ(level->method, Method::Patch);
  EXPECT_EQ(level->path, "/v1/vocabulary/901");
  EXPECT_EQ(level->body, R"({"proficiency":3})");
  EXPECT_TRUE(level->retryable());
  const auto remove = lexipoint::api::removeRequest("se_x-1");
  ASSERT_TRUE(remove);
  EXPECT_EQ(remove->method, Method::Delete);
  EXPECT_EQ(remove->path, "/v1/vocabulary/se_x-1");
  const auto clear = lexipoint::api::clearRequest("901");
  ASSERT_TRUE(clear);
  EXPECT_EQ(clear->method, Method::Patch);
  EXPECT_EQ(clear->body, R"({"notes":null,"customTranslation":null,"tags":[]})");
  // The id goes into the path: anything but a plain id is refused.
  const std::string tooLong(lexipoint::config::kMaxSavedIdBytes + 1, '9');
  EXPECT_FALSE(lexipoint::api::removeRequest(tooLong));
  EXPECT_TRUE(lexipoint::api::removeRequest(std::string(lexipoint::config::kMaxSavedIdBytes, '9')));
  for (const char* bad : {"", "9/../me", "9?x=1", "9 1", "９"}) {
    EXPECT_FALSE(lexipoint::api::setProficiencyRequest(bad, 1)) << bad;
    EXPECT_FALSE(lexipoint::api::removeRequest(bad)) << bad;
    EXPECT_FALSE(lexipoint::api::clearRequest(bad)) << bad;
  }
}

TEST(Requests, LoggedPathsLeaveOutTheSavedExpressionId) {
  EXPECT_EQ(lexipoint::api::loggablePath("/v1/vocabulary/901"), "/v1/vocabulary/{id}");
  EXPECT_EQ(lexipoint::api::loggablePath("/v1/vocabulary"), "/v1/vocabulary");
  EXPECT_EQ(lexipoint::api::loggablePath("/v1/dictionary/lookup"), "/v1/dictionary/lookup");
}
