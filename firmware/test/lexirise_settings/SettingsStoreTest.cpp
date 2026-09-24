#include <gtest/gtest.h>

#include <map>
#include <string>
#include <thread>

#include "Fakes.h"
#include "lexirise/settings/SettingsStore.h"

using lexipoint::LoadOutcome;
using lexipoint::Settings;
using lexipoint::SettingsStore;
namespace config = lexipoint::config;

namespace {

using lexipoint::fakes::FakeFiles;

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

TEST(SettingsStore, OversizedFileIsMovedAsideNotOverwritten) {
  FakeFiles fs;
  const std::string big = fileWithKey(kKey) + std::string(config::kSettingsMaxBytes, '#');
  fs.files[config::kSettingsPath] = big;
  fs.files[config::kSettingsBadPath] = "older";
  SettingsStore store(fs);
  EXPECT_EQ(store.load(), LoadOutcome::Unreadable);
  EXPECT_FALSE(store.snapshot().hasApiKey());
  EXPECT_EQ(fs.files.at(config::kSettingsBadPath), big);
  // A later save writes a fresh file; the user's original is still in .bad.
  ASSERT_EQ(store.update([](Settings& s) {
    s.tags = "x";
    return true;
  }),
            SettingsStore::UpdateResult::Saved);
  EXPECT_EQ(fs.files.at(config::kSettingsBadPath), big);
  EXPECT_EQ(fs.files.count(config::kSettingsPath), 1u);
}

TEST(SettingsStore, ReadErrorAlsoKeepsTheFile) {
  FakeFiles fs;
  fs.files[config::kSettingsPath] = fileWithKey(kKey);
  fs.failReads = true;
  SettingsStore store(fs);
  EXPECT_EQ(store.load(), LoadOutcome::Unreadable);
  EXPECT_EQ(fs.files.at(config::kSettingsBadPath), fileWithKey(kKey));
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
