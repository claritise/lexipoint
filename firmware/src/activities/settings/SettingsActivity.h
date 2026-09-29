#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "SettingInfo.h"
#include "activities/UiTabListActivity.h"
#include "components/OptionPopup.h"

class SettingsActivity final : public UiTabListActivity {
  int selectedCategoryIndex = 0;  // Currently selected category
  int settingsCount = 0;

  // Per-category settings derived from shared list + device-only actions
  std::vector<SettingInfo> displaySettings;
  std::vector<SettingInfo> readerSettings;
  std::vector<SettingInfo> controlsSettings;
  std::vector<SettingInfo> systemSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;

  bool preserveQuickResumeTimeoutOn = false;
  bool quickResumeTimeoutAutoEnabled = false;

  OptionPopup optionPopup;

  // Row structure (label/actionValue) for *currentSettings, rebuilt only when
  // the active category or a category's setting list changes
  // (rebuildRowItems(), called from selectCategory()/rebuildSettingsLists())
  // — not on every repaint. rowValues_ holds the live per-row value text,
  // refreshed every buildScreen() call by assigning into the existing
  // strings (no vector growth).
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;
  void rebuildRowItems();

  static constexpr int categoryCount = 4;
  static constexpr StrId categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                         StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};

  // --- UiTabListActivity contract ---
  int listCount() const override { return settingsCount; }
  int tabCount() const override { return categoryCount; }
  int activeTab() const override { return selectedCategoryIndex; }
  const char* tabLabel(int index) const override { return I18N.get(categoryNames[index]); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleButtons() override;
  bool handleCustomInput() override;

  static std::string settingValueText(const SettingInfo& setting);
  void selectCategory(int categoryIndex);

  void enterCategory(int categoryIndex);
  void toggleCurrentSetting();
  void openSleepTimeoutPicker();
  void rebuildLibraryIndex();
  void rebuildSettingsLists();
  void syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged);

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void render(RenderLock&&) override;
};
