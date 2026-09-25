// The live card source (LiveSource.h) with the controller: phase 0, the analysis (A), the lookup (B),
// stepping, and the answers that close the card. A scripted Lexirise; synthetic data.

#include <gtest/gtest.h>

#include "FakeApi.h"
#include "FakeMetrics.h"
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
  explicit Saving(const bool complete = true)
      : source(rig.api, rig.tap(1, 0), rig.page, {"xteink"}),
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
  while (source.hasPendingWrites()) source.apply(source.fetch(2200, /*closing=*/true));
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
  const std::string firstPass = std::string(kAnalyze).insert(std::string(kAnalyze).size() - 1, R"(,"morphoPending":true)");
  rig.api.analyzeReplies = {apiOk(firstPass),
                            apiOk(R"({"occurrences":[{"word":"雨","isWordLike":true,"charStart":0,"charEnd":1,)"
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
  r.rig.api.wordsReplies = {apiOk(
      R"({"occurrences":[{"word":"一边","isWordLike":true,"charStart":0,"charEnd":2,"entryId":20},)"
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
