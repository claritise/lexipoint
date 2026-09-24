// languages.md §1 and §7: the dc:language table, the kana heuristic, override precedence, the
// settings that switch languages off, and the legacy language= alias.

#include <gtest/gtest.h>

#include "lexirise/settings/Settings.h"
#include "lexirise/text/BookLanguage.h"

using lexipoint::Language;
using lexipoint::Settings;
using lexipoint::text::BookLanguage;
using lexipoint::text::LanguageSource;
using lexipoint::text::parseLanguageTag;
using lexipoint::text::TaggedLanguage;

TEST(BookLanguage, DcLanguageTable) {
  for (const char* ja : {"ja", "JA", "ja-JP", "ja_JP", " ja ", "jpn"}) {
    EXPECT_EQ(parseLanguageTag(ja), TaggedLanguage::Japanese) << ja;
  }
  for (const char* zh : {"zh", "zh-CN", "zh-SG", "zh-Hans", "zh-Hans-CN", "cmn", "cmn-Hans", "zho", "chi"}) {
    EXPECT_EQ(parseLanguageTag(zh), TaggedLanguage::Chinese) << zh;
  }
  for (const char* hant : {"zh-TW", "zh-HK", "zh-MO", "zh-Hant", "zh-Hant-TW", "zh_tw", "cmn-Hant"}) {
    EXPECT_EQ(parseLanguageTag(hant), TaggedLanguage::ChineseTraditional) << hant;
  }
  for (const char* other : {"", "und", "en", "en-US", "ko", "fr", "jaa", "zhx"}) {
    EXPECT_EQ(parseLanguageTag(other), TaggedLanguage::Unknown) << other;
  }
}

TEST(BookLanguage, MetadataWinsOverTheSentence) {
  const Settings s;
  EXPECT_EQ(BookLanguage("zh-CN", std::nullopt).decide("ひらがな", s).language, Language::Chinese);
  const auto ja = BookLanguage("ja", std::nullopt).decide("学生", s);
  EXPECT_EQ(ja.language, Language::Japanese);
  EXPECT_EQ(ja.source, LanguageSource::Metadata);
  EXPECT_FALSE(BookLanguage("zh-TW", std::nullopt).decide("學生", s).language);  // Traditional: StarDict
}

TEST(BookLanguage, SentenceHeuristicWhenMetadataIsMissingOrWrong) {
  Settings s;
  s.defaultLanguage = Language::Chinese;
  const auto kana = BookLanguage("en", std::nullopt).decide("彼は学生だ。", s);  // a Japanese novel stamped "en"
  EXPECT_EQ(kana.language, Language::Japanese);
  EXPECT_EQ(kana.source, LanguageSource::Kana);
  EXPECT_EQ(BookLanguage("", std::nullopt).decide("カタカナ", s).language, Language::Japanese);
  const auto han = BookLanguage("und", std::nullopt).decide("我是学生。", s);
  EXPECT_EQ(han.language, Language::Chinese);  // Han only: the configured default
  EXPECT_EQ(han.source, LanguageSource::DefaultForHan);
  s.defaultLanguage = Language::Japanese;
  EXPECT_EQ(BookLanguage("und", std::nullopt).decide("学生", s).language, Language::Japanese);
  EXPECT_FALSE(BookLanguage("en", std::nullopt).decide("An English sentence.", s).language);  // StarDict
}

TEST(BookLanguage, MiddleDotAndLongVowelMarkAreNotKana) {
  Settings s;
  s.defaultLanguage = Language::Chinese;
  // Chinese transliterated names use ・ and sometimes ー: still Han-only, so the default applies.
  EXPECT_EQ(BookLanguage("", std::nullopt).decide("哈利・波特来了。", s).language, Language::Chinese);
  EXPECT_EQ(BookLanguage("", std::nullopt).decide("ー", s).language, std::nullopt);
  EXPECT_EQ(BookLanguage("", std::nullopt).decide("ハリー・ポッター", s).language, Language::Japanese);
}

TEST(BookLanguage, OverrideBeatsEverything) {
  const Settings s;
  const auto d = BookLanguage("ja", Language::Chinese).decide("ひらがな", s);
  EXPECT_EQ(d.language, Language::Chinese);
  EXPECT_EQ(d.source, LanguageSource::Override);
  EXPECT_EQ(BookLanguage("zh-TW", Language::Chinese).decide("", s).language, Language::Chinese);
}

TEST(BookLanguage, SwitchedOffLanguagesGoToStarDict) {
  Settings s;
  s.chinese.enabled = false;
  const auto off = BookLanguage("zh", std::nullopt).decide("学生", s);
  EXPECT_FALSE(off.language);
  EXPECT_EQ(off.detected, Language::Chinese);                                  // still known, for the punctuation
  EXPECT_FALSE(BookLanguage("ja", Language::Chinese).decide("", s).language);  // override can't force it on
  EXPECT_EQ(BookLanguage("ja", std::nullopt).decide("", s).language, Language::Japanese);
  s.enabled = false;
  EXPECT_FALSE(BookLanguage("ja", std::nullopt).decide("", s).language);
}

TEST(BookLanguage, LegacyLanguageKeyIsTheDefault) {
  // language= (the pre-sections flat key) is read as default_language.
  const auto parsed = lexipoint::parseSettings("language=zh\n");
  EXPECT_EQ(parsed.settings.defaultLanguage, Language::Chinese);
  EXPECT_TRUE(parsed.migratedLegacyKeys);
}

TEST(BookLanguage, SourceNames) {
  EXPECT_STREQ(lexipoint::text::languageSourceName(LanguageSource::Kana), "kana");
  EXPECT_STREQ(lexipoint::text::languageSourceName(LanguageSource::None), "none");
}
