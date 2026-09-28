#pragma once

// The home menu's items (ActivityManager::goHome, HomeActivity). LEXIPOINT: its own header, so the menu index
// (home/HomeMenuIndex.h) is host-tested.

enum class HomeMenuItem {
  NONE,
  FILE_BROWSER,
  LIBRARY,
  OPDS_BROWSER,
  FILE_TRANSFER,
#if LEXIRISE
  VOCAB_SYNC,  // LEXIPOINT: Sync Vocabulary (v0.2 V7b)
#endif
  SETTINGS_MENU
};
