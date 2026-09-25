#if LEXIRISE

#include "SafeFile.h"

namespace lexipoint {
namespace {

void quarantine(SettingsFiles& files, const SafeFilePaths& paths, const char* path) {
  if (files.exists(paths.bad)) files.remove(paths.bad);
  files.rename(path, paths.bad);
}

}  // namespace

SafeRead readSafely(SettingsFiles& files, const SafeFilePaths& paths, std::string& out) {
  // A .tmp is only ever a save that never reached its rename: the real file (or its .bak) is newer.
  if (files.exists(paths.tmp)) files.remove(paths.tmp);

  SafeRead outcome = SafeRead::Ok;
  const char* readFrom = paths.path;
  auto status = files.read(paths.path, paths.maxBytes, out);
  if (status == SettingsFiles::ReadStatus::Missing && files.exists(paths.backup)) {
    // Interrupted between "final → .bak" and "tmp → final": the .bak is the last good file.
    if (!files.rename(paths.backup, paths.path)) readFrom = paths.backup;
    status = files.read(readFrom, paths.maxBytes, out);
    outcome = SafeRead::RecoveredBackup;
  } else if (status == SettingsFiles::ReadStatus::Ok && files.exists(paths.backup)) {
    // Interrupted after the new file landed but before the .bak was dropped.
    files.remove(paths.backup);
  }

  switch (status) {
    case SettingsFiles::ReadStatus::Ok:
      return outcome;
    case SettingsFiles::ReadStatus::Missing:
      return SafeRead::Missing;
    case SettingsFiles::ReadStatus::TooLarge:
    case SettingsFiles::ReadStatus::Error:
    default:
      quarantine(files, paths, readFrom);  // the main file, or a .bak that couldn't be put back
      return SafeRead::Unreadable;
  }
}

bool replaceSafely(SettingsFiles& files, const SafeFilePaths& paths, const std::string_view content) {
  if (!files.ensureDir(paths.dir)) return false;
  if (!files.write(paths.tmp, content)) {
    files.remove(paths.tmp);
    return false;
  }
  if (files.exists(paths.backup)) files.remove(paths.backup);
  const bool hadFile = files.exists(paths.path);
  if (hadFile && !files.rename(paths.path, paths.backup)) {
    files.remove(paths.tmp);
    return false;
  }
  if (!files.rename(paths.tmp, paths.path)) {
    if (hadFile) files.rename(paths.backup, paths.path);
    files.remove(paths.tmp);
    return false;
  }
  if (hadFile) files.remove(paths.backup);
  return true;
}

}  // namespace lexipoint

#endif  // LEXIRISE
