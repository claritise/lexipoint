#pragma once

// The home menu's rows after the books, in order: Browse Files, Library, File Transfer,
// [Sync Vocabulary] (LEXIPOINT, v0.2 V7b), Settings. Pure (the enum from ActivityManager.h's HomeMenuItem.h);
// tests: test/home_menu.

#include "activities/HomeMenuItem.h"

inline int homeMenuIndexOf(HomeMenuItem item, bool hasVocabSync) {
  int i = 0;
  if (item == HomeMenuItem::FILE_BROWSER) return i;
  ++i;
  if (item == HomeMenuItem::LIBRARY) return i;
  ++i;
  if (item == HomeMenuItem::FILE_TRANSFER) return i;
  ++i;
  if (item == HomeMenuItem::VOCAB_SYNC) return hasVocabSync ? i : 0;
  if (hasVocabSync) ++i;
  if (item == HomeMenuItem::SETTINGS_MENU) return i;
  return 0;
}

inline HomeMenuItem homeMenuItemAt(int idx, bool hasVocabSync) {
  int i = 0;
  if (idx == i++) return HomeMenuItem::FILE_BROWSER;
  if (idx == i++) return HomeMenuItem::LIBRARY;
  if (idx == i++) return HomeMenuItem::FILE_TRANSFER;
  if (hasVocabSync && idx == i++) return HomeMenuItem::VOCAB_SYNC;
  if (idx == i) return HomeMenuItem::SETTINGS_MENU;
  return HomeMenuItem::NONE;
}
