#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "lexirise/lookup/StarDictChoice.h"

using lexipoint::Language;
using lexipoint::Settings;
using lexipoint::lookup::anyStarDict;
using lexipoint::lookup::chooseStarDict;
using lexipoint::lookup::StarDictChoice;

namespace {
constexpr const char* kWord = "\xE9\xA3\x9F\xE3\x81\xB9\xE3\x82\x8B";  // 食べる
}  // namespace

TEST(StarDictChoice, TheLanguagesOwnFolderWinsOverCrossPoints) {
  Settings s;
  s.japanese.stardict = "jmdict";
  EXPECT_EQ(chooseStarDict(s, Language::Japanese, kWord, "global"), (StarDictChoice{"jmdict", "global"}));
  EXPECT_EQ(chooseStarDict(s, Language::Chinese, kWord, "global"), (StarDictChoice{"global", ""}));  // zh has none
  EXPECT_EQ(chooseStarDict(s, std::nullopt, kWord, "global"), (StarDictChoice{"global", ""}));       // a non-CJK tap
  EXPECT_EQ(chooseStarDict(Settings(), Language::Japanese, kWord, ""), (StarDictChoice{"", ""}));
}

TEST(StarDictChoice, CrossPointsIsTheFallbackOnlyWhenItsAnotherOne) {
  Settings s;
  s.japanese.stardict = "jmdict";
  EXPECT_EQ(chooseStarDict(s, Language::Japanese, kWord, ""), (StarDictChoice{"jmdict", ""}));
  EXPECT_EQ(chooseStarDict(s, Language::Japanese, kWord, "jmdict"), (StarDictChoice{"jmdict", ""}));
  EXPECT_NE(chooseStarDict(s, Language::Japanese, kWord, "a"),
            chooseStarDict(s, Language::Japanese, kWord, "b"));  // reopens
}

TEST(StarDictChoice, CountsWhileTheLanguagesLookupsAreOff) {
  Settings s;
  s.chinese.stardict = "cedict";
  s.chinese.enabled = false;  // StarDict answers every Chinese tap then
  EXPECT_EQ(chooseStarDict(s, Language::Chinese, kWord, "").folder, "cedict");
  s.enabled = false;
  EXPECT_EQ(chooseStarDict(s, Language::Chinese, kWord, "").folder, "cedict");
}

TEST(StarDictChoice, AnyLanguageStarDict) {
  Settings s;
  EXPECT_FALSE(lexipoint::lookup::anyLanguageStarDict(s));
  s.chinese.stardict = "cedict";
  EXPECT_TRUE(lexipoint::lookup::anyLanguageStarDict(s));
}

TEST(StarDictChoice, AnyStarDictFollowsTheBooksKnownLanguage) {
  using lexipoint::text::BookLanguage;
  const BookLanguage untagged("", std::nullopt);
  const BookLanguage english("en", std::nullopt);
  const BookLanguage japanese("ja", std::nullopt);
  const BookLanguage chinese("zh-CN", std::nullopt);
  const BookLanguage traditional("zh-TW", std::nullopt);
  Settings s;
  EXPECT_FALSE(anyStarDict(s, "", untagged));
  EXPECT_TRUE(anyStarDict(s, "global", japanese));  // CrossPoint's own answers every book
  s.chinese.stardict = "cedict";
  EXPECT_TRUE(anyStarDict(s, "", chinese));
  EXPECT_FALSE(anyStarDict(s, "", traditional));  // Traditional: the (Simplified) Chinese one isn't its
  EXPECT_FALSE(anyStarDict(s, "", japanese));     // the metadata names the other language
  EXPECT_TRUE(anyStarDict(s, "", untagged));      // its sentences decide
  EXPECT_TRUE(anyStarDict(s, "", english));       // as for Lexirise: a CJK novel stamped "en"
  EXPECT_FALSE(anyStarDict(s, "", BookLanguage("zh", lexipoint::Language::Japanese)));  // the override wins
}

TEST(StarDictChoice, OpensTheFolderElseTheFallbackAndReopensOnlyForAnotherChoice) {
  using lexipoint::lookup::needsOpen;
  using lexipoint::lookup::OpenedFrom;
  using lexipoint::lookup::openStarDict;
  std::vector<std::string> tried;
  const auto onCard = [&tried](const std::vector<std::string>& present) {
    return [&tried, present](const std::string& folder) {
      tried.push_back(folder);
      return std::find(present.begin(), present.end(), folder) != present.end();
    };
  };
  const StarDictChoice ja{"jmdict", "global"};
  EXPECT_EQ(openStarDict(ja, onCard({"jmdict", "global"})), OpenedFrom::Folder);
  EXPECT_EQ(tried, (std::vector<std::string>{"jmdict"}));
  tried.clear();
  EXPECT_EQ(openStarDict(ja, onCard({"global"})), OpenedFrom::Fallback);  // jmdict removed from the card
  EXPECT_EQ(tried, (std::vector<std::string>{"jmdict", "global"}));
  tried.clear();
  EXPECT_EQ(openStarDict(StarDictChoice{"jmdict", ""}, onCard({})), OpenedFrom::Neither);
  EXPECT_EQ(tried, (std::vector<std::string>{"jmdict"}));  // no fallback to try

  std::optional<StarDictChoice> opened;
  EXPECT_TRUE(needsOpen(opened, ja));  // never opened
  opened = ja;
  EXPECT_FALSE(needsOpen(opened, ja));
  EXPECT_TRUE(needsOpen(opened, StarDictChoice{"cedict", "global"}));  // a Chinese tap next
}

TEST(StarDictChoice, ALanguagesOwnDictionaryOnlyAnswersItsScript) {
  Settings s;
  s.japanese.stardict = "jmdict";
  // An English word in a Japanese book: CrossPoint's (an English dictionary, say) answers, as before P7.
  EXPECT_EQ(chooseStarDict(s, Language::Japanese, "computer", "global"), (StarDictChoice{"global", ""}));
  EXPECT_EQ(chooseStarDict(s, Language::Japanese, "\xE3\x83\xBC", "global").folder, "jmdict");  // ー alone
  EXPECT_TRUE(lexipoint::lookup::hasJaZhWordChar("\xE3\x80\x8C\xE9\xA3\x9F"));                  // 「食
  EXPECT_FALSE(lexipoint::lookup::hasJaZhWordChar("\xE3\x80\x82"));                             // 。
  EXPECT_FALSE(lexipoint::lookup::hasJaZhWordChar(""));
}

TEST(StarDictChoice, PrepareOpensOncePerChoiceAndAsksForTheIndexOnEachOpen) {
  using lexipoint::lookup::OpenedFrom;
  using lexipoint::lookup::prepareStarDict;
  int opens = 0;
  int indexChecks = 0;
  bool present = true;
  const auto open = [&](const std::string&) {
    opens++;
    return present;
  };
  const auto needsIndex = [&] {
    indexChecks++;
    return true;
  };
  std::optional<StarDictChoice> opened;
  const StarDictChoice ja{"jmdict", ""};
  const auto first = prepareStarDict(ja, opened, open, needsIndex);
  ASSERT_TRUE(first.has_value());
  EXPECT_TRUE(first->ok());
  EXPECT_TRUE(first->needsIndex);
  EXPECT_FALSE(prepareStarDict(ja, opened, open, needsIndex).has_value());  // same choice: kept
  EXPECT_EQ(opens, 1);
  const auto zh = prepareStarDict(StarDictChoice{"cedict", ""}, opened, open, needsIndex);  // the other language
  ASSERT_TRUE(zh.has_value());
  EXPECT_EQ(opens, 2);
  EXPECT_EQ(indexChecks, 2);  // asked again for the new dictionary

  present = false;  // a choice that can't be opened...
  const auto missing = prepareStarDict(StarDictChoice{"gone", ""}, opened, open, needsIndex);
  ASSERT_TRUE(missing.has_value());
  EXPECT_FALSE(missing->ok());
  EXPECT_FALSE(missing->needsIndex);  // no index check without a dictionary
  EXPECT_EQ(indexChecks, 2);
  EXPECT_FALSE(prepareStarDict(StarDictChoice{"gone", ""}, opened, open, needsIndex).has_value());  // ...isn't retried
  EXPECT_EQ(missing->from, OpenedFrom::Neither);
}

TEST(StarDictChoice, ATraditionalChineseBookKeepsCrossPointsDictionary) {
  // The Chinese group is Simplified (H8 parks Traditional): its dictionary isn't one for zh-TW books.
  using lexipoint::text::BookLanguage;
  Settings s;
  s.chinese.stardict = "cedict";
  const BookLanguage traditional("zh-TW", std::nullopt);
  const auto decision = traditional.decide("\xE5\x80\x91", s);  // 們
  EXPECT_EQ(decision.detected, Language::Chinese);              // still cut as Chinese
  EXPECT_FALSE(decision.dictionaryLanguage().has_value());
  EXPECT_EQ(chooseStarDict(s, decision.dictionaryLanguage(), "\xE5\x80\x91", "global"), (StarDictChoice{"global", ""}));
  EXPECT_FALSE(anyStarDict(s, "", traditional));  // no dictionary of its own: a long-press stays CrossPoint's
  EXPECT_TRUE(anyStarDict(s, "global", traditional));
  EXPECT_TRUE(BookLanguage("zh-TW", Language::Chinese).decide("", s).dictionaryLanguage().has_value());  // overridden
}
