#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "lexirise/net/JsonReader.h"

using lexipoint::json::Path;
using lexipoint::json::Result;
using lexipoint::json::Type;

namespace {

// Records "path=value" for every scalar, with [] for indices.
class Recorder : public lexipoint::json::Visitor {
 public:
  std::vector<std::string> seen;
  int begins = 0;
  int ends = 0;
  void onValue(const Path& path, Type, std::string_view text) override {
    std::string p;
    for (size_t i = 0; i < path.depth(); i++) {
      if (path.isIndex(i)) {
        p += "[" + std::to_string(path.index(i)) + "]";
      } else {
        p += (p.empty() ? "" : ".") + path.at(i).key;
      }
    }
    seen.push_back(p + "=" + std::string(text));
  }
  void onBegin(const Path&, bool) override { begins++; }
  void onEnd(const Path&, bool) override { ends++; }
};

Recorder readOk(const std::string& doc) {
  Recorder r;
  EXPECT_EQ(lexipoint::json::read(doc, r), Result::Ok) << doc;
  return r;
}

}  // namespace

TEST(JsonReader, ReportsPathsForNestedValues) {
  const Recorder r = readOk(R"( {"a":{"b":[1,{"c":"x"},true,null]},"d":-1.5e+3} )");
  EXPECT_EQ(r.seen, (std::vector<std::string>{"a.b[0]=1", "a.b[1].c=x", "a.b[2]=true", "a.b[3]=null", "d=-1.5e+3"}));
  EXPECT_EQ(r.begins, 4);
  EXPECT_EQ(r.ends, 4);
}

TEST(JsonReader, DecodesEscapesAndSurrogates) {
  EXPECT_EQ(readOk(R"(["\"\\\/\b\f\n\r\t"])").seen[0], "[0]=\"\\/\b\f\n\r\t");
  EXPECT_EQ(readOk(R"(["\u6771\u4eac"])").seen[0], "[0]=東京");
  EXPECT_EQ(readOk(R"(["\ud842\udfb7"])").seen[0], "[0]=𠮷");
  EXPECT_EQ(readOk(R"(["\ud842x"])").seen[0], "[0]=\xEF\xBF\xBDx");  // lone high surrogate
  EXPECT_EQ(readOk(R"(["\udfb7"])").seen[0], "[0]=\xEF\xBF\xBD");    // lone low surrogate
  EXPECT_EQ(readOk(R"(["\ud842\u0041"])").seen[0],
            "[0]=\xEF\xBF\xBD"
            "A");                                       // high + non-low
  EXPECT_EQ(readOk("[\"東京\"]").seen[0], "[0]=東京");  // raw UTF-8
}

TEST(JsonReader, LongStringsAreKeptWhole) {
  const std::string longText(5000, 'x');
  EXPECT_EQ(readOk("{\"k\":\"" + longText + "\",\"n\":1}").seen, (std::vector<std::string>{"k=" + longText, "n=1"}));
}

TEST(JsonReader, PathMatchingPatterns) {
  struct V : lexipoint::json::Visitor {
    int hits = 0;
    void onValue(const Path& path, Type, std::string_view) override {
      if (path.matches({"occurrences", "[]", "word"})) hits++;
      EXPECT_FALSE(path.matches({"occurrences", "*", "word"}));
    }
  } v;
  lexipoint::json::read(R"({"occurrences":[{"word":"a"},{"word":"b","x":1}],"word":"c"})", v);
  EXPECT_EQ(v.hits, 2);
}

TEST(JsonReader, RejectsMalformedAndTruncated) {
  for (const char* bad : {"",      "{",          "[1,]",       "{\"a\":}", "{\"a\" 1}", "{a:1}",     "[01]",
                          "[1.]",  "[-]",        "[1e]",       "[tru]",    "[\"abc",    "[\"\\x\"]", "[\"\\u12\"]",
                          "{} {}", "[\"a\nb\"]", "{\"a\":1,}", "{\"a\":1", "nul",       "[1 2]"}) {
    Recorder r;
    EXPECT_EQ(lexipoint::json::read(bad, r), Result::Malformed) << bad;
  }
}

TEST(JsonReader, DepthIsLimited) {
  Recorder r;
  const std::string deep(lexipoint::config::kJsonMaxDepth + 1, '[');
  EXPECT_EQ(lexipoint::json::read(deep + std::string(lexipoint::config::kJsonMaxDepth + 1, ']'), r), Result::TooDeep);
  const std::string ok(lexipoint::config::kJsonMaxDepth, '[');
  EXPECT_EQ(lexipoint::json::read(ok + std::string(lexipoint::config::kJsonMaxDepth, ']'), r), Result::Ok);
}
