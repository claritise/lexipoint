// The live card source (LiveSource.h) with the controller: phase 0, the analysis (A), the lookup (B),
// stepping, and the answers that close the card. A scripted Lexirise; synthetic data.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "FakeApi.h"
#include "FakeMetrics.h"
#include "Fakes.h"
#include "lexirise/card/BenchFixtures.h"
#include "lexirise/card/BenchSource.h"
#include "lexirise/card/CardController.h"
#include "lexirise/card/CardFrame.h"
#include "lexirise/card/CardSession.h"
#include "lexirise/card/LiveSource.h"
#include "lexirise/card/WordSelectFlow.h"
#include "lexirise/lookup/Fallback.h"

using namespace lexipoint::card;
using lexipoint::Language;
namespace config = lexipoint::config;
using lexipoint::api::ApiError;
using lexipoint::card::test::FakeMetrics;
using lexipoint::fakes::apiFailure;
using lexipoint::fakes::apiOk;
using lexipoint::fakes::FakeApi;
using lexipoint::text::buildSentence;
using lexipoint::text::PageModel;
using lexipoint::text::Script;
using lexipoint::text::TapContext;
using lexipoint::text::TextLine;

namespace {

const FakeMetrics kMetrics;

constexpr const char* kAnalyze =
    R"({"occurrences":[)"
    R"({"word":"彼","isWordLike":true,"transliteration":"kare","charStart":0,"charEnd":1,"entryId":1},)"
    R"({"word":"は","isWordLike":true,"transliteration":"wa","charStart":1,"charEnd":2,"entryId":2},)"
    R"({"word":"本","isWordLike":true,"transliteration":"hon","charStart":2,"charEnd":3,"entryId":3},)"
    R"({"word":"を","isWordLike":true,"transliteration":"wo","charStart":3,"charEnd":4,"entryId":4},)"
    R"({"word":"読んだ","lemma":"読む","isWordLike":true,"transliteration":"yonda","charStart":4,"charEnd":7,)"
    R"("entryId":5,"lemmaEntryId":6},)"
    R"({"word":"。","isWordLike":false,"charStart":7,"charEnd":8}],)"
    R"("entryMetaById":{"6":{"transliteration":"yomu","rank":400}},)"
    R"("stateByEntryId":{"3":{"saved_expression_id":77,"proficiency":3}}})";

constexpr const char* kLookupYomu =
    R"({"word":"読む","transliteration":"yomu","rank":350,"frequency_score":0.8,"system_tags":["JLPT-N5"],)"
    R"("translation_status":"ready","translations":[{"translation":"to read","part_of_speech":["verb"]}]})";

struct Rig {
  FakeApi api;
  PageModel model;
  ReaderPage page;
  Rig() {
    TextLine a;
    a.tokens = {"彼", "は", "本", "を"};
    a.startsParagraph = true;
    TextLine b;
    b.tokens = {"読んだ", "。"};
    model.lines = {a, b};
    page.lines = {{100, {{"彼", 20, 26}, {"は", 46, 26}, {"本", 72, 26}, {"を", 98, 26}}},
                  {140, {{"読んだ", 20, 78}, {"。", 98, 26}}}};
  }
  TapContext tap(const size_t line, const size_t token) const {
    TapContext t;
    t.sentence = buildSentence(model, {line, token}, Script::Japanese);
    t.language.language = Language::Japanese;
    return t;
  }
};

}  // namespace

TEST(LiveSource, PhaseZeroThenAThenB) {
  Rig rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tap(1, 0), rig.page);  // tapped 読んだ
  CardController c(source, ReadingMode::Kana);
  c.open(0);

  // Phase 0: the tapped character, highlighted alone, before any call.
  EXPECT_EQ(c.state().phase, Phase::Pending);
  EXPECT_EQ(c.state().pendingText, "読");
  const PageScene zero = source.scene(c.word(), true, kMetrics, c.highlightCodepoints());
  EXPECT_EQ(zero.page.commands.at(1).text, "読");
  EXPECT_TRUE(rig.api.analyzed.empty());

  ASSERT_TRUE(source.hasWork(0));
  EXPECT_EQ(source.advance(), LiveSource::Advance::Changed);  // A
  EXPECT_TRUE(c.sourceChanged(0));
  EXPECT_EQ(c.state().phase, Phase::Analyzed);
  EXPECT_EQ(c.word(), 4);
  EXPECT_EQ(c.currentWord().word, "読む");
  EXPECT_EQ(c.currentWord().reading, "よむ");
  EXPECT_EQ(source.scene(c.word(), true, kMetrics, c.highlightCodepoints()).page.commands.at(1).text, "読んだ");
  EXPECT_TRUE(rig.api.looked.empty());

  EXPECT_EQ(source.advance(), LiveSource::Advance::Changed);  // B
  EXPECT_EQ(rig.api.looked, std::vector<std::string>{"読む"});
  EXPECT_TRUE(c.sourceChanged(0));
  EXPECT_EQ(c.state().phase, Phase::Complete);
  EXPECT_EQ(c.currentWord().senses, std::vector<std::string>{"to read"});
  EXPECT_EQ(c.currentWord().badge, "N5");
  EXPECT_FALSE(source.hasWork(0));
  EXPECT_EQ(source.advance(), LiveSource::Advance::Idle);
}

TEST(LiveSource, SteppingLooksUpOnlyTheNewWordWithItsSavedLevel) {
  Rig rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze)};
  rig.api.lookupReplies = {apiOk(kLookupYomu), apiOk(R"({"word":"を"})"), apiOk(R"({"word":"本"})")};
  LiveSource source(rig.api, rig.tap(1, 0), rig.page);
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  source.advance();
  c.sourceChanged(0);
  source.advance();
  c.sourceChanged(0);
  ASSERT_TRUE(c.step(-1, 0));  // を
  EXPECT_EQ(c.state().phase, Phase::Analyzed);
  EXPECT_TRUE(source.hasWork(0));
  source.advance();
  c.sourceChanged(0);
  ASSERT_TRUE(c.step(-1, 0));  // 本: saved at 3 (fresh)
  EXPECT_EQ(c.state().level, Level::Fresh);
  source.advance();
  EXPECT_EQ(rig.api.looked, (std::vector<std::string>{"読む", "を", "本"}));
  EXPECT_EQ(rig.api.analyzed.size(), 1u);
  ASSERT_TRUE(c.step(+1, 0));  // back to を: already looked up
  EXPECT_FALSE(source.hasWork(0));
}

TEST(LiveSource, APhaseBFailureStillShowsTheWord) {
  Rig rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze)};
  rig.api.lookupReplies = {apiFailure(ApiError::Timeout)};
  LiveSource source(rig.api, rig.tap(0, 2), rig.page);
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  source.advance();
  EXPECT_EQ(source.advance(), LiveSource::Advance::Changed);
  EXPECT_EQ(source.error(), ApiError::Timeout);
  c.sourceChanged(0);
  EXPECT_EQ(c.currentWord().word, "本");
  EXPECT_EQ(c.state().phase, Phase::Unanswered);  // the meaning row says "offline"
  EXPECT_TRUE(c.currentWord().senses.empty());
  EXPECT_FALSE(source.hasWork(0));  // not retried on its own
}

TEST(LiveSource, NoWordOrNoAnswerClosesTheCard) {
  Rig rig;
  rig.api.analyzeReplies = {apiOk(R"({"occurrences":[{"word":"。","isWordLike":false,"charStart":0,"charEnd":1}]})")};
  LiveSource notFound(rig.api, rig.tap(0, 0), rig.page);
  EXPECT_EQ(notFound.advance(), LiveSource::Advance::NotFound);

  Rig offline;
  offline.api.analyzeReplies = {apiFailure(ApiError::NoWifi)};
  LiveSource unavailable(offline.api, offline.tap(0, 0), offline.page);
  EXPECT_EQ(unavailable.advance(), LiveSource::Advance::Unavailable);
  EXPECT_EQ(unavailable.error(), ApiError::NoWifi);
  EXPECT_EQ(unavailable.wordCount(), 0);
}

namespace {

// The card on 読む, after phase A (and B when `complete`), driven the way the device drives it: frames
// laid out and shown (ShownTargets), taps at the centres of their targets through PendingInput and the
// session, answers fetched and applied.
struct Saving {
  Rig rig;
  LiveSource source;
  CardController c;
  ShownTargets targets;
  PendingInput input;
  CardSession session;
  unsigned long now = 0;
  explicit Saving(const bool complete = true, std::vector<std::string> tags = {"xteink"})
      : source(rig.api, rig.tap(1, 0), rig.page, std::move(tags)),
        c(source, ReadingMode::Kana),
        session(c, targets, input, &source) {
    rig.api.analyzeReplies = {apiOk(kAnalyze)};
    rig.api.lookupReplies = {apiOk(kLookupYomu)};
    c.open(now);
    show();
    fetchOne();                // A
    if (complete) fetchOne();  // B
  }
  // The frame for the card as it is now, on screen from `now`.
  void show() {
    targets.drawing(composeFrame(c, kMetrics).card.hits, c.steps(), c.state().view);
    targets.shown(++now);
  }
  CardSession::Answer fetchOne() {
    CardSession::Answer a = session.apply(session.fetch(now), now + 1);
    ++now;
    show();
    return a;
  }
  // A tap on the first target of that kind (and index) on screen, handled as the activity does.
  Outcome tap(const Target target, const int index = 0) {
    const ShownFrame* frame = targets.at(now);
    for (const Hit& h : frame->hits) {
      if (h.target == target && h.index == index) {
        input.tap(h.rect.x + h.rect.w / 2, h.rect.y + h.rect.h / 2, ++now);
        const Outcome o = session.handleInput(now);
        show();
        return o;
      }
    }
    ADD_FAILURE() << "no such target on screen";
    return {};
  }
  Outcome level(const int index) { return tap(Target::Level, index); }
  Outcome step(const int direction) {
    input.step(direction, ++now);
    const Outcome o = session.handleInput(now);
    show();
    return o;
  }
  // Everything the card has to send, with time moving on past each change's Undo window.
  void drain() {
    while (true) {
      if (session.hasWork(now)) {
        fetchOne();
      } else if (session.hasPendingWrites()) {
        now += lexipoint::config::kToastMs;
      } else {
        return;
      }
    }
  }
};

}  // namespace

TEST(LiveSave, ANewWordIsSavedAsD9SaysThenItsLevelIsPatched) {
  Saving s;
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":901}})"), apiOk("{}")};
  const Outcome o = s.level(1);  // L
  ASSERT_EQ(o.changes.size(), 1u);
  EXPECT_EQ(s.c.state().level, Level::Learning);  // shown at once
  EXPECT_EQ(s.c.state().toast, "Saved as learning  \xC2\xB7  Undo");
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 1u);
  const auto& post = s.rig.api.written[0];
  EXPECT_EQ(post.method, lexipoint::net::Method::Post);
  EXPECT_EQ(post.body, R"({"language":"ja","text":"読む","mode":"word","translation":"to read","proficiency":2,)"
                       R"("tags":["xteink"],"notes":"彼は本を読んだ。"})");
  s.level(3);  // K: a saved word changes level
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 2u);
  EXPECT_EQ(s.rig.api.written[1].method, lexipoint::net::Method::Patch);
  EXPECT_EQ(s.rig.api.written[1].path, "/v1/vocabulary/901");
  EXPECT_EQ(s.rig.api.written[1].body, R"({"proficiency":4})");
}

TEST(LiveSave, TheBookTagGoesWithTheSave) {
  Saving s(/*complete=*/true, {"xteink", "book:h98593b64"});  // bookSaveTags' list for 活着
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":901}})")};
  s.level(1);
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 1u);
  EXPECT_NE(s.rig.api.written[0].body.find(R"("tags":["xteink","book:h98593b64"])"), std::string::npos)
      << s.rig.api.written[0].body;
}

namespace {

// The card loop's deck check at `nowMs`: the card's due work (a toast's end) handled first, as the loop does, then
// CardSession::shouldFetchDeck with the loop's copy of when the card is next due.
bool deckStepDue(Saving& s, const unsigned long nowMs, const bool rendering, const bool touching) {
  s.c.tick(nowMs);
  if (touching) s.session.touched(nowMs);
  return s.session.shouldFetchDeck(nowMs, rendering, touching, s.c.nextDueMs());
}

// Cards in the book "Kokoro" with Deck per book on (C4, V3): their saves carry book:kokoro, and the book's deck is
// kept in a decks.ini on a fake card, shared by every card as on the device.
struct DeckBook {
  lexipoint::fakes::FakeFiles files;
  lexipoint::deck::DeckStore decks{files};
};

struct DeckSaving {
  Saving s{/*complete=*/true, {"xteink", "book:kokoro"}};
  explicit DeckSaving(DeckBook& book) {
    s.source.setBookDeck({"kokoro", "Lexipoint: Kokoro"}, book.decks);
    s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":901}})")};
  }
  void save() {
    s.level(1);
    s.drain();
  }
  // The card left idle: its deck steps, each once the card has been idle long enough.
  int idle() {
    int calls = 0;
    for (int i = 0; i < 8; i++) {
      s.now += config::kDeckIdleMs;
      if (!deckStepDue(s, s.now, false, false)) continue;
      s.session.applyDeck(s.session.fetchDeck(), s.now);
      calls++;
    }
    return calls;
  }
};

constexpr const char* kNoBookDeck =
    R"({"decks":[{"id":3,"title":"JLPT N5","deck_type":"snapshot","unit_type":"word"}]})";
constexpr const char* kCreated = R"({"success":true,"deck":{"id":12}})";

}  // namespace

TEST(LiveDeck, ANewBooksDeckIsMadeOnceTheCardIsIdleAfterTheSave) {
  DeckBook book;
  DeckSaving d(book);
  d.s.rig.api.deckReplies = {apiOk(kNoBookDeck), apiOk(kCreated)};
  d.save();
  EXPECT_TRUE(d.s.rig.api.decked.empty());  // the save alone: the deck waits for an idle card
  EXPECT_FALSE(deckStepDue(d.s, d.s.now, false, false));
  EXPECT_EQ(d.idle(), 2);
  ASSERT_EQ(d.s.rig.api.written.size(), 1u);  // the save, untouched
  ASSERT_EQ(d.s.rig.api.decked.size(), 2u);
  EXPECT_EQ(d.s.rig.api.decked[0].path, "/v1/decks?language=ja");
  EXPECT_EQ(d.s.rig.api.decked[1].method, lexipoint::net::Method::Post);
  EXPECT_NE(d.s.rig.api.decked[1].body.find(R"("user_tags":["book:kokoro"])"), std::string::npos);
  EXPECT_NE(d.s.rig.api.decked[1].body.find(R"("title":"Lexipoint: Kokoro")"), std::string::npos);
  EXPECT_EQ(book.files.files[config::kDecksPath], "ja:kokoro=12\n");

  d.s.level(3);  // a level change later: a PATCH, and no more deck calls
  d.s.drain();
  EXPECT_EQ(d.idle(), 0);
}

TEST(LiveDeck, TheBooksDeckFromAnotherDeviceIsReused) {
  DeckBook book;
  DeckSaving d(book);
  d.s.rig.api.deckReplies = {apiOk(R"({"decks":[{"deckId":"d_7","title":"My Kokoro deck","deckType":"dynamic",)"
                                   R"("unitType":"word","ruleType":"user_tag_filter","userTags":["book:kokoro"]}]})")};
  d.save();
  EXPECT_EQ(d.idle(), 1);  // found (camelCase): none created
  EXPECT_EQ(book.files.files[config::kDecksPath], "ja:kokoro=d_7\n");
}

TEST(LiveDeck, ARecordedDeckIsCheckedOnceAndA404MakesItAgain) {
  DeckBook book;
  book.files.files[config::kDecksPath] = "ja:kokoro=7\n";
  DeckSaving d(book);
  d.s.rig.api.deckReplies = {apiOk(R"({"success":true,"deck":{"id":7},"items":[],"totalCount":0,"hasMore":false})")};
  d.save();
  EXPECT_EQ(d.idle(), 1);
  EXPECT_EQ(d.s.rig.api.decked[0].path, "/v1/decks/7?limit=1");
  DeckSaving next(book);  // the next card, same boot: checked already
  next.save();
  EXPECT_EQ(next.idle(), 0);

  DeckBook deleted;  // deleted in Lexirise
  deleted.files.files[config::kDecksPath] = "ja:kokoro=7\n";
  DeckSaving g(deleted);
  lexipoint::api::ApiResponse notFound = apiFailure(ApiError::Http);
  notFound.status = 404;
  g.s.rig.api.deckReplies = {notFound, apiOk(R"({"decks":[]})"), apiOk(R"({"success":true,"deck":{"id":13}})")};
  g.save();
  EXPECT_EQ(g.idle(), 3);
  EXPECT_EQ(deleted.files.files[config::kDecksPath], "ja:kokoro=13\n");
}

TEST(LiveDeck, A404WhoseForgetCantBeSavedDoesntLoop) {
  DeckBook book;
  book.files.files[config::kDecksPath] = "ja:kokoro=7\n";
  DeckSaving d(book);
  lexipoint::api::ApiResponse notFound = apiFailure(ApiError::Http);
  notFound.status = 404;
  d.s.rig.api.deckReplies = {notFound, apiFailure(ApiError::NoWifi)};
  d.save();
  book.files.failWriteOf = config::kDecksTmpPath;
  EXPECT_EQ(d.idle(), 2);  // the 404, then the list (offline): no second GET of the gone deck
  EXPECT_EQ(d.s.rig.api.decked[1].path, "/v1/decks?language=ja");
}

TEST(LiveDeck, ACreationWhoseAnswerIsLostIsFoundByALaterBootsList) {
  DeckBook book;
  DeckSaving d(book);
  d.s.rig.api.deckReplies = {apiOk(R"({"decks":[]})"), apiOk(R"({"ok":true})")};  // created, but no id to read
  d.save();
  EXPECT_EQ(d.idle(), 2);
  EXPECT_EQ(book.files.files.count(config::kDecksPath), 0u);

  DeckSaving second(book);  // the next card with a save, same boot: only the list, and no second creation
  second.s.rig.api.deckReplies = {apiOk(R"({"decks":[]})")};
  second.save();
  EXPECT_EQ(second.idle(), 1);
  EXPECT_EQ(second.s.rig.api.decked[0].method, lexipoint::net::Method::Get);

  lexipoint::deck::DeckStore reboot(book.files);  // a later boot: the list shows the deck made before
  Saving later{/*complete=*/true, {"xteink", "book:kokoro"}};
  later.source.setBookDeck({"kokoro", "Lexipoint: Kokoro"}, reboot);
  later.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":903}})")};
  later.rig.api.deckReplies = {
      apiOk(R"({"decks":[{"id":40,"title":"Lexipoint: Kokoro","deck_type":"dynamic",)"
            R"("unit_type":"word","rule_type":"user_tag_filter","user_tags":["book:kokoro"]}]})")};
  later.level(1);
  later.drain();
  later.now += config::kDeckIdleMs;
  ASSERT_TRUE(deckStepDue(later, later.now, false, false));
  later.session.applyDeck(later.session.fetchDeck(), later.now);
  EXPECT_EQ(book.files.files[config::kDecksPath], "ja:kokoro=40\n");
}

TEST(LiveDeck, OnlyAReadableWholeListLeadsToACreation) {
  for (const char* list :
       {R"({"decks":[{"weird":1}]})",                                                    // unreadable
        R"({"decks":[],"hasMore":true})",                                                // another page
        R"({"decks":[{"id":3,"title":"a"},{"id":"x/1","title":"Lexipoint: Kokoro"}]})",  // a dropped entry
        R"([1,2])"}) {                                                                   // not decks
    DeckBook book;
    DeckSaving d(book);
    d.s.rig.api.deckReplies = {apiOk(list), apiOk(kCreated)};
    d.save();
    EXPECT_EQ(d.idle(), 1) << list;
    EXPECT_EQ(d.s.rig.api.decked.size(), 1u) << list;  // never on to Create
  }
}

TEST(LiveDeck, TheOtherLanguagesDeckOfTheBookIsntTaken) {
  DeckBook book;
  DeckSaving d(book);
  // The server ignored ?language=: the book's Chinese deck (same title and tag) is listed with the Japanese one's.
  d.s.rig.api.deckReplies = {apiOk(R"({"decks":[{"id":5,"title":"Lexipoint: Kokoro","deck_type":"dynamic",)"
                                   R"("unit_type":"word","rule_type":"user_tag_filter","user_tags":["book:kokoro"],)"
                                   R"("language":"zh"}]})"),
                             apiOk(kCreated)};
  d.save();
  EXPECT_EQ(d.idle(), 2);  // not taken: its own deck is made
  EXPECT_EQ(book.files.files[config::kDecksPath], "ja:kokoro=12\n");
}

TEST(LiveDeck, EachLanguageOfTheBookGetsItsOwnDeck) {
  DeckBook book;
  book.decks.want("zh:kokoro");  // an earlier card in the book saved a Chinese word
  DeckSaving d(book);
  d.s.rig.api.deckReplies = {apiOk(R"({"decks":[]})"), apiOk(kCreated), apiOk(R"({"decks":[]})"),
                             apiOk(R"({"success":true,"deck":{"id":13}})")};
  d.save();  // a Japanese word
  EXPECT_EQ(d.idle(), 4);
  EXPECT_EQ(d.s.rig.api.decked[0].path, "/v1/decks?language=ja");
  EXPECT_EQ(d.s.rig.api.decked[2].path, "/v1/decks?language=zh");
  EXPECT_NE(d.s.rig.api.decked[3].body.find(R"("language":"zh")"), std::string::npos);
  EXPECT_EQ(book.files.files[config::kDecksPath], "ja:kokoro=12\nzh:kokoro=13\n");
}

TEST(LiveDeck, DeckPerBookTurnedOffWhileTheCardIsOpenStopsIt) {
  DeckBook book;
  DeckSaving d(book);
  d.s.rig.api.deckReplies = {apiOk(kNoBookDeck), apiOk(kCreated)};
  d.save();
  d.s.session.setDeckAllowed(false);  // from the web page (LexiriseCardActivity reads it when the settings change)
  EXPECT_EQ(d.idle(), 0);
  d.s.session.setDeckAllowed(true);
  EXPECT_EQ(d.idle(), 2);
}

TEST(LiveDeck, NoStepWhileASaveFailedToastIsUp) {
  DeckBook book;
  DeckSaving d(book);
  d.save();  // tagged and saved: the deck is wanted
  d.s.rig.api.writeReplies = {apiFailure(ApiError::Network)};
  d.s.level(3);  // a level change that fails: "Save failed · Retry", on the same network a deck call would use
  d.s.drain();
  ASSERT_NE(d.s.c.state().toast.find("Retry"), std::string::npos);
  EXPECT_FALSE(deckStepDue(d.s, d.s.now + config::kDeckIdleMs, false, false));         // idle, but the toast is up
  EXPECT_TRUE(deckStepDue(d.s, d.s.now + config::kFailureToastMs + 1, false, false));  // gone
}

TEST(LiveDeck, AFingerOnTheScreenIsntIdle) {
  DeckBook book;
  DeckSaving d(book);
  d.save();
  d.s.now += config::kDeckIdleMs;
  EXPECT_FALSE(deckStepDue(d.s, d.s.now, false, /*touching=*/true));                // mid-swipe or long-press
  EXPECT_FALSE(deckStepDue(d.s, d.s.now + config::kDeckIdleMs - 1, false, false));  // idle from the lift
  EXPECT_TRUE(deckStepDue(d.s, d.s.now + config::kDeckIdleMs, false, false));
}

TEST(LiveDeck, OfflineOrA429SkipsQuietlyUntilALaterSave) {
  for (const ApiError error : {ApiError::NoWifi, ApiError::RateLimited}) {
    DeckBook book;
    DeckSaving d(book);
    d.s.rig.api.deckReplies = {apiFailure(error)};
    d.save();
    EXPECT_EQ(d.idle(), 1);  // one try, no retry loop
    EXPECT_EQ(book.files.files.count(config::kDecksPath), 0u);
    EXPECT_EQ(d.s.c.state().level, Level::Learning);  // the save stands
  }
}

TEST(LiveDeck, NoDeckWorkWithoutASaveThatCarriedTheBookTag) {
  DeckBook book;
  DeckSaving failed(book);  // the save failed: no deck
  failed.s.rig.api.writeReplies = {apiFailure(ApiError::Network)};
  failed.save();
  EXPECT_EQ(failed.idle(), 0);

  DeckSaving untagged(book);  // Tag with book title off: the saves don't carry the tag the deck is filled by
  untagged.s.source.setBookDeck({"other", "Lexipoint: Other"}, book.decks);
  untagged.save();
  EXPECT_EQ(untagged.idle(), 0);
}

TEST(LiveDeck, ClosingSendsOnlyTheWritesAndALaterCardMakesTheDeck) {
  DeckBook book;
  {
    DeckSaving quick(book);  // saved, closed at once
    quick.s.level(1);
    while (quick.s.session.hasPendingWrites()) {
      quick.s.session.applyClosing(quick.s.session.fetch(quick.s.now, /*closing=*/true), quick.s.now);
    }
    EXPECT_TRUE(quick.s.rig.api.decked.empty());
  }
  {
    DeckSaving another(book);  // another quick card: still nothing sent for the deck
    another.s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":902}})")};
    another.s.level(1);
    while (another.s.session.hasPendingWrites()) {
      another.s.session.applyClosing(another.s.session.fetch(another.s.now, /*closing=*/true), another.s.now);
    }
    EXPECT_TRUE(another.s.rig.api.decked.empty());
  }
  DeckSaving later(book);  // a card left open in the book, no save of its own: the deck is still wanted
  later.s.rig.api.deckReplies = {apiOk(R"({"decks":[]})"), apiOk(kCreated)};
  EXPECT_EQ(later.idle(), 2);
  EXPECT_EQ(book.files.files[config::kDecksPath], "ja:kokoro=12\n");
}

TEST(LiveDeck, AWriteQueuedWhileDeckWorkWaitsGoesFirst) {
  DeckBook book;
  DeckSaving d(book);
  d.s.rig.api.deckReplies = {apiOk(kNoBookDeck), apiOk(kCreated)};
  d.save();
  d.s.now += config::kDeckIdleMs;
  ASSERT_TRUE(deckStepDue(d.s, d.s.now, false, false));
  d.s.rig.api.writeReplies = {apiOk("{}")};
  d.s.level(3);  // a level change: input, then a PATCH in its Undo window
  EXPECT_FALSE(deckStepDue(d.s, d.s.now + config::kDeckIdleMs, false, false));  // the write is queued
  d.s.drain();
  EXPECT_EQ(d.s.rig.api.written.size(), 2u);
  EXPECT_TRUE(d.s.rig.api.decked.empty());
  EXPECT_EQ(d.idle(), 2);
}

TEST(LiveDeck, AStepWaitsWhileARedrawOrInputIsWaiting) {
  DeckBook book;
  DeckSaving d(book);
  d.save();
  d.s.now += config::kDeckIdleMs;
  ASSERT_TRUE(deckStepDue(d.s, d.s.now, false, false));
  d.s.session.redrawAsked();
  EXPECT_FALSE(deckStepDue(d.s, d.s.now + config::kDeckIdleMs, false, false));
  d.s.session.frameShown();
  d.s.input.tap(1, 1, d.s.now);  // queued, not yet handled
  EXPECT_FALSE(deckStepDue(d.s, d.s.now + config::kDeckIdleMs, false, false));
}

TEST(LiveDeck, TheIdleTimeStartsWhenTheCardOpens) {
  DeckBook book;
  book.decks.want("ja:kokoro");  // wanted by an earlier card in the book
  DeckSaving d(book);
  d.s.now += 10 * config::kDeckIdleMs;  // the device has been up a while: the card isn't idle just by opening
  d.s.session.opened(d.s.now);
  EXPECT_FALSE(deckStepDue(d.s, d.s.now, false, false));
  EXPECT_TRUE(deckStepDue(d.s, d.s.now + config::kDeckIdleMs, false, false));
}

TEST(LiveDeck, AStepWaitsForAnIdleCard) {
  DeckBook book;
  DeckSaving d(book);
  d.save();
  d.s.now += config::kDeckIdleMs;
  EXPECT_TRUE(deckStepDue(d.s, d.s.now, false, false));
  EXPECT_FALSE(deckStepDue(d.s, d.s.now, /*rendering=*/true, false));
  d.s.session.redrawAsked();
  EXPECT_FALSE(deckStepDue(d.s, d.s.now, false, false));  // a frame on its way
  d.s.session.frameShown();
  d.s.step(1);  // input: the idle time starts again
  EXPECT_FALSE(deckStepDue(d.s, d.s.now, false, false));
  EXPECT_TRUE(deckStepDue(d.s, d.s.now + config::kDeckIdleMs, false, false));
}

TEST(LiveSource, ASavedWordMetInAnotherBookShowsWhere) {
  // 本 (entry 3, item 77) was saved from another book, recorded here (V2); its item holds the sentence and tags.
  Rig rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze)};
  rig.api.lookupReplies = {apiOk(R"({"word":"本"})")};
  rig.api.itemReplies = {apiOk(R"({"id":77,"notes":"古い本を読む。","sentence_text":null,)"
                               R"("user_tags":[{"id":1,"name":"xteink"},{"id":2,"name":"book:kokoro"}]})")};
  lexipoint::fakes::FakeFiles files;
  lexipoint::BookTagStore titles(files);
  titles.remember("kokoro", "Kokoro");
  LiveSource source(rig.api, rig.tap(0, 2), rig.page);  // tapped 本
  source.setBookTitles(titles);
  while (source.hasWork(0)) source.advance();  // A, B, the item
  ASSERT_EQ(rig.api.items.size(), 1u);
  EXPECT_EQ(rig.api.items[0].method, lexipoint::net::Method::Get);
  EXPECT_EQ(rig.api.items[0].path, "/v1/vocabulary/77");
  const CardWord& hon = source.word(source.startWord());
  ASSERT_TRUE(hon.metBefore);
  EXPECT_EQ(hon.metBefore->text, "古い本を読む。");
  EXPECT_EQ(hon.metBeforeBook, "Kokoro");
  EXPECT_FALSE(source.word(0).metBefore);  // 彼: not saved
}

TEST(LiveSave, ASaveBeforePhaseBWaitsForTheTranslation) {
  Saving s(/*complete=*/false);
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":5}})")};
  s.level(0);
  s.step(-1);  // the card moves on before B
  s.drain();
  // The word on screen first (を), then 読む's lookup before its save: its translation is in the payload.
  EXPECT_EQ(s.rig.api.looked, (std::vector<std::string>{"を", "読む"}));
  ASSERT_EQ(s.rig.api.written.size(), 1u);
  EXPECT_NE(s.rig.api.written[0].body.find(R"("translation":"to read")"), std::string::npos);
}

TEST(LiveSave, AnUndoInsideTheToastsWindowSendsNothing) {
  Saving s;
  s.level(1);
  s.tap(Target::ToastUndo);
  EXPECT_EQ(s.c.state().level, Level::None);
  EXPECT_EQ(s.c.state().toast, "Removed from Lexirise");
  s.drain();
  EXPECT_TRUE(s.rig.api.written.empty());
}

TEST(LiveSave, RemovingAWordThisCardSavedDeletesAndClears) {
  Saving s;
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":901}})"), apiOk("{}")};
  s.level(1);
  s.drain();  // the POST, after the toast's window
  s.tap(Target::RankRow);
  s.tap(Target::Tab, tabCount(Language::Japanese) - 1);
  s.tap(Target::Action, 0);  // ⋯ Undo save
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 3u);  // POST, then DELETE + PATCH clear
  EXPECT_EQ(s.rig.api.written[1].method, lexipoint::net::Method::Delete);
  EXPECT_EQ(s.rig.api.written[1].path, "/v1/vocabulary/901");
  EXPECT_EQ(s.rig.api.written[2].body, R"({"notes":null,"customTranslation":null,"tags":[]})");
}

TEST(LiveSave, RemovingAWordSavedBeforeKeepsWhatTheUserWrote) {
  Saving s;
  s.rig.api.writeReplies = {apiOk("{}")};
  s.step(-1);
  s.step(-1);  // 本: saved as 77 before the card opened
  s.drain();
  s.tap(Target::RankRow);
  s.tap(Target::Tab, tabCount(Language::Japanese) - 1);
  s.tap(Target::Action, 0);  // ⋯ Undo save
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 1u);  // DELETE only: its notes, translation and tags stay
  EXPECT_EQ(s.rig.api.written[0].method, lexipoint::net::Method::Delete);
  EXPECT_EQ(s.rig.api.written[0].path, "/v1/vocabulary/77");
}

TEST(LiveSave, ASavedWordsLevelIsPatchedOnceItsWindowCloses) {
  Saving s;
  s.step(-1);  // を
  s.step(-1);  // 本: saved as 77 at 3 (fresh)
  s.drain();
  ASSERT_EQ(s.c.currentWord().word, "本");
  s.rig.api.writeReplies = {apiOk("{}")};
  s.level(3);
  s.level(0);  // changed its mind within the window: one PATCH, to where it ended
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 1u);
  EXPECT_EQ(s.rig.api.written[0].body, R"({"proficiency":1})");
  EXPECT_EQ(s.rig.api.written[0].path, "/v1/vocabulary/77");
}

TEST(LiveSave, ADoubleTapSendsOnce) {
  Saving s;
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":1}})")};
  EXPECT_EQ(s.level(2).changes.size(), 1u);
  EXPECT_TRUE(s.level(2).changes.empty());  // already fresh
  s.drain();
  EXPECT_EQ(s.rig.api.written.size(), 1u);
}

TEST(LiveSave, AFailedSavePutsTheWordBackAndDropsWhatFollowed) {
  Saving s;
  s.rig.api.writeReplies = {apiFailure(ApiError::NoWifi)};
  s.level(1);
  s.level(3);  // built on the save: dropped with it
  s.drain();
  EXPECT_EQ(s.rig.api.written.size(), 1u);
  EXPECT_EQ(s.c.state().level, Level::None);
  EXPECT_EQ(s.c.state().toast, "Save failed  \xC2\xB7  Retry");
}

TEST(LiveSave, TheLaterActionsDontPretend) {
  Saving s;
  s.tap(Target::RankRow);                                // ▼: the detail view
  s.tap(Target::Tab, tabCount(Language::Japanese) - 1);  // ⋯
  s.tap(Target::Action, 1);                              // Save the sentence as a card: v0.2
  EXPECT_EQ(s.c.state().toast, "Not in this version yet");
  s.drain();
  EXPECT_TRUE(s.rig.api.written.empty());
}

TEST(LiveSession, ASaveThenCloseInOneBatchIsStillSent) {
  Saving s;
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":1}})")};
  const ShownFrame* frame = s.targets.at(s.now);
  for (const Hit& h : frame->hits) {
    if (h.target == Target::Level && h.index == 1) s.input.tap(h.rect.x + 1, h.rect.y + 1, ++s.now);
  }
  s.input.home(++s.now);  // Home on the card: close
  const Outcome o = s.session.handleInput(s.now);
  EXPECT_EQ(o.effect, Effect::Close);
  ASSERT_EQ(o.changes.size(), 1u);
  // The activity's close: only what the queued writes need, then gone.
  while (s.session.hasPendingWrites()) s.session.apply(s.session.fetch(s.now, /*closing=*/true), ++s.now);
  ASSERT_EQ(s.rig.api.written.size(), 1u);
  EXPECT_EQ(s.rig.api.written[0].method, lexipoint::net::Method::Post);
}

TEST(LiveSession, ClosingSendsTheSavesWithoutLookingUpTheWordOnScreen) {
  Saving s;
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":1}})")};
  s.level(1);
  s.step(-1);  // を: not looked up yet
  const size_t looked = s.rig.api.looked.size();
  while (s.session.hasPendingWrites()) s.session.apply(s.session.fetch(s.now, /*closing=*/true), ++s.now);
  EXPECT_EQ(s.rig.api.looked.size(), looked);  // を wasn't looked up on the way out
  EXPECT_EQ(s.rig.api.written.size(), 1u);
}

TEST(LiveSession, AFailureAfterTheCardMovedOnStillSaysSo) {
  Saving s(/*complete=*/false);
  s.rig.api.writeReplies = {apiFailure(ApiError::NoWifi)};
  s.level(1);
  s.step(-1);  // を
  s.drain();
  EXPECT_EQ(s.c.state().toast, "Save failed  \xC2\xB7  Retry");
  s.step(+1);  // back to 読む: put back
  EXPECT_EQ(s.c.state().level, Level::None);
}

TEST(LiveSession, ATapDuringTheFirstRefreshOnAMidSentenceWordStillCounts) {
  Rig rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze)};
  LiveSource source(rig.api, rig.tap(1, 0), rig.page);  // 読んだ: word 4, not 0
  CardController c(source, ReadingMode::Kana);
  ShownTargets targets;
  PendingInput input;
  CardSession session(c, targets, input, &source);
  c.open(0);
  targets.drawing(composeFrame(c, kMetrics).card.hits, c.steps(), c.state().view);  // phase 0
  targets.shown(10);
  session.apply(session.fetch(20), 20);  // A arrives; its frame starts refreshing
  targets.drawing(composeFrame(c, kMetrics).card.hits, c.steps(), c.state().view);
  input.tap(1, 1, 30);  // the page above the card, on the phase-0 frame: close
  EXPECT_EQ(session.handleInput(30).effect, Effect::Close);
}

TEST(LiveSession, NotFoundAndUnavailableEndTheCard) {
  Rig rig;
  rig.api.analyzeReplies = {apiFailure(ApiError::NoWifi)};
  LiveSource source(rig.api, rig.tap(0, 0), rig.page);
  CardController c(source, ReadingMode::Kana);
  ShownTargets targets;
  PendingInput input;
  CardSession session(c, targets, input, &source);
  c.open(0);
  const CardSession::Answer a = session.apply(session.fetch(1), 1);
  ASSERT_TRUE(a.ended);
  EXPECT_EQ(a.ended->kind, LiveOutcome::Kind::Unavailable);
  EXPECT_EQ(a.ended->error, ApiError::NoWifi);
}

TEST(LiveSession, TheSameWordTwiceInASentenceIsOneEntry) {
  Rig rig;
  TextLine line;
  line.tokens = {"本", "と", "本", "。"};
  line.startsParagraph = true;
  rig.model.lines = {line};
  rig.page.lines = {{100, {{"本", 20, 26}, {"と", 46, 26}, {"本", 72, 26}, {"。", 98, 26}}}};
  rig.api.analyzeReplies = {
      apiOk(R"({"occurrences":[{"word":"本","isWordLike":true,"charStart":0,"charEnd":1,"entryId":3,"lemmaEntryId":3},)"
            R"({"word":"と","isWordLike":true,"charStart":1,"charEnd":2,"entryId":8},)"
            R"({"word":"本","isWordLike":true,"charStart":2,"charEnd":3,"entryId":3,"lemmaEntryId":3}]})")};
  rig.api.lookupReplies = {apiOk(R"({"word":"本","translations":[{"translation":"book"}]})")};
  rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":5}})"), apiOk("{}")};
  LiveSource source(rig.api, rig.tap(0, 0), rig.page);
  CardController c(source, ReadingMode::Kana);
  ShownTargets targets;
  PendingInput input;
  CardSession session(c, targets, input, &source);
  c.open(0);
  session.apply(session.fetch(1), 1);
  session.apply(session.fetch(2), 2);
  const Hit fresh{Target::Level, 2, {}};
  for (const LevelChange& ch : c.tap(&fresh, 3).changes) source.queue(ch);
  while (session.hasWork(10000)) session.apply(session.fetch(10000), 10000);
  c.step(+1, 11000);
  c.step(+1, 12000);  // the second 本
  EXPECT_EQ(c.state().level, Level::Fresh);
  const Hit known{Target::Level, 3, {}};
  for (const LevelChange& ch : c.tap(&known, 20000).changes) source.queue(ch);
  while (session.hasWork(30000)) session.apply(session.fetch(30000), 30000);
  ASSERT_EQ(rig.api.written.size(), 2u);
  EXPECT_EQ(rig.api.written[1].method, lexipoint::net::Method::Patch);  // not a second POST
  EXPECT_EQ(rig.api.written[1].path, "/v1/vocabulary/5");
}

TEST(LiveSession, UndoThenSaveAgainIsAFullSave) {
  Saving s;
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":901}})"), apiOk("{}"), apiOk("{}"),
                            apiOk(R"({"result":{"savedExpressionId":901}})")};
  s.level(0);
  s.drain();  // POST
  s.tap(Target::RankRow);
  s.tap(Target::Tab, tabCount(Language::Japanese) - 1);
  s.tap(Target::Action, 0);  // removed: DELETE + clear
  s.drain();
  s.tap(Target::RankRow);  // back to the card
  s.level(1);
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 4u);
  EXPECT_EQ(s.rig.api.written[3].method, lexipoint::net::Method::Post);  // tags, notes, translation again
  EXPECT_NE(s.rig.api.written[3].body.find(R"("tags":["xteink"])"), std::string::npos);
}

TEST(LiveSession, AFailedClearStillCountsAsRemoved) {
  Saving s;
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":901}})"), apiOk("{}"),
                            apiFailure(ApiError::Timeout)};
  s.level(0);
  s.drain();
  s.tap(Target::RankRow);
  s.tap(Target::Tab, tabCount(Language::Japanese) - 1);
  s.tap(Target::Action, 0);
  s.now += lexipoint::config::kToastMs;
  const CardSession::Answer removal = s.fetchOne();  // DELETE ok, the clear times out
  EXPECT_TRUE(removal.clearFailed);
  EXPECT_FALSE(removal.writeFailed);
  EXPECT_EQ(s.c.state().level, Level::None);  // not put back
}

TEST(LiveSession, NoCallWhileAFrameIsOnItsWay) {
  Saving s(/*complete=*/false);  // phase B still to fetch
  EXPECT_TRUE(s.session.shouldFetch(s.now, /*rendering=*/false));
  EXPECT_FALSE(s.session.shouldFetch(s.now, /*rendering=*/true));  // a render holds the lock
  s.session.redrawAsked();                                         // a step's A, a toast: asked for...
  EXPECT_FALSE(s.session.shouldFetch(s.now, false));
  s.session.frameShown();  // ...and on screen: now the call
  EXPECT_TRUE(s.session.shouldFetch(s.now, false));
}

TEST(LiveSession, ASaveWaitsOutItsUndoWindow) {
  Saving s;
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":1}})")};
  s.level(1);
  EXPECT_FALSE(s.session.hasWork(s.now));  // the toast's Undo is still live: nothing to send yet
  EXPECT_TRUE(s.session.hasWork(s.now + lexipoint::config::kToastMs));
}

TEST(LiveSession, ASaveAfterAFailedLookupRetriesItOnce) {
  Saving s(/*complete=*/false);
  s.rig.api.lookupReplies = {apiFailure(ApiError::RateLimited), apiOk(kLookupYomu)};
  s.fetchOne();  // B fails: the word without its meaning
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":1}})")};
  s.level(1);
  EXPECT_FALSE(s.session.hasWork(s.now));  // not even the lookup inside the Undo window
  s.now += lexipoint::config::kToastMs;
  const CardSession::Answer retry = s.fetchOne();
  EXPECT_TRUE(retry.redraw);  // the meaning it brings is drawn
  EXPECT_EQ(s.c.currentWord().senses, std::vector<std::string>{"to read"});
  s.drain();
  EXPECT_EQ(s.rig.api.looked.size(), 2u);  // once more, for the save
  ASSERT_EQ(s.rig.api.written.size(), 1u);
  EXPECT_NE(s.rig.api.written[0].body.find(R"("translation":"to read")"), std::string::npos);
}

TEST(LiveSession, ARetriedLookupThatChangesNothingOnScreenIsntDrawnAgain) {
  // What the card draws is the word and the controller's state: a retry that fails alike changes neither.
  Saving s(/*complete=*/false);
  s.rig.api.lookupReplies = {apiFailure(ApiError::RateLimited), apiFailure(ApiError::RateLimited)};
  s.fetchOne();  // B fails
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":1}})")};
  s.level(1);
  s.now += lexipoint::config::kToastMs;
  const CardSession::Answer retry = s.fetchOne();  // the lookup again, for the save, on screen
  EXPECT_EQ(s.rig.api.looked.size(), 2u);
  EXPECT_FALSE(retry.redraw);
}

TEST(LiveSession, ALookupForAWordOffScreenIsntDrawnAndNoFlagLingers) {
  Saving s(/*complete=*/false);
  s.rig.api.lookupReplies = {apiFailure(ApiError::RateLimited), apiOk(R"({"word":"を"})"), apiOk(kLookupYomu)};
  s.fetchOne();  // 読む's B fails
  s.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":1}})")};
  s.level(1);
  s.step(-1);  // を
  s.now += lexipoint::config::kToastMs;
  const CardSession::Answer onScreen = s.fetchOne();  // を's lookup: its meaning, drawn
  ASSERT_EQ(s.rig.api.looked, (std::vector<std::string>{"読む", "を"}));
  EXPECT_TRUE(onScreen.redraw);
  const CardSession::Answer offScreen = s.fetchOne();  // 読む's again, for the save: off screen
  ASSERT_EQ(s.rig.api.looked.size(), 3u);
  EXPECT_FALSE(offScreen.redraw);
  const CardSession::Answer write = s.fetchOne();  // the save: nothing new on screen, no flag left over
  ASSERT_EQ(s.rig.api.written.size(), 1u);
  EXPECT_FALSE(write.redraw);
}

TEST(LiveSession, AFailureDropsTheChangesOnTheSameWordElsewhereInTheSentence) {
  Rig rig;
  TextLine line;
  line.tokens = {"本", "と", "本", "。"};
  line.startsParagraph = true;
  rig.model.lines = {line};
  rig.page.lines = {{100, {{"本", 20, 26}, {"と", 46, 26}, {"本", 72, 26}, {"。", 98, 26}}}};
  rig.api.analyzeReplies = {
      apiOk(R"({"occurrences":[{"word":"本","isWordLike":true,"charStart":0,"charEnd":1,"entryId":3,"lemmaEntryId":3},)"
            R"({"word":"と","isWordLike":true,"charStart":1,"charEnd":2,"entryId":8},)"
            R"({"word":"本","isWordLike":true,"charStart":2,"charEnd":3,"entryId":3,"lemmaEntryId":3}]})")};
  rig.api.lookupReplies = {apiOk(R"({"word":"本","translations":[{"translation":"book"}]})")};
  rig.api.writeReplies = {apiFailure(ApiError::NoWifi)};
  LiveSource source(rig.api, rig.tap(0, 0), rig.page);
  CardController c(source, ReadingMode::Kana);
  ShownTargets targets;
  PendingInput input;
  CardSession session(c, targets, input, &source);
  c.open(0);
  session.apply(session.fetch(1), 1);
  const Hit t{Target::Level, 0, {}};
  for (const LevelChange& ch : c.tap(&t, 2).changes) source.queue(ch);  // before its phase B
  c.step(+1, 3);
  c.step(+1, 4);  // the second 本: already T on the card
  const Hit k{Target::Level, 3, {}};
  for (const LevelChange& ch : c.tap(&k, 5).changes) source.queue(ch);  // merges into the queued save
  while (session.hasWork(10000)) session.apply(session.fetch(10000), 10000);
  EXPECT_EQ(rig.api.written.size(), 1u);  // one POST, failed: nothing more goes
  EXPECT_EQ(c.state().level, Level::None);
}

TEST(LiveSession, AnUndoInsideTheWindowAfterAFailedLookupCostsNothing) {
  Saving s(/*complete=*/false);
  s.rig.api.lookupReplies = {apiFailure(ApiError::Timeout)};
  s.fetchOne();  // B fails
  const size_t looked = s.rig.api.looked.size();
  s.level(0);
  s.tap(Target::ToastUndo);
  s.drain();
  EXPECT_EQ(s.rig.api.looked.size(), looked);  // no retry
  EXPECT_TRUE(s.rig.api.written.empty());
}

TEST(LiveSession, TheUsersOwnItemRemovedThenSetAgainIsAPatch) {
  Saving s;
  s.rig.api.writeReplies = {apiOk("{}")};
  s.step(-1);
  s.step(-1);  // 本: the user's own item (77)
  s.drain();
  s.tap(Target::RankRow);
  s.tap(Target::Tab, tabCount(Language::Japanese) - 1);
  s.tap(Target::Action, 0);  // ⋯ Undo save: DELETE only
  s.drain();
  s.tap(Target::RankRow);
  s.level(3);  // changed its mind: K
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 2u);
  EXPECT_EQ(s.rig.api.written[1].method, lexipoint::net::Method::Patch);  // never a POST over their notes
  EXPECT_EQ(s.rig.api.written[1].path, "/v1/vocabulary/77");
  EXPECT_EQ(s.rig.api.written[1].body, R"({"proficiency":4})");
}

TEST(LiveSession, ClosingOfflineWithASaveWhoseLookupFailedStillEnds) {
  Saving s(/*complete=*/false);
  s.rig.api.lookupReplies = {apiFailure(ApiError::NoWifi)};
  s.rig.api.writeReplies = {apiFailure(ApiError::NoWifi)};
  s.fetchOne();  // B fails
  s.level(1);
  int calls = 0;
  while (s.session.hasPendingWrites() && calls++ < 10) {
    s.session.apply(s.session.fetch(s.now, /*closing=*/true), ++s.now);
  }
  EXPECT_FALSE(s.session.hasPendingWrites());
  EXPECT_EQ(calls, 2);  // the lookup's one retry, then the POST (failed): done
  EXPECT_EQ(s.rig.api.written.size(), 1u);
}

TEST(LiveSession, WhatWordSelectDoesAfterTheCard) {
  using lexipoint::card::afterCard;
  using lexipoint::card::AfterCard;
  using lexipoint::card::PagePoint;
  using Kind = LiveOutcome::Kind;
  for (const bool starDict : {false, true}) {
    EXPECT_EQ(afterCard(LiveOutcome{Kind::Closed}, starDict), AfterCard::Closed);
    EXPECT_EQ(afterCard(LiveOutcome{Kind::NotFound}, starDict), AfterCard::NotFound);  // a Lexirise miss is final
    LiveOutcome unsent{Kind::Closed};
    unsent.unsentSaves = 1;
    unsent.lookUpAt = PagePoint{10, 20};
    EXPECT_EQ(afterCard(unsent, starDict), AfterCard::UnsentSave);  // never fails silently, said first
  }
  EXPECT_EQ(afterCard(LiveOutcome{Kind::Unavailable}, true), AfterCard::RunStarDict);
  EXPECT_EQ(afterCard(LiveOutcome{Kind::Unavailable}, false), AfterCard::NoDictionary);
}

TEST(LiveSession, QueuedInputGoesBeforeANetworkCall) {
  Saving s(/*complete=*/false);  // phase B to fetch
  s.input.step(-1, s.now);       // read while a render held the lock
  EXPECT_FALSE(s.session.shouldFetch(s.now, /*rendering=*/false));
  s.session.handleInput(s.now);
  EXPECT_TRUE(s.session.shouldFetch(s.now, false));
}

TEST(LiveSession, AnUnreadableResponseIsLoggedByItsStart) {
  Rig rig;
  rig.api.analyzeReplies = {apiOk(std::string("{\"occurrences\":") + std::string(300, 'x'))};
  LiveSource source(rig.api, rig.tap(0, 0), rig.page);
  CardController c(source, ReadingMode::Kana);
  ShownTargets targets;
  PendingInput input;
  CardSession session(c, targets, input, &source);
  c.open(0);
  const CardSession::Answer a = session.apply(session.fetch(1), 1);
  ASSERT_TRUE(a.ended);
  EXPECT_EQ(a.ended->error, ApiError::Malformed);
  EXPECT_EQ(a.unreadable.size(), lexipoint::config::kLoggedBodyBytes);
  EXPECT_EQ(a.unreadable.rfind("{\"occurrences\":", 0), 0u);
}

namespace {

lexipoint::api::ApiResponse refused(const ApiError error, const uint32_t retryAfterS = 0) {
  lexipoint::api::ApiResponse r = apiFailure(error);
  r.retryAfterS = retryAfterS;
  return r;
}

bool drawn(const DisplayList& list, const std::string& text) {
  for (const Command& c : list.commands) {
    if (c.kind == Command::Kind::Text && c.text == text) return true;
  }
  return false;
}

}  // namespace

TEST(LiveErrors, ASaveThatFailsOffersRetryWhichSendsItAgain) {
  Saving s;
  s.rig.api.writeReplies = {apiFailure(ApiError::Timeout), apiOk(R"({"result":{"savedExpressionId":5}})")};
  s.level(1);
  s.drain();
  EXPECT_EQ(s.c.state().toast, "Save failed  \xC2\xB7  Retry");
  EXPECT_TRUE(s.c.state().toastUndo);  // tappable
  EXPECT_EQ(s.c.state().level, Level::None);
  s.tap(Target::ToastUndo);  // Retry
  EXPECT_EQ(s.c.state().level, Level::Learning);
  EXPECT_EQ(s.c.state().toast, "Trying again\xE2\x80\xA6");
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 2u);
  EXPECT_EQ(s.rig.api.written[1].method, lexipoint::net::Method::Post);
  EXPECT_EQ(s.c.state().level, Level::Learning);
}

TEST(LiveErrors, ARejectedKeySaysSoWithoutARetry) {
  Saving s;
  s.rig.api.writeReplies = {refused(ApiError::Unauthorized)};
  s.level(1);
  s.drain();
  EXPECT_EQ(s.c.state().toast, "Lexirise key rejected");
  EXPECT_FALSE(s.c.state().toastUndo);
  EXPECT_EQ(s.c.state().level, Level::None);
}

TEST(LiveErrors, ARateLimitSaysHowLongAndOffersRetry) {
  Saving s;
  s.rig.api.writeReplies = {refused(ApiError::RateLimited, 42)};
  s.level(1);
  s.drain();
  EXPECT_EQ(s.c.state().toast, "Rate limited: try in 42 s  \xC2\xB7  Retry");
  EXPECT_TRUE(s.c.state().toastUndo);
}

TEST(LiveErrors, PhaseBOfflineSaysSoInTheMeaningRow) {
  Saving s(/*complete=*/false);
  s.rig.api.lookupReplies = {apiFailure(ApiError::NoWifi)};
  s.fetchOne();
  EXPECT_EQ(s.c.state().phase, Phase::Unanswered);
  EXPECT_TRUE(drawn(composeFrame(s.c, kMetrics).card, "offline"));
}

TEST(LiveErrors, TheMeaningRowSaysWhyPhaseBBroughtNone) {
  for (const auto& [reply, text] : std::vector<std::pair<lexipoint::api::ApiResponse, std::string>>{
           {apiFailure(ApiError::Timeout), "offline"},
           {refused(ApiError::RateLimited, 30), "Lexirise: rate limited"},
           {refused(ApiError::Unauthorized), "Lexirise key rejected"},
           {apiOk("{"), "meaning unavailable"}}) {
    Saving s(/*complete=*/false);
    s.rig.api.lookupReplies = {reply};
    s.fetchOne();
    EXPECT_EQ(s.c.state().phase, Phase::Unanswered) << text;
    EXPECT_TRUE(drawn(composeFrame(s.c, kMetrics).card, text)) << text;
  }
}

TEST(LiveErrors, RetryLooksTheWordUpAgainSoTheSaveHasItsTranslation) {
  Saving s(/*complete=*/false);
  s.rig.api.lookupReplies = {apiFailure(ApiError::Timeout), apiFailure(ApiError::Timeout), apiOk(kLookupYomu)};
  s.rig.api.writeReplies = {apiFailure(ApiError::Timeout), apiOk(R"({"result":{"savedExpressionId":5}})")};
  s.fetchOne();  // B times out
  s.level(1);
  s.drain();  // the save's retry of the lookup times out too, and so does the POST
  ASSERT_EQ(s.c.state().toast, "Save failed  \xC2\xB7  Retry");
  s.tap(Target::ToastUndo);  // Retry: WiFi is back
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 2u);
  EXPECT_NE(s.rig.api.written[1].body.find(R"("translation":"to read")"), std::string::npos);
}

TEST(LiveErrors, AFailureStaysUpLongerThanAnUndo) {
  Saving s;
  s.rig.api.writeReplies = {apiFailure(ApiError::Timeout)};
  s.level(1);
  s.drain();
  ASSERT_TRUE(s.c.state().toastUndo);
  s.c.tick(s.now + lexipoint::config::kToastMs);
  EXPECT_FALSE(s.c.state().toast.empty());  // Retry is still there
  s.c.tick(s.now + lexipoint::config::kFailureToastMs);
  EXPECT_TRUE(s.c.state().toast.empty());
}

TEST(LiveErrors, RetryResendsEveryFailureItCovers) {
  Saving s;
  s.rig.api.lookupReplies = {apiOk(kLookupYomu),
                             apiOk(R"({"word":"を","translations":[{"translation":"object marker"}]})")};
  s.rig.api.writeReplies = {refused(ApiError::RateLimited, 30), refused(ApiError::RateLimited, 29),
                            apiOk(R"({"result":{"savedExpressionId":1}})"),
                            apiOk(R"({"result":{"savedExpressionId":2}})")};
  s.level(1);  // 読む
  s.step(-1);
  s.fetchOne();  // を's meaning
  s.level(2);    // を
  s.drain();     // both refused: one toast, both on it
  EXPECT_EQ(s.c.state().toast, "Rate limited: try in 29 s  \xC2\xB7  Retry");
  s.tap(Target::ToastUndo);
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 4u);  // both sent again
  EXPECT_EQ(s.rig.api.written[2].method, lexipoint::net::Method::Post);
  EXPECT_EQ(s.rig.api.written[3].method, lexipoint::net::Method::Post);
}

TEST(LiveErrors, RetryDuringARateLimitWaitsItOut) {
  Saving s;
  s.rig.api.writeReplies = {refused(ApiError::RateLimited, 30), apiOk(R"({"result":{"savedExpressionId":1}})")};
  s.level(1);
  s.drain();  // refused: "try in 30 s · Retry"
  const unsigned long failedAt = s.now;
  s.tap(Target::ToastUndo);                // Retry at once
  EXPECT_FALSE(s.session.hasWork(s.now));  // not sent into the back-off
  EXPECT_TRUE(s.session.hasWork(failedAt + 30'000));
  s.now = failedAt + 30'000;
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 2u);
  EXPECT_EQ(s.c.state().level, Level::Learning);
}

TEST(LiveErrors, ARetryWaitingOutARateLimitWhenTheCardClosesIsReported) {
  Saving s;
  s.rig.api.writeReplies = {refused(ApiError::RateLimited, 60), refused(ApiError::RateLimited, 58)};
  s.level(1);
  s.drain();
  s.tap(Target::ToastUndo);               // Retry: waits out the 60 s
  while (s.session.hasPendingWrites()) {  // the card closes (the activity's flushWrites)
    s.session.applyClosing(s.session.fetch(s.now, /*closing=*/true), ++s.now);
  }
  EXPECT_EQ(s.session.unsentSaves(), 1);  // → LiveOutcome::unsentSaves: word select says "Lexirise: rate limited"
  EXPECT_EQ(lexipoint::lookup::noticeForUnsentSave(s.session.unsentError()), lexipoint::lookup::Notice::RateLimited);
}

TEST(LiveErrors, OneRetryWaitsOutTheLatestBackOffForEveryFailure) {
  Saving s;
  s.rig.api.lookupReplies = {apiOk(kLookupYomu),
                             apiOk(R"({"word":"を","translations":[{"translation":"object marker"}]})")};
  s.rig.api.writeReplies = {apiFailure(ApiError::Timeout), refused(ApiError::RateLimited, 60),
                            apiOk(R"({"result":{"savedExpressionId":1}})"),
                            apiOk(R"({"result":{"savedExpressionId":2}})")};
  s.level(1);  // 読む: times out
  s.step(-1);
  s.fetchOne();
  s.level(2);  // を: rate limited
  s.drain();
  const unsigned long failedAt = s.now;
  s.tap(Target::ToastUndo);
  EXPECT_FALSE(s.session.hasWork(s.now));  // not even the timed-out one: it'd be refused into the back-off
  s.now = failedAt + 60'000;
  s.drain();
  EXPECT_EQ(s.rig.api.written.size(), 4u);
}

TEST(LiveErrors, AStepKeepsAFailureAndItsRetry) {
  Saving s;
  s.rig.api.writeReplies = {apiFailure(ApiError::Timeout), apiOk(R"({"result":{"savedExpressionId":1}})")};
  s.level(1);
  s.drain();
  s.step(-1);  // the user moved on while the save was timing out
  EXPECT_EQ(s.c.state().toast, "Save failed  \xC2\xB7  Retry");
  EXPECT_TRUE(s.c.state().toastUndo);
  s.tap(Target::ToastUndo);  // Retry still works, for the word it was about
  s.drain();
  ASSERT_EQ(s.rig.api.written.size(), 2u);
  EXPECT_NE(s.rig.api.written[1].body.find(R"("text":"読む")"), std::string::npos);
}

TEST(LiveSession, WhereWordSelectGoesAsAnAnswerCloses) {
  using lexipoint::card::CloseStep;
  using lexipoint::card::closeStep;
  using lexipoint::card::PagePoint;
  for (const bool touch : {false, true}) {
    const CloseStep byEntry = touch ? CloseStep::BackToReader : CloseStep::Redraw;  // popup-ui.md §3
    EXPECT_EQ(closeStep(std::nullopt, false, touch), byEntry);              // ✕, Home, Back, StarDict's definition
    EXPECT_EQ(closeStep(PagePoint{1, 2}, true, touch), CloseStep::LookUp);  // a long-press on another word
    EXPECT_EQ(closeStep(PagePoint{1, 2}, false, touch), byEntry);           // ... on no word: as any close
  }
}

TEST(LiveSession, WhatWordSelectDoesOnceANoticeIsRead) {
  using lexipoint::card::AfterNotice;
  using lexipoint::card::afterNotice;
  using lexipoint::card::AfterPopup;
  using lexipoint::card::PagePoint;
  for (const bool touch : {false, true}) {
    // What was waiting for the notice comes first, even when a long-press opened word select.
    EXPECT_EQ(afterNotice(AfterPopup::runStarDict(), touch), AfterNotice::RunStarDict);
    const AfterPopup close = AfterPopup::finishClose(PagePoint{10, 20});
    EXPECT_EQ(afterNotice(close, touch), AfterNotice::FinishClose);  // an unsent save's notice
    ASSERT_TRUE(close.lookUpAt.has_value());
    EXPECT_EQ(close.lookUpAt->y, 20);
  }
  EXPECT_EQ(afterNotice(std::nullopt, true), AfterNotice::BackToReader);  // "Not found" after a long-press
  EXPECT_EQ(afterNotice(std::nullopt, false), AfterNotice::Redraw);       // ...after the menu's Look Up
}

// P9, lookup-flow.md §6: past a sentence's last word the side buttons go on into the page's next sentence.
namespace {

constexpr const char* kAnalyzeRain =
    R"({"occurrences":[)"
    R"({"word":"雨","isWordLike":true,"transliteration":"ame","charStart":0,"charEnd":1,"entryId":11},)"
    R"({"word":"が","isWordLike":true,"transliteration":"ga","charStart":1,"charEnd":2,"entryId":12},)"
    R"({"word":"降る","isWordLike":true,"transliteration":"furu","charStart":2,"charEnd":4,"entryId":13},)"
    R"({"word":"。","isWordLike":false,"charStart":4,"charEnd":5}]})";

// Two sentences on the page: 彼は本を読んだ。 / 雨が降る。
struct TwoSentences {
  FakeApi api;
  PageModel model;
  ReaderPage page;
  TwoSentences() {
    model.lines = {{{"彼", "は", "本", "を", "読んだ", "。"}, true}, {{"雨", "が", "降る", "。"}, true}};
    page.lines = {
        {100, {{"彼", 20, 26}, {"は", 46, 26}, {"本", 72, 26}, {"を", 98, 26}, {"読んだ", 124, 78}, {"。", 202, 26}}},
        {140, {{"雨", 20, 26}, {"が", 46, 26}, {"降る", 72, 52}, {"。", 124, 26}}}};
  }
  TapContext tapped(const size_t token) const {
    TapContext t;
    t.sentence = buildSentence(model, {0, token}, Script::Japanese);
    t.language.language = Language::Japanese;
    return t;
  }
  LiveSource::NextSentence next() const {
    return [this](const TapContext& current) {
      TapContext t;
      t.sentence = lexipoint::text::buildSentenceAfter(model, *current.sentence, Script::Japanese);
      if (t.sentence) t.language.language = Language::Japanese;
      return t;
    };
  }
};

// Opens on 読んだ (the tapped sentence's last word) with its phases A and B done.
void openOnTheLastWord(LiveSource& source, CardController& c) {
  c.open(0);
  source.advance();
  c.sourceChanged(0);
  source.advance();
  c.sourceChanged(0);
}

}  // namespace

TEST(LiveSaving, ATapOnTheWordsOwnHighlightDoesNothingAndElsewhereLooksUp) {
  // P10: a tap on the page looks up the word there, except the card's own word (already on the card): it
  // would close and reopen the same card, with another request.
  Saving s;
  const Rect word = composeFrame(s.c, kMetrics).scene.wordOnPage;
  ASSERT_GT(word.w, 0);
  s.input.tap(word.x + word.w / 2, word.y + word.h / 2, ++s.now);
  EXPECT_NE(s.session.handleInput(s.now).effect, Effect::Close);
  s.show();
  s.input.tap(word.right() + 60, word.y + word.h / 2, ++s.now);  // further along the line
  const Outcome o = s.session.handleInput(s.now);
  EXPECT_EQ(o.effect, Effect::Close);
  ASSERT_TRUE(o.lookUpAt.has_value());
  EXPECT_EQ(o.lookUpAt->x, word.right() + 60);
}

TEST(LiveSaving, TheCardsOwnWordTakesNoSwipeAndKeepsAWaitingStep) {
  // P10 R4: the word's highlight is its own target, not the card: a swipe that starts on it isn't the card's.
  Saving s;
  const Rect word = composeFrame(s.c, kMetrics).scene.wordOnPage;
  s.input.swipe(Swipe::Up, word.x + word.w / 2, word.y + word.h / 2, ++s.now);
  s.session.handleInput(s.now);
  EXPECT_EQ(s.c.state().view, View::Card);
  s.show();
  s.input.longPress(word.x + word.w / 2, word.y + word.h / 2, ++s.now);  // nor a long-press
  EXPECT_NE(s.session.handleInput(s.now).effect, Effect::Close);
}

TEST(CardOwnWord, OnlyWhereTheReadersPageIsShownAndBehindTheCard) {
  const auto ownWordHits = [](const DisplayList& card) {
    int n = 0;
    for (const Hit& h : card.hits) n += h.target == Target::OwnWord;
    return n;
  };
  Saving s;
  EXPECT_GT(ownWordHits(composeFrame(s.c, kMetrics).card), 0);
  // A landscape book (its page isn't drawn under the card): no hit where the reader never drew the word.
  EXPECT_EQ(ownWordHits(composeFrame(s.c, kMetrics, /*pageVisible=*/false).card), 0);
  s.tap(Target::RankRow);  // the detail view covers the page
  ASSERT_EQ(s.c.state().view, View::Expanded);
  EXPECT_EQ(ownWordHits(composeFrame(s.c, kMetrics).card), 0);

  // The bench's low word sits under the card: where they overlap, the card takes the touch.
  BenchSource low(benchJapanese(), /*low=*/true);
  CardController c(low, ReadingMode::Kana);
  c.open(0);
  c.tick(lexipoint::config::kBenchPhaseBMs);
  const Frame frame = composeFrame(c, kMetrics);
  ASSERT_FALSE(frame.scene.wordPieces.empty());
  const Rect piece = frame.scene.wordPieces.front();
  const Hit* hit = hitAt(frame.card.hits, piece.x + piece.w / 2, piece.y + piece.h / 2);
  ASSERT_NE(hit, nullptr);
  EXPECT_NE(hit->target, Target::OwnWord);
}

TEST(LiveSteps, PastTheLastWordIntoTheNextSentence) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu), apiOk(R"({"word":"雨"})")};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  ASSERT_EQ(c.word(), 4);

  const int steps = c.steps();
  EXPECT_FALSE(c.step(+1, 1000));  // 雨が降る。 starts loading; the card stays on 読んだ meanwhile
  EXPECT_TRUE(c.awaitingNext());
  EXPECT_TRUE(source.extending());
  EXPECT_EQ(c.word(), 4);
  EXPECT_EQ(c.steps(), steps);
  EXPECT_EQ(c.state().phase, Phase::Complete);  // never a detail view of nothing
  EXPECT_FALSE(c.step(+1, 1100));               // already on its way
  EXPECT_EQ(source.advance(), LiveSource::Advance::Changed);
  EXPECT_TRUE(c.sourceChanged(1200));  // it came: the card is on its first word
  ASSERT_EQ(rig.api.analyzed.size(), 2u);
  EXPECT_EQ(rig.api.analyzed[1], "雨が降る。");
  EXPECT_FALSE(c.awaitingNext());
  EXPECT_EQ(c.word(), 5);
  EXPECT_EQ(c.steps(), steps + 1);  // a touch on the old frame means nothing now
  EXPECT_EQ(c.currentWord().word, "雨");
  EXPECT_EQ(c.state().phase, Phase::Analyzed);
  EXPECT_EQ(source.scene(5, false, kMetrics, 0).sentence.text, "雨が降る。");  // the Context tab follows

  ASSERT_TRUE(c.step(-1, 2000));  // back into the first sentence: nothing reloaded
  EXPECT_EQ(c.currentWord().word, "読む");
  ASSERT_TRUE(c.step(+1, 3000));  // and on again, straight there
  ASSERT_TRUE(c.step(+1, 3100));
  ASSERT_TRUE(c.step(+1, 3200));
  EXPECT_EQ(c.currentWord().word, "降る");
  EXPECT_FALSE(c.step(+1, 3300));  // the page ends
  EXPECT_FALSE(c.awaitingNext());
  EXPECT_EQ(rig.api.analyzed.size(), 2u);
}

TEST(LiveSteps, SteppingBackWhileItLoadsStaysPut) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000);
  ASSERT_TRUE(c.step(-1, 1100));  // changed mind: back to を
  EXPECT_FALSE(c.awaitingNext());
  EXPECT_EQ(source.fetch(1150).kind, LiveSource::Fetched::Kind::Entry);  // を's own lookup comes first
  while (source.extending()) source.advance(1150);                       // then the next sentence arrives anyway
  c.sourceChanged(1200);
  EXPECT_EQ(c.word(), 3);  // no jump
  ASSERT_TRUE(c.step(+1, 1300));
  ASSERT_TRUE(c.step(+1, 1400));  // its words are there now: straight on
  EXPECT_EQ(c.currentWord().word, "雨");
}

TEST(LiveSteps, AWordAlreadySavedCarriesItsLevelIntoTheNextSentence) {
  TwoSentences rig;
  // The next sentence has 読んだ (entry 6) again; Lexirise hasn't had the save yet (its Undo window).
  constexpr const char* kAgain =
      R"({"occurrences":[)"
      R"({"word":"雨","isWordLike":true,"charStart":0,"charEnd":1,"entryId":11},)"
      R"({"word":"読んだ","lemma":"読む","isWordLike":true,"charStart":1,"charEnd":4,"entryId":5,"lemmaEntryId":6}]})";
  rig.model.lines[1].tokens = {"雨", "読んだ", "。"};
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAgain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  const Hit fresh{Target::Level, 2, {}};
  for (const LevelChange& ch : c.tap(&fresh, 1000).changes) source.queue(ch);  // readyAt: +2 s
  c.step(+1, 1100);
  source.advance(1200);  // the analysis first: the save is still in its window
  c.sourceChanged(1200);
  ASSERT_EQ(c.currentWord().word, "雨");
  ASSERT_TRUE(c.step(+1, 1300));
  EXPECT_EQ(c.state().level, Level::Fresh);  // the level the user set, not Lexirise's "not saved"
  EXPECT_EQ(source.sameWord(6), (std::vector<int>{4, 6}));
}

TEST(LiveSteps, AWordSavedHereIsMetBeforeInTheNextSentence) {
  TwoSentences rig;
  // The next sentence has 読んだ again; Lexirise's answer for it doesn't carry the save's sentence.
  constexpr const char* kAgain =
      R"({"occurrences":[)"
      R"({"word":"雨","isWordLike":true,"charStart":0,"charEnd":1,"entryId":11},)"
      R"({"word":"読んだ","lemma":"読む","isWordLike":true,"charStart":1,"charEnd":4,"entryId":5,"lemmaEntryId":6}],)"
      R"("stateByEntryId":{"6":{"saved_expression_id":901,"proficiency":3}}})";
  rig.model.lines[1].tokens = {"雨", "読んだ", "。"};
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAgain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":901}})")};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {"xteink", "book:kokoro"}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  const Hit fresh{Target::Level, 2, {}};
  for (const LevelChange& ch : c.tap(&fresh, 1000).changes) source.queue(ch);
  source.advance(1000 + config::kToastMs);  // the save goes (POST)
  ASSERT_EQ(rig.api.written.size(), 1u);
  EXPECT_FALSE(source.word(4).metBefore);  // saved from this very sentence
  c.step(+1, 5000);
  while (source.extending()) source.advance(6000);
  c.sourceChanged(6000);
  ASSERT_EQ(source.wordCount(), 7);
  const CardWord& again = source.word(6);
  ASSERT_TRUE(again.metBefore);  // the sentence the save sent, though the answer didn't carry it
  EXPECT_EQ(again.metBefore->text, "彼は本を読んだ。");
  ASSERT_TRUE(c.step(+1, 7000));  // onto the copy, looked up: its item is never asked for (the save sent it)
  while (source.hasWork(8000)) source.advance(8000);
  EXPECT_TRUE(rig.api.items.empty());
  EXPECT_TRUE(source.word(6).metBefore);
}

TEST(LiveSteps, ASaveLandingAfterTheNextSentenceUpdatesItsCopy) {
  TwoSentences rig;
  constexpr const char* kAgain =
      R"({"occurrences":[)"
      R"({"word":"雨","isWordLike":true,"charStart":0,"charEnd":1,"entryId":11},)"
      R"({"word":"読んだ","lemma":"読む","isWordLike":true,"charStart":1,"charEnd":4,"entryId":5,"lemmaEntryId":6}]})";
  rig.model.lines[1].tokens = {"雨", "読んだ", "。"};
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAgain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":901}})")};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {"xteink"}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  const Hit fresh{Target::Level, 2, {}};
  for (const LevelChange& ch : c.tap(&fresh, 1000).changes) source.queue(ch);  // in its Undo window
  c.step(+1, 1100);
  source.advance(1200);  // the next sentence first: its 読んだ isn't saved yet
  c.sourceChanged(1200);
  ASSERT_EQ(source.wordCount(), 7);
  EXPECT_FALSE(source.word(6).metBefore);
  while (source.hasPendingWrites()) source.advance(1000 + config::kToastMs);  // the save lands
  ASSERT_EQ(rig.api.written.size(), 1u);
  ASSERT_TRUE(source.word(6).metBefore);  // the copy follows the save
  EXPECT_EQ(source.word(6).metBefore->text, "彼は本を読んだ。");
  EXPECT_FALSE(source.word(4).metBefore);  // the sentence it was saved from
}

namespace {

// A save landing after the next sentence's copy of the word was built: the session redraws when that copy is on
// screen (its "Met before" appears now), not when it's elsewhere.
CardSession::Answer saveLandsWithTheCopy(const bool onScreen) {
  TwoSentences rig;
  constexpr const char* kAgain =
      R"({"occurrences":[)"
      R"({"word":"雨","isWordLike":true,"charStart":0,"charEnd":1,"entryId":11},)"
      R"({"word":"読んだ","lemma":"読む","isWordLike":true,"charStart":1,"charEnd":4,"entryId":5,"lemmaEntryId":6}]})";
  rig.model.lines[1].tokens = {"雨", "読んだ", "。"};
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAgain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu), apiOk(R"({"word":"雨"})"), apiOk(kLookupYomu)};
  rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":901}})")};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {"xteink"}, rig.next());
  CardController c(source, ReadingMode::Kana);
  ShownTargets targets;
  PendingInput input;
  CardSession session(c, targets, input, &source);
  c.open(0);
  const auto drain = [&](const unsigned long t) {
    while (session.hasWork(t) && rig.api.written.empty()) session.apply(session.fetch(t), t);
  };
  drain(10);  // A and B
  const Hit fresh{Target::Level, 2, {}};
  for (const LevelChange& ch : c.tap(&fresh, 1000).changes) source.queue(ch);  // in its Undo window
  c.step(+1, 1100);
  drain(1200);  // the next sentence and 雨's lookup, not the save (still in its window)
  if (onScreen) {
    EXPECT_TRUE(c.step(+1, 1400));  // onto the copy of 読んだ
    drain(1500);                    // its lookup
  }
  EXPECT_EQ(c.word(), onScreen ? 6 : 5);
  EXPECT_FALSE(source.word(6).metBefore);
  EXPECT_TRUE(rig.api.written.empty());
  const unsigned long landed = 1000 + config::kToastMs;
  const CardSession::Answer answer = session.apply(session.fetch(landed), landed);  // the save lands
  EXPECT_EQ(rig.api.written.size(), 1u);
  EXPECT_TRUE(source.word(6).metBefore);
  return answer;
}

}  // namespace

TEST(LiveSteps, ASaveLandingWhileItsCopyIsOnScreenRedrawsIt) {
  EXPECT_TRUE(saveLandsWithTheCopy(/*onScreen=*/true).redraw);
  EXPECT_FALSE(saveLandsWithTheCopy(/*onScreen=*/false).redraw);  // off screen: nothing to draw
}

TEST(LiveSteps, RemovingTheSaveClearsItsCopysMetBefore) {
  TwoSentences rig;
  constexpr const char* kAgain =
      R"({"occurrences":[)"
      R"({"word":"雨","isWordLike":true,"charStart":0,"charEnd":1,"entryId":11},)"
      R"({"word":"読んだ","lemma":"読む","isWordLike":true,"charStart":1,"charEnd":4,"entryId":5,"lemmaEntryId":6}]})";
  rig.model.lines[1].tokens = {"雨", "読んだ", "。"};
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAgain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":901}})"), apiOk("{}"), apiOk("{}")};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {"xteink"}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  source.queue({4, Level::None, Level::Fresh, 1000});
  while (source.hasPendingWrites()) source.advance(1000);  // saved
  c.step(+1, 1100);
  while (source.extending()) source.advance(1200);
  ASSERT_TRUE(source.word(6).metBefore);
  source.queue({4, Level::Fresh, Level::None, 2000});  // removed (⋯ Undo save): DELETE, then the clear
  while (source.hasPendingWrites()) source.advance(2000);
  ASSERT_EQ(rig.api.written.size(), 3u);
  EXPECT_FALSE(source.word(6).metBefore);  // the copy follows
}

TEST(LiveSteps, APunctuationOnlySentenceIsSkippedAndThePageEndStops) {
  TwoSentences rig;
  // After 読んだ。: a line of dots (no word in it), then the rain.
  rig.model.lines = {rig.model.lines[0], {{"……"}, true}, rig.model.lines[1]};
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(R"({"occurrences":[]})"), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000);
  while (source.extending()) source.advance();
  c.sourceChanged(2000);
  ASSERT_EQ(rig.api.analyzed.size(), 3u);
  EXPECT_EQ(rig.api.analyzed[1], "……");
  EXPECT_EQ(c.currentWord().word, "雨");  // the dots were passed over
  c.step(+1, 3000);
  c.step(+1, 3100);
  EXPECT_FALSE(c.step(+1, 3200));  // the page's last word
  EXPECT_FALSE(c.awaitingNext());
  EXPECT_TRUE(c.state().toast.empty());  // the page ending is no failure
}

TEST(LiveSteps, WithoutANextSentenceTheCardStops) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page);  // no NextSentence: the bench's behaviour
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  EXPECT_FALSE(c.step(+1, 1000));
  EXPECT_EQ(c.word(), 4);
}

TEST(LiveSteps, ANextSentenceWithNoAnswerSaysSoAndIsTriedAgain) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiFailure(ApiError::Network), apiFailure(ApiError::Unauthorized),
                            apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000);
  EXPECT_EQ(source.advance(), LiveSource::Advance::Changed);  // offline: the card doesn't close
  EXPECT_TRUE(c.sourceChanged(1100));
  EXPECT_EQ(c.word(), 4);  // still on 読んだ
  EXPECT_FALSE(c.awaitingNext());
  EXPECT_EQ(c.state().toast, CardStrings{}.nextSentenceFailed);
  c.step(+1, 2000);  // tried again: a rejected key this time
  source.advance();
  c.sourceChanged(2100);
  EXPECT_EQ(c.state().toast, CardStrings{}.keyRejected);
  c.step(+1, 3000);  // and again: it comes
  source.advance();
  c.sourceChanged(3100);
  EXPECT_EQ(c.currentWord().word, "雨");
}

TEST(LiveSteps, ASaveInTheNextSentenceNotesItsOwnSentence) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu), apiOk(R"({"word":"雨","translations":[{"translation":"rain"}]})")};
  rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":9}})")};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000);
  source.advance();  // its analysis
  c.sourceChanged(1000);
  const Hit learning{Target::Level, 1, {}};
  for (const LevelChange& ch : c.tap(&learning, 2000).changes) source.queue(ch);
  while (source.hasWork(100000)) source.advance(100000);
  ASSERT_EQ(rig.api.written.size(), 1u);
  EXPECT_NE(rig.api.written[0].body.find("雨が降る"), std::string::npos);
  EXPECT_EQ(rig.api.written[0].body.find("彼は本を"), std::string::npos);
}

TEST(LiveSteps, ClosingWhileTheNextSentenceLoadsStillSendsTheSaves) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":9}})")};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  const Hit learning{Target::Level, 1, {}};
  for (const LevelChange& ch : c.tap(&learning, 2000).changes) source.queue(ch);
  c.step(+1, 2100);  // the next sentence starts loading
  while (source.hasPendingWrites()) source.apply(source.fetch(2200, /*closing=*/true), 2200);
  EXPECT_EQ(rig.api.analyzed.size(), 1u);  // closing skips the next sentence's analysis
  EXPECT_EQ(rig.api.written.size(), 1u);
}

TEST(LiveSteps, ARateLimitSaysSo) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiFailure(ApiError::RateLimited)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000);
  source.advance();
  c.sourceChanged(1100);
  EXPECT_EQ(c.state().toast, CardStrings{}.rateLimited);
}

TEST(LiveSteps, AFailedSavesRetryOutlivesTheNextSentenceFailing) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiFailure(ApiError::Network)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  rig.api.writeReplies = {apiFailure(ApiError::Network)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  ShownTargets targets;
  PendingInput input;
  CardSession session(c, targets, input, &source);
  openOnTheLastWord(source, c);
  const Hit learning{Target::Level, 1, {}};
  for (const LevelChange& ch : c.tap(&learning, 1000).changes) source.queue(ch);
  session.apply(session.fetch(10000), 10000);  // the save fails: "Save failed · Retry"
  ASSERT_TRUE(c.state().toastUndo);
  const std::string retryToast = c.state().toast;
  c.step(+1, 11000);
  session.apply(session.fetch(11100), 11100);  // the next sentence fails too
  EXPECT_EQ(c.state().toast, retryToast);      // the Retry stays
  EXPECT_TRUE(c.state().toastUndo);
}

TEST(LiveSteps, SentencesItCantAskAboutArePassedOver) {
  // A book that doesn't say: a line of dots has no language (nothing to send), so the card goes past it
  // without a call, to the rain.
  TwoSentences rig;
  rig.model.lines = {rig.model.lines[0], {{"……"}, true}, rig.model.lines[1]};
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  const auto next = [&rig](const TapContext& current) {
    TapContext t;
    t.sentence = lexipoint::text::buildSentenceAfter(rig.model, *current.sentence, Script::Japanese);
    if (t.sentence && t.sentence->text != "……") t.language.language = Language::Japanese;
    return t;
  };
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, next);
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000);
  while (source.extending()) source.advance();
  c.sourceChanged(2000);
  ASSERT_EQ(rig.api.analyzed.size(), 2u);
  EXPECT_EQ(rig.api.analyzed[1], "雨が降る。");
  EXPECT_EQ(c.currentWord().word, "雨");
}

TEST(LiveSteps, ALevelTappedWhileItWaitsKeepsTheCardAndItsUndo) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000);  // waiting for 雨が降る。
  const Hit learning{Target::Level, 1, {}};
  for (const LevelChange& ch : c.tap(&learning, 1100).changes) source.queue(ch);
  EXPECT_FALSE(c.awaitingNext());  // the tap was about this word
  source.advance(1200);            // the analysis lands
  c.sourceChanged(1200);
  EXPECT_EQ(c.word(), 4);  // no jump under the user's Undo
  EXPECT_TRUE(c.state().toastUndo);
}

TEST(LiveSteps, ATapOnTheCardsOwnWordKeepsTheWait) {
  // P10 R4: the word on the page isn't the card: a tap there changes nothing, the step still goes on.
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000);  // waiting for 雨が降る。
  const Hit ownWord{Target::OwnWord, 0, {}};
  EXPECT_EQ(c.tap(&ownWord, 1100).effect, Effect::None);
  EXPECT_TRUE(c.awaitingNext());
  source.advance(1200);
  c.sourceChanged(1200);
  EXPECT_EQ(c.word(), 5);  // on into the next sentence
}

TEST(LiveSteps, ANextSentenceFailingLeavesASavesUndo) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiFailure(ApiError::Network)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  const Hit learning{Target::Level, 1, {}};
  for (const LevelChange& ch : c.tap(&learning, 1000).changes) source.queue(ch);
  const std::string undo = c.state().toast;
  c.step(+1, 1100);
  source.advance(1200);  // the analysis fails (the save is still in its window, not sent)
  c.sourceChanged(1200);
  EXPECT_EQ(c.state().toast, undo);  // "Saved as learning · Undo" stays
  EXPECT_TRUE(c.state().toastUndo);
}

TEST(LiveSteps, ARetryAfterASkippedSentenceDoesNotAskAboutItAgain) {
  TwoSentences rig;
  rig.model.lines = {rig.model.lines[0], {{"……"}, true}, rig.model.lines[1]};
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(R"({"occurrences":[]})"), apiFailure(ApiError::Network),
                            apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000);
  while (source.extending()) source.advance();  // the dots (no word), then the rain (offline)
  c.sourceChanged(1100);
  ASSERT_EQ(c.state().toast, CardStrings{}.nextSentenceFailed);
  c.step(+1, 2000);  // tried again: from after the dots
  while (source.extending()) source.advance();
  c.sourceChanged(2100);
  EXPECT_EQ(c.currentWord().word, "雨");
  EXPECT_EQ(rig.api.analyzed, (std::vector<std::string>{"彼は本を読んだ。", "……", "雨が降る。", "雨が降る。"}));
}

TEST(LiveSteps, ASwipeOrHomeWhileItWaitsKeepsTheCard) {
  for (const bool swipe : {true, false}) {
    TwoSentences rig;
    rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAnalyzeRain)};
    rig.api.lookupReplies = {apiOk(kLookupYomu)};
    LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
    CardController c(source, ReadingMode::Kana);
    openOnTheLastWord(source, c);
    const Hit rank{Target::RankRow, 0, {}};
    c.tap(&rank, 900);  // the detail view
    c.step(+1, 1000);
    ASSERT_TRUE(c.awaitingNext());
    if (swipe) {
      c.swipe(Swipe::Left);  // next tab
    } else {
      c.home();  // back to the card view
    }
    EXPECT_FALSE(c.awaitingNext()) << swipe;
    source.advance(1100);
    c.sourceChanged(1100);
    EXPECT_EQ(c.word(), 4) << swipe;  // no jump after it
  }
}

TEST(LiveSteps, APressHeldThroughTheAnalysisDoesNotSkipTheFirstWord) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000, 950);  // at the last word: waits
  source.advance(3000);   // the analysis blocks the loop until 3000
  c.sourceChanged(3000);  // the jump
  ASSERT_EQ(c.currentWord().word, "雨");
  // A second press went down during the call; the loop first sees it at 3010, and its release at 3200.
  EXPECT_FALSE(c.step(+1, 3200, 3010));
  EXPECT_EQ(c.currentWord().word, "雨");                                // still the first word
  EXPECT_TRUE(c.step(+1, 4000, 3000 + config::kStepAfterJumpGraceMs));  // one made after the jump moves on
  EXPECT_EQ(c.currentWord().word, "が");
  EXPECT_TRUE(c.step(-1, 4100, 3010));  // back is never dropped
}

TEST(LiveSteps, AQueuedStepCarriesItsPressTime) {
  // The same held press as above, through the input queue the activity uses (CardInput handleInput).
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  ShownTargets shown;
  openOnTheLastWord(source, c);
  c.step(+1, 1000, 950);
  source.advance(3000);
  c.sourceChanged(3000);  // the jump
  PendingInput held;
  held.step(+1, 3200, 3010);  // first seen just after the call
  handleInput(c, shown, held, 3200);
  EXPECT_EQ(c.currentWord().word, "雨");
  PendingInput fresh;
  fresh.step(+1, 4000, 3900);
  handleInput(c, shown, fresh, 4000);
  EXPECT_EQ(c.currentWord().word, "が");
  PendingInput plain;
  plain.step(+1, 5000);  // no press time known: never dropped as held
  EXPECT_FALSE(plain[0].pressedMs.has_value());
  EXPECT_FALSE(InputEvent{}.pressedMs.has_value());  // an event built by hand: no press time, never dropped
}

TEST(LiveSteps, ABackPressHeldThroughTheAnalysisStepsBackFromTheWaitingWord) {
  TwoSentences rig;
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000, 950);  // waits on 読んだ (4)
  source.advance(3000);   // the call blocks until 3000
  c.sourceChanged(3000);  // the jump to 雨 (5)
  ASSERT_EQ(c.word(), 5);
  // Back was pressed during the call (first seen at 3010): meant from 読んだ, so を (3), as without the call.
  EXPECT_TRUE(c.step(-1, 3200, 3010));
  EXPECT_EQ(c.word(), 3);
  EXPECT_TRUE(c.step(-1, 3300, 3020));  // a second one during the call goes on back
  EXPECT_EQ(c.word(), 2);
  EXPECT_TRUE(c.step(-1, 3400, 3030));
  EXPECT_TRUE(c.step(-1, 3500, 3040));
  EXPECT_EQ(c.word(), 0);
  EXPECT_FALSE(c.step(-1, 3600, 3050));  // at the start already: nothing moves, nothing redraws
  EXPECT_EQ(c.word(), 0);
  EXPECT_TRUE(c.step(+1, 4000, 3900));  // a later press is an ordinary step
  EXPECT_EQ(c.word(), 1);
}

TEST(LiveSteps, AHeldBackPressCancelsAWaitStartedAfterTheJump) {
  // Three sentences, the second one word long: after the jump to it, an ordinary press waits on the third;
  // a back press held through the first analysis then steps back from 読んだ, and the wait is over.
  TwoSentences rig;
  rig.model.lines = {{{"彼", "は", "本", "を", "読んだ", "。"}, true}, {{"雨", "。"}, true}, {{"風", "。"}, true}};
  rig.page.lines.resize(1);
  rig.page.lines.push_back({140, {{"雨", 20, 26}, {"。", 46, 26}}});
  rig.page.lines.push_back({180, {{"風", 20, 26}, {"。", 46, 26}}});
  constexpr const char* kRain =
      R"({"occurrences":[{"word":"雨","isWordLike":true,"charStart":0,"charEnd":1,"entryId":11},)"
      R"({"word":"。","isWordLike":false,"charStart":1,"charEnd":2}]})";
  constexpr const char* kWind =
      R"({"occurrences":[{"word":"風","isWordLike":true,"charStart":0,"charEnd":1,"entryId":14},)"
      R"({"word":"。","isWordLike":false,"charStart":1,"charEnd":2}]})";
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kRain), apiOk(kWind)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  c.step(+1, 1000, 950);  // waits on 読んだ (4)
  source.advance(3000);
  c.sourceChanged(3000);
  ASSERT_EQ(c.word(), 5);                // 雨, its sentence's only word
  EXPECT_FALSE(c.step(+1, 3200, 3150));  // an ordinary press: waits on 風
  ASSERT_TRUE(c.awaitingNext());
  EXPECT_TRUE(c.step(-1, 3300, 3010));  // pressed during the call: を (3)
  EXPECT_EQ(c.word(), 3);
  EXPECT_FALSE(c.awaitingNext());
  source.advance(4000);
  c.sourceChanged(4000);
  EXPECT_EQ(c.word(), 3);  // 風 arrives: the card stays where it was sent
}

TEST(LiveSteps, TheSameEntryIdInTheOtherLanguageIsAnotherWord) {
  // A book that doesn't say: the next sentence is decided Chinese, and one of its words happens to have
  // the entry id of a Japanese word already on the card. It's a different Lexirise item.
  TwoSentences rig;
  constexpr const char* kZh =
      R"({"occurrences":[)"
      R"({"word":"雨","isWordLike":true,"charStart":0,"charEnd":1,"entryId":11},)"
      R"({"word":"读","isWordLike":true,"charStart":1,"charEnd":2,"entryId":5,"lemmaEntryId":6}]})";
  rig.model.lines[1].tokens = {"雨", "读", "。"};
  rig.api.analyzeReplies = {apiOk(kAnalyze), apiOk(kZh)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  const auto next = [&rig](const TapContext& current) {
    TapContext t;
    t.sentence = lexipoint::text::buildSentenceAfter(rig.model, *current.sentence, Script::Chinese);
    if (t.sentence) t.language.language = Language::Chinese;
    return t;
  };
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, next);
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  const Hit fresh{Target::Level, 2, {}};
  for (const LevelChange& ch : c.tap(&fresh, 1000).changes) source.queue(ch);
  c.step(+1, 5000);
  source.advance(5000);
  c.sourceChanged(5000);
  ASSERT_EQ(source.wordCount(), 7);
  EXPECT_EQ(source.sameWord(4), std::vector<int>{4});  // 読む (ja) alone
  EXPECT_EQ(source.sameWord(6), std::vector<int>{6});  // 读 (zh) alone
  ASSERT_TRUE(c.step(+1, 6000));
  EXPECT_EQ(c.state().level, Level::None);  // no level carried over from the Japanese word
}

// v0.2 V1: a sentence Lexirise returns already refined (its words cut into morphemes) is shown word by word,
// from its word-level answer (lookup/WholeWords.h).
namespace {

struct Refined {
  Rig rig;
  explicit Refined(std::vector<std::string> tokens) {
    TextLine line;
    line.tokens = std::move(tokens);
    line.startsParagraph = true;
    rig.model.lines = {line};
    ReaderLine drawn{100, {}};
    int x = 20;
    for (const std::string& t : rig.model.lines[0].tokens) {
      drawn.tokens.push_back({t, x, 26});
      x += 26;
    }
    rig.page.lines = {drawn};
  }
};

// 小さな声。 refined: 小さ · な · 声, and word-level: 小さな (ranked) · 声.
constexpr const char* kSmallRefined =
    R"({"occurrences":[{"word":"小さ","isWordLike":true,"charStart":0,"charEnd":2,"entryId":7,"lemmaEntryId":7},)"
    R"({"word":"な","isWordLike":true,"charStart":2,"charEnd":3,"entryId":8},)"
    R"({"word":"声","isWordLike":true,"transliteration":"koe","charStart":3,"charEnd":4,"entryId":9},)"
    R"({"word":"。","isWordLike":false,"charStart":4,"charEnd":5}],"morphoPending":false})";
constexpr const char* kSmallWords =
    R"({"occurrences":[{"word":"小さな","isWordLike":true,"transliteration":"chiisana","charStart":0,"charEnd":3,)"
    R"("entryId":10},{"word":"声","isWordLike":true,"transliteration":"koe","charStart":3,"charEnd":4,"entryId":9},)"
    R"({"word":"。","isWordLike":false,"charStart":4,"charEnd":5}],)"
    R"("entryMetaById":{"10":{"transliteration":"chiisana","rank":2398}},"morphoPending":false})";

}  // namespace

TEST(LiveWholeWords, ARefinedSentenceIsShownWordByWord) {
  Refined r({"小さな", "声", "。"});
  r.rig.api.analyzeReplies = {apiOk(kSmallRefined)};
  r.rig.api.wordsReplies = {apiOk(kSmallWords)};
  r.rig.api.lookupReplies = {apiOk(R"({"word":"小さな","translations":[{"translation":"small"}]})")};
  LiveSource source(r.rig.api, r.rig.tap(0, 0), r.rig.page);
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  EXPECT_EQ(source.advance(), LiveSource::Advance::Changed);  // A
  c.sourceChanged(0);
  EXPECT_EQ(r.rig.api.analyzedWords, std::vector<std::string>{"小さな声。"});
  EXPECT_EQ(c.currentWord().word, "小さな");  // not 小さ
  EXPECT_EQ(source.scene(c.word(), true, kMetrics, c.highlightCodepoints()).page.commands.at(1).text, "小さな");
  source.advance();  // B
  EXPECT_EQ(r.rig.api.looked, std::vector<std::string>{"小さな"});
  ASSERT_TRUE(c.step(+1, 0));
  EXPECT_EQ(c.currentWord().word, "声");  // one step, not two
  EXPECT_FALSE(c.step(+1, 0));
}

TEST(LiveWholeWords, TheNextSentenceAsksForItsOwnWords) {
  TwoSentences rig;
  const std::string firstPass =
      std::string(kAnalyze).insert(std::string(kAnalyze).size() - 1, R"(,"morphoPending":true)");
  rig.api.analyzeReplies = {
      apiOk(firstPass), apiOk(R"({"occurrences":[{"word":"雨","isWordLike":true,"charStart":0,"charEnd":1,)"
                              R"("entryId":11},{"word":"が","isWordLike":true,"charStart":1,"charEnd":2,"entryId":12},)"
                              R"({"word":"降","isWordLike":true,"charStart":2,"charEnd":3,"entryId":14},)"
                              R"({"word":"る","isWordLike":true,"charStart":3,"charEnd":4,"entryId":15},)"
                              R"({"word":"。","isWordLike":false,"charStart":4,"charEnd":5}],"morphoPending":false})")};
  rig.api.wordsReplies = {apiOk(R"({"occurrences":[)"
                                R"({"word":"雨","isWordLike":true,"charStart":0,"charEnd":1,"entryId":11},)"
                                R"({"word":"が","isWordLike":true,"charStart":1,"charEnd":2,"entryId":12},)"
                                R"({"word":"降る","isWordLike":true,"charStart":2,"charEnd":4,"entryId":13},)"
                                R"({"word":"。","isWordLike":false,"charStart":4,"charEnd":5}],)"
                                R"("entryMetaById":{"13":{"rank":900}}})")};
  rig.api.lookupReplies = {apiOk(kLookupYomu), apiOk(R"({"word":"雨"})")};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);
  EXPECT_TRUE(rig.api.analyzedWords.empty());  // the tapped sentence was a first-pass answer
  EXPECT_FALSE(c.step(+1, 1000));
  source.advance();
  c.sourceChanged(1200);
  EXPECT_EQ(rig.api.analyzedWords, std::vector<std::string>{"雨が降る。"});
  ASSERT_TRUE(c.step(+1, 2000));
  ASSERT_TRUE(c.step(+1, 2100));
  EXPECT_EQ(c.currentWord().word, "降る");  // whole
  EXPECT_FALSE(c.step(+1, 2200));
}

TEST(LiveWholeWords, AWholeWordTwiceInASentenceIsOneEntry) {
  // 一边吃饭一边。 refined as 一 · 边 · 吃饭 · 一 · 边: both 一边 are entry 20 again, so a level set on the first
  // is the second's too (the same-word rule, by the whole word's own entry: it has no lemma).
  Refined r({"一边", "吃饭", "一边", "。"});
  r.rig.api.analyzeReplies = {apiOk(
      R"({"occurrences":[{"word":"一","isWordLike":true,"charStart":0,"charEnd":1,"entryId":11,"lemmaEntryId":11},)"
      R"({"word":"边","isWordLike":true,"charStart":1,"charEnd":2,"entryId":12},)"
      R"({"word":"吃饭","isWordLike":true,"charStart":2,"charEnd":4,"entryId":13},)"
      R"({"word":"一","isWordLike":true,"charStart":4,"charEnd":5,"entryId":11,"lemmaEntryId":11},)"
      R"({"word":"边","isWordLike":true,"charStart":5,"charEnd":6,"entryId":12},)"
      R"({"word":"。","isWordLike":false,"charStart":6,"charEnd":7}],"morphoPending":false})")};
  r.rig.api.wordsReplies = {
      apiOk(R"({"occurrences":[{"word":"一边","isWordLike":true,"charStart":0,"charEnd":2,"entryId":20},)"
            R"({"word":"吃饭","isWordLike":true,"charStart":2,"charEnd":4,"entryId":13},)"
            R"({"word":"一边","isWordLike":true,"charStart":4,"charEnd":6,"entryId":20},)"
            R"({"word":"。","isWordLike":false,"charStart":6,"charEnd":7}],"entryMetaById":{"20":{"rank":1092}}})")};
  r.rig.api.lookupReplies = {apiOk(R"({"word":"一边","translations":[{"translation":"while"}]})")};
  r.rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":5}})"), apiOk("{}")};
  LiveSource source(r.rig.api, r.rig.tap(0, 0), r.rig.page);
  CardController c(source, ReadingMode::Kana);
  ShownTargets targets;
  PendingInput input;
  CardSession session(c, targets, input, &source);
  c.open(0);
  session.apply(session.fetch(1), 1);
  session.apply(session.fetch(2), 2);
  ASSERT_EQ(c.currentWord().word, "一边");
  const Hit fresh{Target::Level, 2, {}};
  for (const LevelChange& ch : c.tap(&fresh, 3).changes) source.queue(ch);
  while (session.hasWork(10000)) session.apply(session.fetch(10000), 10000);
  c.step(+1, 11000);
  c.step(+1, 12000);  // the second 一边
  EXPECT_EQ(c.currentWord().word, "一边");
  EXPECT_EQ(c.state().level, Level::Fresh);
}

// C16 end to end: each form's name, worked out while the analysis is fetched (outside RenderLock), is what the card
// shows after phase A, after phase B, and after a save and its removal (every copy of the saved word).
TEST(LiveNames, EachFormsNameHoldsThroughBAndASaveAndItsRemoval) {
  Rig rig;
  TextLine line;
  line.tokens = {"書いた", "本", "を", "読んだ", "し", "、", "また", "読んだ", "。"};
  line.startsParagraph = true;
  rig.model.lines = {line};
  rig.page.lines = {{100,
                     {{"書いた", 20, 78},
                      {"本", 98, 26},
                      {"を", 124, 26},
                      {"読んだ", 150, 78},
                      {"し", 228, 26},
                      {"、", 254, 26},
                      {"また", 280, 52},
                      {"読んだ", 332, 78},
                      {"。", 410, 26}}}};
  rig.api.analyzeReplies = {apiOk(
      R"({"occurrences":[)"
      R"({"word":"書いた","lemma":"書く","isWordLike":true,"charStart":0,"charEnd":3,"entryId":10,"lemmaEntryId":11},)"
      R"({"word":"本","isWordLike":true,"charStart":3,"charEnd":4,"entryId":3,"lemmaEntryId":3},)"
      R"({"word":"を","isWordLike":true,"charStart":4,"charEnd":5,"entryId":4},)"
      R"({"word":"読んだ","lemma":"読む","isWordLike":true,"charStart":5,"charEnd":8,"entryId":5,"lemmaEntryId":6},)"
      R"({"word":"し","isWordLike":true,"charStart":8,"charEnd":9,"entryId":7},)"
      R"({"word":"、","isWordLike":false,"charStart":9,"charEnd":10},)"
      R"({"word":"また","isWordLike":true,"charStart":10,"charEnd":12,"entryId":8},)"
      R"({"word":"読んだ","lemma":"読む","isWordLike":true,"charStart":12,"charEnd":15,"entryId":5,"lemmaEntryId":6},)"
      R"({"word":"。","isWordLike":false,"charStart":15,"charEnd":16}]})")};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":901}})"), apiOk("{}"), apiOk("{}")};
  LiveSource source(rig.api, rig.tap(0, 3), rig.page);  // tapped the first 読んだ
  CardController c(source, ReadingMode::Kana);
  ShownTargets targets;
  PendingInput input;
  CardSession session(c, targets, input, &source);
  c.open(0);
  constexpr int kKaita = 0, kHon = 1, kYonda = 3, kYondaAgain = 6;
  const auto expectNames = [&source](const char* when) {
    for (const int w : {kYonda, kYondaAgain}) {
      EXPECT_EQ(source.word(w).conjugation, "past") << when << " " << w;
      ASSERT_EQ(source.word(w).forms.size(), 2u) << when << " " << w;
      EXPECT_EQ(source.word(w).forms[0].form, "読む") << when;
      EXPECT_EQ(source.word(w).forms[1].form, "読んだ") << when;
    }
    EXPECT_EQ(source.word(kKaita).conjugation, "past") << when;
    ASSERT_EQ(source.word(kKaita).forms.size(), 2u) << when;
    EXPECT_EQ(source.word(kKaita).forms[0].form, "書く") << when;
    EXPECT_TRUE(source.word(kHon).conjugation.empty()) << when;  // the dictionary form: no name, no forms
    EXPECT_TRUE(source.word(kHon).forms.empty()) << when;
  };
  session.apply(session.fetch(1), 1);  // A
  ASSERT_EQ(c.word(), kYonda);
  expectNames("after A");
  session.apply(session.fetch(2), 2);  // B (lookups keep the form and the dictionary form: the name is kept)
  EXPECT_EQ(rig.api.looked, std::vector<std::string>{"読む"});
  expectNames("after B");
  source.queue({kYonda, Level::None, Level::Fresh});
  while (session.hasWork(10000)) session.apply(session.fetch(10000), 10000);
  ASSERT_EQ(rig.api.written.size(), 1u);  // the save
  expectNames("after the save");
  source.queue({kYonda, Level::Fresh, Level::None});
  while (session.hasWork(20000)) session.apply(session.fetch(20000), 20000);
  ASSERT_EQ(rig.api.written.size(), 3u);  // DELETE, then the clear
  expectNames("after the removal");
}

namespace {

// One sentence over the cap, which the card has as two cuts, with 本 in each; 本 was saved from cut `savedFrom`
// (its note that cut).
struct LongSentence {
  FakeApi api;
  PageModel model;
  ReaderPage page;
  TapContext tap;
  std::string firstCut, secondCut;
  explicit LongSentence(const int savedFrom) {
    TextLine line;
    line.tokens.push_back("本");
    for (int i = 0; i < 125; i++) line.tokens.push_back("あ");
    line.tokens.push_back("本");
    for (int i = 0; i < 10; i++) line.tokens.push_back("い");
    line.tokens.push_back("。");
    line.startsParagraph = true;
    model.lines = {line};
    page.lines.push_back({100, {}});
    for (const std::string& t : line.tokens) page.lines[0].tokens.push_back({t, 20, 26});
    tap.sentence = buildSentence(model, {0, 0}, Script::Japanese);
    tap.language.language = Language::Japanese;
    const auto second = lexipoint::text::buildSentenceAfter(model, *tap.sentence, Script::Japanese);
    EXPECT_TRUE(tap.sentence->truncatedRight);
    EXPECT_TRUE(second && second->truncatedLeft);
    firstCut = tap.sentence->text;
    secondCut = second->text;
    const size_t hon = secondCut.find("本");
    const uint32_t at = lexipoint::text::utf16Length(std::string_view(secondCut).substr(0, hon));
    const std::string saved = R"("stateByEntryId":{"3":{"saved_expression_id":77,"proficiency":2}})";
    api.itemReplies = {apiOk(R"({"item":{"notes":")" + (savedFrom == 1 ? firstCut : secondCut) + R"("}})")};
    api.analyzeReplies = {
        apiOk(R"({"occurrences":[{"word":"本","isWordLike":true,"charStart":0,"charEnd":1,"entryId":3,)"
              R"("lemmaEntryId":3}],)" +
              saved + "}"),
        apiOk(R"({"occurrences":[{"word":"本","isWordLike":true,"charStart":)" + std::to_string(at) + R"(,"charEnd":)" +
              std::to_string(at + 1) + R"(,"entryId":3,"lemmaEntryId":3}],)" + saved + "}")};
    api.lookupReplies = {apiOk(R"({"word":"本"})"), apiOk(R"({"word":"本"})")};
  }
  LiveSource::NextSentence next() {
    return [this](const TapContext& current) {
      TapContext t;
      t.sentence = lexipoint::text::buildSentenceAfter(model, *current.sentence, Script::Japanese);
      if (t.sentence) t.language.language = Language::Japanese;
      return t;
    };
  }
};

}  // namespace

TEST(LiveSteps, ASavedWordsCopyInTheNextCutOfTheSameLongSentenceIsntMetBefore) {
  LongSentence rig(/*savedFrom=*/1);
  LiveSource source(rig.api, rig.tap, rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  while (source.hasWork(0)) source.advance();  // A, B, the item
  c.sourceChanged(0);
  ASSERT_EQ(rig.api.items.size(), 1u);
  EXPECT_FALSE(source.word(0).metBefore);  // saved from this very cut
  c.step(+1, 1000);                        // on into the second cut
  while (source.hasWork(1100)) {
    source.advance(1100);
    c.sourceChanged(1100);
  }
  ASSERT_EQ(source.wordCount(), 2);
  EXPECT_EQ(rig.api.analyzed[1], rig.secondCut);
  EXPECT_FALSE(source.word(1).metBefore);  // the same long sentence, cut: not "Met before"
  EXPECT_EQ(rig.api.items.size(), 1u);     // the copy has the item already
}

TEST(LiveSteps, AWordSavedFromTheNextCutLosesItsMetBeforeOnceThatCutComes) {
  LongSentence rig(/*savedFrom=*/2);
  LiveSource source(rig.api, rig.tap, rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  while (source.hasWork(0)) source.advance();  // A, B, the item
  c.sourceChanged(0);
  EXPECT_TRUE(source.word(0).metBefore);  // the second cut isn't on the card yet: it reads as another sentence
  c.step(+1, 1000);
  while (source.hasWork(1100)) {
    source.advance(1100);
    c.sourceChanged(1100);
  }
  ASSERT_EQ(source.wordCount(), 2);
  EXPECT_FALSE(source.word(0).metBefore);  // rebuilt against the sentence joined back
  EXPECT_FALSE(source.word(1).metBefore);
}

TEST(LiveSteps, AnEarlierCutsWordOnScreenIsRedrawnWhenTheNextCutJoins) {
  // On 本 in the first cut (its note the second cut: "Met before" shows), a step on starts the next cut loading and a
  // step back cancels it: the card stays on 本. When the cut lands, 本's "Met before" goes, and is drawn.
  LongSentence rig(/*savedFrom=*/2);
  LiveSource source(rig.api, rig.tap, rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  ShownTargets targets;
  PendingInput input;
  CardSession session(c, targets, input, &source);
  c.open(0);
  for (unsigned long t = 1; session.hasWork(t) && t < 10; t++) session.apply(session.fetch(t), t);  // A, B
  ASSERT_EQ(c.word(), 0);
  EXPECT_TRUE(source.word(0).metBefore);
  c.step(+1, 1000);
  c.step(-1, 1010);
  bool redrawn = false;
  for (unsigned long t = 1100; session.hasWork(t) && t < 1200; t++) {
    redrawn = session.apply(session.fetch(t), t).redraw || redrawn;
  }
  ASSERT_EQ(source.wordCount(), 2);
  EXPECT_EQ(c.word(), 0);
  EXPECT_FALSE(source.word(0).metBefore);
  EXPECT_TRUE(redrawn);
}

TEST(LiveSteps, AVerbEndingACutIsNamedOnceTheNextCutShowsWhatFollows) {
  // 書け ends the first cut (the cap cut right after it): what follows is unknown, and 書け could be 書ける, 書けば…
  // cut short, so it has no name. The next cut starts with と (書けと): 書け is the imperative.
  FakeApi api;
  PageModel model;
  TextLine line;
  line.tokens.push_back("本");
  for (int i = 0; i < 117; i++) line.tokens.push_back("あ");
  for (const char* t : {"書け", "と", "言", "った", "。"}) line.tokens.push_back(t);
  line.startsParagraph = true;
  model.lines = {line};
  ReaderPage page;
  page.lines.push_back({100, {}});
  for (const std::string& t : line.tokens) page.lines[0].tokens.push_back({t, 20, 26});
  TapContext tap;
  tap.sentence = buildSentence(model, {0, 0}, Script::Japanese);
  tap.language.language = Language::Japanese;
  ASSERT_TRUE(tap.sentence && tap.sentence->truncatedRight);
  ASSERT_EQ(tap.sentence->text.substr(tap.sentence->text.size() - 6), "書け");
  api.analyzeReplies = {
      apiOk(R"({"occurrences":[{"word":"本","isWordLike":true,"charStart":0,"charEnd":1,"entryId":3},)"
            R"({"word":"書け","lemma":"書く","isWordLike":true,"charStart":118,"charEnd":120,"entryId":4,)"
            R"("lemmaEntryId":5}]})"),
      apiOk(R"({"occurrences":[{"word":"と","isWordLike":true,"charStart":0,"charEnd":1,"entryId":6},)"
            R"({"word":"言った","lemma":"言う","isWordLike":true,"charStart":1,"charEnd":4,"entryId":7,)"
            R"("lemmaEntryId":8}]})")};
  api.lookupReplies = {apiOk(R"({"word":"本"})"), apiOk(R"({"word":"書く"})"), apiOk(R"({"word":"と"})")};
  LiveSource source(api, tap, page, {}, [&model](const TapContext& current) {
    TapContext t;
    t.sentence = lexipoint::text::buildSentenceAfter(model, *current.sentence, Script::Japanese);
    if (t.sentence) t.language.language = Language::Japanese;
    return t;
  });
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  source.advance();
  c.sourceChanged(0);
  EXPECT_TRUE(source.word(1).conjugation.empty());  // what follows 書け isn't on the card yet
  c.step(+1, 1000);
  c.step(+1, 1010);  // past 書け: the next cut loads
  while (source.hasWork(1100)) {
    source.advance(1100);
    c.sourceChanged(1100);
  }
  ASSERT_EQ(source.wordCount(), 4);
  EXPECT_EQ(source.word(1).conjugation, "imperative");  // 書け + と
  EXPECT_EQ(source.word(3).conjugation, "past");        // 言った, in the new cut
}

TEST(LiveSteps, AVerbEndingACutStaysUnnamedWhenThePieceAfterItHasNoWord) {
  // 書け ends the first cut; the rest of the sentence is only "……", which has no word, so the card goes on to the next
  // paragraph's sentence in its place. That one isn't 書け's neighbour: 書け isn't named from it.
  FakeApi api;
  PageModel model;
  TextLine line;
  line.tokens.push_back("本");
  for (int i = 0; i < 117; i++) line.tokens.push_back("あ");
  line.tokens.push_back("書け");
  line.tokens.push_back("……");
  line.startsParagraph = true;
  TextLine rain;
  rain.tokens = {"と", "雨", "が", "降る", "。"};
  rain.startsParagraph = true;
  model.lines = {line, rain};
  ReaderPage page;
  for (const TextLine& l : model.lines) {
    page.lines.push_back({100, {}});
    for (const std::string& t : l.tokens) page.lines.back().tokens.push_back({t, 20, 26});
  }
  TapContext tap;
  tap.sentence = buildSentence(model, {0, 0}, Script::Japanese);
  tap.language.language = Language::Japanese;
  ASSERT_TRUE(tap.sentence && tap.sentence->truncatedRight);
  api.analyzeReplies = {
      apiOk(R"({"occurrences":[{"word":"本","isWordLike":true,"charStart":0,"charEnd":1,"entryId":3},)"
            R"({"word":"書け","lemma":"書く","isWordLike":true,"charStart":118,"charEnd":120,"entryId":4,)"
            R"("lemmaEntryId":5}]})"),
      apiOk(R"({"occurrences":[]})"),
      apiOk(R"({"occurrences":[{"word":"と","isWordLike":true,"charStart":0,"charEnd":1,"entryId":6},)"
            R"({"word":"雨","isWordLike":true,"charStart":1,"charEnd":2,"entryId":7}]})")};
  api.lookupReplies = {apiOk(R"({"word":"本"})"), apiOk(R"({"word":"書く"})")};
  LiveSource source(api, tap, page, {}, [&model](const TapContext& current) {
    TapContext t;
    t.sentence = lexipoint::text::buildSentenceAfter(model, *current.sentence, Script::Japanese);
    if (t.sentence) t.language.language = Language::Japanese;
    return t;
  });
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  source.advance();
  c.sourceChanged(0);
  c.step(+1, 1000);
  c.step(+1, 1010);
  while (source.extending()) source.advance(1100);
  c.sourceChanged(1200);
  ASSERT_EQ(api.analyzed.size(), 3u);
  EXPECT_EQ(api.analyzed[1], "……");
  ASSERT_EQ(source.wordCount(), 4);
  EXPECT_TRUE(source.word(1).conjugation.empty());  // not named from と, the next paragraph's first word
}

namespace {

// The card on a word of 彼は本を読んだ。 (本 is saved: item 77), with its item scripted.
struct ItemCard {
  Rig rig;
  LiveSource source;
  CardController c;
  ShownTargets targets;
  PendingInput input;
  CardSession session;
  // Tapped at `line`, `token`.
  ItemCard(const size_t line, const size_t token, std::vector<lexipoint::api::ApiResponse> items,
           const char* analyze = kAnalyze)
      : source(rig.api, rig.tap(line, token), rig.page),
        c(source, ReadingMode::Kana),
        session(c, targets, input, &source) {
    rig.api.analyzeReplies = {apiOk(analyze)};
    rig.api.lookupReplies = {apiOk(R"({"word":"本"})")};
    rig.api.itemReplies = {items.begin(), items.end()};
    c.open(0);
  }
  // Every call the card has now; whether any answer asked for a redraw.
  bool drain(const unsigned long t) {
    bool redraw = false;
    while (session.hasWork(t)) redraw = session.apply(session.fetch(t), t).redraw || redraw;
    return redraw;
  }
};

constexpr int kHon = 2;  // 本's word on the card

}  // namespace

TEST(LiveItem, AskedOnceTheWordsPhaseBHasRunAndOnlyOnce) {
  ItemCard card(0, 2, {apiOk(R"({"notes":"古い本を読む。"})")});
  card.source.advance();  // A: the card's first frame
  EXPECT_TRUE(card.rig.api.looked.empty());
  EXPECT_TRUE(card.rig.api.items.empty());
  card.source.advance();  // B: the dictionary first
  EXPECT_EQ(card.rig.api.looked.size(), 1u);
  EXPECT_TRUE(card.rig.api.items.empty());
  card.source.takeShownChanged();
  ASSERT_TRUE(card.source.hasWork(0));
  EXPECT_EQ(card.source.advance(), LiveSource::Advance::Idle);  // the item: nothing but "Met before" changes
  ASSERT_EQ(card.rig.api.items.size(), 1u);
  EXPECT_EQ(card.rig.api.items[0].method, lexipoint::net::Method::Get);
  EXPECT_EQ(card.rig.api.items[0].path, "/v1/vocabulary/77");
  EXPECT_TRUE(card.source.takeShownChanged());  // the word on screen shows it now
  ASSERT_TRUE(card.source.word(kHon).metBefore);
  EXPECT_EQ(card.source.word(kHon).metBefore->text, "古い本を読む。");
  EXPECT_FALSE(card.source.hasWork(0));
  card.c.sourceChanged(0);
  ASSERT_TRUE(card.c.step(-1, 10));  // は, then back to 本: asked once per card
  card.drain(20);
  ASSERT_TRUE(card.c.step(+1, 30));
  card.drain(40);
  EXPECT_EQ(card.rig.api.items.size(), 1u);
  EXPECT_TRUE(card.source.word(kHon).metBefore);
}

TEST(LiveItem, OnlyForASavedWordTheCardIsOn) {
  ItemCard card(1, 0, {apiOk(R"({"notes":"古い本を読む。"})")});  // tapped 読んだ: not saved
  card.drain(0);
  EXPECT_TRUE(card.rig.api.items.empty());  // 本 is saved, but the card isn't on it
  EXPECT_FALSE(card.source.word(kHon).metBefore);
  ASSERT_TRUE(card.c.step(-1, 10));  // を
  card.drain(20);
  EXPECT_TRUE(card.rig.api.items.empty());
  ASSERT_TRUE(card.c.step(-1, 30));  // stepped onto 本: its lookup, then its item, and the card redrawn with it
  EXPECT_EQ(card.c.word(), kHon);
  card.session.apply(card.session.fetch(40), 40);  // B
  EXPECT_TRUE(card.rig.api.items.empty());
  EXPECT_TRUE(card.session.apply(card.session.fetch(50), 50).redraw);  // the item
  ASSERT_EQ(card.rig.api.items.size(), 1u);
  EXPECT_TRUE(card.source.word(kHon).metBefore);
  EXPECT_FALSE(card.session.hasWork(60));
}

TEST(LiveItem, AFailureIsQuietAndNotAskedAgain) {
  ItemCard card(0, 2, {apiFailure(ApiError::Timeout)});
  card.drain(0);
  ASSERT_EQ(card.rig.api.items.size(), 1u);
  EXPECT_FALSE(card.source.word(kHon).metBefore);  // "First time you've met this word."
  EXPECT_EQ(card.source.error(), ApiError::None);  // no error on the card
  EXPECT_EQ(card.c.state().toast, "");
  ASSERT_TRUE(card.c.step(-1, 10));
  card.drain(20);
  ASSERT_TRUE(card.c.step(+1, 30));
  card.drain(40);
  EXPECT_EQ(card.rig.api.items.size(), 1u);
}

TEST(LiveItem, AnUnreadableItemIsLoggedByItsStartAndNotAskedAgain) {
  ItemCard card(0, 2, {apiOk(R"(["not an item"])")});
  card.session.apply(card.session.fetch(0), 0);                                     // A
  card.session.apply(card.session.fetch(1), 1);                                     // B
  const CardSession::Answer answer = card.session.apply(card.session.fetch(2), 2);  // the item
  EXPECT_EQ(answer.unreadable, R"(["not an item"])");
  EXPECT_FALSE(answer.redraw);
  EXPECT_FALSE(card.source.word(kHon).metBefore);
  EXPECT_FALSE(card.session.hasWork(3));
}

TEST(LiveItem, AnItemWithNothingInItChangesNothingOnScreen) {
  ItemCard card(0, 2, {apiOk(R"({"item":{"notes":null,"user_tags":[]}})")});
  card.session.apply(card.session.fetch(0), 0);
  card.session.apply(card.session.fetch(1), 1);
  EXPECT_FALSE(card.session.apply(card.session.fetch(2), 2).redraw);
  EXPECT_EQ(card.rig.api.items.size(), 1u);
  EXPECT_FALSE(card.source.word(kHon).metBefore);
}

TEST(LiveItem, NeverAskedAsTheCardCloses) {
  ItemCard card(0, 2, {apiOk(R"({"notes":"古い本を読む。"})")});
  card.source.advance();  // A
  card.source.advance();  // B: the item is due now
  ASSERT_TRUE(card.source.hasWork(0));
  EXPECT_EQ(card.source.fetch(0, /*closing=*/true).kind, LiveSource::Fetched::Kind::None);
  // With a level change queued, closing sends the change and still asks for no item.
  card.source.queue({kHon, Level::Fresh, Level::Known, 0});
  const LiveSource::Fetched sent = card.source.fetch(0, /*closing=*/true);
  EXPECT_EQ(sent.kind, LiveSource::Fetched::Kind::Write);
  card.source.apply(sent, 0);
  EXPECT_TRUE(card.rig.api.items.empty());
}

TEST(LiveItem, TheSentenceTextWhenThereAreNoNotes) {
  // A word saved in the Lexirise app: no notes, its sentence in sentence_text; a plain-string tag.
  ItemCard card(0, 2,
                {apiOk(R"({"item":{"notes":"  ","sentence_text":"古い本を読む。","user_tags":["book:kokoro"]}})")});
  lexipoint::fakes::FakeFiles files;
  lexipoint::BookTagStore titles(files);
  titles.remember("kokoro", "Kokoro");
  card.source.setBookTitles(titles);
  card.drain(0);
  ASSERT_TRUE(card.source.word(kHon).metBefore);
  EXPECT_EQ(card.source.word(kHon).metBefore->text, "古い本を読む。");
  EXPECT_EQ(card.source.word(kHon).metBeforeBook, "Kokoro");
}

TEST(LiveItem, NotAskedWhenPhaseBFoundLexiriseOutOfReach) {
  ItemCard card(0, 2, {apiOk(R"({"notes":"古い本を読む。"})")});
  card.rig.api.lookupReplies = {apiFailure(ApiError::NoWifi)};
  card.drain(0);
  EXPECT_EQ(card.c.state().phase, Phase::Unanswered);
  EXPECT_TRUE(card.rig.api.items.empty());
}

TEST(LiveItem, AnIdThatCantGoInAPathIsntAsked) {
  std::string analyze = kAnalyze;
  const std::string id = R"("saved_expression_id":77)";
  analyze.replace(analyze.find(id), id.size(), R"("saved_expression_id":"7/../me")");
  ItemCard card(0, 2, {apiOk(R"({"notes":"古い本を読む。"})")}, analyze.c_str());
  card.drain(0);
  EXPECT_TRUE(card.rig.api.items.empty());
  EXPECT_FALSE(card.source.word(kHon).metBefore);
  EXPECT_FALSE(card.source.hasWork(0));  // and not asked again
}

TEST(LiveItem, ItsCopiesInLaterSentencesShowItWithoutAnotherCall) {
  // 読む is saved (item 901, from the app); the next sentence has it again, once under another entry id.
  TwoSentences rig;
  std::string first = kAnalyze;
  first.replace(first.find(R"("stateByEntryId":{)"), std::string(R"("stateByEntryId":{)").size(),
                R"("stateByEntryId":{"6":{"saved_expression_id":901,"proficiency":2},)");
  constexpr const char* kAgain =
      R"({"occurrences":[)"
      R"({"word":"雨","isWordLike":true,"charStart":0,"charEnd":1,"entryId":11},)"
      R"({"word":"読んだ","lemma":"読む","isWordLike":true,"charStart":1,"charEnd":4,"entryId":50,)"
      R"("lemmaEntryId":60}],"stateByEntryId":{"60":{"saved_expression_id":901,"proficiency":2}}})";
  rig.model.lines[1].tokens = {"雨", "読んだ", "。"};
  rig.api.analyzeReplies = {apiOk(first), apiOk(kAgain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  rig.api.itemReplies = {apiOk(R"({"notes":"昨日読んだ本。"})")};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  while (source.hasWork(0)) source.advance(0);  // A, B, the item
  c.sourceChanged(0);
  ASSERT_EQ(rig.api.items.size(), 1u);
  ASSERT_TRUE(source.word(4).metBefore);
  c.step(+1, 1000);  // into the next sentence
  while (source.extending()) source.advance(1100);
  c.sourceChanged(1100);
  ASSERT_EQ(source.wordCount(), 7);
  ASSERT_TRUE(c.step(+1, 1200));  // onto the copy
  while (source.hasWork(1300)) source.advance(1300);
  EXPECT_EQ(rig.api.items.size(), 1u);
  ASSERT_TRUE(source.word(6).metBefore);
  EXPECT_EQ(source.word(6).metBefore->text, "昨日読んだ本。");
}

TEST(LiveItem, AStepPastTheEndLoadsTheNextSentenceBeforeTheItem) {
  // On 読んだ (saved: item 901, the sentence's last word) after its phase B, before its item: a step past the end
  // loads the next sentence first, and the card moves on without the item; stepped back, it's asked then.
  TwoSentences rig;
  std::string first = kAnalyze;
  first.replace(first.find(R"("stateByEntryId":{)"), std::string(R"("stateByEntryId":{)").size(),
                R"("stateByEntryId":{"6":{"saved_expression_id":901,"proficiency":2},)");
  rig.api.analyzeReplies = {apiOk(first), apiOk(kAnalyzeRain)};
  rig.api.lookupReplies = {apiOk(kLookupYomu)};
  rig.api.itemReplies = {apiOk(R"({"notes":"昨日読んだ本。"})")};
  LiveSource source(rig.api, rig.tapped(4), rig.page, {}, rig.next());
  CardController c(source, ReadingMode::Kana);
  openOnTheLastWord(source, c);  // A, B: the item is due now
  ASSERT_TRUE(source.hasWork(0));
  c.step(+1, 1000);  // past the end
  EXPECT_TRUE(source.extending());
  source.advance(1100);
  EXPECT_EQ(rig.api.analyzed.size(), 2u);  // the next sentence, not the item
  EXPECT_TRUE(rig.api.items.empty());
  c.sourceChanged(1100);
  EXPECT_EQ(c.word(), 5);  // 雨
  while (source.hasWork(1200)) source.advance(1200);
  EXPECT_TRUE(rig.api.items.empty());  // the card left 読んだ before its item was asked
  ASSERT_TRUE(c.step(-1, 1300));       // back onto 読んだ
  while (source.hasWork(1400)) source.advance(1400);
  EXPECT_EQ(rig.api.items.size(), 1u);
  EXPECT_TRUE(source.word(4).metBefore);
}

TEST(LiveItem, ARefusalIsAskedAgainOnceTheBlockCanBeOver) {
  // A 429's back-off (or a rejected key) refuses the call without the network: not kept as "no item".
  lexipoint::api::ApiResponse limited = apiFailure(ApiError::RateLimited);
  limited.retryAfterS = 30;
  ItemCard card(0, 2, {limited, apiFailure(ApiError::Unauthorized), apiOk(R"({"notes":"古い本を読む。"})")});
  card.drain(0);
  ASSERT_EQ(card.rig.api.items.size(), 1u);
  EXPECT_FALSE(card.source.word(kHon).metBefore);
  EXPECT_FALSE(card.session.hasWork(29999));  // no call while blocked
  card.drain(30000);                          // asked again: a rejected key now (lifted only by a key check)
  ASSERT_EQ(card.rig.api.items.size(), 2u);
  const unsigned long later = 30000 + config::kRetryAfterDefaultS * 1000UL;
  EXPECT_FALSE(card.session.hasWork(later - 1));
  card.drain(later);
  EXPECT_EQ(card.rig.api.items.size(), 3u);
  EXPECT_TRUE(card.source.word(kHon).metBefore);
  EXPECT_FALSE(card.session.hasWork(later + 1));
}

TEST(LiveItem, ALevelChangeOnTheWordWaitsForItsItemAndKeepsMetBefore) {
  ItemCard card(0, 2, {apiOk(R"({"notes":"古い本を読む。"})")});
  card.rig.api.writeReplies = {apiOk("{}")};
  card.session.apply(card.session.fetch(0), 0);  // A
  card.session.apply(card.session.fetch(1), 1);  // B
  card.source.queue({kHon, Level::Fresh, Level::Known, 0});
  EXPECT_EQ(card.source.fetch(2).kind, LiveSource::Fetched::Kind::Item);  // the item, then the PATCH
  card.rig.api.items.clear();
  card.drain(3);
  ASSERT_EQ(card.rig.api.written.size(), 1u);
  EXPECT_EQ(card.rig.api.written[0].method, lexipoint::net::Method::Patch);
  EXPECT_EQ(card.rig.api.items.size(), 1u);  // the fetch above was only looked at, not applied: asked in the drain
  ASSERT_TRUE(card.source.word(kHon).metBefore);
  EXPECT_EQ(card.source.word(kHon).metBefore->text, "古い本を読む。");
  EXPECT_EQ(card.source.savedLevel(kHon), Level::Known);
}

TEST(LiveItem, ARefusalJustBeforeTheMillisWrapWaitsItOut) {
  // millis() is 32-bit on the device: a 429 (Retry-After 30 s) 5 s before it wraps is still blocked past the wrap.
  constexpr unsigned long kRefused = 0xFFFFFFFFUL - 5000;
  const auto wrapped = [](const unsigned long t) { return static_cast<unsigned long>(static_cast<uint32_t>(t)); };
  lexipoint::api::ApiResponse limited = apiFailure(ApiError::RateLimited);
  limited.retryAfterS = 30;
  ItemCard card(0, 2, {limited, apiOk(R"({"notes":"古い本を読む。"})")});
  card.session.apply(card.session.fetch(kRefused), kRefused);  // A
  card.session.apply(card.session.fetch(kRefused), kRefused);  // B
  card.session.apply(card.session.fetch(kRefused), kRefused);  // the item: refused
  ASSERT_EQ(card.rig.api.items.size(), 1u);
  EXPECT_FALSE(card.session.hasWork(wrapped(kRefused + 10000)));  // past the wrap, still in the back-off
  EXPECT_FALSE(card.session.hasWork(wrapped(kRefused + 29999)));
  card.drain(wrapped(kRefused + 30000));
  EXPECT_EQ(card.rig.api.items.size(), 2u);
  EXPECT_TRUE(card.source.word(kHon).metBefore);
}

TEST(LiveItem, APassedRetryTimeIsForgottenNotMisreadLater) {
  // Refused on 本, the card steps to は and stays there well past the retry time; long after (over 2^31 ms, when the
  // signed compare would read an old deadline as still ahead), back on 本 the item is asked at once.
  ItemCard card(0, 2, {apiFailure(ApiError::RateLimited), apiOk(R"({"notes":"古い本を読む。"})")});
  card.drain(0);
  ASSERT_EQ(card.rig.api.items.size(), 1u);
  ASSERT_TRUE(card.c.step(-1, 10));
  card.drain(20);
  const unsigned long passed = config::kRetryAfterDefaultS * 1000UL;
  EXPECT_FALSE(card.session.hasWork(passed));  // on は: nothing to ask, the retry time forgotten
  const unsigned long muchLater = passed + 0x80000000UL + 1000;
  ASSERT_TRUE(card.c.step(+1, muchLater));
  card.drain(muchLater);
  EXPECT_EQ(card.rig.api.items.size(), 2u);
  EXPECT_TRUE(card.source.word(kHon).metBefore);
}

TEST(LiveItem, AnItemLandingAfterTheCardSteppedOffFillsTheWordQuietly) {
  ItemCard card(0, 2, {apiOk(R"({"notes":"古い本を読む。"})")});
  card.session.apply(card.session.fetch(0), 0);  // A
  card.session.apply(card.session.fetch(1), 1);  // B
  LiveSource::Fetched item = card.session.fetch(2);
  ASSERT_EQ(item.kind, LiveSource::Fetched::Kind::Item);
  ASSERT_TRUE(card.c.step(-1, 3));                              // off 本 while the call ran
  EXPECT_FALSE(card.session.apply(std::move(item), 4).redraw);  // は on screen didn't change
  EXPECT_TRUE(card.source.word(kHon).metBefore);                // 本 has it
  card.drain(5);                                                // は's lookup
  ASSERT_TRUE(card.c.step(+1, 6));
  EXPECT_FALSE(card.session.hasWork(7));  // back on 本: nothing to ask
  EXPECT_EQ(card.rig.api.items.size(), 1u);
}

TEST(LiveItem, RemovingTheUsersOwnItemKeepsItsMetBefore) {
  // 本 was saved outside this card: its removal is a DELETE only (its notes stay in Lexirise), and so does its
  // "Met before"; its item isn't asked again.
  ItemCard card(0, 2, {apiOk(R"({"notes":"古い本を読む。"})")});
  card.rig.api.writeReplies = {apiOk("{}")};
  card.drain(0);
  ASSERT_TRUE(card.source.word(kHon).metBefore);
  card.source.queue({kHon, Level::Fresh, Level::None, 0});
  card.drain(1);
  ASSERT_EQ(card.rig.api.written.size(), 1u);
  EXPECT_EQ(card.rig.api.written[0].method, lexipoint::net::Method::Delete);
  EXPECT_EQ(card.source.savedLevel(kHon), Level::None);
  EXPECT_TRUE(card.source.word(kHon).metBefore);
  EXPECT_EQ(card.rig.api.items.size(), 1u);
}

TEST(LiveItem, ARefusalsRetryTimeStartsWhenTheAnswerCame) {
  // The call blocked 5 s (WiFi, TLS) before the 429: the retry time counts from its answer, as AccessPolicy's does.
  lexipoint::api::ApiResponse limited = apiFailure(ApiError::RateLimited);
  limited.retryAfterS = 30;
  ItemCard card(0, 2, {limited, apiOk(R"({"notes":"古い本を読む。"})")});
  card.session.apply(card.session.fetch(0), 0);  // A
  card.session.apply(card.session.fetch(1), 1);  // B
  LiveSource::Fetched refused = card.session.fetch(2);
  ASSERT_EQ(refused.kind, LiveSource::Fetched::Kind::Item);
  card.session.apply(std::move(refused), 5000);
  EXPECT_FALSE(card.session.hasWork(34999));
  EXPECT_TRUE(card.session.hasWork(35000));
}

// C17 Ignore (V5): the reader's own list ("stop marking this word on the page"), never Lexirise.

namespace {

constexpr int kIgnore = ActionId::Ignore;
constexpr int kYomuWord = 4;  // 読む's word on the card (Saving's tapped word: not saved)
constexpr const char* kIgnoredUndo = "Ignored: won't be marked again  \xC2\xB7  Undo";

// The card's ⋯ tab, and a tap on its Ignore row.
Outcome ignoreRow(Saving& s) {
  if (s.c.state().view != View::Expanded) s.tap(Target::RankRow);
  s.tap(Target::Tab, tabCount(Language::Japanese) - 1);
  return s.tap(Target::Action, kIgnore);
}

// The card on 本 (word 2: saved as 77, fresh), its phase B and item (offline: none) done.
void onHon(Saving& s) {
  s.step(-1);
  s.step(-1);
  s.drain();
  ASSERT_EQ(s.c.currentWord().word, "本");
}

// The activity's write of the card's ignores (CardSession::saveIgnores), the lock a plain call here.
CardSession::IgnoresSaved save(Saving& s, const Outcome& o, const lexipoint::IgnoredWordStore& store) {
  EXPECT_EQ(s.source.ignoredStore(), &store);  // the store the card was given is the one written
  bool locked = false;
  const CardSession::IgnoresSaved saved = s.session.saveIgnores(o, s.now, [&locked](auto&& f) {
    locked = true;
    f();
  });
  if (saved.failed) EXPECT_TRUE(locked);  // taken back under the lock (an eviction handed over takes it too)
  return saved;
}

// Every call the card made to Lexirise.
size_t calls(const FakeApi& api) {
  return api.analyzed.size() + api.analyzedWords.size() + api.looked.size() + api.written.size() + api.decked.size() +
         api.items.size();
}

}  // namespace

TEST(LiveIgnore, AnUnsavedWordGoesOnTheListAtOnceAndNothingIsSent) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  const size_t before = calls(s.rig.api);
  const Outcome o = ignoreRow(s);
  ASSERT_EQ(o.ignores.size(), 1u);
  EXPECT_EQ(o.ignores[0].word, kYomuWord);
  EXPECT_TRUE(o.ignores[0].ignored);
  EXPECT_TRUE(o.changes.empty());
  EXPECT_EQ(s.c.state().toast, kIgnoredUndo);
  EXPECT_TRUE(s.c.state().toastUndo);
  EXPECT_EQ(s.c.state().level, Level::None);  // still not saved: nothing in Lexirise changes
  EXPECT_TRUE(s.source.ignored(kYomuWord));   // for V9's marks
  EXPECT_FALSE(save(s, o, store).failed);
  EXPECT_EQ(files.files[config::kIgnoredPath], "ja:6\n");  // 読む's lemma entry
  s.drain();
  s.now += config::kToastMs * 10;
  s.drain();
  EXPECT_EQ(calls(s.rig.api), before);  // no call to Lexirise, then or later
}

TEST(LiveIgnore, ASavedWordKeepsItsLevelAndLexiriseIsntTouched) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  onHon(s);
  const size_t before = calls(s.rig.api);
  save(s, ignoreRow(s), store);
  EXPECT_EQ(s.c.state().level, Level::Fresh);
  EXPECT_TRUE(s.source.ignored(kHon));
  EXPECT_EQ(files.files[config::kIgnoredPath], "ja:3\n");
  s.now += config::kToastMs * 10;
  s.drain();
  EXPECT_EQ(calls(s.rig.api), before);
  EXPECT_TRUE(s.rig.api.written.empty());
}

TEST(LiveIgnore, UndoTakesItOffTheList) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  save(s, ignoreRow(s), store);
  const Outcome o = s.tap(Target::ToastUndo);
  ASSERT_EQ(o.ignores.size(), 1u);
  EXPECT_FALSE(o.ignores[0].ignored);
  EXPECT_TRUE(s.c.state().toast.empty());
  EXPECT_FALSE(s.source.ignored(kYomuWord));
  EXPECT_FALSE(save(s, o, store).failed);
  EXPECT_EQ(files.files[config::kIgnoredPath], "");
  EXPECT_TRUE(s.rig.api.written.empty());
}

TEST(LiveIgnore, AWordAlreadyIgnoredOnlySaysSo) {
  // On the list from an earlier card: the approved card shows nothing else (no row, no state word).
  lexipoint::fakes::FakeFiles files;
  files.files[config::kIgnoredPath] = "ja:6\n";
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  EXPECT_TRUE(s.source.ignored(kYomuWord));
  const Outcome o = ignoreRow(s);
  EXPECT_TRUE(o.ignores.empty());
  EXPECT_EQ(s.c.state().toast, "Ignored: won't be marked again");
  EXPECT_FALSE(s.c.state().toastUndo);
  EXPECT_FALSE(s.source.ignored(kHon));  // other words aren't
}

TEST(LiveIgnore, TheSameEntryElsewhereFollowsAndALevelChangeKeepsIt) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Rig rig;
  TextLine line;
  line.tokens = {"本", "と", "本", "。"};
  line.startsParagraph = true;
  rig.model.lines = {line};
  rig.page.lines = {{100, {{"本", 20, 26}, {"と", 46, 26}, {"本", 72, 26}, {"。", 98, 26}}}};
  rig.api.analyzeReplies = {
      apiOk(R"({"occurrences":[{"word":"本","isWordLike":true,"charStart":0,"charEnd":1,"entryId":3,"lemmaEntryId":3},)"
            R"({"word":"と","isWordLike":true,"charStart":1,"charEnd":2,"entryId":8},)"
            R"({"word":"本","isWordLike":true,"charStart":2,"charEnd":3,"entryId":3,"lemmaEntryId":3}]})")};
  rig.api.lookupReplies = {apiOk(R"({"word":"本","translations":[{"translation":"book"}]})")};
  rig.api.writeReplies = {apiOk(R"({"result":{"savedExpressionId":5}})")};
  LiveSource source(rig.api, rig.tap(0, 0), rig.page);
  source.setIgnoredWords(store);
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  source.advance(1);
  source.advance(2);
  c.sourceChanged(2);
  const Hit ignore{Target::Action, kIgnore, {}};
  EXPECT_EQ(c.tap(&ignore, 3).ignores.size(), 1u);
  EXPECT_TRUE(source.ignored(0));
  EXPECT_TRUE(source.ignored(2));  // one entry
  EXPECT_FALSE(source.ignored(1));
  const Hit tracked{Target::Level, 0, {}};  // a save: Lexirise as always, the ignore stays the reader's
  for (const LevelChange& ch : c.tap(&tracked, 10).changes) source.queue(ch);
  while (source.hasWork(10000)) source.advance(10000);
  ASSERT_EQ(rig.api.written.size(), 1u);
  EXPECT_EQ(rig.api.written[0].method, lexipoint::net::Method::Post);
  EXPECT_EQ(rig.api.written[0].body.find("suspended"), std::string::npos);
  EXPECT_TRUE(source.ignored(2));
}

TEST(LiveIgnore, AWordWithoutAnEntryIdIsKeptByItsForm) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Rig rig;
  rig.api.analyzeReplies = {apiOk(R"({"occurrences":[{"word":"彼","isWordLike":true,"charStart":0,"charEnd":1},)"
                                  R"({"word":"は","isWordLike":true,"charStart":1,"charEnd":2,"entryId":2}]})")};
  LiveSource source(rig.api, rig.tap(0, 0), rig.page);
  source.setIgnoredWords(store);
  source.advance(0);
  ASSERT_GE(source.wordCount(), 1);
  const auto key = source.ignoreKey(0);
  ASSERT_TRUE(key);
  EXPECT_EQ(key->entryId, 0u);
  EXPECT_EQ(key->text, "彼");
  EXPECT_TRUE(source.setIgnored(0, true));
  EXPECT_TRUE(source.ignored(0));
}

TEST(LiveIgnore, ACardSeesTheListAsItOpenedAndItsOwnChanges) {
  // Ignored on one card, it's ignored on the next (the file, as after a reboot).
  lexipoint::fakes::FakeFiles files;
  {
    lexipoint::IgnoredWordStore store(files);
    Saving s;
    s.source.setIgnoredWords(store);
    EXPECT_FALSE(save(s, ignoreRow(s), store).failed);
  }
  lexipoint::IgnoredWordStore rebooted(files);
  Saving next;
  next.source.setIgnoredWords(rebooted);
  EXPECT_TRUE(next.source.ignored(kYomuWord));
}

TEST(LiveIgnore, AListThatCantBeWrittenTakesTheIgnoreBack) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  files.failWriteOf = config::kIgnoredTmpPath;
  const Outcome o = ignoreRow(s);
  EXPECT_EQ(o.effect, Effect::Redraw);
  const CardSession::IgnoresSaved saved = save(s, o, store);
  EXPECT_TRUE(saved.failed);
  EXPECT_TRUE(saved.redraw);
  EXPECT_FALSE(s.source.ignored(kYomuWord));
  EXPECT_EQ(s.c.state().toast, "Save failed");
  EXPECT_FALSE(s.c.state().toastUndo);
  EXPECT_TRUE(s.rig.api.written.empty());
}

TEST(LiveIgnore, AnIgnoreThenCloseInOneBatchIsStillWritten) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  s.tap(Target::RankRow);
  s.tap(Target::Tab, tabCount(Language::Japanese) - 1);
  for (const Hit& h : s.targets.at(s.now)->hits) {
    if (h.target == Target::Action && h.index == kIgnore) s.input.tap(h.rect.x + 1, h.rect.y + 1, ++s.now);
  }
  s.input.home(++s.now);
  s.input.home(++s.now);
  const Outcome o = s.session.handleInput(s.now);
  EXPECT_EQ(o.effect, Effect::Close);
  EXPECT_FALSE(save(s, o, store).failed);
  EXPECT_EQ(files.files[config::kIgnoredPath], "ja:6\n");
  EXPECT_FALSE(s.session.hasPendingWrites());
}

TEST(LiveIgnore, LexiriseSuspendedIsntTheReadersIgnore) {
  // A word suspended in the Lexirise app (its item says so; not read): the card's Ignore is the local list.
  ItemCard card(0, 2, {apiOk(R"({"notes":null,"suspended":true})")});
  card.drain(0);
  EXPECT_EQ(card.rig.api.items.size(), 1u);
  EXPECT_FALSE(card.source.ignored(kHon));
}

TEST(LiveIgnore, AnUndoWhoseWriteFailsLeavesItIgnored) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  save(s, ignoreRow(s), store);
  files.failWriteOf = config::kIgnoredTmpPath;
  const CardSession::IgnoresSaved saved = save(s, s.tap(Target::ToastUndo), store);
  EXPECT_TRUE(saved.failed);
  EXPECT_TRUE(s.source.ignored(kYomuWord));  // still on the list, as the file says
  EXPECT_TRUE(store.contains({Language::Japanese, 6, {}}));
  EXPECT_EQ(s.c.state().toast, "Save failed");
  EXPECT_FALSE(s.c.state().toastUndo);
}

TEST(LiveIgnore, AFailureAsTheCardClosesNeedsNoRedraw) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  files.failWriteOf = config::kIgnoredTmpPath;
  Outcome o = ignoreRow(s);
  o.effect = Effect::Close;  // as when a close came in the same batch
  const CardSession::IgnoresSaved saved = save(s, o, store);
  EXPECT_TRUE(saved.failed);
  EXPECT_FALSE(saved.redraw);
  EXPECT_FALSE(s.source.ignored(kYomuWord));
}

TEST(LiveIgnore, AWordWithNoUsableKeyCantBeIgnored) {
  // No entry id and a form over kIgnoredTextMaxBytes (never cut): "Save failed", nothing to write.
  Rig rig;
  const std::string longWord(config::kIgnoredTextMaxBytes + 1, 'x');
  rig.api.analyzeReplies = {
      apiOk(R"({"occurrences":[{"word":")" + longWord + R"(","isWordLike":true,"charStart":0,"charEnd":1}]})")};
  LiveSource source(rig.api, rig.tap(0, 0), rig.page);
  CardController c(source, ReadingMode::Kana);
  c.open(0);
  source.advance(0);
  c.sourceChanged(0);
  ASSERT_EQ(source.wordCount(), 1);
  EXPECT_FALSE(source.ignoreKey(0));
  const Hit ignore{Target::Action, ActionId::Ignore, {}};
  const Outcome o = c.tap(&ignore, 1);
  EXPECT_TRUE(o.ignores.empty());
  EXPECT_EQ(c.state().toast, "Save failed");
  EXPECT_FALSE(source.ignored(0));
}

TEST(LiveIgnore, AnIgnoreReplacesASaveFailedRetry) {
  // Decided: like any later toast (a level tap's too), the Ignore's replaces "Save failed · Retry": its Retry goes.
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  s.rig.api.writeReplies = {apiFailure(ApiError::NoWifi)};
  s.level(1);
  s.drain();
  ASSERT_EQ(s.c.state().toast, "Save failed  \xC2\xB7  Retry");
  ignoreRow(s);
  EXPECT_EQ(s.c.state().toast, kIgnoredUndo);
  s.tap(Target::ToastUndo);  // the ignore's Undo, not the save's Retry
  EXPECT_FALSE(s.source.ignored(kYomuWord));
  s.drain();
  EXPECT_EQ(s.rig.api.written.size(), 1u);  // the failed POST only: no retry sent
}

TEST(LiveIgnore, AnUndoAfterSteppingAwayDoesNothing) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  save(s, ignoreRow(s), store);
  s.step(-1);  // the toast (and its Undo) belonged to 読む
  EXPECT_TRUE(s.c.state().toast.empty());
  const Hit undo{Target::ToastUndo, 0, {}};
  EXPECT_TRUE(s.c.tap(&undo, ++s.now).ignores.empty());
  EXPECT_TRUE(s.source.ignored(kYomuWord));
}

TEST(LiveIgnore, AnIgnoreAndItsUndoInOneBatchWriteNothing) {
  // Decided: only each word's last change in a batch is written, so a first write that would have failed never
  // runs, the end state matches the file, and no "Save failed" shows for a change the user took back. (On the
  // device the Undo needs the toast's frame, so both rarely come in one batch; saveIgnores doesn't count on it.)
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  const Hit ignore{Target::Action, kIgnore, {}};
  const Hit undo{Target::ToastUndo, 0, {}};
  Outcome batch = s.c.tap(&ignore, ++s.now);
  const Outcome undone = s.c.tap(&undo, ++s.now);
  batch.ignores.insert(batch.ignores.end(), undone.ignores.begin(), undone.ignores.end());
  ASSERT_EQ(batch.ignores.size(), 2u);
  files.failWriteOf = config::kIgnoredTmpPath;  // the ignore's write would fail
  EXPECT_FALSE(save(s, batch, store).failed);
  EXPECT_EQ(files.writes, 0);
  EXPECT_FALSE(s.source.ignored(kYomuWord));
  EXPECT_TRUE(s.c.state().toast.empty());
}

TEST(LiveIgnore, TwoWordsInOneBatchWithOneWriteFailing) {
  // Written in tap order; the fake fails one write, so 読む's (the first) fails and is taken back; 本's lands.
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  const Hit ignore{Target::Action, kIgnore, {}};
  Outcome batch = s.c.tap(&ignore, ++s.now);  // 読む
  s.c.step(-1, ++s.now);
  s.c.step(-1, ++s.now);  // 本
  ASSERT_EQ(s.c.word(), kHon);
  const Outcome hon = s.c.tap(&ignore, ++s.now);
  batch.ignores.insert(batch.ignores.end(), hon.ignores.begin(), hon.ignores.end());
  ASSERT_EQ(batch.ignores.size(), 2u);
  files.failWriteOf = config::kIgnoredTmpPath;
  const int writesBefore = files.writes;
  EXPECT_TRUE(save(s, batch, store).failed);
  EXPECT_TRUE(s.source.ignored(kHon));
  EXPECT_FALSE(s.source.ignored(kYomuWord));
  EXPECT_EQ(files.files[config::kIgnoredPath], "ja:3\n");
  EXPECT_EQ(s.c.state().toast, "Save failed");
  EXPECT_EQ(files.writes - writesBefore, 2);
}

TEST(LiveIgnore, AFailedIgnoresToastSurvivesAStepInTheSameBatch) {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  files.failWriteOf = config::kIgnoredTmpPath;
  save(s, ignoreRow(s), store);
  ASSERT_EQ(s.c.state().toast, "Save failed");
  s.step(-1);  // like a failed save's toast, it stays for its time
  EXPECT_EQ(s.c.state().toast, "Save failed");
}

TEST(LiveIgnore, WithoutAListTheCardCantIgnore) {
  Saving s;  // given no store
  EXPECT_EQ(s.source.ignoredStore(), nullptr);
  const Outcome o = ignoreRow(s);
  EXPECT_TRUE(o.ignores.empty());
  EXPECT_EQ(s.c.state().toast, "Save failed");
}

TEST(LiveIgnore, ADoubleTapKeepsTheUndo) {
  // A mis-tap mustn't turn the Undo into the plain toast (there's no un-ignore after it): in one batch and in two.
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  s.tap(Target::RankRow);
  s.tap(Target::Tab, tabCount(Language::Japanese) - 1);  // the ⋯ tab on screen
  const Hit ignore{Target::Action, kIgnore, {}};
  Outcome batch = s.c.tap(&ignore, ++s.now);
  const Outcome again = s.c.tap(&ignore, ++s.now);  // the same batch
  EXPECT_EQ(again.effect, Effect::None);
  EXPECT_TRUE(again.ignores.empty());
  EXPECT_TRUE(s.c.state().toastUndo);
  EXPECT_FALSE(save(s, batch, store).failed);
  s.show();
  const Outcome later = s.tap(Target::Action, kIgnore);  // the next batch, on the toast's frame
  EXPECT_TRUE(later.ignores.empty());
  EXPECT_TRUE(s.c.state().toastUndo);
  EXPECT_EQ(s.c.state().toast, kIgnoredUndo);
  EXPECT_EQ(s.tap(Target::ToastUndo).ignores.size(), 1u);  // and the Undo still works
  EXPECT_FALSE(s.source.ignored(kYomuWord));
}

TEST(LiveIgnore, TheKeyIsPerPassAKnownLimit) {
  // なれない: ‹なれる› in one pass, ‹なる› in the other (lexirise-api-notes.md): two entry keys, so an ignore under one
  // doesn't cover the other (C17 As built's known limits).
  lexipoint::api::Occurrence fast;
  fast.entryId = 20;
  fast.lemmaEntryId = 21;  // なれる
  lexipoint::api::Occurrence refined = fast;
  refined.lemmaEntryId = 22;  // なる
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  const auto key = [](const lexipoint::api::Occurrence& occ) {
    return *lexipoint::ignoredKeyFor(Language::Japanese, lexipoint::lookup::entryKeyOf(occ), "なる");
  };
  EXPECT_EQ(store.write(key(fast), true), lexipoint::IgnoredWordStore::Write::Written);
  EXPECT_TRUE(store.contains(key(fast)));
  EXPECT_FALSE(store.contains(key(refined)));
}

TEST(LiveIgnore, TwoIgnoresInOneBatchAtTheCapThenUndo) {
  // Written in tap order (the file stays newest last); the toast's Undo (本's, the newest) brings back exactly the
  // key 本's ignore pushed out.
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWords full;
  for (uint32_t id = 1000; id < 1000 + config::kIgnoredIdsMax; id++) full.add({Language::Japanese, id, {}});
  files.files[config::kIgnoredPath] = lexipoint::serializeIgnored(full);
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  s.tap(Target::RankRow);
  s.tap(Target::Tab, tabCount(Language::Japanese) - 1);
  const Hit ignore{Target::Action, kIgnore, {}};
  Outcome batch = s.c.tap(&ignore, ++s.now);  // 読む
  s.c.step(-1, ++s.now);
  s.c.step(-1, ++s.now);  // 本
  ASSERT_EQ(s.c.word(), kHon);
  const Outcome hon = s.c.tap(&ignore, ++s.now);
  batch.ignores.insert(batch.ignores.end(), hon.ignores.begin(), hon.ignores.end());
  EXPECT_FALSE(save(s, batch, store).failed);
  const std::string text = files.files[config::kIgnoredPath];
  EXPECT_EQ(text.substr(text.size() - std::string("ja:6\nja:3\n").size()), "ja:6\nja:3\n");  // 読む, then 本
  EXPECT_FALSE(store.contains({Language::Japanese, 1000, {}}));                              // 読む pushed out 1000
  EXPECT_FALSE(store.contains({Language::Japanese, 1001, {}}));                              // 本 pushed out 1001
  s.show();
  const Outcome undo = s.tap(Target::ToastUndo);
  ASSERT_EQ(undo.ignores.size(), 1u);
  ASSERT_TRUE(undo.ignores[0].restore);
  EXPECT_EQ(undo.ignores[0].restore->entryId, 1001u);
  EXPECT_FALSE(save(s, undo, store).failed);
  EXPECT_TRUE(store.contains({Language::Japanese, 1001, {}}));  // back, as oldest
  EXPECT_FALSE(store.contains({Language::Japanese, 3, {}}));
  EXPECT_TRUE(store.contains({Language::Japanese, 6, {}}));
  EXPECT_EQ(files.files[config::kIgnoredPath].substr(0, 10), "ja:1001\nja");
}

namespace {

// A full list (ids 1000…1999, 1000 the oldest), a card on 読む given it, the ⋯ tab on screen.
struct FullList {
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store{files};
  Saving s;
  FullList() {
    lexipoint::IgnoredWords full;
    for (uint32_t id = 1000; id < 1000 + config::kIgnoredIdsMax; id++) full.add({Language::Japanese, id, {}});
    files.files[config::kIgnoredPath] = lexipoint::serializeIgnored(full);
    s.source.setIgnoredWords(store);
    s.tap(Target::RankRow);
    s.tap(Target::Tab, tabCount(Language::Japanese) - 1);
  }
  bool has(const uint32_t id) { return store.contains({Language::Japanese, id, {}}); }
  size_t size() { return store.list().size(); }
  // One batch of taps (I: Ignore this word, U: the toast's Undo), then its write.
  void batch(const std::string& taps) {
    const Hit ignore{Target::Action, kIgnore, {}};
    const Hit undo{Target::ToastUndo, 0, {}};
    Outcome all;
    for (const char t : taps) {
      const Outcome o = s.c.tap(t == 'I' ? &ignore : &undo, ++s.now);
      all.ignores.insert(all.ignores.end(), o.ignores.begin(), o.ignores.end());
    }
    EXPECT_FALSE(save(s, all, store).failed) << taps;
  }
};

}  // namespace

TEST(LiveIgnore, AnUndoThenIgnoreAgainInOneBatchAtTheCapKeepsTheOldest) {
  FullList f;
  f.batch("I");  // 読む (6) in, 1000 out
  ASSERT_FALSE(f.has(1000));
  f.batch("UI");  // its Undo brings 1000 back, and the Ignore again pushes it out again, for its own Undo
  EXPECT_TRUE(f.has(6));
  EXPECT_FALSE(f.has(1000));
  EXPECT_EQ(f.size(), config::kIgnoredIdsMax);
  f.batch("U");
  EXPECT_FALSE(f.has(6));
  EXPECT_TRUE(f.has(1000));  // back, as oldest
  EXPECT_EQ(f.size(), config::kIgnoredIdsMax);
  EXPECT_EQ(f.files.files[config::kIgnoredPath].substr(0, 8), "ja:1000\n");
}

TEST(LiveIgnore, EveryShortBatchAtTheCapLosesNoWord) {
  // Any sequence of Ignore / Undo taps, in one batch or two, on a full list: never a word lost or one too many; the
  // word and the key it pushed out are never both on the list, and the card agrees with the file.
  for (const std::string& first : {"", "I"}) {
    for (int length = 1; length <= 4; length++) {
      for (int bits = 0; bits < (1 << length); bits++) {
        std::string taps;
        for (int i = 0; i < length; i++) taps += (bits >> i) & 1 ? 'U' : 'I';
        FullList f;
        if (!first.empty()) f.batch(first);
        f.batch(taps);
        const std::string at = first + "|" + taps;
        EXPECT_EQ(f.size(), config::kIgnoredIdsMax) << at;
        EXPECT_NE(f.has(6), f.has(1000)) << at;
        EXPECT_EQ(f.s.source.ignored(kYomuWord), f.has(6)) << at;
        if (f.s.c.state().toastUndo) {  // its Undo, in a later batch, still brings everything back
          f.batch("U");
          EXPECT_FALSE(f.has(6)) << at;
          EXPECT_TRUE(f.has(1000)) << at;
          EXPECT_EQ(f.size(), config::kIgnoredIdsMax) << at;
        }
      }
    }
  }
}

TEST(LiveIgnore, TheIgnoresUndoLastsLongerThanASaves) {
  // config::kIgnoreToastMs: an ignore can't be undone on the card once its toast is gone; a save keeps kToastMs.
  static_assert(config::kIgnoreToastMs > config::kToastMs, "the Ignore's Undo is offered longer");
  lexipoint::fakes::FakeFiles files;
  lexipoint::IgnoredWordStore store(files);
  Saving s;
  s.source.setIgnoredWords(store);
  const Hit ignore{Target::Action, kIgnore, {}};
  const unsigned long at = ++s.now;
  s.c.tap(&ignore, at);
  s.c.tick(at + config::kIgnoreToastMs - 100);  // 4.9 s: still there
  EXPECT_TRUE(s.c.state().toastUndo);
  EXPECT_EQ(s.c.state().toast, kIgnoredUndo);
  s.c.tick(at + config::kIgnoreToastMs);  // 5 s: gone
  EXPECT_TRUE(s.c.state().toast.empty());
  EXPECT_FALSE(s.c.state().toastUndo);
  // A save's toast keeps kToastMs.
  const Hit learning{Target::Level, 1, {}};
  const unsigned long saved = at + config::kIgnoreToastMs + 10;
  s.c.tap(&learning, saved);
  s.c.tick(saved + config::kToastMs - 1);
  EXPECT_TRUE(s.c.state().toastUndo);
  s.c.tick(saved + config::kToastMs);
  EXPECT_TRUE(s.c.state().toast.empty());
  // The plain already-ignored toast and "Save failed" aren't the Ignore's Undo: they keep their own times.
  const unsigned long again = saved + config::kToastMs + 10;
  s.c.tap(&ignore, again);
  EXPECT_EQ(s.c.state().toast, "Ignored: won't be marked again");
  s.c.tick(again + config::kToastMs);
  EXPECT_TRUE(s.c.state().toast.empty());
}
