#include <gtest/gtest.h>

#include <map>
#include <string>
#include <thread>

#include "lexirise/settings/SettingsStore.h"

using lexipoint::LoadOutcome;
using lexipoint::Settings;
using lexipoint::SettingsFiles;
using lexipoint::SettingsStore;
namespace config = lexipoint::config;

namespace {

// In-memory card with SdFat's rename semantics and one-shot failure injection.
class FakeFiles : public SettingsFiles {
 public:
  std::map<std::string, std::string> files;
  std::string failWriteOf;   // path whose next write fails
  std::string failRenameTo;  // destination whose next rename fails
  int writes = 0;

  ReadStatus read(const char* path, size_t maxBytes, std::string& out) override {
    const auto it = files.find(path);
    if (it == files.end()) return ReadStatus::Missing;
    if (it->second.size() > maxBytes) return ReadStatus::TooLarge;
    out = it->second;
    return ReadStatus::Ok;
  }
  bool write(const char* path, std::string_view content) override {
    writes++;
    if (failWriteOf == path) {
      failWriteOf.clear();
      files[path] = "[acc";  // torn
      return false;
    }
    files[path] = std::string(content);
    return true;
  }
  bool exists(const char* path) override { return files.count(path) != 0; }
  bool remove(const char* path) override { return files.erase(path) != 0; }
  bool rename(const char* from, const char* to) override {
    if (failRenameTo == to) {
      failRenameTo.clear();
      return false;
    }
    const auto it = files.find(from);
    if (it == files.end() || files.count(to) != 0) return false;
    files[to] = it->second;
    files.erase(it);
    return true;
  }
  bool ensureDir(const char*) override { return true; }
};

const std::string kKey = "lx_TESTKEYtestkey0123456789";

std::string fileWithKey(const std::string& key) {
  Settings s;
  s.apiKey = key;
  return lexipoint::serializeSettings(s);
}

}  // namespace

TEST(SettingsStore, FirstBootUsesDefaultsAndWritesNothing) {
  FakeFiles fs;
  SettingsStore store(fs);
  EXPECT_EQ(store.load(), LoadOutcome::Defaults);
  EXPECT_FALSE(store.snapshot().hasApiKey());
  EXPECT_EQ(fs.writes, 0);
}

TEST(SettingsStore, UpdateSavesAndLoadsBack) {
  FakeFiles fs;
  {
    SettingsStore store(fs);
    store.load();
    ASSERT_EQ(store.update([](Settings& s) {
      s.apiKey = kKey;
      return true;
    }),
              SettingsStore::UpdateResult::Saved);
    EXPECT_EQ(store.revision(), 1u);
  }
  SettingsStore reloaded(fs);
  EXPECT_EQ(reloaded.load(), LoadOutcome::Loaded);
  EXPECT_EQ(reloaded.snapshot().apiKey, kKey);
  EXPECT_EQ(fs.files.size(), 1u);  // no .tmp or .bak left behind
}

TEST(SettingsStore, FailedWriteKeepsOldFileAndMemory) {
  FakeFiles fs;
  fs.files[config::kSettingsPath] = fileWithKey(kKey);
  SettingsStore store(fs);
  store.load();
  fs.failWriteOf = config::kSettingsTmpPath;
  EXPECT_EQ(store.update([](Settings& s) {
    s.apiKey.clear();
    return true;
  }),
            SettingsStore::UpdateResult::SaveFailed);
  EXPECT_EQ(store.snapshot().apiKey, kKey);
  EXPECT_EQ(store.revision(), 0u);
  EXPECT_EQ(fs.files.at(config::kSettingsPath), fileWithKey(kKey));
  EXPECT_EQ(fs.files.count(config::kSettingsTmpPath), 0u);
}

TEST(SettingsStore, FailedFinalRenameRestoresOldFile) {
  FakeFiles fs;
  fs.files[config::kSettingsPath] = fileWithKey(kKey);
  SettingsStore store(fs);
  store.load();
  fs.failRenameTo = config::kSettingsPath;  // tmp → final fails once; the .bak → final restore works
  EXPECT_EQ(store.update([](Settings& s) {
    s.apiKey.clear();
    return true;
  }),
            SettingsStore::UpdateResult::SaveFailed);
  EXPECT_EQ(fs.files.at(config::kSettingsPath), fileWithKey(kKey));
  EXPECT_EQ(fs.files.size(), 1u);
}

TEST(SettingsStore, RecoversBackupFromInterruptedSave) {
  FakeFiles fs;  // crash after "final → .bak", before "tmp → final"
  fs.files[config::kSettingsBackupPath] = fileWithKey(kKey);
  fs.files[config::kSettingsTmpPath] = fileWithKey("lx_NEWERbutunfinished000");
  SettingsStore store(fs);
  EXPECT_EQ(store.load(), LoadOutcome::RecoveredBackup);
  EXPECT_EQ(store.snapshot().apiKey, kKey);
  EXPECT_EQ(fs.files.count(config::kSettingsPath), 1u);
  EXPECT_EQ(fs.files.size(), 1u);
}

TEST(SettingsStore, DropsStaleBackupWhenNewFileLanded) {
  FakeFiles fs;  // crash after "tmp → final", before ".bak" was removed
  fs.files[config::kSettingsPath] = fileWithKey(kKey);
  fs.files[config::kSettingsBackupPath] = fileWithKey("lx_OLDERkey0000000000000");
  SettingsStore store(fs);
  EXPECT_EQ(store.load(), LoadOutcome::Loaded);
  EXPECT_EQ(store.snapshot().apiKey, kKey);
  EXPECT_EQ(fs.files.size(), 1u);
}

TEST(SettingsStore, OversizedFileIsLeftAloneWithDefaults) {
  FakeFiles fs;
  fs.files[config::kSettingsPath] = std::string(config::kSettingsMaxBytes + 1, '#');
  SettingsStore store(fs);
  EXPECT_EQ(store.load(), LoadOutcome::Unreadable);
  EXPECT_FALSE(store.snapshot().hasApiKey());
  EXPECT_EQ(fs.files.at(config::kSettingsPath).size(), config::kSettingsMaxBytes + 1);
}

TEST(SettingsStore, LegacyFileIsRewrittenInSections) {
  FakeFiles fs;
  fs.files[config::kSettingsPath] = "api_key = " + kKey + "\n";
  SettingsStore store(fs);
  store.load();
  EXPECT_EQ(store.snapshot().apiKey, kKey);
  EXPECT_NE(fs.files.at(config::kSettingsPath).find("[account]"), std::string::npos);
}

TEST(SettingsStore, SnapshotsNeverTearUnderConcurrentUpdates) {
  FakeFiles fs;
  SettingsStore store(fs);
  store.load();
  constexpr int kUpdates = 200;
  std::thread writer([&] {
    for (int i = 0; i < kUpdates; i++) {
      store.update([i](Settings& s) {
        s.tags = "t" + std::to_string(i);
        s.baseUrl = "https://h" + std::to_string(i) + ".example";
        return true;
      });
    }
  });
  for (int i = 0; i < kUpdates; i++) {
    const Settings s = store.snapshot();
    if (s.tags == config::kDefaultTags) continue;
    EXPECT_EQ("https://h" + s.tags.substr(1) + ".example", s.baseUrl);
  }
  writer.join();
  EXPECT_EQ(store.revision(), static_cast<uint32_t>(kUpdates));
}

TEST(SettingsStore, DeclinedUpdateWritesNothing) {
  FakeFiles fs;
  SettingsStore store(fs);
  store.load();
  EXPECT_EQ(store.update([](Settings& s) {
    s.apiKey = kKey;  // changed, then declined: must not stick
    return false;
  }),
            SettingsStore::UpdateResult::Declined);
  EXPECT_FALSE(store.snapshot().hasApiKey());
  EXPECT_EQ(fs.writes, 0);
  EXPECT_EQ(store.revision(), 0u);
}
