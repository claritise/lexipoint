// Measured answers, on the host (carried from V7a's review): Lexirise's real GET /v1/vocabulary pages, kept in the
// gitignored research/ folder (they hold account data: never committed), each read whole and streamed in socket-sized
// pieces. Skipped unless LEXIPOINT_RAW_PAGES names a folder of them, one body per `vocab-*.json`:
//
//   LEXIPOINT_RAW_PAGES=<folder> ctest --test-dir build/test -R RawPages --output-on-failure

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "lexirise/api/VocabPage.h"

namespace {

std::string readAll(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

}  // namespace

TEST(RawPages, MeasuredVocabularyPagesParseWholeAndStreamed) {
  const char* dir = std::getenv("LEXIPOINT_RAW_PAGES");
  if (dir == nullptr) GTEST_SKIP() << "LEXIPOINT_RAW_PAGES not set";
  int pages = 0;
  for (const auto& file : std::filesystem::directory_iterator(dir)) {
    const std::string name = file.path().filename().string();
    if (name.rfind("vocab-", 0) != 0) continue;
    const std::string body = readAll(file.path());
    lexipoint::api::VocabPage whole;
    ASSERT_EQ(lexipoint::api::parseVocabPage(body, whole), lexipoint::api::ParseStatus::Ok) << name;
    EXPECT_FALSE(whole.items.empty()) << name;
    for (const auto& item : whole.items) EXPECT_TRUE(!item.word || item.usable()) << name;
    lexipoint::api::VocabPageReader reader;
    for (size_t at = 0; at < body.size(); at += 1024) {
      ASSERT_TRUE(reader.onBody(body.data() + at, std::min<size_t>(1024, body.size() - at))) << name;
    }
    lexipoint::api::VocabPage streamed;
    ASSERT_EQ(reader.finish(streamed), lexipoint::api::ParseStatus::Ok) << name;
    EXPECT_EQ(streamed.items.size(), whole.items.size()) << name;
    std::printf("%s: %zu items, %zu bytes, listCount %s\n", name.c_str(), whole.items.size(), body.size(),
                whole.listCount() ? std::to_string(*whole.listCount()).c_str() : "none");
    pages++;
  }
  EXPECT_GT(pages, 0) << "no vocab-*.json in " << dir;
}
