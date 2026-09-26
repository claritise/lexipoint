// C4 (V3): a Lexirise deck per book, filled by its book tag.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Fakes.h"
#include "lexirise/deck/BookDeck.h"

using lexipoint::Language;
using lexipoint::Settings;
using lexipoint::api::ApiError;
using lexipoint::api::DeckSummary;
using namespace lexipoint::deck;
namespace config = lexipoint::config;

namespace {

BookDeck kokoro() { return {"kokoro", "Lexipoint: Kokoro"}; }

// Answers as sendDeckStep reads them: a 2xx was sent (and a readable list is whole unless said otherwise).
DeckAnswer ok(std::optional<std::string> id = std::nullopt, const bool complete = true) {
  return {ApiError::None, 200, std::move(id), false, true, complete};
}
DeckAnswer failed(const ApiError error, const int status = 0) {  // never reached Lexirise
  return {error, status, std::nullopt, false, false, false};
}
DeckAnswer sentFailed(const ApiError error, const int status = 0) {  // reached Lexirise, then failed
  return {error, status, std::nullopt, false, true, false};
}
DeckAnswer unreadable() { return {ApiError::None, 200, std::nullopt, true, true, false}; }

DeckSummary summary(std::string id, std::string title, std::vector<std::string> tags, std::string type = "dynamic") {
  DeckSummary d;
  d.id = std::move(id);
  d.title = std::move(title);
  d.deckType = std::move(type);
  d.unitType = "word";
  d.ruleType = "user_tag_filter";
  d.userTags = std::move(tags);
  return d;
}

}  // namespace

TEST(DeckState, NothingUntilASaveWithTheBookTagWentThrough) { EXPECT_EQ(nextStep({}), DeckStep::None); }

TEST(DeckState, ANewBookListsThenCreates) {
  DeckState s;
  want(s);
  EXPECT_EQ(nextStep(s), DeckStep::List);
  answer(s, DeckStep::List, ok());  // none of the user's decks is the book's
  EXPECT_TRUE(s.listedNone);
  EXPECT_EQ(nextStep(s), DeckStep::Create);
  answer(s, DeckStep::Create, ok("12"));
  EXPECT_EQ(s.id, "12");
  EXPECT_EQ(nextStep(s), DeckStep::None);
  want(s);  // the next save: recorded and checked this boot, nothing to do
  EXPECT_EQ(nextStep(s), DeckStep::None);
}

TEST(DeckState, ADeckFoundByTheListIsReused) {
  DeckState s;
  want(s);
  answer(s, DeckStep::List, ok("7"));
  EXPECT_EQ(s.id, "7");
  EXPECT_TRUE(s.checked);
  EXPECT_EQ(nextStep(s), DeckStep::None);  // never created
}

TEST(DeckState, ARecordedDeckIsCheckedOncePerBoot) {
  DeckState s{std::string("7")};
  want(s);
  EXPECT_EQ(nextStep(s), DeckStep::Check);
  answer(s, DeckStep::Check, ok());
  EXPECT_EQ(nextStep(s), DeckStep::None);
  want(s);
  EXPECT_FALSE(s.wanted);
}

TEST(DeckState, ADeletedDeckIsForgottenAndMadeAgain) {
  DeckState s{std::string("7")};
  want(s);
  answer(s, DeckStep::Check, failed(ApiError::Http, 404));
  EXPECT_EQ(s.id, std::nullopt);
  EXPECT_EQ(nextStep(s), DeckStep::List);
  answer(s, DeckStep::List, ok());
  EXPECT_EQ(nextStep(s), DeckStep::Create);
  answer(s, DeckStep::Create, ok("13"));
  EXPECT_EQ(s.id, "13");
}

TEST(DeckState, OfflineA429OrAnUnreadableListWaitsForTheNextSave) {
  for (const DeckStep step : {DeckStep::Check, DeckStep::List}) {
    for (const DeckAnswer& a :
         {failed(ApiError::NoWifi), failed(ApiError::RateLimited, 429), failed(ApiError::Server, 500), unreadable()}) {
      DeckState s;
      if (step == DeckStep::Check) s.id = "7";
      want(s);
      answer(s, step, a);
      EXPECT_EQ(nextStep(s), DeckStep::None);
      EXPECT_FALSE(s.listedNone);  // never on to Create from an answer that wasn't read
      want(s);                     // the next tagged save: the same step again
      EXPECT_EQ(nextStep(s), step);
    }
  }
}

TEST(DeckState, ASentCreationIsThisBootsOneWhateverCameBack) {
  for (const DeckAnswer& a :
       {unreadable(), sentFailed(ApiError::Timeout), sentFailed(ApiError::Network), sentFailed(ApiError::Server, 502),
        sentFailed(ApiError::Http, 422), sentFailed(ApiError::RateLimited, 429), ok()}) {
    DeckState s;
    want(s);
    answer(s, DeckStep::List, ok());
    answer(s, DeckStep::Create, a);
    EXPECT_TRUE(s.createdThisBoot);
    EXPECT_EQ(nextStep(s), DeckStep::None);
    want(s);
    EXPECT_EQ(nextStep(s), DeckStep::List);  // a lost answer's deck is found by the list
    answer(s, DeckStep::List, ok());         // a whole list without it: still no second creation this boot
    EXPECT_EQ(nextStep(s), DeckStep::None);
    EXPECT_FALSE(s.wanted);  // nothing left this boot: an idle card doesn't keep asking
    want(s);
    answer(s, DeckStep::List, ok("21"));  // listed: recorded
    EXPECT_EQ(s.id, "21");
  }
}

TEST(DeckState, ACreationThatNeverLeftIsTriedAgainOnTheNextSave) {
  // A failed connect (or connect timeout, or a write that failed): nothing reached Lexirise.
  for (const DeckAnswer& a : {failed(ApiError::Network), failed(ApiError::Timeout), failed(ApiError::NoWifi)}) {
    DeckState s;
    want(s);
    answer(s, DeckStep::List, ok());
    answer(s, DeckStep::Create, a);
    EXPECT_FALSE(s.createdThisBoot);
    want(s);
    EXPECT_EQ(nextStep(s), DeckStep::List);
    answer(s, DeckStep::List, ok());
    EXPECT_EQ(nextStep(s), DeckStep::Create);
  }
}

TEST(DeckState, ALaterBootCreatesAfterAWholeListWithoutTheDeck) {
  DeckState reboot;  // what a creation whose answer was lost leaves for the next boot: nothing recorded
  want(reboot);
  answer(reboot, DeckStep::List, ok());
  EXPECT_EQ(nextStep(reboot), DeckStep::Create);
}

TEST(DeckState, AListThatIsntWholeOrReadableNeverSaysTheDeckIsntThere) {
  for (const DeckAnswer& a : {ok(std::nullopt, /*complete=*/false), unreadable()}) {
    DeckState s;
    want(s);
    answer(s, DeckStep::List, a);
    EXPECT_FALSE(s.listedNone);
    EXPECT_EQ(nextStep(s), DeckStep::None);  // no creation from part of a list
  }
}

TEST(FindBookDeck, TheTagFirstThenTheTitle) {
  const BookDeck deck = kokoro();
  EXPECT_EQ(findBookDeck({summary("1", "Other", {"book:x"}), summary("2", "Renamed", {"book:kokoro"})}, deck,
                         Language::Japanese),
            "2");
  EXPECT_EQ(findBookDeck({summary("3", "Lexipoint: Kokoro", {})}, deck, Language::Japanese),
            "3");  // made by hand with the title
  EXPECT_EQ(findBookDeck({summary("4", "Lexipoint: Kokoro", {}, "snapshot")}, deck, Language::Japanese), std::nullopt);
  EXPECT_EQ(findBookDeck({summary("5", "Lexipoint: Kokoro 2", {"book:kokoro-2"})}, deck, Language::Japanese),
            std::nullopt);
  DeckSummary sentences = summary("6", "x", {"book:kokoro"});
  sentences.unitType = "sentence";  // a sentence deck on the tag isn't the word deck
  EXPECT_EQ(findBookDeck({sentences}, deck, Language::Japanese), std::nullopt);
  EXPECT_EQ(findBookDeck({}, deck, Language::Japanese), std::nullopt);

  DeckSummary untyped;  // a list that doesn't say the type: the exact title counts
  untyped.id = "8";
  untyped.title = "Lexipoint: Kokoro";
  EXPECT_EQ(findBookDeck({untyped}, deck, Language::Japanese), "8");
  DeckSummary starred = summary("9", "Lexipoint: Kokoro", {"book:kokoro"});
  starred.starred = true;  // someone else's deck with our title and tag, starred: never ours
  EXPECT_EQ(findBookDeck({starred}, deck, Language::Japanese), std::nullopt);
  EXPECT_EQ(findBookDeck({starred, untyped}, deck, Language::Japanese), "8");
  // Our title, but listed tags that aren't the book's: another deck.
  EXPECT_EQ(findBookDeck({summary("10", "Lexipoint: Kokoro", {"book:other"})}, deck, Language::Japanese), std::nullopt);

  // A title match whose unit is sentences (C3's deck, same title and tag) isn't the word deck.
  DeckSummary sentenceTitle = untyped;
  sentenceTitle.unitType = "sentence";
  EXPECT_EQ(findBookDeck({sentenceTitle}, deck, Language::Japanese), std::nullopt);
}

TEST(FindBookDeck, AQueryDeckWithOurTitleIsntTheBooks) {
  const BookDeck deck = kokoro();
  DeckSummary query = summary("18", "Lexipoint: Kokoro", {});
  query.ruleType = "saved_vocab_query";  // the user's own deck, made with our title
  EXPECT_EQ(findBookDeck({query}, deck, Language::Japanese), std::nullopt);
  query.ruleType.clear();  // the list doesn't say: the title counts
  EXPECT_EQ(findBookDeck({query}, deck, Language::Japanese), "18");
}

TEST(FindBookDeck, ADeckInAnotherLanguageIsntTheBooks) {
  const BookDeck deck = kokoro();
  DeckSummary zh = summary("11", "Lexipoint: Kokoro", {"book:kokoro"});
  zh.language = "zh";
  EXPECT_EQ(findBookDeck({zh}, deck, Language::Japanese), std::nullopt);
  EXPECT_EQ(findBookDeck({zh}, deck, Language::Chinese), "11");
  zh.language = "zh-Hans";
  EXPECT_EQ(findBookDeck({zh}, deck, Language::Chinese), "11");
  DeckSummary unsaid = summary("12", "Lexipoint: Kokoro", {"book:kokoro"});  // no language: the query's
  EXPECT_EQ(findBookDeck({zh, unsaid}, deck, Language::Japanese), "12");
  // Other spellings: one that reads as the other language is left out; one this can't read may be ours.
  for (const char* ja : {"ja_JP", "japanese", "jpn", "JA"}) {
    DeckSummary d = summary("16", "Lexipoint: Kokoro", {"book:kokoro"});
    d.language = ja;
    EXPECT_EQ(findBookDeck({d}, deck, Language::Japanese), "16") << ja;
  }
  DeckSummary zhCn = summary("17", "Lexipoint: Kokoro", {"book:kokoro"});
  zhCn.language = "zh_CN";
  EXPECT_EQ(findBookDeck({zhCn}, deck, Language::Japanese), std::nullopt);
  EXPECT_EQ(findBookDeck({zhCn}, deck, Language::Chinese), "17");
}

TEST(FindBookDeck, OwnershipDecidesWhenSaidElseStarred) {
  const BookDeck deck = kokoro();
  DeckSummary mine = summary("13", "Lexipoint: Kokoro", {"book:kokoro"});
  mine.starred = true;
  mine.owned = true;  // the user starred their own deck: still theirs
  EXPECT_EQ(findBookDeck({mine}, deck, Language::Japanese), "13");
  DeckSummary others = summary("14", "Lexipoint: Kokoro", {"book:kokoro"});
  others.owned = false;
  EXPECT_EQ(findBookDeck({others}, deck, Language::Japanese), std::nullopt);
  DeckSummary starredOnly = summary("15", "Lexipoint: Kokoro", {"book:kokoro"});
  starredOnly.starred = true;  // no ownership said: a starred deck is taken as someone else's
  EXPECT_EQ(findBookDeck({starredOnly}, deck, Language::Japanese), std::nullopt);
}

TEST(BookDeckFor, OnlyWhileTheBookIsTaggedAndDecksAreOn) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::BookTagStore titles(files);
  Settings s;
  const auto deck = bookDeckFor(s, "活着", "/Books/活着.epub", titles);
  ASSERT_TRUE(deck);
  EXPECT_EQ(deck->slug, "h98593b64");
  EXPECT_EQ(deck->tag(), "book:h98593b64");
  EXPECT_EQ(deck->title, "Lexipoint: 活着");
  EXPECT_EQ(bookDeckFor(s, "", "/Books/untitled.epub", titles)->title, "Lexipoint: untitled");
  titles.remember("kokoro", "Kokoro (first edition)");  // the record's title wins: one deck name everywhere
  EXPECT_EQ(bookDeckFor(s, "Kokoro", "/k.epub", titles)->title, "Lexipoint: Kokoro (first edition)");

  s.deckPerBook = false;
  EXPECT_FALSE(bookDeckFor(s, "Kokoro", "/k.epub", titles));
  s.deckPerBook = true;
  s.tagBook = false;  // the deck is filled by the book tag: none without it
  EXPECT_FALSE(bookDeckFor(s, "Kokoro", "/k.epub", titles));
  s.tagBook = true;
  s.enabled = false;
  EXPECT_FALSE(bookDeckFor(s, "Kokoro", "/k.epub", titles));
}

TEST(Decks, ParsesItsLinesAndRoundTrips) {
  const DeckList list = parseDecks(
      "ja:kokoro=7\r\n"
      "zh:h98593b64=d_8\n"
      "en:x=1\n"    // not a lookup language
      "ja:Bad=1\n"  // not a slug
      "ja:y=4/2\n"  // not a plain id
      "ja:z=\n"     // no id
      "ja:p=?\n"    // an older build's "created, not confirmed": ignored
      "JA:up=1\n"   // not as deckKey writes it
      "garbage\n"
      "ja:kokoro=9");  // listed again: the last line wins
  EXPECT_EQ(list, (DeckList{{"zh:h98593b64", "d_8"}, {"ja:kokoro", "9"}}));
  EXPECT_EQ(serializeDecks(list), "zh:h98593b64=d_8\nja:kokoro=9\n");
  EXPECT_EQ(parseDecks(serializeDecks(list)), list);
  EXPECT_EQ(deckKey(Language::Chinese, "h98593b64"), "zh:h98593b64");
}

TEST(Decks, TheOldestAreForgottenPastTheLimit) {
  DeckList list;
  for (size_t i = 0; i <= config::kDecksMax; i++) setDeckIn(list, "ja:b" + std::to_string(i), "1");
  ASSERT_EQ(list.size(), config::kDecksMax);
  EXPECT_EQ(list.front().key, "ja:b1");
  EXPECT_LE(serializeDecks(list).size(), config::kDecksMaxBytes);
}

namespace {

DeckCall call(std::string key, const DeckStep step, DeckAnswer a) { return {std::move(key), step, std::move(a)}; }

}  // namespace

TEST(DeckStore, RemembersChecksAndForgets) {
  lexipoint::fakes::FakeFiles files;
  files.files[config::kDecksPath] = "ja:kokoro=7\n";
  DeckStore store(files);
  EXPECT_EQ(store.state("ja:kokoro").id, "7");
  EXPECT_EQ(store.next("ja:kokoro"), DeckStep::None);  // not wanted yet
  store.want("ja:kokoro");
  EXPECT_EQ(store.next("ja:kokoro"), DeckStep::Check);
  EXPECT_TRUE(store.apply(call("ja:kokoro", DeckStep::Check, ok())));
  EXPECT_EQ(store.next("ja:kokoro"), DeckStep::None);
  EXPECT_EQ(files.writes, 0);  // a check writes nothing

  store.want("zh:h98593b64");
  EXPECT_EQ(store.next("zh:h98593b64"), DeckStep::List);
  EXPECT_TRUE(store.apply(call("zh:h98593b64", DeckStep::List, ok("12"))));
  EXPECT_EQ(files.files[config::kDecksPath], "ja:kokoro=7\nzh:h98593b64=12\n");

  store.want("ja:kokoro");  // checked this boot: nothing to do
  EXPECT_EQ(store.next("ja:kokoro"), DeckStep::None);
  DeckStore reboot(files);
  EXPECT_EQ(reboot.state("zh:h98593b64").id, "12");
  reboot.want("ja:kokoro");
  EXPECT_TRUE(reboot.apply(call("ja:kokoro", DeckStep::Check, failed(ApiError::Http, 404))));
  EXPECT_EQ(files.files[config::kDecksPath], "zh:h98593b64=12\n");
  EXPECT_EQ(reboot.next("ja:kokoro"), DeckStep::List);
}

TEST(DeckStore, OnlyAConfirmedDeckIsWritten) {
  lexipoint::fakes::FakeFiles files;
  files.files[config::kDecksPath] = "ja:old=?\n";  // an older build's line: read as nothing recorded
  DeckStore store(files);
  EXPECT_EQ(store.state("ja:old").id, std::nullopt);
  store.want("ja:kokoro");
  EXPECT_TRUE(store.anyWanted());
  store.apply(call("ja:kokoro", DeckStep::List, ok()));
  EXPECT_EQ(store.next("ja:kokoro"), DeckStep::Create);
  const int writes = files.writes;
  store.apply(call("ja:kokoro", DeckStep::Create, sentFailed(ApiError::Timeout)));
  EXPECT_EQ(files.writes, writes);  // nothing to record
  EXPECT_FALSE(store.anyWanted());

  DeckStore reboot(files);  // a later boot: the list shows it (it was made after all)
  reboot.want("ja:kokoro");
  EXPECT_EQ(reboot.next("ja:kokoro"), DeckStep::List);
  reboot.apply(call("ja:kokoro", DeckStep::List, ok("30")));
  EXPECT_EQ(files.files[config::kDecksPath], "ja:kokoro=30\n");
}

TEST(DeckStore, AFailedSaveStillChangesMemorySoNothingRepeats) {
  lexipoint::fakes::FakeFiles files;
  files.files[config::kDecksPath] = "ja:kokoro=7\n";
  DeckStore store(files);
  store.want("ja:kokoro");
  files.failWriteOf = config::kDecksTmpPath;
  EXPECT_FALSE(store.apply(call("ja:kokoro", DeckStep::Check, failed(ApiError::Http, 404))));  // Forget not saved
  EXPECT_EQ(store.next("ja:kokoro"), DeckStep::List);  // on to the list, not the same 404 again
  files.failWriteOf = config::kDecksTmpPath;
  EXPECT_FALSE(store.apply(call("ja:kokoro", DeckStep::List, ok("8"))));
  EXPECT_EQ(store.state("ja:kokoro").id, "8");
  EXPECT_EQ(store.next("ja:kokoro"), DeckStep::None);
}

TEST(DeckStore, ABookWhoseCreationWentIsNeverForgottenThisBoot) {
  lexipoint::fakes::FakeFiles files;
  DeckStore store(files);
  store.want("ja:a");
  store.apply(call("ja:a", DeckStep::List, ok()));
  store.apply(call("ja:a", DeckStep::Create, sentFailed(ApiError::Timeout)));  // sent, answer lost
  for (size_t i = 0; i < config::kDeckWorkMax + 5; i++) {                      // many more books, each settled
    const std::string key = "ja:b" + std::to_string(i);
    EXPECT_EQ(store.next("zh:b" + std::to_string(i)), DeckStep::None);  // a read never adds (deckDue's probe)
    store.want(key);
    store.apply(call(key, DeckStep::List, ok(std::to_string(100 + i))));
  }
  store.want("ja:a");  // book A's next tagged save
  EXPECT_EQ(store.next("ja:a"), DeckStep::List);
  store.apply(call("ja:a", DeckStep::List, ok()));  // a whole list still without it
  EXPECT_EQ(store.next("ja:a"), DeckStep::None);    // never a second creation this boot
  EXPECT_FALSE(store.anyWanted());
}

TEST(DeckStore, WorkLeftIsNeverDropped) {
  lexipoint::fakes::FakeFiles files;
  DeckStore store(files);
  store.want("ja:keep");
  for (size_t i = 0; i < config::kDeckWorkMax + 5; i++) {
    const std::string key = "ja:b" + std::to_string(i);
    store.want(key);
    store.apply(call(key, DeckStep::List, ok(std::to_string(100 + i))));
  }
  EXPECT_EQ(store.next("ja:keep"), DeckStep::List);  // still wanted
}

TEST(DeckStore, AnIdALineCantHoldIsntRecordedOrWritten) {
  lexipoint::fakes::FakeFiles files;
  DeckStore store(files);
  store.want("ja:kokoro");
  EXPECT_FALSE(store.apply(call("ja:kokoro", DeckStep::List, ok("x/1"))));  // not a plain id
  EXPECT_EQ(files.writes, 0);
  EXPECT_EQ(store.next("ja:kokoro"), DeckStep::None);  // in memory for this boot: no repeat call
}

TEST(DeckStore, AnUnreadableFileIsSetAside) {
  lexipoint::fakes::FakeFiles big;
  big.files[config::kDecksPath] = std::string(config::kDecksMaxBytes + 1, 'x');
  DeckStore store(big);
  EXPECT_EQ(store.state("ja:kokoro").id, std::nullopt);
  EXPECT_EQ(big.files.count(config::kDecksBadPath), 1u);
}
