// lookup-flow.md §4: what to try in StarDict for a tap.

#include <gtest/gtest.h>

#include <map>

#include "lexirise/lookup/StarDictCandidates.h"

using lexipoint::lookup::starDictCandidates;
using lexipoint::text::TextLine;

namespace {

TextLine line(std::vector<std::string> tokens) {
  TextLine l;
  l.tokens = std::move(tokens);
  return l;
}

}  // namespace

TEST(StarDictCandidates, LongestCjkRunFromTheTapDownToOneCharacter) {
  const TextLine l = line({"我", "们", "在", "学", "习", "中", "文", "。"});
  const std::vector<std::string> want = {"学习中文", "学习中", "学习", "学"};
  EXPECT_EQ(starDictCandidates(l, 3), want);  // stops at 。
  EXPECT_EQ(starDictCandidates(l, 6), std::vector<std::string>{"文"});
}

TEST(StarDictCandidates, CappedAtTheConfiguredLength) {
  const TextLine l = line({"一", "二", "三", "四", "五", "六", "七", "八", "九", "十"});
  const auto c = starDictCandidates(l, 0);
  ASSERT_EQ(c.size(), lexipoint::config::kStarDictMaxPrefixChars);
  EXPECT_EQ(c.front(), "一二三四五六七八");
  EXPECT_EQ(c.back(), "一");
}

TEST(StarDictCandidates, StopsAtLatinAndCountsCharactersInsideATokenToo) {
  EXPECT_EQ(starDictCandidates(line({"食べ", "る", "A"}), 0), (std::vector<std::string>{"食べる", "食べ", "食"}));
}

TEST(StarDictCandidates, LatinOrPunctuationIsTheTokenItself) {
  EXPECT_EQ(starDictCandidates(line({"hello", "world"}), 1), std::vector<std::string>{"world"});
  EXPECT_EQ(starDictCandidates(line({"「", "猫"}), 0), std::vector<std::string>{"「"});
  EXPECT_TRUE(starDictCandidates(line({"a"}), 3).empty());
}

TEST(StarDictCandidates, ProbeStopsAtTheFirstHitOrAnError) {
  using lexipoint::lookup::ProbeResult;
  using lexipoint::lookup::probeStarDict;
  const std::vector<std::string> c = {"学习中文", "学习中", "学习", "学"};
  std::vector<std::string> tried;
  const auto dict = [&](const std::map<std::string, ProbeResult>& answers) {
    return [&tried, answers](const std::string& word) {
      tried.push_back(word);
      const auto it = answers.find(word);
      return it == answers.end() ? ProbeResult::NotFound : it->second;
    };
  };
  EXPECT_EQ(probeStarDict(c, dict({{"学习", ProbeResult::Found}})), ProbeResult::Found);
  EXPECT_EQ(tried, (std::vector<std::string>{"学习中文", "学习中", "学习"}));
  tried.clear();
  EXPECT_EQ(probeStarDict(c, dict({{"学习中", ProbeResult::Error}, {"学", ProbeResult::Found}})), ProbeResult::Error);
  EXPECT_EQ(tried.size(), 2u);  // a read error isn't masked by a shorter word
  tried.clear();
  EXPECT_EQ(probeStarDict(c, dict({})), ProbeResult::NotFound);
  EXPECT_EQ(tried.size(), 4u);
  EXPECT_EQ(probeStarDict({}, dict({})), ProbeResult::NotFound);
}

TEST(StarDictCandidates, WordMarksStayInTheRunHangulIsTheToken) {
  EXPECT_EQ(starDictCandidates(line({"コ", "ー", "ヒ", "ー", "を"}), 0).front(), "コーヒーを");
  EXPECT_EQ(starDictCandidates(line({"人", "々", "が"}), 0).front(), "人々が");
  EXPECT_EQ(starDictCandidates(line({"사", "과", "나", "무"}), 0), std::vector<std::string>{"사"});  // spaced script
}
