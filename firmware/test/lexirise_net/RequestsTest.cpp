#include <gtest/gtest.h>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/api/Requests.h"

using lexipoint::Language;
using lexipoint::api::analyzeRequest;
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
  EXPECT_NE(analyzeRequest(Language::Japanese, "x").body.find(R"("language":"ja")"), std::string::npos);
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
