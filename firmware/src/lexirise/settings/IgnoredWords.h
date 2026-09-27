#pragma once

// The words the reader ignored (C17, V5): "stop marking this word on the page" (V9's A1 marks and A3's skipping,
// docs/v0.2/page-annotations.md). On the reader only: never sent to Lexirise, so the user's reviews there are
// untouched (claritise, 2026-09-27). Kept in /.lexirise/ignored.ini, one key per line, newest last:
// `<ja|zh>:<entry key>` (lookup::entryKeyOf: the lemma's entry id, else the word's own), or `<ja|zh>:~<dictionary
// form>` for a word Lexirise gave no entry id. Pure parts plus
// the store; tests: test/lexirise_settings/IgnoredWordsTest.cpp. Where it's documented: docs/v0.1/settings.md §3.

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "SafeFile.h"
#include "Settings.h"

namespace lexipoint {

// A word's identity in the list: its language and entry key (lookup::entryKeyOf: the lemma's entry id, else the word's
// own; the entry the card treats as one word, the one a save targets, and what analyze/text gives each occurrence on a
// page, so A1 can match on it); without any id, its dictionary form (`text`, empty when there's an id). Known limit:
// the refined and word-level passes can name different lemmas for one word, so its key can differ between them.
struct IgnoredKey {
  Language language = Language::Japanese;
  uint32_t entryId = 0;
  std::string text;
  bool operator==(const IgnoredKey&) const = default;
};

// The key for a word: its entry id when it has one, else its dictionary form when that's one line of at most
// config::kIgnoredTextMaxBytes (never cut: a cut form could be another word's); nullopt otherwise.
std::optional<IgnoredKey> ignoredKeyFor(Language language, uint32_t entryId, std::string_view headword);

// The list, oldest first in each part. Ids take 8 bytes each in RAM (the language in the top bits). contains() is a
// linear scan (1000 ids: well under a millisecond); V9's marks, asking for every word on a page, may want a sorted
// index.
class IgnoredWords {
 public:
  bool contains(const IgnoredKey& key) const;
  // At the end (newest) unless it's there; past config::kIgnoredIdsMax ids (or kIgnoredTextsMax forms) the oldest
  // is forgotten (into `evicted`, when given, so a failed save can put it back). False when nothing changed.
  bool add(const IgnoredKey& key, std::optional<IgnoredKey>* evicted = nullptr);
  // False when it wasn't there; `at` (when given): where it was, for putBack.
  bool remove(const IgnoredKey& key, size_t* at = nullptr);
  // Undoes add (the key out, the evicted one back as oldest) or remove (the key back at `at`), exactly.
  void undoAdd(const IgnoredKey& key, const std::optional<IgnoredKey>& evicted);
  void putBack(const IgnoredKey& key, size_t at);
  void reserve(size_t ids, size_t texts);        // at most the caps
  bool hasRoomFor(const IgnoredKey& key) const;  // its part (ids or forms) is below its cap
  size_t size() const { return ids_.size() + texts_.size(); }
  bool operator==(const IgnoredWords&) const = default;

 private:
  friend std::string serializeIgnored(const IgnoredWords& list);
  std::vector<uint64_t> ids_;
  struct Text {
    Language language;
    std::string text;
    bool operator==(const Text&) const = default;
  };
  std::vector<Text> texts_;
};

// Lines that aren't a key (an unknown language, an id of 0 or not a number, an empty or over-long form) are skipped;
// a key listed twice counts once, where it's newest (each line a linear search: fine for 1000 keys once per boot;
// V9's sorted index would make it cheaper). Ids first, then forms, each part oldest first.
IgnoredWords parseIgnored(std::string_view text);
std::string serializeIgnored(const IgnoredWords& list);
std::string ignoredKeyText(const IgnoredKey& key);  // its line in the file, without the newline (a log's words)

// The device's ignored.ini. Loaded on first use; safe across tasks (one mutex, held across the SD write).
class IgnoredWordStore {
 public:
  explicit IgnoredWordStore(SettingsFiles& files) : files_(files) {}

  void load();                           // reads the file now (as a card opens), so contains() never reads the SD card
  IgnoredWords list();                   // a copy (loaded first): tests and tools; the card asks contains()
  bool contains(const IgnoredKey& key);  // loaded first; from memory once loaded (a card's, under RenderLock)
  // Ignored or not; only a change writes. The list changes in place and is put back exactly when the write fails.
  // Ignoring at a cap pushes out the oldest key: `evicted` (when given) gets it, so the ignore's Undo can pass it
  // back as `restore`, which takes the key off and puts that one back as oldest, in one write.
  enum class Write : uint8_t { Written, Unchanged, Failed };  // Failed: not saved (memory unchanged), or not a key
  Write write(const IgnoredKey& key, bool ignored, std::optional<IgnoredKey>* evicted = nullptr,
              const std::optional<IgnoredKey>& restore = std::nullopt);

 private:
  void loadLocked();  // requires mutex_

  SettingsFiles& files_;
  std::mutex mutex_;
  bool loaded_ = false;
  IgnoredWords list_;
};

// The device-wide store over the SD card (SettingsFilesHal.cpp; not linked into host tests).
IgnoredWordStore& ignoredWordStore();

}  // namespace lexipoint
