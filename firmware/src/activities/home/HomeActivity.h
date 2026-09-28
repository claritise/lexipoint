#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "./FileBrowserActivity.h"
#include "./HomeMenuIndex.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#if LEXIRISE
#include "lexirise/vocab/HomeSync.h"    // LEXIPOINT
#include "lexirise/vocab/ManualSync.h"  // LEXIPOINT
#endif

struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
#if LEXIRISE
  bool hasVocabSync = false;                              // LEXIPOINT: the Sync Vocabulary row (v0.2 V7b)
  std::unique_ptr<lexipoint::vocab::HomeSync> vocabSync;  // LEXIPOINT: a sync under way, or its result shown
  std::atomic<bool> vocabSyncDrawn{false};                // LEXIPOINT: its popup was drawn: the next step may run
  lexipoint::vocab::HomeSyncFlow vocabSyncFlow;           // LEXIPOINT: what input does to it (dismissed on release)
  void loopVocabSync();                                   // LEXIPOINT
#endif
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  size_t coverBufferSize = 0;      // Bytes allocated to coverBuffer
  // Logical rect last passed to drawRecentBookCover. The cover snapshot only
  // needs to cover this region, not the entire framebuffer, so we cache the
  // tile instead of all 48 KB. Set in render() before the call.
  int coverRectX = 0;
  int coverRectY = 0;
  int coverRectW = 0;
  int coverRectH = 0;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;
  const bool cleanInitialRefresh;

  // Menu index <-> item (onEnter, loop): home/HomeMenuIndex.h.
  static int menuItemToIndex(HomeMenuItem item, bool hasOpdsUrl, bool hasVocabSync = false) {
    return homeMenuIndexOf(item, hasOpdsUrl, hasVocabSync);
  }
  static HomeMenuItem indexToMenuItem(int idx, bool hasOpdsUrl, bool hasVocabSync = false) {
    return homeMenuItemAt(idx, hasOpdsUrl, hasVocabSync);
  }
  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onLibraryOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();

  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE, bool cleanInitialRefresh = false)
      : Activity("Home", renderer, mappedInput),
        initialMenuItem(initialMenuItemValue),
        cleanInitialRefresh(cleanInitialRefresh) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
#if LEXIRISE
  // LEXIPOINT: a Sync Vocabulary under way keeps the reader awake (a long first sync mustn't be cut by auto-sleep).
  bool preventAutoSleep() override { return vocabSync && vocabSync->running(); }
#endif
};
