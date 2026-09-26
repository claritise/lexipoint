// C2: a book's tag, `book:<slug>` of its title, the same on every device.

#include <gtest/gtest.h>

#include <string>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/text/BookSlug.h"

using lexipoint::text::bookSlug;
using lexipoint::text::bookTag;
namespace config = lexipoint::config;

TEST(BookSlug, ALatinTitleIsLowerCaseWordsJoinedByDashes) {
  EXPECT_EQ(bookSlug("Norwegian Wood", "/a.epub"), "norwegian-wood");
  EXPECT_EQ(bookTag("Norwegian Wood", "/a.epub"), "book:norwegian-wood");
}

TEST(BookSlug, AccentsAndPunctuationAreSeparators) {
  // Only ASCII letters and digits are kept: é is dropped like punctuation, not folded to e.
  EXPECT_EQ(bookSlug("Café au lait!", "/a.epub"), "caf-au-lait");
  EXPECT_EQ(bookSlug("  \"The Wind-Up Bird\" -- Chronicle...  ", "/a.epub"), "the-wind-up-bird-chronicle");
}

TEST(BookSlug, ATitleWithNoAsciiIsAHashOfTheTitle) {
  // Pinned: another device (or firmware) must give the same tag for the same book.
  EXPECT_EQ(bookSlug("活着", "/Books/活着.epub"), "h98593b64");                           // Chinese
  EXPECT_EQ(bookSlug("変身", "/Books/変身.epub"), "h1aae0462");                           // Japanese
  EXPECT_EQ(bookSlug("ノルウェイの森", "/x.epub"), "h02f5a81f");                          // Japanese, kana
  EXPECT_EQ(bookSlug("活着", "/Other/copy.epub"), bookSlug("活着", "/Books/活着.epub"));  // the path doesn't count
  EXPECT_EQ(bookSlug("三体 2", "/a.epub").front(), 'h');  // one digit is fewer than the minimum
}

TEST(BookSlug, AMixedTitleWithEnoughAsciiKeepsIt) {
  EXPECT_EQ(bookSlug("1Q84", "/a.epub"), "1q84");
  EXPECT_EQ(bookSlug("1Q84 BOOK 1 (新潮文庫)", "/a.epub"), "1q84-book-1");
  EXPECT_EQ(bookSlug("ノルウェイの森 Norwegian Wood", "/a.epub"), "norwegian-wood");
}

TEST(BookSlug, ALongTitleIsCutAtADash) {
  const std::string slug = bookSlug("The Strange Case of Dr Jekyll and Mr Hyde and Other Tales of Terror", "/a.epub");
  EXPECT_EQ(slug, "the-strange-case-of-dr-jekyll-and");
  EXPECT_LE(slug.size(), config::kBookSlugMaxBytes);
  EXPECT_LE(bookTag("The Strange Case of Dr Jekyll and Mr Hyde", "/a.epub").size(), config::kMaxTagLength);

  const std::string word(60, 'a');  // no dash to cut at: cut at the cap
  EXPECT_EQ(bookSlug(word, "/a.epub"), std::string(config::kBookSlugMaxBytes, 'a'));
  // A dash in the first half only would leave too little: cut at the cap, never ending on a dash.
  const std::string early = bookSlug("ab " + word, "/a.epub");
  EXPECT_EQ(early.size(), config::kBookSlugMaxBytes);
  EXPECT_NE(early.back(), '-');
}

TEST(BookSlug, TheSameTitleGivesTheSameSlug) {
  // Two books with one title share a tag (and so a deck): the slug is the title's, not the file's.
  EXPECT_EQ(bookSlug("Kokoro", "/Books/Kokoro (Penguin).epub"), bookSlug("Kokoro", "/Books/kokoro.epub"));
  EXPECT_EQ(bookSlug("こころ", "/1.epub"), bookSlug("こころ", "/2.epub"));
}

TEST(BookSlug, AnUntitledBookHashesItsPath) {
  EXPECT_EQ(bookSlug("", "/Books/untitled.epub"), "h74959a8c");
  EXPECT_EQ(bookSlug("   ", "/Books/untitled.epub"), "h74959a8c");
  EXPECT_EQ(bookSlug("\t\x01\r\n\x7F", "/Books/untitled.epub"), "h74959a8c");  // only control characters
  EXPECT_TRUE(lexipoint::text::isUntitled(" \x01 "));
  EXPECT_FALSE(lexipoint::text::isUntitled(" 活着 "));
  EXPECT_EQ(bookSlug("\x01活着\n", "/a.epub"), bookSlug("活着", "/a.epub"));  // hashed trimmed at both ends
  EXPECT_NE(bookSlug("", "/Books/other.epub"), bookSlug("", "/Books/untitled.epub"));
}

TEST(BookSlug, Fnv1aMatchesTheReferenceVectors) {
  EXPECT_EQ(lexipoint::text::fnv1a32(""), 0x811c9dc5u);
  EXPECT_EQ(lexipoint::text::fnv1a32("a"), 0xe40c292cu);
  EXPECT_EQ(lexipoint::text::fnv1a32("foobar"), 0xbf9cf968u);
}
