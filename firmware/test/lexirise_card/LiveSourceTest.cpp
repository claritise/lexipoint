// The live card source (LiveSource.h) with the controller: phase 0, the analysis (A), the lookup (B),
// stepping, and the answers that close the card. A scripted Lexirise; synthetic data.

#include <gtest/gtest.h>

#include "FakeApi.h"
#include "FakeMetrics.h"
#include "lexirise/card/CardController.h"
#include "lexirise/card/CardFrame.h"
#include "lexirise/card/CardSession.h"
#include "lexirise/card/LiveSource.h"
#include "lexirise/lookup/Fallback.h"

using namespace lexipoint::card;
using lexipoint::Language;
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
  EXPECT_TRUE(c.sourceChanged());
  EXPECT_EQ(c.state().phase, Phase::Analyzed);
  EXPECT_EQ(c.word(), 4);
  EXPECT_EQ(c.currentWord().word, "読む");
  EXPECT_EQ(c.currentWord().reading, "よむ");
  EXPECT_EQ(source.scene(c.word(), true, kMetrics, c.highlightCodepoints()).page.commands.at(1).text, "読んだ");
  EXPECT_TRUE(rig.api.looked.empty());

  EXPECT_EQ(source.advance(), LiveSource::Advance::Changed);  // B
  EXPECT_EQ(rig.api.looked, std::vector<std::string>{"読む"});
  EXPECT_TRUE(c.sourceChanged());
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
  c.sourceChanged();
  source.advance();
  c.sourceChanged();
  ASSERT_TRUE(c.step(-1, 0));  // を
  EXPECT_EQ(c.state().phase, Phase::Analyzed);
  EXPECT_TRUE(source.hasWork(0));
  source.advance();
  c.sourceChanged();
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
  c.sourceChanged();
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
    targets.drawing(composeFrame(c, kMetrics).card.hits, c.steps());
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
  targets.drawing(composeFrame(c, kMetrics).card.hits, c.steps());  // phase 0
  targets.shown(10);
  session.apply(session.fetch(20), 20);  // A arrives; its frame starts refreshing
  targets.drawing(composeFrame(c, kMetrics).card.hits, c.steps());
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
  using Kind = LiveOutcome::Kind;
  for (const bool starDict : {false, true}) {
    EXPECT_EQ(afterCard(LiveOutcome{Kind::Closed}, starDict), AfterCard::Redraw);
    EXPECT_EQ(afterCard(LiveOutcome{Kind::NotFound}, starDict), AfterCard::NotFound);  // a Lexirise miss is final
    LiveOutcome unsent{Kind::Closed};
    unsent.unsentSaves = 1;
    EXPECT_EQ(afterCard(unsent, starDict), AfterCard::UnsentSave);  // never fails silently
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
