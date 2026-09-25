#pragma once

#include <Epub/Page.h>
#include <I18n.h>

#include <memory>
#include <vector>

#include "activities/Activity.h"
#include "util/Dictionary.h"
#if LEXIRISE
#include <optional>
#include <string>

#include "lexirise/card/ReaderScene.h"       // LEXIPOINT
#include "lexirise/card/WordSelectFlow.h"    // LEXIPOINT
#include "lexirise/lookup/Fallback.h"        // LEXIPOINT
#include "lexirise/lookup/StarDictChoice.h"  // LEXIPOINT
#include "lexirise/text/BookLanguage.h"      // LEXIPOINT
#include "lexirise/text/SentenceBuilder.h"   // LEXIPOINT
#include "lexirise/text/TapContext.h"        // LEXIPOINT
#endif

// Word selection over the current reader page: Left/Right step through words
// in reading order, Up/Down jump rows, Confirm looks the word up and opens
// DictionaryDefinitionActivity, Back returns to the reader. On touch devices a
// touch-down moves the highlight and a tap on a word looks it up directly.
class DictionaryWordSelectActivity final : public Activity {
 public:
  explicit DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        std::unique_ptr<Page> page, int marginLeft, int marginTop)
      : Activity("DictionaryWordSelect", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

#if LEXIRISE
  // LEXIPOINT: the book's <dc:language> (and later its override), so a tap can pick its language.
  void setBook(const std::string& dcLanguage) { book.emplace(dcLanguage, std::nullopt); }
  // LEXIPOINT: opened by a long-press on the page: select the word there and look it up at once.
  void setInitialTouch(const int x, const int y) {
    initialTouchX = x;
    initialTouchY = y;
  }
#endif

 private:
  // Screen box of one selectable word. `text` points into the owned Page's
  // TextBlock arena (NUL-terminated), valid for this activity's lifetime.
  struct WordBox {
    int16_t x;
    int16_t y;
    int16_t width;
    uint16_t row;
    const char* text;
    EpdFontFamily::Style style;
#if LEXIRISE
    uint16_t line;   // LEXIPOINT: index among the page's text lines (lexipoint::text::PageModel)
    uint16_t token;  // LEXIPOINT: index in that line's block, counting every token
#endif
  };

  enum class Popup : uint8_t { None, Busy, NotFound, Error };

  void extractWords();
  int closestInRow(uint16_t row, int centerX) const;
  int wordAt(int x, int y) const;
  void moveVertical(int direction);
  void performLookup();
  void runStarDict();  // LEXIPOINT: performLookup's StarDict half (the Lexirise card hands back to it)
  bool drawHighlightWithSnapshot();
  void drawHints() const;

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  int fontId = 0;
  int lineHeight = 0;

  std::vector<WordBox> words;
#if LEXIRISE
  std::optional<lexipoint::text::BookLanguage> book;  // LEXIPOINT
  lexipoint::text::PageModel pageModel;               // LEXIPOINT: built in extractWords()
  lexipoint::card::ReaderPage readerPage;             // LEXIPOINT: the same page, as drawn (the live card)
  int initialTouchX = -1;                             // LEXIPOINT: setInitialTouch()
  int initialTouchY = -1;
  bool lookupPending = false;  // LEXIPOINT: the long-press lookup runs on the first loop()
  bool touchEntry = false;     // LEXIPOINT: a long-press on a word opened this: its answer closes to the reader
  void answerClosed();         // LEXIPOINT: back to the reader (touchEntry) or to this page
  void finishClose(const std::optional<lexipoint::card::PagePoint>& lookUpAt);  // LEXIPOINT: card::closeStep
  // LEXIPOINT: what waits for the notice on screen to be read (StarDict's turn, or a card's close).
  std::optional<lexipoint::card::AfterPopup> afterPopup;
  bool starDictPending = false;  // LEXIPOINT: Lexirise had no answer: StarDict runs on the next loop()
  bool starDictOffline = false;  // LEXIPOINT: its answer carries the `offline` mark
  void fallBack(lexipoint::lookup::Fallback fallback);           // LEXIPOINT: the notice, then StarDict
  static StrId noticeString(lexipoint::lookup::Notice notice);   // LEXIPOINT
  bool starDictSet() const { return !starDict.folder.empty(); }  // LEXIPOINT: for the selected word
  // LEXIPOINT: the selected word's sentence and language; nullopt without a book or a word.
  std::optional<lexipoint::text::TapContext> describeSelected(const lexipoint::Settings& settings) const;
  lexipoint::lookup::StarDictChoice starDict;  // LEXIPOINT: the dictionary for the selected word's language
  std::optional<lexipoint::lookup::StarDictChoice> dictOpened;  // LEXIPOINT: what `dict` was opened for
  // LEXIPOINT: false when Lexirise isn't asked about this word.
  bool openLexiriseCard(lexipoint::text::TapContext context, const lexipoint::Settings& settings);
  void showLookupPopup(Popup kind, StrId message);  // LEXIPOINT
  bool starDictLookup(std::string& definition, std::string& headword, Dictionary::LookupResult* result);
#endif
  int selected = 0;
  uint16_t rowCount = 0;
  unsigned long lastHorizontalMoveTime = 0;

  Dictionary dict;
  bool dictOpenAttempted = false;
  bool dictOpenOk = false;
  bool dictNeedsIndex = false;

  Popup popup = Popup::None;
  StrId popupMsg = StrId::STR_DICT_NOT_FOUND;
  unsigned long popupTime = 0;

  // Differential highlight repaint: the pixels under the current highlight
  // box, so a cursor move restores them and repaints only the two affected
  // boxes instead of re-running the full two-pass page render (which also
  // reloads every SD-font glyph on the page). snapshotIdx is the word whose
  // under-pixels are saved; -1 means the framebuffer no longer holds a clean
  // page (popup drawn, sub-activity shown) and the next render must be full.
  static constexpr size_t SNAPSHOT_CAPACITY = 4096;
  std::unique_ptr<uint8_t[]> snapshot;
  int16_t snapshotX = 0;
  int16_t snapshotY = 0;
  int16_t snapshotW = 0;
  int16_t snapshotH = 0;
  int snapshotIdx = -1;
};
