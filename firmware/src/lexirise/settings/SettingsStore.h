#pragma once

// Lexirise settings persistence (docs/v0.1/settings.md §3). Owns the in-memory Settings and
// the crash-safe save of /.lexirise/config.ini (SafeFile.h). The file I/O goes through SettingsFiles so the
// save and recovery logic is host-testable (test/lexirise_settings/SettingsStoreTest.cpp); the SD card adapter
// lives in SettingsFilesHal.cpp.
//
// Threading: the store is shared across FreeRTOS tasks by design (CrossPoint's web handlers run on
// the main task today, but nothing here relies on that). Two locks, never nested the other way round:
//   - writeMutex_ serialises updates, and is held across the SD write (writers only);
//   - dataMutex_ guards the in-memory copy, held only for a copy/assign, never across I/O.
// So snapshot() (render path, lookups) never waits behind an SD write.

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

#include "SafeFile.h"
#include "Settings.h"

namespace lexipoint {

enum class LoadOutcome {
  Defaults,         // no file yet (first boot)
  Loaded,           // config.ini parsed
  RecoveredBackup,  // config.ini was missing, the .bak from an interrupted save was restored
  Unreadable,       // too large or unreadable: defaults in memory, the file moved to config.ini.bad
};

class SettingsStore {
 public:
  explicit SettingsStore(SettingsFiles& files) : files_(files) {}

  // Boot-time load. Cleans a leftover .tmp, recovers a .bak, and rewrites a legacy-format file.
  LoadOutcome load();
  // What the last load() found (the web page warns when settings were reset).
  LoadOutcome lastLoad() const { return lastLoad_; }

  // A consistent copy of the current settings. Cheap enough to call per lookup.
  Settings snapshot() const;

  enum class UpdateResult { Saved, Declined, SaveFailed };

  // Applies `mutate` to a copy; if it returns true the copy is saved, and only then made current.
  // Declined (mutate returned false) and SaveFailed both leave memory and card as they were.
  UpdateResult update(const std::function<bool(Settings&)>& mutate);

  // Bumped by every successful update; lets screens notice a change made from the web page.
  uint32_t revision() const;

 private:
  LoadOutcome loadLocked();                   // requires writeMutex_
  bool saveLocked(const Settings& settings);  // requires writeMutex_

  SettingsFiles& files_;
  mutable std::mutex dataMutex_;
  std::mutex writeMutex_;
  Settings current_;
  uint32_t revision_ = 0;
  LoadOutcome lastLoad_ = LoadOutcome::Defaults;
};

// The device-wide store over the SD card (SettingsFilesHal.cpp; not linked into host tests).
SettingsStore& settingsStore();

}  // namespace lexipoint
