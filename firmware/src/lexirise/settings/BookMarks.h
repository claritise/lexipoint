#pragma once

// Each book's "Page marks" row in the reader menu (V9a, page-annotations.md §2 "V9a decisions", signed off
// 2026-09-29): On (the default: Settings → Lexirise's "On the page" rows decide) or Off (no marks in this book, and a
// card's side buttons step every word). Kept in /.lexirise/marks-off.ini, one book path per line for the books turned
// off, newest last (config::kBookMarksOffMax at most: one more forgets the oldest, whose marks come back). A moved or
// renamed book is a new book. Pure parts plus the store; tests: test/lexirise_settings/BookMarksTest.cpp.

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "SafeFile.h"

namespace lexipoint {

using BookMarksOffList = std::vector<std::string>;  // oldest first

// Lines that are empty are skipped; a book listed twice counts once, where it's newest.
BookMarksOffList parseBookMarksOff(std::string_view text);
std::string serializeBookMarksOff(const BookMarksOffList& list);
bool marksOnIn(const BookMarksOffList& list, std::string_view path);
// Turns a book's marks on (its line goes) or off (its line at the end), then forgets the oldest past the caps. False
// (nothing changed) for a path that can't be a line.
bool setMarksIn(BookMarksOffList& list, std::string_view path, bool on);

// The device's marks-off.ini. Loaded on first use; safe across tasks (one mutex, held across the SD write, which only
// a menu tap makes).
class BookMarksStore {
 public:
  explicit BookMarksStore(SettingsFiles& files) : files_(files) {}
  bool on(std::string_view path);
  bool set(std::string_view path, bool on);  // saved, then current; false leaves both as they were

 private:
  void loadLocked();  // requires mutex_
  SettingsFiles& files_;
  std::mutex mutex_;
  bool loaded_ = false;
  BookMarksOffList list_;
};

// The device-wide store over the SD card (SettingsFilesHal.cpp; not linked into host tests).
BookMarksStore& bookMarksStore();

// The open book's row in a reader menu: open() reads it where the menu opens (main task), toggle() saves the other
// value on a tap; the render task reads on().
class BookMarksRow {
 public:
  explicit BookMarksRow(BookMarksStore& store) : store_(store) {}
  void open(std::string bookPath);
  bool on() const { return on_.load(); }
  bool toggle();  // false: not saved, the value stays

 private:
  BookMarksStore& store_;
  std::string path_;
  std::atomic<bool> on_{true};
};

}  // namespace lexipoint
