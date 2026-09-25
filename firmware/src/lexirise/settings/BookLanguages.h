#pragma once

// Each book's lookup language, set in the reader menu (languages.md §1, step 3: the per-book override):
// for a book whose <dc:language> is missing or wrong, and whose Han-only sentences would otherwise go
// to "Language when a book doesn't say". Kept in /.lexirise/books.ini, one `<ja|zh>=<book path>` line
// per book, newest last; Auto (no line) lets the book's metadata and text decide. A moved or renamed
// book is a new book. Pure parts plus the store; tests: test/lexirise_settings/BookLanguagesTest.cpp.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SafeFile.h"
#include "Settings.h"

namespace lexipoint {

struct BookLanguageEntry {
  std::string path;
  Language language = Language::Japanese;
  bool operator==(const BookLanguageEntry& o) const { return path == o.path && language == o.language; }
};
using BookLanguageList = std::vector<BookLanguageEntry>;

// Lines that aren't `ja=` / `zh=` with a path are skipped; a book listed twice keeps its last line.
BookLanguageList parseBookLanguages(std::string_view text);
std::string serializeBookLanguages(const BookLanguageList& list);

std::optional<Language> bookLanguageIn(const BookLanguageList& list, std::string_view path);
// Sets (or with nullopt, clears) a book's choice, moving it to the end, then forgets the oldest while
// there are more than config::kBookLanguagesMax or they'd serialise past config::kBookLanguagesMaxBytes.
// False (and nothing changed) for a path that can't be a line (empty, or with a line break).
bool setBookLanguageIn(BookLanguageList& list, std::string_view path, std::optional<Language> language);

// The reader menu's cycle: Auto (nullopt), then each of kLanguages in order, then Auto again.
std::optional<Language> nextBookLanguage(std::optional<Language> current);

// The device's books.ini. Loaded on first use; safe across tasks (one mutex, held across the SD write,
// which only a menu tap makes).
class BookLanguageStore {
 public:
  explicit BookLanguageStore(SettingsFiles& files) : files_(files) {}

  std::optional<Language> get(std::string_view path);
  // Saved, then current; false leaves both memory and card as they were.
  bool set(std::string_view path, std::optional<Language> language);

 private:
  void loadLocked();  // requires mutex_

  SettingsFiles& files_;
  std::mutex mutex_;
  bool loaded_ = false;
  BookLanguageList list_;
};

// The device-wide store over the SD card (SettingsFilesHal.cpp; not linked into host tests).
BookLanguageStore& bookLanguageStore();

// The open book's row in a reader menu: open() reads the book's choice where the menu opens (main task),
// cycle() saves the next one on a tap; the render task reads language().
class BookLanguageRow {
 public:
  explicit BookLanguageRow(BookLanguageStore& store) : store_(store) {}
  void open(std::string bookPath);
  std::optional<Language> language() const;  // nullopt: Auto
  bool cycle();                              // false: not saved, the choice stays

 private:
  void show(std::optional<Language> language);  // what language() reads

  BookLanguageStore& store_;
  std::string path_;
  std::atomic<uint8_t> slot_{0};  // 0: Auto; i + 1: kLanguages[i] (one byte, so the render task's read is whole)
};

}  // namespace lexipoint
