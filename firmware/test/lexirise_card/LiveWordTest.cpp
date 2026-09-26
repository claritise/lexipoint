// A lookup's answer as the card shows it (LiveWord.h). Synthetic data.

#include <gtest/gtest.h>

#include "Fakes.h"
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
  EXPECT_EQ(w.conjugation, "te-form");  // the bench's own line and Form tab (C16)
  ASSERT_EQ(w.forms.size(), 3u);
  EXPECT_EQ(w.forms[0].form, "煩わしい");
  EXPECT_TRUE(w.forms[0].dictionaryForm);  // labelled CardStrings::dictionaryForm
  EXPECT_EQ(w.forms[1].form, "煩わしく");
  EXPECT_EQ(w.forms[1].label, "adverbial stem");
  EXPECT_EQ(w.forms[2].form, "煩わしくて");
  EXPECT_EQ(w.forms[2].label, "te-form");
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

TEST(LiveWord, AConjugatedVerbsFormTabGoesFromItsDictionaryForm) {
  LookupCard c;
  c.surface = "食べさせられた";
  c.lemma = "食べる";
  c.charEnd = 7;
  const CardWord w = cardWord(c, PageSentence::single("食べさせられた。"));
  EXPECT_EQ(w.conjugation, "causative-passive past");
  ASSERT_EQ(w.forms.size(), 4u);
  EXPECT_EQ(w.forms[0].form, "食べる");
  EXPECT_EQ(w.forms[3].form, "食べさせられた");
  EXPECT_EQ(w.forms[3].label, "past");

  EXPECT_TRUE(cardWord(c).conjugation.empty());  // its place unknown: 食べさせられたら may follow

  c.surface = "食べちゃった";  // not a form it names: the form alone, no name
  const CardWord unnamed = cardWord(c);
  EXPECT_TRUE(unnamed.conjugation.empty());
  ASSERT_EQ(unnamed.forms.size(), 1u);
  EXPECT_EQ(unnamed.forms[0].form, "食べちゃった");
  EXPECT_TRUE(unnamed.forms[0].label.empty());

  c.language = Language::Chinese;  // Chinese: no form line
  c.surface = "选择";
  c.lemma = "";
  EXPECT_TRUE(cardWord(c).conjugation.empty());
}

namespace {

LookupCard savedWord(std::string notes, std::vector<std::string> tags) {
  LookupCard c;
  c.surface = "煩わしくて";
  c.lemma = "煩わしい";
  EntryState state;
  state.savedExpressionId = "9";
  state.proficiency = 2;
  state.notes = std::move(notes);
  state.userTags = std::move(tags);
  c.saved = state;
  return c;
}

}  // namespace

TEST(LiveWord, MetBeforeInABookRecordedHere) {
  const lexipoint::BookTagList titles{{"norwegian-wood", "ノルウェイの森"}};
  const LookupCard c = savedWord("人づきあいが煩わしいと思うこともある。", {"xteink", "book:norwegian-wood"});
  const CardWord w = cardWord(c, PageSentence::single("毎朝の満員電車が煩わしくて、"), &titles);
  ASSERT_TRUE(w.metBefore);
  EXPECT_EQ(w.metBefore->text, "人づきあいが煩わしいと思うこともある。");
  EXPECT_EQ(w.metBefore->text.substr(w.metBefore->markStart, w.metBefore->markLength), "煩わしい");
  EXPECT_EQ(w.metBeforeBook, "ノルウェイの森");
}

TEST(LiveWord, MetBeforeInABookSavedElsewhereIsTheSentenceAlone) {
  const lexipoint::BookTagList titles;
  const LookupCard c = savedWord("　昨日は煩わしかった。", {"book:h98593b64"});
  const CardWord w = cardWord(c, PageSentence::single("今日の文。"), &titles);
  ASSERT_TRUE(w.metBefore);
  EXPECT_EQ(w.metBefore->text, "昨日は煩わしかった。");  // the paragraph's indent left out
  EXPECT_EQ(w.metBefore->text.substr(w.metBefore->markStart, w.metBefore->markLength), "煩わし");  // the stem
  EXPECT_TRUE(w.metBeforeBook.empty());
  EXPECT_TRUE(cardWord(c, PageSentence::single("今日の文。")).metBeforeBook.empty());  // no record at all: the same
}

TEST(LiveWord, NoMetBeforeForThisSentenceOrWithoutOne) {
  const lexipoint::BookTagList titles;
  const LookupCard here = savedWord("毎朝の満員電車が煩わしくて、", {"book:x"});
  EXPECT_FALSE(cardWord(here, PageSentence::single("　毎朝の満員電車が煩わしくて、"), &titles)
                   .metBefore);  // saved from this sentence
  EXPECT_FALSE(cardWord(savedWord("", {}), PageSentence::single("文。"), &titles).metBefore);  // saved without one
  LookupCard unsaved = here;
  unsaved.saved.reset();
  EXPECT_FALSE(cardWord(unsaved, PageSentence::single("文。"), &titles).metBefore);
  // Another sentence of this same book counts: a new context.
  const LookupCard sameBook = savedWord("別の文で煩わしい。", {"book:x"});
  EXPECT_TRUE(cardWord(sameBook, PageSentence::single("毎朝の満員電車が煩わしくて、"), &titles).metBefore);
  // A sentence that doesn't hold the word: shown unmarked.
  const CardWord unmarked = cardWord(savedWord("全然ちがう文。", {}), PageSentence::single("ほかの話。"), &titles);
  ASSERT_TRUE(unmarked.metBefore);
  EXPECT_EQ(unmarked.metBefore->markLength, 0u);
}

TEST(LiveWord, AOneCharacterStemIsntMarked) {
  // する's stem す would match です; the sentence is shown unmarked instead.
  LookupCard c;
  c.surface = "した";
  c.lemma = "する";
  EntryState state;
  state.savedExpressionId = "9";
  state.notes = "それは本です。";
  c.saved = state;
  const CardWord w = cardWord(c, PageSentence::single("何をした？"));
  ASSERT_TRUE(w.metBefore);
  EXPECT_EQ(w.metBefore->markLength, 0u);
}

TEST(LiveWord, AnotherCutOfTheSameLongSentenceIsntMetBefore) {
  // A long sentence is cut around each tap: two words of it save two overlapping cuts.
  const std::string whole = "あいうえおかきくけこさしすせそたちつてと煩わしいなにぬねのはひふへほまみむめも";
  const std::string savedCut = whole.substr(0, 3 * 30);  // its first 30 characters
  const std::string hereCut = whole.substr(3 * 10);      // from the 11th on
  LookupCard c;
  c.surface = "煩わしい";
  EntryState state;
  state.savedExpressionId = "9";
  state.notes = savedCut;
  c.saved = state;
  EXPECT_FALSE(cardWord(c, PageSentence::single(hereCut)).metBefore);
  EXPECT_FALSE(cardWord(c, PageSentence::single(whole)).metBefore);  // one inside the other
  EXPECT_TRUE(cardWord(c, PageSentence::single("まったく別の文で煩わしい。")).metBefore);
}

TEST(LiveWord, ASuruVerbsNounIsUnderlined) {
  LookupCard c;
  c.surface = "勉強した";
  c.lemma = "勉強する";
  EntryState state;
  state.savedExpressionId = "9";
  state.notes = "英語の勉強を始めた。";
  c.saved = state;
  const CardWord w = cardWord(c, PageSentence::single("昨日勉強した。"));
  ASSERT_TRUE(w.metBefore);
  EXPECT_EQ(w.metBefore->text.substr(w.metBefore->markStart, w.metBefore->markLength), "勉強");
}

TEST(LiveWord, AnEmptySentenceHereIsntTheSavedOne) {
  const LookupCard c = savedWord("昨日は煩わしかった。", {});
  EXPECT_TRUE(cardWord(c, PageSentence::single("")).metBefore);
  EXPECT_TRUE(cardWord(c, PageSentence::single("　 ")).metBefore);  // spaces only
}

TEST(LiveWord, TheNextCharacterOnThePageCanUnnameACutStem) {
  LookupCard c;
  c.surface = "書け";
  c.lemma = "書く";
  c.charStart = 0;
  c.charEnd = 2;
  EXPECT_TRUE(cardWord(c, PageSentence::single("書けば分かる。")).conjugation.empty());  // 書け + ば: 書けば cut short
  EXPECT_TRUE(cardWord(c, PageSentence::single("書けない。")).conjugation.empty());  // 書け + ない: 書けない cut short
  EXPECT_EQ(cardWord(c, PageSentence::single("書け！")).conjugation, "imperative");
  c.surface = "書こ";
  EXPECT_TRUE(cardWord(c, PageSentence::single("書こう。")).conjugation.empty());
  c.charEnd = 0;  // its place unknown: 書こう may be it, cut short
  EXPECT_TRUE(cardWord(c, PageSentence::single("書こう。")).conjugation.empty());
}

TEST(LiveWord, AWordWhereThePageCutTheSentenceHasNoNameALongerFormCouldTake) {
  LookupCard c;
  c.surface = "書け";
  c.lemma = "書く";
  c.charEnd = 2;
  EXPECT_TRUE(cardWord(c, PageSentence::single("書け", true)).conjugation.empty());       // 書け|ない on the next page
  EXPECT_EQ(cardWord(c, PageSentence::single("書け", false)).conjugation, "imperative");  // the sentence ends there
  c.surface = "書いた";
  c.charEnd = 3;
  EXPECT_TRUE(cardWord(c, PageSentence::single("書いた", true)).conjugation.empty());  // 書いた|ら
  EXPECT_EQ(cardWord(c, PageSentence::single("書いた", false)).conjugation, "past");
  c.surface = "書きました";  // no longer form starts with it: named even at a cut
  c.charEnd = 5;
  EXPECT_EQ(cardWord(c, PageSentence::single("書きました", true)).conjugation, "polite past");
}

TEST(LiveWord, ANameWorkedOutForAnotherFormIsntReused) {
  LookupCard c;
  c.surface = "書いた";
  c.lemma = "書く";
  c.charEnd = 3;
  FormName other;
  other.surface = "書いて";
  other.word = "書く";
  other.conjugation = "te-form";
  EXPECT_EQ(cardWord(c, PageSentence::single("書いた。"), nullptr, &other).conjugation, "past");  // worked out again
  other.surface = "書いた";
  EXPECT_EQ(cardWord(c, PageSentence::single("書いた。"), nullptr, &other).conjugation,
            "te-form");  // the same form: reused as is
}

TEST(LiveWord, TheNextCharacterOnThePage) {
  LookupCard c;
  c.surface = "書け";
  c.lemma = "書く";
  c.charStart = 2;  // after 𠮷, a non-BMP character: two UTF-16 units
  c.charEnd = 4;
  EXPECT_EQ(nextOnPage(c, PageSentence::single("\xF0\xA0\xAE\xB7書け。")), "。");
  c.charStart = 0;
  c.charEnd = 2;
  EXPECT_EQ(nextOnPage(c, PageSentence::single("書けと言った", true)),
            "と");  // mid-sentence: known, though the sentence was cut
  EXPECT_EQ(nextOnPage(c, PageSentence::single("書け", false)), "");           // the sentence's end
  EXPECT_EQ(nextOnPage(c, PageSentence::single("書け", true)), std::nullopt);  // cut right after it
  c.charEnd = 5;
  EXPECT_EQ(nextOnPage(c, PageSentence::single("書け")), std::nullopt);  // past the text
  c.surface = "書";
  c.charEnd = 2;  // inside 𠮷's surrogate pair
  EXPECT_EQ(nextOnPage(c, PageSentence::single("書\xF0\xA0\xAE\xB7")), std::nullopt);
  c.charEnd = c.charStart;  // its place unknown
  EXPECT_EQ(nextOnPage(c, PageSentence::single("書け。")), std::nullopt);
}

TEST(LiveWord, AChineseWordHasNoForms) {
  LookupCard c;
  c.language = Language::Chinese;
  c.surface = "选择了";
  c.lemma = "选择";
  c.charEnd = 3;
  const FormName name = formNameOf(c, PageSentence::single("选择了。"));
  EXPECT_TRUE(name.conjugation.empty());
  EXPECT_TRUE(name.forms.empty());
}

TEST(LiveWord, AShortSentenceInsideTheSavedOneIsAnotherSentence) {
  LookupCard c;
  c.surface = "そう";
  EntryState state;
  state.savedExpressionId = "9";
  state.notes = "そうかもしれないと彼は長いあいだ考えていた。";
  c.saved = state;
  EXPECT_TRUE(cardWord(c, PageSentence::single("そうか。")).metBefore);  // inside it, but a short sentence of its own
  EXPECT_FALSE(cardWord(c, PageSentence::single("そうかもしれないと彼は長いあいだ考えていた"))
                   .metBefore);  // most of it: the same
  state.notes = "そうか。";      // the other way: the saved one short, inside the page's long one
  c.saved = state;
  EXPECT_TRUE(cardWord(c, PageSentence::single("そうか。と彼は長いあいだ考えていたが、答えは出なかった。")).metBefore);
  EXPECT_FALSE(cardWord(c, PageSentence::single("「そうか。」")).metBefore);  // most of it: the same
}

TEST(LiveWord, SameSentenceTrustsThePagesCutsAndOnlyANearCapNote) {
  // saved, page, page cut at its start, at its end, the same sentence?
  struct Row {
    std::string saved;
    std::string page;
    bool cutAtStart;
    bool cutAtEnd;
    bool same;
  };
  const std::string whole = "昨日は雨が降っていたので、私たちは駅の近くの小さな喫茶店で長いあいだ話していた。";
  std::string nearCap;  // a note the cap cut: kCutNoteMinPercentOfCap of it or more
  for (size_t i = 0; i < lexipoint::config::kMaxSentenceUnits; i++) nearCap += "あ";
  const Row rows[] = {
      {whole, whole, false, false, true},
      // The page's inside the note: its flags decide, never its text
      {"「分かった」と彼は言って家を出た。", "「分かった」", false, false, false},  // a line of its own
      {"「分かった」と彼は言って家を出た。", "「分かった」", false, true, true},    // the page bottom cut it
      {"本当に今日は来てくれてありがとう。", "ありがとう。", false, false, false},
      {"本当に今日は来てくれてありがとう。", "ありがとう。", true, false, true},  // the page top cut it
      {"今天真的非常谢谢你。", "谢谢你。", false, false, false},
      {"今天真的非常谢谢你。", "谢谢你。", true, false, true},
      {"「行こう。」と彼は言って、長いあいだ窓の外を見ていた。", "「行こう。", false, true, true},
      {"「行こう。」と彼は言って、長いあいだ窓の外を見ていた。", "「行こう。", false, false, false},
      {"そうかもしれないと彼は長いあいだ考えていた。", "そうか。", false, false, false},  // not inside: another
      {whole, whole.substr(0, 3 * 30), false, true, true},                                // most of it
      {whole, whole.substr(3 * 5, 3 * 10), true, false, false},  // cut at the start only, the note goes on after
      {whole, whole.substr(3 * 5, 3 * 10), true, true, true},    // cut at both ends
      // The note's inside the page's: only a near-cap note is taken to be a cut
      {"行こう。", "それじゃあ、みんなで一緒に行こう。", false, false, false},
      {"行こう。", "それじゃあ、みんなで一緒に行こう。", true, true, false},  // the page's flags don't say
      {"谢谢你。", "今天真的非常谢谢你。", false, false, false},
      {"「そうか」", "「そうか」と彼は言った。", false, false, false},
      {"第一章", "第一章　始まりの朝のこと。", false, false, false},
      {"我爱你", "我爱你们，我的朋友们，永远。", false, false, false},
      {"そうか。", "そうか。と彼は長いあいだ考えていたが、答えは出なかった。", false, false, false},
      {"そうか。", "「そうか。」", false, false, true},       // most of it
      {whole.substr(0, 3 * 13), whole, false, false, false},  // a short cut note after a reflow: the known limit
      {nearCap, nearCap + "い。", false, false, true},        // a cut the cap made
      {nearCap, "う" + nearCap + "い。", false, false, true},
      // Two cuts of one long sentence
      {whole.substr(0, 3 * 30), whole.substr(3 * 10), true, false, true},
      {"まったく別の文で煩わしい。", whole, false, false, false},
      // Known limit (C14): a short fragment the page top cut, ending another saved sentence, reads as that one.
      {"彼は「行こう。」と言った。", "と言った。", true, false, true},
  };
  for (const Row& r : rows) {
    EXPECT_EQ(sameSentence(r.saved, {r.page, r.cutAtStart, r.cutAtEnd}), r.same)
        << r.saved << " / " << r.page << " " << r.cutAtStart << r.cutAtEnd;
  }
}

TEST(LiveWord, AChineseSavedWordsMetBefore) {
  LookupCard c;
  c.language = Language::Chinese;
  c.surface = "谢谢";
  EntryState state;
  state.savedExpressionId = "9";
  state.notes = "今天真的非常谢谢你。";
  c.saved = state;
  const CardWord shown = cardWord(c, PageSentence::single("谢谢你们的帮助。"));
  ASSERT_TRUE(shown.metBefore);
  EXPECT_EQ(shown.metBefore->text.substr(shown.metBefore->markStart, shown.metBefore->markLength), "谢谢");
  EXPECT_FALSE(cardWord(c, PageSentence::single("今天真的非常谢谢你。")).metBefore);  // the same sentence
  EXPECT_TRUE(cardWord(c, PageSentence::single("谢谢你。")).metBefore);  // a short one of its own inside it
}

TEST(LiveWord, ASuruVerbGivenAsItsNounStartsTheFormTabFromNounPlusSuru) {
  LookupCard c;
  c.surface = "勉強した";
  c.lemma = "勉強";  // analyze/text's lemma for a する verb
  c.charStart = 2;
  c.charEnd = 6;
  const CardWord w = cardWord(c, PageSentence::single("昨日勉強した。"));
  EXPECT_EQ(w.word, "勉強");  // the headword stays Lexirise's
  EXPECT_EQ(w.conjugation, "past");
  ASSERT_EQ(w.forms.size(), 2u);
  EXPECT_EQ(w.forms[0].form, "勉強する");
  EXPECT_TRUE(w.forms[0].dictionaryForm);
  EXPECT_EQ(w.forms[1].form, "勉強した");
}
