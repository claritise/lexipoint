#pragma once
#include <Epub.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "lexirise/settings/BookLanguages.h"
#include "lexirise/settings/BookMarks.h"

class EpubReaderMenuActivity final : public UiListActivity {
 public:
  // Menu actions available from the reader menu.
  enum class MenuAction {
    SELECT_CHAPTER,
    FOOTNOTES,
    TEXT_SETTINGS,
    NIGHT_MODE,
    FRONTLIGHT,
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    ROTATE_SCREEN,
    BOOKMARKS,
    TOGGLE_BOOKMARK,
    SCREENSHOT,
    DISPLAY_QR,
    GO_HOME,
    DELETE_CACHE,
    LOOKUP_LANGUAGE,  // LEXIPOINT: the book's lookup language, cycled in place (languages.md §1, step 3)
    PAGE_MARKS,       // LEXIPOINT: the book's page marks (V9a), On / Off in place
  };

  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  // LEXIPOINT (V9a): `pageMarks`: the book can show marks (Lexirise usable and "Mark words on the page"), so the
  // "Page marks" row shows.
  static void buildMenuItems(std::vector<MenuItem>& items, bool hasFootnotes, bool hasBookmarks, bool pageMarks);

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  const int currentPage, const int totalPages, const int bookProgressPercent,
                                  const uint8_t currentOrientation, const bool hasFootnotes, bool hasBookmarks);

  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

  // LEXIPOINT: the open book, for its Lookup language row.
  void setBookPath(std::string path) {
    bookMarks.open(path);
    bookLanguage.open(std::move(path));
  }
  // LEXIPOINT: a Lookup language row's value (the list menu's and the toolbar's More panel).
  static StrId bookLanguageLabel(std::optional<lexipoint::Language> language);  // nullopt: Auto

 private:
  // Row storage: menuItems is at most MAX_MENU_ITEMS, so a
  // fixed-capacity array avoids any heap allocation for the row list. Labels
  // are set once in the constructor (buildMenuRowItems()); buildScreen()
  // only refreshes rows whose values reflect live state.
  // LEXIPOINT: sized by the actions (each row is a different one), so a new row can't outgrow it.
  static constexpr size_t MAX_MENU_ITEMS = static_cast<size_t>(MenuAction::PAGE_MARKS) + 1;
  freeink::ui::ListItem menuRowItems[MAX_MENU_ITEMS]{};
  void buildMenuRowItems();

  int listCount() const override {  // LEXIPOINT: never past the row slots (not gated)
    return static_cast<int>(menuItems.size() < MAX_MENU_ITEMS ? menuItems.size() : MAX_MENU_ITEMS);
  }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Popup input runs before any button or touch handling.
  bool handleCustomInput() override;
  // Back closes on RELEASE and Confirm activates on RELEASE; everything else
  // (row navigation, page jumps) falls through to the base handler.
  bool handleButtons() override;
  // Header via GUI.drawHeader inside the safe area for the battery indicator.
  void drawChrome() override;

  void closeCancelled();

  // Fixed menu layout
  std::vector<MenuItem> menuItems;

  OptionPopup optionPopup;
  lexipoint::BookLanguageRow bookLanguage{lexipoint::bookLanguageStore()};
  lexipoint::BookMarksRow bookMarks{lexipoint::bookMarksStore()};  // LEXIPOINT (V9a)
  std::string title = "Reader Menu";
  uint8_t pendingOrientation = 0;
  uint8_t selectedPageTurnOption = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
