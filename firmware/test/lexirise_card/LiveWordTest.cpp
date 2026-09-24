// A lookup's answer as the card shows it (LiveWord.h). Synthetic data.

#include <gtest/gtest.h>

#include "lexirise/card/LiveWord.h"

using namespace lexipoint::card;
using lexipoint::Language;
using lexipoint::api::EntryState;
using lexipoint::lookup::LookupCard;

TEST(LiveWord, JapaneseReadingInKanaWithTheBooksForm) {
  LookupCard c;
  c.language = Language::Japanese;
  c.surface = "煩わしくて";
  c.lemma = "煩わしい";
  c.reading = "wazurawashii";
  c.level = "JLPT-N1";
  c.partOfSpeech = "adjective";
  c.senses = {{"troublesome", "adjective"}, {"annoying", ""}};
  c.rank = 29774;
  c.frequency = 0.2f;
  const CardWord w = cardWord(c);
  EXPECT_EQ(w.word, "煩わしい");
  EXPECT_EQ(w.reading, "わずらわしい");
  EXPECT_EQ(w.romaji, "wazurawashii");
  EXPECT_EQ(w.surface, "煩わしくて");
  ASSERT_EQ(w.forms.size(), 1u);
  EXPECT_EQ(w.forms[0].form, "煩わしくて");
  EXPECT_EQ(w.badge, "N1");
  EXPECT_EQ(w.senses, (std::vector<std::string>{"troublesome", "annoying"}));
  EXPECT_EQ(w.rank, 29774u);
  EXPECT_FLOAT_EQ(w.frequency, 0.2f);
}

TEST(LiveWord, KatakanaAndUnconvertibleReadings) {
  LookupCard c;
  c.surface = "コーヒー";
  c.reading = "kōhī";
  EXPECT_EQ(cardWord(c).reading, "コーヒー");  // its own reading
  EXPECT_TRUE(cardWord(c).surface.empty());    // the dictionary form: no form line, "Not inflected"
  EXPECT_TRUE(cardWord(c).forms.empty());
  c.surface = "一緒";
  c.reading = "ic hi";  // unconvertible: the romaji, never half-converted
  EXPECT_EQ(cardWord(c).reading, "ic hi");
}

TEST(LiveWord, ChineseKeepsPinyinAndHasNoForm) {
  LookupCard c;
  c.language = Language::Chinese;
  c.surface = "选择";
  c.reading = "xuǎn zé";
  c.level = "HSK-7+";
  const CardWord w = cardWord(c);
  EXPECT_EQ(w.reading, "xuǎn zé");
  EXPECT_TRUE(w.romaji.empty());
  EXPECT_EQ(w.badge, "HSK 7+");
}

TEST(LiveWord, BadgesLevelsAndPhases) {
  EXPECT_EQ(badgeFor("HSK-4"), "HSK 4");
  EXPECT_EQ(badgeFor("JLPT-N5"), "N5");
  EXPECT_EQ(badgeFor(""), "");
  EXPECT_EQ(badgeFor("kanji"), "");
  EXPECT_EQ(levelOf(std::nullopt), Level::None);
  for (int p = 1; p <= 4; p++) {
    EntryState s;
    s.savedExpressionId = "1";
    s.proficiency = p;
    EXPECT_EQ(proficiencyOf(levelOf(s)), p);
  }
  EntryState reset;  // an undone save: still an item, proficiency 0 (unknown)
  reset.savedExpressionId = "1";
  EXPECT_EQ(levelOf(reset), Level::None);
  EXPECT_EQ(proficiencyOf(Level::None), 0);
  LookupCard c;
  EXPECT_EQ(phaseOf(c), Phase::Analyzed);
  c.complete = true;
  EXPECT_EQ(phaseOf(c), Phase::Complete);
  c.translationPending = true;
  EXPECT_EQ(phaseOf(c), Phase::TranslationPending);
  c.translationUnavailable = true;
  EXPECT_EQ(phaseOf(c), Phase::Unanswered);
}
