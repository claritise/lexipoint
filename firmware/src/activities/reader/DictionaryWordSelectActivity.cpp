#include "DictionaryWordSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cctype>
#include <climits>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "WordBoxes.h"  // LEXIPOINT: one word-box rule, shared with the reader's long-press check (not gated)
#include "components/UITheme.h"
#if LEXIRISE
#include "lexirise/LexiriseService.h"            // LEXIPOINT
#include "lexirise/card/LexiriseCardActivity.h"  // LEXIPOINT
#include "lexirise/card/ReaderPageFor.h"         // LEXIPOINT
#include "lexirise/deck/BookDeck.h"              // LEXIPOINT
#include "lexirise/lookup/PageTap.h"             // LEXIPOINT
#include "lexirise/lookup/StarDictCandidates.h"  // LEXIPOINT
#include "lexirise/lookup/StarDictChoice.h"      // LEXIPOINT
#include "lexirise/settings/BookTags.h"          // LEXIPOINT
#endif

namespace {

constexpr unsigned long POPUP_DURATION_MS = 1500;
constexpr unsigned long WORD_REPEAT_START_MS = 500;
constexpr unsigned long WORD_REPEAT_INTERVAL_MS = 500;

void indexBuildYield(void*) { vTaskDelay(1); }

}  // namespace

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  lineHeight = renderer.getLineHeight(fontId);
  // No null check: a failed allocation just disables the differential
  // fast path (drawHighlightWithSnapshot skips the read), keeping the
  // full-repaint path as the fallback.
  snapshot = makeUniqueNoThrow<uint8_t[]>(SNAPSHOT_CAPACITY);
  extractWords();
  // Start on the middle row's word nearest mid-screen instead of top-left:
  // any word on the page is then at most half a page of moves away.
  if (!words.empty()) {
    const int initial = closestInRow(rowCount / 2, renderer.getScreenWidth() / 2);
    if (initial >= 0) selected = initial;
  }
#if LEXIRISE
  // LEXIPOINT: a long-press opened this: that word, looked up on the first loop (after this render).
  if (initialTouchX >= 0) {
    const int touched = wordAt(initialTouchX, initialTouchY);
    if (touched >= 0) {
      selected = touched;
      lookupPending = true;
      touchEntry = true;  // popup-ui.md §3: its card's Back returns to the reader
    }
  }
#endif
  requestUpdate();
}

void DictionaryWordSelectActivity::extractWords() {
  words.clear();
  words.reserve(128);
  rowCount = 0;

  // Single walk: collect the selectable words while accumulating their text
  // and styles (~2KB transient string, freed on return). Widths are measured
  // afterwards: merging the page's codepoints into the SD font's persistent
  // advance table first keeps getTextAdvanceX on the in-RAM path instead of
  // loading glyphs from SD one overflow slot at a time.
  std::string pageText;
  pageText.reserve(2048);
  uint8_t styleMask = 0;

#if LEXIRISE
  uint16_t textLine = 0;  // LEXIPOINT: counts lines as lexipoint::text::forEachTextLine() does
#endif
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto* block = line->getBlock();
    if (!block || !block->valid()) continue;

    bool rowHasWords = false;
    const int ascender = renderer.getFontAscenderSize(fontId);
    const int top = word_boxes::lineTop(*line, *block, marginTop, ascender);  // LEXIPOINT: WordBoxes.h
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      const char* text = block->wordText(i);
      if (!word_boxes::isSelectableToken(text)) {  // LEXIPOINT: WordBoxes.h
#if LEXIRISE
        pageText.append(text);  // LEXIPOINT: the page model measures every line's last token
        pageText.push_back(' ');
        styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(block->wordStyle(i)) & 0x03));
#endif
        continue;
      }

      WordBox box;
      box.x = static_cast<int16_t>(word_boxes::wordLeft(*line, *block, i, marginLeft));  // LEXIPOINT
      box.y = static_cast<int16_t>(top);
      box.style = block->wordStyle(i);
      box.width = 0;  // measured below, once the advance table is ready
      box.row = rowCount;
      box.text = text;
#if LEXIRISE
      box.line = textLine;  // LEXIPOINT
      box.token = i;
#endif
      words.push_back(box);
      rowHasWords = true;

      pageText.append(text);
      pageText.push_back(' ');
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(box.style) & 0x03));
    }
    if (rowHasWords) rowCount++;
#if LEXIRISE
    textLine++;  // LEXIPOINT
#endif
  }

  if (styleMask == 0) styleMask = 0x01;  // REGULAR
#if LEXIRISE
  pageText.append(lexipoint::lookup::kEmProbe);  // LEXIPOINT: measured in REGULAR
  styleMask |= 0x01;
#endif
  renderer.ensureSdCardFontReady(fontId, pageText.c_str(), styleMask);
  for (auto& word : words) {
    word.width = static_cast<int16_t>(renderer.getTextAdvanceX(fontId, word.text, word.style));
  }
#if LEXIRISE
  // LEXIPOINT: measured here, with the glyphs ready and before the render task draws this activity.
  pageModel = lexipoint::lookup::pageModelFor(renderer, fontId, *page);
  // The card's page snapshot, only where a card can open (Lexirise usable for this book).
  if (book && lexipoint::lookup::lexiriseConfigured(*book)) {
    readerPage = lexipoint::card::readerPageFor(renderer, fontId, *page, marginLeft, marginTop);
  }
#endif
}

// Index of the word whose box (with finger-sized slop) contains the touch
// point; -1 when the touch lands on no word.
int DictionaryWordSelectActivity::wordAt(const int x, const int y) const {
  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    const WordBox& word = words[i];
    if (word_boxes::hit(word.x, word.y, word.width, lineHeight, x, y)) return i;  // LEXIPOINT: WordBoxes.h
  }
  return -1;
}

#if LEXIRISE
// LEXIPOINT: wordAt() over the boxes extractWords() would make (WordBoxes.h), measuring only the words of
// the lines under the press. The caller holds the render lock (the measuring shares the glyph cache with
// the render task).
bool DictionaryWordSelectActivity::pressOnWord(GfxRenderer& renderer, const Page& page, const int marginLeft,
                                               const int marginTop, const int x, const int y) {
  const int fontId = SETTINGS.getReaderFontId();
  return word_boxes::anyWordAt(page, marginLeft, marginTop, renderer.getLineHeight(fontId),
                               renderer.getFontAscenderSize(fontId), x, y,
                               [&renderer, fontId](const char* text, const EpdFontFamily::Style style) {
                                 return renderer.getTextAdvanceX(fontId, text, style);
                               });
}
#endif

// Index of the word in `row` whose horizontal center is closest to centerX;
// -1 when the row has no words.
int DictionaryWordSelectActivity::closestInRow(const uint16_t row, const int centerX) const {
  int best = -1;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    if (words[i].row != row) continue;
    const int distance = std::abs(words[i].x + words[i].width / 2 - centerX);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

void DictionaryWordSelectActivity::moveVertical(const int direction) {
  const WordBox& current = words[selected];
  const int targetRow = static_cast<int>(current.row) + direction;
  if (targetRow < 0 || targetRow >= static_cast<int>(rowCount)) return;

  const int best = closestInRow(static_cast<uint16_t>(targetRow), current.x + current.width / 2);
  if (best >= 0 && best != selected) {
    selected = best;
    requestUpdate();
  }
}

void DictionaryWordSelectActivity::performLookup() {
#if LEXIRISE
  // LEXIPOINT: Lexirise first, in the card; StarDict only when it isn't asked or had no answer
  // (lookup-flow.md §4, offline-and-errors.md §1).
  using lexipoint::lookup::Gate;
  // One settings copy for the whole lookup (the web page may change them from another task meanwhile).
  const lexipoint::Settings settings = lexipoint::settingsStore().snapshot();
  const Gate gate = book ? lexipoint::lookup::lexiriseGate(settings, *book, lexipoint::service().blocked()) : Gate::Off;
  // The sentence and its language: for Lexirise, or when a language has its own StarDict dictionary. Plain
  // StarDict lookups (upstream's path) skip the work.
  auto tap =
      gate == Gate::Ask || lexipoint::lookup::anyLanguageStarDict(settings) ? describeSelected(settings) : std::nullopt;
  starDict = lexipoint::lookup::chooseStarDict(settings, tap ? tap->language.dictionaryLanguage() : std::nullopt,
                                               words.empty() ? "" : words[selected].text, SETTINGS.dictionaryName);
  if (gate == Gate::Ask && tap && openLexiriseCard(std::move(*tap), settings)) return;
  fallBack(lexipoint::lookup::gateFallback(gate, lexipoint::lookup::takeNoKeyNoticeIf(gate),
                                           lexipoint::lookup::takeUnannouncedIf(gate), starDictSet()));
  return;
#endif
  runStarDict();
}

// LEXIPOINT: split from performLookup(), unchanged: the Lexirise card hands back to it.
void DictionaryWordSelectActivity::runStarDict() {
  popup = Popup::Busy;
#if LEXIRISE
  // LEXIPOINT: the selected word's language's own dictionary, else (removed from the card, say) CrossPoint's;
  // a tap in the other language reopens (lookup/StarDictChoice.h).
  if (const auto state = lexipoint::lookup::prepareStarDict(
          starDict, dictOpened, [this](const std::string& folder) { return dict.open(folder.c_str()); },
          [this] { return dict.needsIndex(); })) {
    if (state->from == lexipoint::lookup::OpenedFrom::Fallback) {
      LOG_INF("LXLOOK", "Offline dictionary %s can't be opened: CrossPoint's answers", starDict.folder.c_str());
    }
    dictOpenOk = state->ok();
    dictNeedsIndex = state->needsIndex;
  }
#else
  if (!dictOpenAttempted) {
    dictOpenAttempted = true;
    dictOpenOk = dict.open(SETTINGS.dictionaryName);
    // needsIndex() opens and validates the .qidx sidecar, so ask it once per
    // open rather than once per word: the answer only changes when we build
    // the sidecar ourselves, which is handled below.
    dictNeedsIndex = dictOpenOk && dict.needsIndex();
  }
#endif
  popupMsg = dictNeedsIndex ? StrId::STR_DICT_INDEXING : StrId::STR_DICT_LOOKING_UP;
  requestUpdateAndWait();  // paint the page + busy popup before blocking on SD

  bool ok = dictOpenOk;
  Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
  if (ok && dictNeedsIndex) {
    ok = dict.buildIndex(&indexBuildYield, nullptr, &indexResult);
    dictNeedsIndex = !ok;  // a successful build leaves the sidecar fresh; a failed one retries
  }

  std::string definition;
  std::string headword;
  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
#if LEXIRISE
  const bool found = ok && starDictLookup(definition, headword, &result);  // LEXIPOINT: longest CJK prefix
#else
  const bool found = ok && dict.lookup(words[selected].text, definition, headword, &result);
#endif

  if (found) {
    popup = Popup::None;
#if LEXIRISE
    if (starDictOffline) headword += std::string(tr(STR_LEXI_CARD_SEPARATOR)) + tr(STR_LEXI_OFFLINE);  // LEXIPOINT: §2
    starDictOffline = false;
#endif
    startActivityForResult(
        std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, std::move(headword),
                                                       std::move(definition), dict.definitionsAreHtml()),
#if LEXIRISE
        [this](const ActivityResult&) { answerClosed(); });  // LEXIPOINT: by entry point
#else
        [this](const ActivityResult&) { requestUpdate(); });
#endif
    return;
  }
  // Name the failure: a genuine miss is "Not found"; a word that WAS found but
  // couldn't be read is a real error — and we distinguish decompression from a
  // low-memory allocation from a generic read error.
  if (!ok) {
    popup = Popup::Error;
    // An index build allocates a scan buffer, so it fails the same way lookups
    // do on a fragmented heap — name that rather than a generic error.
    switch (indexResult) {
      case Dictionary::IndexResult::LowMemory:
        popupMsg = StrId::STR_DICT_LOW_MEMORY;
        break;
      case Dictionary::IndexResult::ReadError:
        popupMsg = StrId::STR_DICT_READ_FAILED;
        break;
      case Dictionary::IndexResult::Ok:
      default:
        popupMsg = StrId::STR_DICT_ERROR;  // dict.open() failed, not the index
        break;
    }
  } else {
    switch (result) {
      case Dictionary::LookupResult::Decompress:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_DECOMPRESS_ERROR;
        break;
      case Dictionary::LookupResult::LowMemory:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_LOW_MEMORY;
        break;
      case Dictionary::LookupResult::ReadError:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_READ_FAILED;
        break;
      case Dictionary::LookupResult::NotFound:
      default:
        popup = Popup::NotFound;
        popupMsg = StrId::STR_DICT_NOT_FOUND;
        break;
    }
  }
  popupTime = millis();
  requestUpdate();
}

void DictionaryWordSelectActivity::loop() {
#if LEXIRISE
  if (lookupPending) {  // LEXIPOINT: the long-press lookup (setInitialTouch)
    lookupPending = false;
    performLookup();
    return;
  }
  if (starDictPending) {  // LEXIPOINT: the card had no answer from Lexirise
    starDictPending = false;
    runStarDict();
    return;
  }
#endif
  if (popup == Popup::NotFound || popup == Popup::Error) {
    if (millis() - popupTime >= POPUP_DURATION_MS) {
      popup = Popup::None;
#if LEXIRISE
      // LEXIPOINT: the notice has been read (card::afterNotice).
      using lexipoint::card::AfterNotice;
      const std::optional<lexipoint::card::AfterPopup> waiting = afterPopup;
      afterPopup.reset();
      switch (lexipoint::card::afterNotice(waiting, touchEntry)) {
        case AfterNotice::RunStarDict:  // after a Lexirise notice, StarDict answers
          runStarDict();
          return;
        case AfterNotice::FinishClose:  // after an unsent save's notice, the rest of the card's close
          finishClose(waiting->lookUpAt);
          return;
        case AfterNotice::BackToReader:  // a long-press opened this: the lookup ends back in the reader
          finish();
          return;
        case AfterNotice::Redraw:
          break;
      }
#endif
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !words.empty()) {
    performLookup();
    return;
  }

  if (words.empty()) return;

  // Touch: a touch-down moves the highlight to the touched word (differential
  // repaint), a tap on a word selects and looks it up in one go.
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTouchDown(tx, ty)) {
    const int hit = wordAt(tx, ty);
    if (hit >= 0 && hit != selected) {
      selected = hit;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const int hit = wordAt(tx, ty);
    if (hit >= 0) {
      selected = hit;
      performLookup();
    }
    return;
  }

  const bool hasNextWord = selected + 1 < static_cast<int>(words.size());
  const unsigned long now = millis();
  const bool repeat =
      mappedInput.getHeldTime() >= WORD_REPEAT_START_MS && now - lastHorizontalMoveTime >= WORD_REPEAT_INTERVAL_MS;
  const bool moveLeft = mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft) ||
                        (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenLeft));
  const bool moveRight = mappedInput.wasPressed(MappedInputManager::Button::ScreenRight) ||
                         (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenRight));
  if (moveLeft && selected > 0) {
    selected--;
    lastHorizontalMoveTime = now;
    requestUpdate();
  } else if (moveRight && hasNextWord) {
    selected++;
    lastHorizontalMoveTime = now;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenUp)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenDown)) {
    moveVertical(1);
  }
}

// Saves the pixels under words[selected]'s highlight box, then draws the
// highlight over them. Returns false when the pixels could not be saved
// (no buffer / oversize box) — the highlight is drawn regardless, but the
// next cursor move must do a full repaint.
bool DictionaryWordSelectActivity::drawHighlightWithSnapshot() {
  const WordBox& word = words[selected];
  int hx = word.x - 2;
  int hy = word.y - 2;
  int hw = word.width + 4;
  int hh = lineHeight + 4;
  // Clamp to the panel so save, draw and restore all use the same box.
  if (hx < 0) {
    hw += hx;
    hx = 0;
  }
  if (hy < 0) {
    hh += hy;
    hy = 0;
  }

  bool saved = false;
  if (snapshot && hw > 0 && hh > 0) {
    saved = renderer.readFramebufferRegion(hx, hy, hw, hh, snapshot.get(), SNAPSHOT_CAPACITY) > 0;
  }
  snapshotX = static_cast<int16_t>(hx);
  snapshotY = static_cast<int16_t>(hy);
  snapshotW = static_cast<int16_t>(hw);
  snapshotH = static_cast<int16_t>(hh);
  snapshotIdx = saved ? selected : -1;

  renderer.fillRect(hx, hy, hw, hh, true);
  renderer.drawText(fontId, word.x, word.y, word.text, false, word.style);
  return saved;
}

// Front-button bar (Back/Confirm/Left/Right). Drawn last on every repaint
// path, including the differential highlight-only path, so it always ends
// up as the top layer even when a highlighted word's box falls under a
// hint's screen area. No side-button hints: the full-bleed reader page has no
// spare gutter for them, so a hint box there would hide text.
void DictionaryWordSelectActivity::drawHints() const {
  // No selectable word on this page: Confirm and navigation are all no-ops
  // (guarded by words.empty() in loop()/performLookup), so only Back does
  // anything and only Back is hinted.
  if (words.empty()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  const auto labels = mappedInput.mapDirectionalLabels(tr(STR_BACK), tr(STR_LOOKUP), tr(STR_DIR_LEFT),
                                                       tr(STR_DIR_RIGHT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  // Differential fast path: only the highlight moved and the framebuffer
  // still holds a clean page (no popup or sub-activity since the last full
  // repaint). Restore the pixels under the old highlight, draw the new one,
  // and push — skipping the two-pass page render entirely.
  if (popup == Popup::None && snapshotIdx >= 0 && !words.empty() && selected != snapshotIdx) {
    renderer.writeFramebufferRegion(snapshotX, snapshotY, snapshotW, snapshotH, snapshot.get());
    // The full path's PrewarmScope cleared the glyph cache on exit; batch-load
    // just the highlighted word's glyphs before drawing them white-on-black.
    renderer.getFontCacheManager()->prewarmCache(
        fontId, words[selected].text, static_cast<uint8_t>(1u << (static_cast<uint8_t>(words[selected].style) & 0x03)));
    if (drawHighlightWithSnapshot()) {
      drawHints();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      return;
    }
    // Snapshot failed (oversize box) — fall through to a full repaint.
  }

  renderer.clearScreen();

  // Same prewarm-scan-then-render pass the reader uses, so SD-card fonts hit
  // the in-RAM glyph cache during the real draw.
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, marginLeft, marginTop);
  scope.endScanAndPrewarm();
  page->render(renderer, fontId, marginLeft, marginTop);

  if (!words.empty()) {
    drawHighlightWithSnapshot();
  }

  drawHints();

  if (popup != Popup::None) {
    // The popup overdraws the page, so the snapshot no longer matches the
    // framebuffer — force the next render onto the full-repaint path.
    snapshotIdx = -1;
    // drawPopup overlays the framebuffer and refreshes the display itself.
    // I18N.get directly: tr() only accepts literal key names.
    GUI.drawPopup(renderer, I18N.get(popupMsg));
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#if LEXIRISE
// LEXIPOINT: opens the Lexirise card on the selected word (lookup-flow.md §4-6, popup-ui.md). False when
// Lexirise isn't asked (no language to send: off, a switched-off language, a non-CJK book). The card
// hands back when Lexirise found no word (Not found) or had no answer (no key, no WiFi, network, a bad
// response): StarDict gets its turn then.
bool DictionaryWordSelectActivity::openLexiriseCard(lexipoint::text::TapContext context,
                                                    const lexipoint::Settings& settings) {
#if LOG_LEVEL >= 2
  lexipoint::lookup::logTap(context);
#endif
  if (!lexipoint::lookup::asksLexirise(context, lexipoint::lookup::lexiriseConfigured(settings, *book))) return false;

  auto outcome = std::make_shared<lexipoint::card::LiveOutcome>();
  // The side buttons go on into the page's next sentence (P9): described like a tap, from this page.
  // The page model is this activity's (it outlives the card, as drawPage's page does); book and settings are
  // copied (settings is this call's).
  auto next = [this, book = *book, settings](const lexipoint::text::TapContext& current) {
    return lexipoint::text::describeNextSentence(pageModel, current, book, settings);
  };
  auto tags = lexipoint::bookSaveTags(settings, bookTitle, bookPath, lexipoint::bookTagStore());
  auto source = std::make_unique<lexipoint::card::LiveSource>(lexipoint::service(), std::move(context), readerPage,
                                                              std::move(tags), std::move(next));
  // The book's deck, filled by its tag (C4): the card makes sure it exists once a save went through.
  if (auto deck = lexipoint::deck::bookDeckFor(settings, bookTitle, bookPath, lexipoint::bookTagStore())) {
    source->setBookDeck(std::move(*deck), lexipoint::deck::deckStore());
  }
  // The page stays this activity's: the card draws it under itself while it's open.
  auto drawPage = [this](GfxRenderer& r) { page->render(r, fontId, marginLeft, marginTop); };
  popup = Popup::None;
  snapshotIdx = -1;  // the card draws over the whole screen: the next render here is a full one
  startActivityForResult(
      std::make_unique<lexipoint::card::LexiriseCardActivity>(renderer, mappedInput, std::move(source),
                                                              std::move(drawPage), outcome),
      [this, outcome](const ActivityResult&) {
        using lexipoint::card::AfterCard;
        switch (lexipoint::card::afterCard(*outcome, starDictSet())) {
          case AfterCard::Closed:
            finishClose(outcome->lookUpAt);
            return;
          case AfterCard::UnsentSave:  // the card closed before a save could go: said first, then the close's way
            afterPopup = lexipoint::card::AfterPopup::finishClose(outcome->lookUpAt);
            showLookupPopup(Popup::Error, noticeString(lexipoint::lookup::noticeForUnsentSave(outcome->unsentError)));
            return;
          case AfterCard::NotFound:
            showLookupPopup(Popup::NotFound, StrId::STR_DICT_NOT_FOUND);
            return;
          case AfterCard::NoDictionary:
          case AfterCard::RunStarDict:
            LOG_INF("LXLOOK", "Lexirise unavailable (%s): %s", lexipoint::api::apiErrorName(outcome->error),
                    starDictSet() ? "StarDict answers" : "no StarDict");
            lexipoint::service().takeUnannouncedBlock();  // told by the card's hand-off
            fallBack(lexipoint::lookup::fallbackFor(outcome->error));
            return;
        }
      });
  return true;
}

// LEXIPOINT: a lookup's answer (the card, or StarDict's definition) was closed: a long-press on the page
// opened this, so back to the reader; opened from the menu, back to choosing a word (popup-ui.md §3).
void DictionaryWordSelectActivity::answerClosed() { finishClose(std::nullopt); }

// LEXIPOINT: where a closed answer goes (card::closeStep): the word a tap or long-press on the page landed on (looked
// up on the next loop() once this page is back on screen; the card used this page's own coordinates), back to
// the reader, or this page.
void DictionaryWordSelectActivity::finishClose(const std::optional<lexipoint::card::PagePoint>& lookUpAt) {
  using lexipoint::card::CloseStep;
  const int word = lookUpAt ? wordAt(lookUpAt->x, lookUpAt->y) : -1;
  switch (lexipoint::card::closeStep(lookUpAt, word >= 0, touchEntry)) {
    case CloseStep::LookUp:
      selected = word;
      lookupPending = true;
      requestUpdate();
      return;
    case CloseStep::BackToReader:
      finish();
      return;
    case CloseStep::Redraw:
      requestUpdate();
      return;
  }
}

// LEXIPOINT: the selected word's sentence and language (its language also picks the StarDict dictionary).
std::optional<lexipoint::text::TapContext> DictionaryWordSelectActivity::describeSelected(
    const lexipoint::Settings& settings) const {
  if (!book || selected < 0 || selected >= static_cast<int>(words.size())) return std::nullopt;
  return lexipoint::text::describeTap(pageModel, {words[selected].line, words[selected].token}, *book, settings);
}

// LEXIPOINT: a Lexirise notice's words.
StrId DictionaryWordSelectActivity::noticeString(const lexipoint::lookup::Notice notice) {
  using lexipoint::lookup::Notice;
  switch (notice) {
    case Notice::NoKey:
      return StrId::STR_LEXI_NO_KEY;
    case Notice::KeyRejected:
      return StrId::STR_LEXI_AUTH_FAILED;
    case Notice::RateLimited:
      return StrId::STR_LEXI_RATE_LIMITED;
    case Notice::SaveFailed:
    case Notice::None:
      break;
  }
  return StrId::STR_LEXI_SAVE_FAILED;
}

// LEXIPOINT: when Lexirise didn't answer: its notice first (a rejected key, a rate limit, no key: 1.5 s),
// then StarDict, on a later loop() with this activity on screen, its answer marked `offline` when Lexirise
// couldn't be reached (offline-and-errors.md §1-2); "No dictionary set" when there's no StarDict.
void DictionaryWordSelectActivity::fallBack(const lexipoint::lookup::Fallback fallback) {
  using lexipoint::lookup::Notice;
  const lexipoint::lookup::FallbackPlan plan = lexipoint::lookup::planFallback(fallback, starDictSet());
  starDictOffline = plan.offline;
  if (plan.notice != Notice::None) {
    showLookupPopup(Popup::Error, noticeString(plan.notice));
    if (plan.starDict) afterPopup = lexipoint::card::AfterPopup::runStarDict();
    return;
  }
  if (plan.noDictionary) {
    showLookupPopup(Popup::Error, StrId::STR_DICT_NO_DICT_SET);
    return;
  }
  starDictPending = plan.starDict;
}

void DictionaryWordSelectActivity::showLookupPopup(const Popup kind, const StrId message) {
  popup = kind;
  popupMsg = message;
  popupTime = millis();
  requestUpdate();
}

// LEXIPOINT: StarDict needs a word, but a CJK token is one character: try the longest CJK run from the
// tap along the line first (lookup-flow.md §4). A Latin token is looked up as it is.
bool DictionaryWordSelectActivity::starDictLookup(std::string& definition, std::string& headword,
                                                  Dictionary::LookupResult* result) {
  using lexipoint::lookup::ProbeResult;
  const WordBox& word = words[selected];
  std::vector<std::string> candidates;
  if (word.line < pageModel.lines.size()) {
    candidates = lexipoint::lookup::starDictCandidates(pageModel.lines[word.line], word.token);
  }
  if (candidates.empty()) candidates.emplace_back(word.text);
  const ProbeResult probe = lexipoint::lookup::probeStarDict(candidates, [&](const std::string& candidate) {
    if (dict.lookup(candidate.c_str(), definition, headword, result)) return ProbeResult::Found;
    return result && *result != Dictionary::LookupResult::NotFound ? ProbeResult::Error : ProbeResult::NotFound;
  });
  return probe == ProbeResult::Found;
}
#endif
