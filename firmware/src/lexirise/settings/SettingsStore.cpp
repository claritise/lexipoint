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

LoadOutcome SettingsStore::loadLocked() {
  std::string text;
  LoadOutcome outcome = LoadOutcome::Loaded;
  switch (readSafely(files_, config::kSettingsFile, text)) {
    case SafeRead::Ok:
      break;
    case SafeRead::RecoveredBackup:
      outcome = LoadOutcome::RecoveredBackup;
      break;
    case SafeRead::Missing:
      return LoadOutcome::Defaults;
    case SafeRead::Unreadable:
    default:
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

bool SettingsStore::saveLocked(const Settings& settings) {
  return replaceSafely(files_, config::kSettingsFile, serializeSettings(settings));
}

}  // namespace lexipoint

#endif  // LEXIRISE
