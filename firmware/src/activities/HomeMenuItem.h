#pragma once

// The home menu's items (ActivityManager::goHome, HomeActivity). LEXIPOINT: its own header, so the menu index
// (home/HomeMenuIndex.h) is host-tested.

enum class HomeMenuItem {
  NONE,
  FILE_BROWSER,
  LIBRARY,
  FILE_TRANSFER,
  VOCAB_SYNC,  // LEXIPOINT: Sync Vocabulary (v0.2 V7b)
  SETTINGS_MENU
};
