#pragma once

// A small text file on the SD card replaced crash-safely (settings.md §3): write .tmp, move the old file
// to .bak, move .tmp into place, drop .bak. At every point either the file or its .bak is complete, and
// readSafely() finishes or undoes each step. Used by config.ini (SettingsStore) and books.ini
// (BookLanguages). Tests: test/lexirise_settings/SettingsStoreTest.cpp, BookLanguagesTest.cpp.

#include <cstddef>
#include <string>
#include <string_view>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint {

// The handful of file operations the stores need. Paths are absolute SD paths.
class SettingsFiles {
 public:
  enum class ReadStatus { Ok, Missing, TooLarge, Error };

  virtual ~SettingsFiles() = default;
  virtual ReadStatus read(const char* path, size_t maxBytes, std::string& out) = 0;
  virtual bool write(const char* path, std::string_view content) = 0;  // create/truncate, fully written
  virtual bool exists(const char* path) = 0;
  virtual bool remove(const char* path) = 0;
  virtual bool rename(const char* from, const char* to) = 0;  // fails if `to` exists (SdFat semantics)
  virtual bool ensureDir(const char* path) = 0;
};

// One safely replaced file and its companions.
struct SafeFilePaths {
  const char* dir;
  const char* path;
  const char* tmp;
  const char* backup;
  const char* bad;  // an unreadable file is moved here, so a later save can't overwrite the user's only copy
  size_t maxBytes;  // a larger (hand-edited) file is unreadable
};

enum class SafeRead {
  Ok,
  Missing,          // no file yet
  RecoveredBackup,  // the file was missing: the .bak of an interrupted save was read (and put back)
  Unreadable,       // too large or unreadable: moved to `bad`
};

// Reads the file into `out`, first finishing or undoing an interrupted replaceSafely().
SafeRead readSafely(SettingsFiles& files, const SafeFilePaths& paths, std::string& out);

// Replaces the file with `content`; false leaves the old one in place.
bool replaceSafely(SettingsFiles& files, const SafeFilePaths& paths, std::string_view content);

namespace config {
inline constexpr SafeFilePaths kSettingsFile{kSettingsDir,        kSettingsPath,    kSettingsTmpPath,
                                             kSettingsBackupPath, kSettingsBadPath, kSettingsMaxBytes};
inline constexpr SafeFilePaths kBookLanguagesFile{kSettingsDir,          kBookLanguagesPath,
                                                  kBookLanguagesTmpPath, kBookLanguagesBackupPath,
                                                  kBookLanguagesBadPath, kBookLanguagesMaxBytes};
}  // namespace config

}  // namespace lexipoint
