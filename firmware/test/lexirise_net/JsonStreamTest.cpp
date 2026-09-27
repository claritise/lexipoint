#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "lexirise/net/JsonReader.h"
#include "lexirise/net/JsonStream.h"

using lexipoint::json::Path;
using lexipoint::json::Result;
using lexipoint::json::StreamReader;
using lexipoint::json::Type;

namespace {

// Every event, with its path: what json::read and the stream must agree on.
class Events : public lexipoint::json::Visitor {
 public:
  std::vector<std::string> seen;
  static std::string pathOf(const Path& path) {
    std::string p;
    for (size_t i = 0; i < path.depth(); i++) {
      p += path.isIndex(i) ? "[" + std::to_string(path.index(i)) + "]" : "." + path.at(i).key;
    }
    return p;
  }
  void onValue(const Path& path, Type type, std::string_view text) override {
    seen.push_back(pathOf(path) + "=" + std::to_string(static_cast<int>(type)) + ":" + std::string(text));
  }
  void onBegin(const Path& path, bool isArray) override {
    seen.push_back("begin" + pathOf(path) + (isArray ? "[" : "{"));
  }
  void onEnd(const Path& path, bool isArray) override { seen.push_back("end" + pathOf(path) + (isArray ? "]" : "}")); }
};

// The document fed `chunk` bytes at a time.
Result streamed(const std::string& doc, const size_t chunk, Events& events, const size_t maxString = 256) {
  StreamReader reader(events, maxString);
  for (size_t at = 0; at < doc.size(); at += chunk) {
    if (!reader.feed(std::string_view(doc).substr(at, chunk))) break;
  }
  return reader.finish();
}

const std::vector<std::string> kGood = {
    R"( {"a":{"b":[1,{"c":"x"},true,null]},"d":-1.5e+3} )",
    R"(["\"\\\/\b\f\n\r\t"])",
    R"(["\u6771\u4eac"])",
    R"(["\ud842\udfb7"])",
    R"(["\ud842x"])",
    R"(["\udfb7"])",
    R"(["\ud842\u0041"])",
    R"(["\ud842\ud842\udfb7"])",
    R"(["\ud842\n"])",
    "[\"東京\"]",
    R"({"items":[{"id":2643686,"dictionary_entry":{"id":1,"translations":[{"t":"a"}]}},{}],"nextOffset":null})",
    R"([0, -0, 0.5, 1e9, 2E-3, 10, [], {}, [[]], {"":1}])",
    "12",
    "\"top\"",
    " true ",
};

}  // namespace

TEST(JsonStream, GivesTheSameEventsAsTheWholeDocumentReaderInAnyChunking) {
  for (const std::string& doc : kGood) {
    Events whole;
    ASSERT_EQ(lexipoint::json::read(doc, whole), Result::Ok) << doc;
    for (const size_t chunk : {size_t{1}, size_t{2}, size_t{3}, size_t{7}, doc.size()}) {
      Events stream;
      EXPECT_EQ(streamed(doc, chunk, stream), Result::Ok) << doc << " in " << chunk;
      EXPECT_EQ(stream.seen, whole.seen) << doc << " in " << chunk;
    }
  }
}

TEST(JsonStream, RejectsWhatTheWholeDocumentReaderRejects) {
  for (const char* bad :
       {"",     "{",     "[1,]",   "{\"a\":}",  "{\"a\" 1}",   "{a:1}", "[01]",       "[1.]",       "[-]",
        "[1e]", "[tru]", "[\"abc", "[\"\\x\"]", "[\"\\u12\"]", "{} {}", "[\"a\nb\"]", "{\"a\":1,}", "{\"a\":1",
        "nul",  "[1 2]", "[1}",    "{\"a\"]",   "-",           "1.",    "[--1]",      "[+1]",       "[.5]"}) {
    Events whole;
    EXPECT_EQ(lexipoint::json::read(bad, whole), Result::Malformed) << bad;
    for (const size_t chunk : {size_t{1}, size_t{3}, size_t{64}}) {
      Events stream;
      EXPECT_EQ(streamed(bad, chunk, stream), Result::Malformed) << bad << " in " << chunk;
    }
  }
}

TEST(JsonStream, DepthIsLimitedAsTheWholeDocumentReaderLimitsIt) {
  const size_t max = lexipoint::config::kJsonMaxDepth;
  Events events;
  EXPECT_EQ(streamed(std::string(max + 1, '[') + std::string(max + 1, ']'), 5, events), Result::TooDeep);
  Events ok;
  EXPECT_EQ(streamed(std::string(max, '[') + std::string(max, ']'), 5, ok), Result::Ok);
}

TEST(JsonStream, LongStringsAreWalkedButNotKept) {
  const std::string longText(5000, 'x');
  const std::string doc = "{\"k\":\"" + longText + "\",\"n\":1,\"s\":\"" + std::string(16, 'y') + "\"}";
  Events events;
  StreamReader reader(events, 16);
  ASSERT_TRUE(reader.feed(doc));
  EXPECT_EQ(reader.finish(), Result::Ok);
  EXPECT_EQ(reader.skippedStrings(), 1u);
  EXPECT_EQ(events.seen, (std::vector<std::string>{"begin{", ".n=1:1", ".s=0:" + std::string(16, 'y'), "end}"}));
  // A number that long is checked and skipped too; a key that long is malformed.
  Events numbers;
  EXPECT_EQ(streamed("[" + std::string(40, '1') + ",2]", 3, numbers, 16), Result::Ok);
  EXPECT_EQ(numbers.seen, (std::vector<std::string>{"begin[", "[1]=1:2", "end]"}));
  Events keys;
  EXPECT_EQ(streamed("{\"" + std::string(40, 'k') + "\":1}", 3, keys, 16), Result::Malformed);
}

TEST(JsonStream, StopsTakingBytesOnceMalformed) {
  Events events;
  StreamReader reader(events);
  EXPECT_FALSE(reader.feed("[1,,"));
  EXPECT_FALSE(reader.feed("2]"));
  EXPECT_EQ(reader.finish(), Result::Malformed);
}
