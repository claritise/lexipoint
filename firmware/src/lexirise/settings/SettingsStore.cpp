#if LEXIRISE

#include "SettingsStore.h"

#include <Logging.h>

#include <utility>

namespace lexipoint {
namespace {

constexpr const char* kLogTag = "LXCFG";

}  // namespace

LoadOutcome SettingsStore::load() {
  std::lock_guard<std::mutex> writeLock(writeMutex_);
  lastLoad_ = loadLocked();
  switch (lastLoad_) {
    case LoadOutcome::Loaded:
      break;
    case LoadOutcome::Defaults:
      LOG_INF(kLogTag, "No settings yet, using defaults");
      break;
    case LoadOutcome::RecoveredBackup:
      LOG_INF(kLogTag, "Settings recovered from the backup of an interrupted save");
      break;
    case LoadOutcome::Unreadable:
    default:
      LOG_ERR(kLogTag, "Settings unreadable: moved to %s, using defaults", config::kSettingsBadPath);
      break;
  }
  return lastLoad_;
}

// Moves an unusable file aside (replacing an older one), so a later save can't overwrite what may be
// the user's only copy of their key and settings.
void SettingsStore::quarantine(const char* path) {
  if (files_.exists(config::kSettingsBadPath)) files_.remove(config::kSettingsBadPath);
  files_.rename(path, config::kSettingsBadPath);
}

LoadOutcome SettingsStore::loadLocked() {
  // A .tmp is only ever a save that never reached its rename: the real file (or its .bak) is newer.
  if (files_.exists(config::kSettingsTmpPath)) files_.remove(config::kSettingsTmpPath);

  LoadOutcome outcome = LoadOutcome::Loaded;
  const char* readFrom = config::kSettingsPath;
  std::string text;
  auto status = files_.read(config::kSettingsPath, config::kSettingsMaxBytes, text);
  if (status == SettingsFiles::ReadStatus::Missing && files_.exists(config::kSettingsBackupPath)) {
    // Interrupted between "final → .bak" and "tmp → final": the .bak is the last good file.
    if (!files_.rename(config::kSettingsBackupPath, config::kSettingsPath)) readFrom = config::kSettingsBackupPath;
    status = files_.read(readFrom, config::kSettingsMaxBytes, text);
    outcome = LoadOutcome::RecoveredBackup;
  } else if (status == SettingsFiles::ReadStatus::Ok && files_.exists(config::kSettingsBackupPath)) {
    // Interrupted after the new file landed but before the .bak was dropped.
    files_.remove(config::kSettingsBackupPath);
  }

  switch (status) {
    case SettingsFiles::ReadStatus::Ok:
      break;
    case SettingsFiles::ReadStatus::Missing:
      return LoadOutcome::Defaults;
    case SettingsFiles::ReadStatus::TooLarge:
    case SettingsFiles::ReadStatus::Error:
    default:
      quarantine(readFrom);  // the main file, or a .bak that couldn't be put back
      return LoadOutcome::Unreadable;
  }

  ParseResult parsed = parseSettings(text);
  if (parsed.migratedLegacyKeys) saveLocked(parsed.settings);  // best effort: memory is right either way
  {
    std::lock_guard<std::mutex> dataLock(dataMutex_);
    current_ = std::move(parsed.settings);
  }
  return outcome;
}

Settings SettingsStore::snapshot() const {
  std::lock_guard<std::mutex> dataLock(dataMutex_);
  return current_;
}

SettingsStore::UpdateResult SettingsStore::update(const std::function<bool(Settings&)>& mutate) {
  std::lock_guard<std::mutex> writeLock(writeMutex_);
  Settings next = snapshot();  // only writers change current_, and they all hold writeMutex_
  if (!mutate(next)) return UpdateResult::Declined;
  if (!saveLocked(next)) return UpdateResult::SaveFailed;
  std::lock_guard<std::mutex> dataLock(dataMutex_);
  current_ = std::move(next);
  revision_++;
  return UpdateResult::Saved;
}

uint32_t SettingsStore::revision() const {
  std::lock_guard<std::mutex> dataLock(dataMutex_);
  return revision_;
}

// Crash-safe replace (SdFat's rename never overwrites): write .tmp, move the old file to .bak, move
// .tmp into place, drop .bak. At every point either config.ini or config.ini.bak is a complete file,
// and load() knows how to finish or undo each step.
bool SettingsStore::saveLocked(const Settings& settings) {
  if (!files_.ensureDir(config::kSettingsDir)) return false;
  if (!files_.write(config::kSettingsTmpPath, serializeSettings(settings))) {
    files_.remove(config::kSettingsTmpPath);
    return false;
  }
  if (files_.exists(config::kSettingsBackupPath)) files_.remove(config::kSettingsBackupPath);
  const bool hadFile = files_.exists(config::kSettingsPath);
  if (hadFile && !files_.rename(config::kSettingsPath, config::kSettingsBackupPath)) {
    files_.remove(config::kSettingsTmpPath);
    return false;
  }
  if (!files_.rename(config::kSettingsTmpPath, config::kSettingsPath)) {
    if (hadFile) files_.rename(config::kSettingsBackupPath, config::kSettingsPath);
    files_.remove(config::kSettingsTmpPath);
    return false;
  }
  if (hadFile) files_.remove(config::kSettingsBackupPath);
  return true;
}

}  // namespace lexipoint

#endif  // LEXIRISE
