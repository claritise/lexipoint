#pragma once

// Settings → System → Lexirise (settings.md §1), CrossPoint's own list screen as KOReaderSettingsActivity
// uses it. The rows and the edits are settings_screen's (pure, tested); this draws them, runs the
// keyboard for the key and the tags, and the connection test.

#if LEXIRISE

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "SettingsScreen.h"
#include "activities/UiListActivity.h"

namespace lexipoint {

class LexiriseSettingsActivity final : public UiListActivity {
 public:
  LexiriseSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  bool handleCustomInput() override;
  void render(RenderLock&& lock) override;  // UiListActivity's, then the tap gate opens
  void redraw();                            // requestUpdate(), closing the tap gate until its frame is up

  void apply(settings_screen::Row row, const SettingsPatch& patch);  // `row`: where a failure is shown
  void editWithKeyboard(settings_screen::Row row);
  void testConnection();
  void refreshStatus();  // main task: copies the service's key status for the render task
  std::string valueFor(settings_screen::Row row, const Settings& settings) const;
  settings_screen::Rows drawnRows() const;

  // The rows of the frame last built (buildScreen, render task), which the list's touch targets and
  // selection were laid out for: listCount() and activateIndex() (main task) read these, so an index always
  // means the row that was built at it, even when a toggle has since hidden rows. Set in onEnter before the
  // first render.
  mutable std::mutex rowsMutex_;
  settings_screen::Rows drawnRows_;
  settings_screen::TapGate tapGate_;
  bool checkPending_ = false;              // a new key was saved: check it on the next loop pass
  std::vector<std::string> dictionaries_;  // StarDict folders on the card, found once in onEnter (before any render)
  // Set on the main task, read by the render task (buildScreen), under statusMutex_:
  mutable std::mutex statusMutex_;
  api::KeyStatus status_;  // the key check as last seen on the main task (the service is main-task only)
  // The last edit's failure, shown as the value of the row it was made on until the next edit.
  struct Notice {
    settings_screen::Row row = settings_screen::Row::ApiKey;
    const char* text = nullptr;  // nullptr: none
  };
  Notice notice_;
  void setNotice(Notice notice);
  const char* noticeFor(settings_screen::Row row) const;  // nullptr: none on that row

  // Fixed row storage, as KOReaderSettingsActivity: values are assigned into existing strings.
  std::string rowValues_[settings_screen::kRowCount];
  freeink::ui::ListItem rowItems_[settings_screen::kRowCount]{};
};

}  // namespace lexipoint

#endif  // LEXIRISE
