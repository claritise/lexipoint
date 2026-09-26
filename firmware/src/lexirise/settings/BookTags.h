#pragma once

// The book tag on each save (C2), and the record of each tag's book title (V4's "Met before" names the book of
// a saved word's `book:<slug>`). Kept in /.lexirise/book-tags.ini, one `<slug>=<title>` line per book, newest
// last; a slug is only written the first time a card opens in its book, so a tap doesn't write to the card. Pure
// parts plus the store; tests: test/lexirise_settings/BookTagsTest.cpp.

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "SafeFile.h"
#include "Settings.h"

namespace lexipoint {

struct BookTagEntry {
  std::string slug;
  std::string title;
  bool operator==(const BookTagEntry& o) const { return slug == o.slug && title == o.title; }
};
using BookTagList = std::vector<BookTagEntry>;

// Lines that aren't `<slug>=<title>` (a slug being [a-z0-9-]) are skipped; a slug listed twice keeps its last.
BookTagList parseBookTags(std::string_view text);
std::string serializeBookTags(const BookTagList& list);

std::optional<std::string> bookTitleIn(const BookTagList& list, std::string_view slug);
// Adds a slug's title at the end unless the slug is listed (its first title stays), then forgets the oldest while
// there are more than config::kBookTagsMax or they'd serialise past config::kBookTagsMaxBytes. The title is kept
// on one line and cut at a character to config::kBookTagTitleMaxBytes. True when the list changed; false too for
// a slug that isn't one.
bool addBookTagIn(BookTagList& list, std::string_view slug, std::string_view title);

// The tags a word saved from a book carries: the user's (settings.tags), then `bookTag` while settings.tagBook,
// unless the user's already hold it.
std::vector<std::string> saveTags(const Settings& settings, std::string_view bookTag);

// The device's book-tags.ini. Loaded on first use; safe across tasks (one mutex, held across the SD write).
class BookTagStore {
 public:
  explicit BookTagStore(SettingsFiles& files) : files_(files) {}

  std::optional<std::string> title(std::string_view slug);
  // Records a slug's title unless it's listed: only a new slug writes. False: not saved (memory unchanged).
  bool remember(std::string_view slug, std::string_view title);

 private:
  void loadLocked();  // requires mutex_

  SettingsFiles& files_;
  std::mutex mutex_;
  bool loaded_ = false;
  BookTagList list_;
};

// The device-wide store over the SD card (SettingsFilesHal.cpp; not linked into host tests).
BookTagStore& bookTagStore();

// The title recorded for a book: its own on one line, cut to config::kBookTagTitleMaxBytes, or for an untitled
// one (text::isUntitled) its file name without the extension.
std::string bookRecordTitle(std::string_view title, std::string_view path);

// saveTags for a card in the open book: its tag is config::kBookTagPrefix + text::bookSlug (an untitled book's from its
// path), and while settings.tagBook its title is recorded in `store` first (the file name for an untitled book).
// A failed record doesn't stop the save.
std::vector<std::string> bookSaveTags(const Settings& settings, std::string_view title, std::string_view path,
                                      BookTagStore& store);

}  // namespace lexipoint
