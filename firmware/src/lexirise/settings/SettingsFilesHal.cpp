#if LEXIRISE

// SD card adapter for the stores (SafeFile.h), and the device-wide store instances.

#include <HalStorage.h>
#include <Logging.h>

#include "BookLanguages.h"
#include "BookTags.h"
#include "SettingsStore.h"

namespace lexipoint {
namespace {

constexpr const char* kLogTag = "LXS";

class HalSettingsFiles final : public SettingsFiles {
 public:
  ReadStatus read(const char* path, const size_t maxBytes, std::string& out) override {
    if (!Storage.exists(path)) return ReadStatus::Missing;
    HalFile file;
    if (!Storage.openFileForRead(kLogTag, path, file)) return ReadStatus::Error;
    const size_t size = file.fileSize();
    if (size > maxBytes) {
      LOG_ERR(kLogTag, "%s is %u bytes (limit %u), ignoring it", path, (unsigned)size, (unsigned)maxBytes);
      return ReadStatus::TooLarge;
    }
    out.assign(size, '\0');
    const int got = size == 0 ? 0 : file.read(out.data(), size);
    if (got < 0 || static_cast<size_t>(got) != size) return ReadStatus::Error;
    return ReadStatus::Ok;
  }

  bool write(const char* path, const std::string_view content) override {
    HalFile file;
    if (!Storage.openFileForWrite(kLogTag, path, file)) return false;
    const size_t written = file.write(content.data(), content.size());
    file.flush();
    if (written != content.size()) {
      LOG_ERR(kLogTag, "Short write to %s: %u/%u", path, (unsigned)written, (unsigned)content.size());
      return false;
    }
    return true;  // closed by the destructor before the caller renames it
  }

  bool exists(const char* path) override { return Storage.exists(path); }
  bool remove(const char* path) override { return Storage.remove(path); }
  bool rename(const char* from, const char* to) override { return Storage.rename(from, to); }
  bool ensureDir(const char* path) override { return Storage.ensureDirectoryExists(path); }
};

HalSettingsFiles& halFiles() {
  static HalSettingsFiles files;
  return files;
}

}  // namespace

SettingsStore& settingsStore() {
  static SettingsStore store(halFiles());
  return store;
}

BookLanguageStore& bookLanguageStore() {
  static BookLanguageStore store(halFiles());
  return store;
}

BookTagStore& bookTagStore() {
  static BookTagStore store(halFiles());
  return store;
}

}  // namespace lexipoint

#endif  // LEXIRISE
