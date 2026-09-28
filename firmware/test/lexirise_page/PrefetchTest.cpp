// v0.2 V7b: the reader's page analysis (page/Prefetch.h) and its hourly count (api/RequestWindow.h), with a scripted
// Lexirise and the in-memory card.

#include <gtest/gtest.h>

#include <limits>
#include <map>
#include <string>

#include "FakeApi.h"
#include "Fakes.h"
#include "lexirise/api/RequestWindow.h"
#include "lexirise/page/PageSentences.h"
#include "lexirise/page/Prefetch.h"
#include "lexirise/vocab/VocabMirror.h"

using lexipoint::Language;
using lexipoint::api::ApiError;
using lexipoint::fakes::apiFailure;
using lexipoint::fakes::apiOk;
using lexipoint::fakes::FakeApi;
using lexipoint::fakes::FakeFiles;
using namespace lexipoint::page;
namespace config = lexipoint::config;

namespace {

constexpr unsigned long kShownMs = 10000;
constexpr unsigned long kDue = kShownMs + config::kPagePrefetchDwellMs;
uint32_t wallS = 0;
uint32_t wallNow() { return wallS; }

// A call's abort: no input as the step starts (its first question), then input 800 ms into the call.
int inputAsked = 0;
bool inputDuringTheCall() {
  if (inputAsked++ == 0) return false;
  lexipoint::fakes::FakeClock::nowMs = kDue + 800;
  return true;
}

std::string answer(const bool pending, const char* word = "猫", const bool saved = false) {
  return std::string(R"({"occurrences":[{"word":")") + word +
         R"(","entryId":7,"charStart":0,"charEnd":1,"isWordLike":true}],"morphoPending":)" +
         (pending ? "true" : "false") + R"(,"entryMetaById":{"7":{"rank":12}},"stateByEntryId":{)" +
         (saved ? R"("7":{"saved_expression_id":70,"proficiency":2})" : "") + "}}";
}

struct Texts final : PageTexts {
  std::map<int, PageText> pages;
  int asked = 0;
  std::optional<PageText> textOf(const int which) override {
    asked++;
    const auto it = pages.find(which);
    if (it == pages.end()) return std::nullopt;
    return it->second;
  }
};

PageText text(const uint32_t start, const std::string& body = "猫") {
  return PageText{PageKey{1, 2, start}, Language::Japanese, body, 1};
}

Conditions ok() {
  Conditions c;
  c.wifiUp = true;
  c.usable = true;
  return c;
}

struct Rig {
  FakeFiles files;
  FakeApi api;
  PageStore store{files};
  PagePrefetcher prefetch{api, store, lexipoint::fakes::FakeClock::now, wallNow};
  Texts texts;
  Rig() {
    texts.pages[0] = text(0);
    texts.pages[1] = text(100);
    prefetch.shown(2, 0, kShownMs);
  }
  PagePrefetcher::Step step(const unsigned long now = kDue) {
    lexipoint::fakes::FakeClock::nowMs = now;
    return prefetch.step(texts, nullptr);
  }
};

}  // namespace

TEST(Prefetch, NothingIsDueBeforeThePageHasBeenUpItsDwell) {
  Rig r;
  EXPECT_FALSE(r.prefetch.due(kDue - 1, ok()));
  EXPECT_TRUE(r.prefetch.due(kDue, ok()));
  r.prefetch.shown(2, 1, kDue);  // the page turned: the dwell starts again (pages turned faster: never analyzed)
  EXPECT_FALSE(r.prefetch.due(kDue + 1, ok()));
  r.prefetch.shown(2, 1, kDue);  // the same drawing, told again on the next pass: nothing changes
  EXPECT_FALSE(r.prefetch.due(kDue + config::kPagePrefetchDwellMs - 1, ok()));
  EXPECT_TRUE(r.prefetch.due(kDue + config::kPagePrefetchDwellMs, ok()));
}

TEST(Prefetch, OnlyOverWiFiAlreadyUpNeverBusyUsableUnblockedAndWithinBudget) {
  Rig r;
  Conditions c = ok();
  EXPECT_TRUE(r.prefetch.due(kDue, c));
  c.wifiUp = false;  // "Only if already on": nothing brings WiFi up for it
  EXPECT_FALSE(r.prefetch.due(kDue, c));
  c = ok();
  c.busy = true;
  EXPECT_FALSE(r.prefetch.due(kDue, c));
  c = ok();
  c.usable = false;
  EXPECT_FALSE(r.prefetch.due(kDue, c));
  c = ok();
  c.blocked = true;
  EXPECT_FALSE(r.prefetch.due(kDue, c));
  c = ok();
  c.usedLastHour = 839;  // 70% of 1200 is 840
  EXPECT_TRUE(r.prefetch.due(kDue, c));
  c.usedLastHour = 840;
  EXPECT_FALSE(r.prefetch.due(kDue, c));
  c.rateLimit = 2400;  // /v1/me said more
  EXPECT_TRUE(r.prefetch.due(kDue, c));
}

TEST(Prefetch, ThePageOnScreenThenTheNextAreAnalyzedOnceEachAndKept) {
  Rig r;
  r.api.pageReplies = {apiOk(answer(true))};
  const auto first = r.step();
  EXPECT_EQ(first.kind, PagePrefetcher::Step::Kind::Analyzed);
  EXPECT_EQ(first.which, 0);
  EXPECT_TRUE(first.written);
  EXPECT_FALSE(first.refined);
  const auto second = r.step();
  EXPECT_EQ(second.kind, PagePrefetcher::Step::Kind::Analyzed);
  EXPECT_EQ(second.which, 1);
  EXPECT_FALSE(r.prefetch.due(kDue, ok()));  // both done
  ASSERT_EQ(r.api.pageRequests.size(), 2u);
  EXPECT_NE(r.api.pageRequests[0].body.find("\"text\":\"猫\""), std::string::npos);
  EXPECT_EQ(r.api.pageRequests[0].body.find("fast"), std::string::npos);  // the default mode, not fast
  EXPECT_TRUE(r.store.read(PageKey{1, 2, 100}, Language::Japanese, 1, textHash("猫")));
}

TEST(Prefetch, AKeptPageIsntAskedAgain) {
  Rig r;
  r.api.pageReplies = {apiOk(answer(true))};
  r.step();
  r.step();
  r.prefetch.shown(2, 1, kShownMs + 5000);  // the next page: its own text was kept a page ago
  r.texts.pages[0] = text(100);
  r.texts.pages.erase(1);  // the section's last page
  const auto shown = r.step(kShownMs + 5000 + config::kPagePrefetchDwellMs);
  EXPECT_EQ(shown.kind, PagePrefetcher::Step::Kind::Cached);
  const auto next = r.step(kShownMs + 5000 + config::kPagePrefetchDwellMs);
  EXPECT_EQ(next.kind, PagePrefetcher::Step::Kind::NoText);
  EXPECT_EQ(r.api.pageRequests.size(), 2u);
}

TEST(Prefetch, ARefinedAnswerGetsItsWordLevelSplitAndTheMerge) {
  Rig r;
  r.api.pageReplies = {apiOk(answer(false)), apiOk(answer(true))};
  const auto s = r.step();
  EXPECT_EQ(s.kind, PagePrefetcher::Step::Kind::Analyzed);
  EXPECT_TRUE(s.refined);
  EXPECT_EQ(s.calls, 2u);
  ASSERT_EQ(r.api.pageRequests.size(), 2u);
  EXPECT_NE(r.api.pageRequests[1].body.find("\"fast\":true"), std::string::npos);
  const auto kept = r.store.read(PageKey{1, 2, 0}, Language::Japanese, 1, textHash("猫"));
  ASSERT_TRUE(kept);
  EXPECT_TRUE(kept->refined);
}

TEST(Prefetch, ARefinedAnswerWhoseSplitFailsIsntKeptAndWaits) {
  Rig r;
  r.api.pageReplies = {apiOk(answer(false)), apiFailure(ApiError::Timeout)};
  const auto s = r.step();
  EXPECT_EQ(s.kind, PagePrefetcher::Step::Kind::Failed);
  EXPECT_FALSE(r.store.read(PageKey{1, 2, 0}, Language::Japanese, 1, textHash("猫")));
  EXPECT_FALSE(r.prefetch.due(kDue + config::kPageFailureWaitMs - 1, ok()));
  EXPECT_TRUE(r.prefetch.due(kDue + config::kPageFailureWaitMs, ok()));
}

TEST(Prefetch, AFailureWaitsAndA429WaitsItsOwnTime) {
  Rig r;
  auto limited = apiFailure(ApiError::RateLimited);
  limited.retryAfterS = 600;
  r.api.pageReplies = {limited};
  EXPECT_EQ(r.step().kind, PagePrefetcher::Step::Kind::Failed);
  EXPECT_FALSE(r.prefetch.due(kDue + config::kPageFailureWaitMs, ok()));
  EXPECT_TRUE(r.prefetch.due(kDue + 600000, ok()));
}

TEST(Prefetch, AnAnswerGivenUpForInputWaitsAWholeDwellAgain) {
  Rig r;
  r.api.pageReplies = {apiOk(answer(true))};
  r.api.pageAbortAfter = 1;
  lexipoint::fakes::FakeClock::nowMs = kDue;
  // Input comes 800 ms into the call: the dwell starts again from then, not from the call's start.
  inputAsked = 0;
  const auto s = r.prefetch.step(r.texts, inputDuringTheCall);
  EXPECT_EQ(s.kind, PagePrefetcher::Step::Kind::Cancelled);
  EXPECT_FALSE(r.store.read(PageKey{1, 2, 0}, Language::Japanese, 1, textHash("猫")));
  EXPECT_FALSE(r.prefetch.due(kDue + 800 + config::kPagePrefetchDwellMs - 1, ok()));
  EXPECT_TRUE(r.prefetch.due(kDue + 800 + config::kPagePrefetchDwellMs, ok()));
}

TEST(Prefetch, NoTextOrTooMuchIsntSent) {
  Rig r;
  r.texts.pages.erase(0);
  EXPECT_EQ(r.step().kind, PagePrefetcher::Step::Kind::NoText);
  r.texts.pages[1].units = config::kPageMaxTextUnits + 1;
  EXPECT_EQ(r.step().kind, PagePrefetcher::Step::Kind::NoText);
  EXPECT_TRUE(r.api.pageRequests.empty());
}

TEST(Prefetch, ThePagesSavedStatesGoToALoadedMirrorOnly) {
  Rig r;
  lexipoint::vocab::VocabStore mirror(r.files);
  r.prefetch.setMirror(&mirror);
  r.api.pageReplies = {apiOk(answer(true, "猫", true))};
  r.step();
  EXPECT_FALSE(mirror.find(Language::Japanese, 7));  // not loaded: its few pending slots stay the cards'
  mirror.load(Language::Japanese);
  r.step();
  const auto entry = mirror.find(Language::Japanese, 7);
  ASSERT_TRUE(entry);
  EXPECT_EQ(entry->savedId, 70u);
  EXPECT_EQ(entry->proficiency, 2);
}

// --- the hourly count ---

TEST(RequestWindow, CountsTheLastHourToTheMinute) {
  lexipoint::api::RequestWindow w;
  const unsigned long start = 5000;
  for (int i = 0; i < 10; i++) w.add(start + static_cast<unsigned long>(i) * 1000UL);
  EXPECT_EQ(w.count(start + 59UL * 60000UL), 10u);
  w.add(start + 59UL * 60000UL);
  EXPECT_EQ(w.count(start + 61UL * 60000UL), 1u);  // the first ten are past the hour
  EXPECT_EQ(w.count(start + 200UL * 60000UL), 0u);
}

// --- a page as the reader lays it out ---

TEST(DescribePage, ItsTextKeyAndLanguage) {
  lexipoint::text::PageModel model;
  model.lines.push_back(lexipoint::text::TextLine{{"猫が", "鳴いた。"}, true});
  model.lines.push_back(lexipoint::text::TextLine{{"犬も。"}, false});
  lexipoint::Settings settings;
  const auto tagged =
      describePage(model, lexipoint::text::BookLanguage("ja", std::nullopt), settings, "/b.epub", 4, 99);
  ASSERT_TRUE(tagged);
  EXPECT_EQ(tagged->page.text, "猫が鳴いた。犬も。");
  EXPECT_EQ(tagged->page.units, 9u);
  EXPECT_EQ(tagged->page.language, Language::Japanese);
  EXPECT_EQ(tagged->page.key, (PageKey{bookKey("/b.epub"), 4, 99}));
  // A book that doesn't say: the page's own text decides (kana: Japanese).
  const auto untagged =
      describePage(model, lexipoint::text::BookLanguage("", std::nullopt), settings, "/b.epub", 4, 99);
  ASSERT_TRUE(untagged);
  EXPECT_EQ(untagged->page.language, Language::Japanese);
  settings.japanese.enabled = false;  // Japanese switched off: no page analysis
  EXPECT_FALSE(describePage(model, lexipoint::text::BookLanguage("ja", std::nullopt), settings, "/b.epub", 4, 99));
  EXPECT_FALSE(describePage(lexipoint::text::PageModel{}, lexipoint::text::BookLanguage("ja", std::nullopt),
                            lexipoint::Settings{}, "/b.epub", 4, 99));
}

// --- review round 1 ---

TEST(Prefetch, ThePageDrawnAgainRestartsItsDwellButKeepsWhatsDone) {
  Rig r;
  r.api.pageReplies = {apiOk(answer(true))};
  r.step();                                   // this page done
  const unsigned long closed = kDue + 20000;  // a card closed over the page: it's drawn again
  r.prefetch.shown(2, 0, closed);
  EXPECT_FALSE(r.prefetch.due(closed + 5, ok()));  // not the moment the reader is back on the page
  EXPECT_TRUE(r.prefetch.due(closed + config::kPagePrefetchDwellMs, ok()));
  EXPECT_EQ(r.step(closed + config::kPagePrefetchDwellMs).which, 1);  // the next page, this one isn't asked again
  EXPECT_EQ(r.api.pageRequests.size(), 2u);
}

TEST(Prefetch, TheDwellHoldsAcrossTheMillisWrap) {
  Rig r;
  const unsigned long nearWrap = std::numeric_limits<unsigned long>::max() - 500;
  r.prefetch.shown(2, 5, nearWrap);
  EXPECT_FALSE(r.prefetch.due(nearWrap + 1000, ok()));  // wrapped, 1000 ms on
  EXPECT_TRUE(r.prefetch.due(nearWrap + config::kPagePrefetchDwellMs, ok()));
}

TEST(Prefetch, AnAnswerThatCantBeReadOrKeptIsntAskedAgainOnThisPage) {
  Rig r;
  r.api.pageReplies = {apiOk("{\"not\":1}"), apiOk(answer(true))};
  const auto bad = r.step();
  EXPECT_EQ(bad.kind, PagePrefetcher::Step::Kind::Unusable);
  EXPECT_EQ(r.step().which, 1);  // on to the next page at once: no failure's wait, not asked again
  EXPECT_EQ(r.api.pageRequests.size(), 2u);
}

TEST(Prefetch, APageOverACapIsntKeptNorAskedAgain) {
  Rig r;
  // Words of 250 bytes: past kPageMaxPoolBytes before the answer ends (a merge of two answers can pass a cap too:
  // the file is refused then, fitsFile).
  std::string big = "{\"occurrences\":[";
  const std::string word(250, 'a');
  for (size_t i = 0; i * word.size() <= config::kPageMaxPoolBytes; i++) {
    big += std::string(i ? "," : "") + "{\"word\":\"" + word + "\",\"charStart\":" + std::to_string(i) +
           ",\"charEnd\":" + std::to_string(i + 1) + "}";
  }
  big += "],\"morphoPending\":true}";
  r.api.pageReplies = {apiOk(big), apiOk(answer(true))};
  EXPECT_EQ(r.step().kind, PagePrefetcher::Step::Kind::Unusable);
  EXPECT_EQ(r.step().which, 1);
  EXPECT_EQ(r.api.pageRequests.size(), 2u);
  PageAnalysis tooBig;
  tooBig.textUnits = config::kPageMaxTextUnits + 1;
  EXPECT_FALSE(fitsFile(tooBig));
  tooBig.textUnits = 1;
  tooBig.pool.assign(config::kPageMaxPoolBytes + 1, 'x');
  EXPECT_FALSE(fitsFile(tooBig));
  EXPECT_FALSE(r.store.write(PageKey{1, 2, 7}, tooBig));
  EXPECT_FALSE(r.files.exists(pagePath(PageKey{1, 2, 7}).c_str()));
}

TEST(RequestWindow, CountsTheLastHourAcrossTheMillisWrap) {
  lexipoint::api::RequestWindow w;
  const unsigned long start = std::numeric_limits<unsigned long>::max() - 30UL * 60000UL;  // half an hour before
  for (int i = 0; i < 10; i++) w.add(start + static_cast<unsigned long>(i) * 1000UL);
  EXPECT_EQ(w.count(start + 59UL * 60000UL), 10u);  // wrapped
  w.add(start + 59UL * 60000UL);
  EXPECT_EQ(w.count(start + 61UL * 60000UL), 1u);
}

TEST(Prefetch, NothingStartsWhileAMenuIsOpenOrPagesTurnByThemselves) {
  Rig r;
  Conditions c = ok();
  c.busy = true;  // ReaderPages: the toolbar or a panel over the page, or automatic page turns (readerBusy)
  EXPECT_FALSE(r.prefetch.due(kDue + 60000, c));
}

TEST(Prefetch, AReflowsNewTextAtTheSameIndexIsAnotherPage) {
  Rig r;
  r.api.pageReplies = {apiOk(answer(true))};
  r.step();
  r.step();
  ASSERT_FALSE(r.prefetch.due(kDue + 100000, ok()));  // both done
  // The font changed: page 0 of the section now starts elsewhere (the first page starts at 0 either way; here the
  // reader is on a later page whose start moved).
  r.prefetch.shown(2, 812, kDue + 100000);
  EXPECT_TRUE(r.prefetch.due(kDue + 100000 + config::kPagePrefetchDwellMs, ok()));
}

TEST(Prefetch, APageIsStampedWithTheWallClockAfterItsCall) {
  Rig r;
  wallS = 0;  // the clock not set yet: the boot's first call sets it (SNTP) during the call
  r.api.pageReplies = {apiOk(answer(true))};
  r.api.pageAbortAfter = 0;
  lexipoint::fakes::FakeClock::nowMs = kDue;
  r.prefetch.step(r.texts, [] {
    wallS = 1790300000;
    return false;
  });
  const auto kept = r.store.read(PageKey{1, 2, 0}, Language::Japanese, 1, textHash("猫"));
  ASSERT_TRUE(kept);
  EXPECT_EQ(kept->analyzedMs, 1790300000ULL * 1000ULL);
  wallS = 0;
}

// --- R8: the reader's conditions (ReaderPages::step, pure) ---

TEST(ReaderConditions, APausedBuildIsntBusyADueOneIs) {
  Rig r;
  ReaderInputs in;
  in.usable = true;
  in.wifiConnected = true;
  EXPECT_TRUE(r.prefetch.due(kDue, readerConditions(in)));  // a chapter's first read: the build paused ahead
  in.layingOut = true;                                      // a build tick due now
  EXPECT_FALSE(r.prefetch.due(kDue, readerConditions(in)));
  in.layingOut = false;
  for (const int gate : {0, 1, 2, 3, 4, 5, 6}) {
    ReaderInputs g = in;
    if (gate == 0) g.readerBusy = true;
    if (gate == 1) g.rendering = true;
    if (gate == 2) g.usable = false;
    if (gate == 3) g.blocked = true;
    if (gate == 4) g.wifiConnected = false;
    if (gate == 5) g.usedLastHour = 1000;
    if (gate == 6) g.inputHeld = true;
    EXPECT_FALSE(r.prefetch.due(kDue, readerConditions(g))) << gate;
  }
  const Conditions c = readerConditions(in);
  EXPECT_TRUE(c.wifiUp);  // the station alone (not folded with busy or usable)
}

namespace {

struct Starts final : PageStarts {
  std::optional<uint32_t> start = 0;
  int asked = 0;
  std::optional<uint32_t> startOf() override {
    asked++;
    return start;
  }
};

Pass passOf(const unsigned long drawnMs) {
  Pass p;
  p.onScreen = true;
  p.drawnMs = drawnMs;
  p.spine = 2;
  p.reader.usable = true;
  p.reader.wifiConnected = true;
  return p;
}

}  // namespace

TEST(PagePass, ACallGivenUpForInputWaitsAWholeDwellFromItsEndThoughTheSameDrawingIsToldEveryPass) {
  Rig r;
  PagePass pass(r.prefetch);
  Starts starts;
  const Pass p = passOf(kShownMs);
  ASSERT_TRUE(pass.ready(p, kDue, starts));
  r.api.pageReplies = {apiOk(answer(true))};
  r.api.pageAbortAfter = 1;
  lexipoint::fakes::FakeClock::nowMs = kDue;
  inputAsked = 0;
  const auto s = r.prefetch.step(r.texts, inputDuringTheCall);
  ASSERT_EQ(s.kind, PagePrefetcher::Step::Kind::Cancelled);
  // The next passes tell the same drawing again: the dwell counts from the call's end, not from the drawing.
  const unsigned long callEnd = kDue + 800;
  EXPECT_FALSE(pass.ready(p, callEnd + 10, starts));
  EXPECT_FALSE(pass.ready(p, callEnd + config::kPagePrefetchDwellMs - 1, starts));
  EXPECT_TRUE(pass.ready(p, callEnd + config::kPagePrefetchDwellMs, starts));
  EXPECT_EQ(starts.asked, 1);  // the start read once per drawing
}

TEST(PagePass, AHeldButtonOrFingerStartsNothing) {
  Rig r;
  PagePass pass(r.prefetch);
  Starts starts;
  Pass p = passOf(kShownMs);
  p.reader.inputHeld = true;  // release-mode page turns, a long Confirm: no edge queued
  EXPECT_FALSE(pass.ready(p, kDue + 60000, starts));
  p.reader.inputHeld = false;
  EXPECT_TRUE(pass.ready(p, kDue + 60000, starts));
}

TEST(PagePass, InputThereBeforeTheStepReadsNothingAndCallsNothing) {
  Rig r;
  r.api.pageReplies = {apiOk(answer(true))};
  lexipoint::fakes::FakeClock::nowMs = kDue;
  const auto s = r.prefetch.step(r.texts, [] { return true; });
  EXPECT_EQ(s.kind, PagePrefetcher::Step::Kind::None);
  EXPECT_EQ(r.texts.asked, 0);  // no page loaded
  EXPECT_TRUE(r.api.pageRequests.empty());
  EXPECT_TRUE(r.prefetch.due(kDue, ok()));  // nothing done: the page is still due
}

TEST(PagePass, TheCheapGatesComeBeforeTheStartIsRead) {
  Rig r;
  PagePass pass(r.prefetch);
  Starts starts;
  for (const int gate : {0, 1, 2, 3}) {
    Pass p = passOf(kShownMs + 1);
    if (gate == 0) p.onScreen = false;
    if (gate == 1) p.drawnMs = 0;
    if (gate == 2) p.reader.usable = false;
    if (gate == 3) p.reader.wifiConnected = false;
    EXPECT_FALSE(pass.ready(p, kDue + 60000, starts)) << gate;
  }
  Pass p = passOf(kShownMs + 1);
  p.reader.rendering = true;  // the section's table isn't read under a render
  EXPECT_FALSE(pass.ready(p, kDue + 60000, starts));
  EXPECT_EQ(starts.asked, 0);
  starts.start.reset();  // not now: asked again next pass
  EXPECT_FALSE(pass.ready(passOf(kShownMs + 1), kDue + 60000, starts));
  starts.start = 0;
  EXPECT_TRUE(pass.ready(passOf(kShownMs + 1), kDue + 60000, starts));
  EXPECT_EQ(starts.asked, 2);
}

TEST(PagePass, ANewDrawingOfTheSamePageRestartsTheDwell) {
  Rig r;
  PagePass pass(r.prefetch);
  Starts starts;
  EXPECT_TRUE(pass.ready(passOf(kShownMs), kDue, starts));
  const unsigned long redrawn = kDue + 100;  // a card closed over the page
  EXPECT_FALSE(pass.ready(passOf(redrawn), redrawn + 1, starts));
  EXPECT_TRUE(pass.ready(passOf(redrawn), redrawn + config::kPagePrefetchDwellMs, starts));
  EXPECT_EQ(starts.asked, 2);
}
