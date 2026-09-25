#if LEXIRISE

#include "LexiriseSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <memory>

#include "MappedInputManager.h"
#include "SettingsStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "lexirise/LexiriseService.h"
#include "util/DictionaryRegistry.h"

namespace fui = freeink::ui;

namespace lexipoint {
namespace {

using settings_screen::AccountLine;
using settings_screen::Group;
using settings_screen::Row;

constexpr const char* kLogTag = "LXSET";
// Room for the tag list as settings.md §1 allows it (normaliseTags caps it anyway).
constexpr size_t kTagsMaxInput = config::kMaxTags * (config::kMaxTagLength + 1);
constexpr size_t kMinutesValueBytes = 32;  // "%d min" in any translation
constexpr int16_t kValueInset = 8;         // air between the value and the row edge, as KOReaderSettingsActivity

StrId headingFor(const Group group) {
  switch (group) {
    case Group::Account:
      return StrId::STR_LEXI_SET_ACCOUNT;
    case Group::Japanese:
      return StrId::STR_LEXI_SET_JAPANESE;
    case Group::Chinese:
      return StrId::STR_LEXI_SET_CHINESE;
    case Group::General:
      break;
  }
  return StrId::STR_LEXI_SET_GENERAL;
}

StrId labelFor(const Row row) {
  switch (row) {
    case Row::Lookups:
      return StrId::STR_LEXI_SET_LEXIRISE_LOOKUPS;
    case Row::ApiKey:
      return StrId::STR_LEXI_SET_API_KEY;
    case Row::Account:
      return StrId::STR_LEXI_SET_ACCOUNT;
    case Row::TestConnection:
      return StrId::STR_LEXI_SET_TEST;
    case Row::JaLookups:
    case Row::ZhLookups:
      return StrId::STR_LEXI_SET_LOOKUPS;
    case Row::JaReading:
      return StrId::STR_LEXI_SET_READINGS;
    case Row::JaDictionary:
    case Row::ZhDictionary:
      return StrId::STR_LEXI_SET_DICTIONARY;
    case Row::DefaultLanguage:
      return StrId::STR_LEXI_SET_DEFAULT_LANGUAGE;
    case Row::Tags:
      return StrId::STR_LEXI_SET_TAGS;
    case Row::WifiIdle:
      break;
  }
  return StrId::STR_LEXI_SET_WIFI_IDLE;
}

const char* onOff(const bool on) { return on ? tr(STR_STATE_ON) : tr(STR_STATE_OFF); }

std::string dictionaryValue(const std::string& folder) {
  return folder.empty() ? tr(STR_LEXI_SET_SAME_AS_CROSSPOINT) : folder;
}

std::string accountValue(const bool hasKey, const api::KeyStatus& status) {
  switch (settings_screen::accountLine(hasKey, status.state)) {
    case AccountLine::NotSet:
      return tr(STR_NOT_SET);
    case AccountLine::NotChecked:
      return tr(STR_LEXI_SET_NOT_CHECKED);
    case AccountLine::Checking:
      return tr(STR_LEXI_SET_CHECKING);
    case AccountLine::Connected: {
      // The name and plan are shown here only, never logged (settings.md §1a).
      std::string line = status.me.name;
      if (!status.me.plan.empty())
        line += (line.empty() ? "" : std::string(tr(STR_LEXI_CARD_SEPARATOR))) + status.me.plan;
      return line.empty() ? tr(STR_LEXI_SET_CONNECTED) : line;
    }
    case AccountLine::KeyRejected:
      return tr(STR_LEXI_SET_KEY_REJECTED);
    case AccountLine::NoNetwork:
      return tr(STR_LEXI_SET_NO_NETWORK);
    case AccountLine::CouldNotConnect:
      break;
  }
  return tr(STR_LEXI_SET_COULD_NOT_CONNECT);
}

}  // namespace

LexiriseSettingsActivity::LexiriseSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("LexiriseSettings", renderer, mappedInput) {}

void LexiriseSettingsActivity::onEnter() {
  std::vector<DictionaryEntry> found;
  DictionaryRegistry::discover(found);
  std::vector<std::string> names;
  names.reserve(found.size());
  for (auto& d : found) names.push_back(std::move(d.name));
  dictionaries_ = settings_screen::offeredDictionaries(std::move(names));
  refreshStatus();
  {
    std::lock_guard<std::mutex> lock(rowsMutex_);
    drawnRows_ = settings_screen::visibleRows(settingsStore().snapshot());
  }
  UiListActivity::onEnter();
}

settings_screen::Rows LexiriseSettingsActivity::drawnRows() const {
  std::lock_guard<std::mutex> lock(rowsMutex_);
  return drawnRows_;
}

int LexiriseSettingsActivity::listCount() const { return static_cast<int>(drawnRows().count); }

const char* LexiriseSettingsActivity::headerTitle() const { return tr(STR_LEXIRISE); }

void LexiriseSettingsActivity::refreshStatus() {
  const api::KeyStatus status = service().keyStatus();
  std::lock_guard<std::mutex> lock(statusMutex_);
  status_ = status;
}

void LexiriseSettingsActivity::setNotice(const Notice notice) {
  std::lock_guard<std::mutex> lock(statusMutex_);
  notice_ = notice;
}

const char* LexiriseSettingsActivity::noticeFor(const Row row) const {
  std::lock_guard<std::mutex> lock(statusMutex_);
  return notice_.text && notice_.row == row ? notice_.text : nullptr;
}

void LexiriseSettingsActivity::apply(const Row row, const SettingsPatch& patch) {
  PatchResult applied;
  bool hasKey = false;
  const auto result = settingsStore().update([&](Settings& s) {
    applied = applyPatch(s, patch);
    hasKey = s.hasApiKey();
    return applied.ok;
  });
  switch (settings_screen::editResult(row, result)) {
    case settings_screen::EditResult::NotAKey:
      LOG_ERR(kLogTag, "Refused %s", applied.field ? applied.field : "?");
      setNotice({row, tr(STR_LEXI_SET_KEY_INVALID)});
      return;
    case settings_screen::EditResult::NotSaved:
      LOG_ERR(kLogTag, "Not saved (%s)", applied.field ? applied.field : "the card");
      setNotice({row, tr(STR_LEXI_SET_SAVE_FAILED)});
      return;
    case settings_screen::EditResult::Saved:
      break;
  }
  setNotice({});
  // A new key: the old status says nothing about it, and it's checked straight away (settings.md §2), from
  // the next loop pass (this can run in the keyboard's result handler, mid activity switch).
  if (applied.keyChanged) {
    service().invalidateKeyStatus();
    checkPending_ = hasKey;
  }
  refreshStatus();
}

void LexiriseSettingsActivity::editWithKeyboard(const Row row) {
  const bool key = row == Row::ApiKey;
  // The key box starts empty: the device never shows the stored key, and an empty entry keeps it.
  std::string initial = key ? std::string() : settingsStore().snapshot().tags;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(labelFor(row)), std::move(initial),
                                              key ? config::kApiKeyMaxLength : kTagsMaxInput,
                                              key ? InputType::Password : InputType::Text),
      [this, key, row](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto& kb = std::get<KeyboardResult>(result.data);
        SettingsPatch patch;
        if (key) {
          patch.apiKey = kb.text;
        } else {
          patch.tags = kb.text;
        }
        apply(row, patch);
      });
}

void LexiriseSettingsActivity::testConnection() {
  service().requestKeyCheck();  // reads Checking while the check runs
  refreshStatus();
  requestUpdateAndWait();
  service().checkKey();
  // Settings isn't reading: WiFi Lexipoint brought up for the check goes back now (net/WifiLease.h).
  service().releaseWifi();
  refreshStatus();
}

bool LexiriseSettingsActivity::handleCustomInput() {
  if (!checkPending_) return false;
  checkPending_ = false;
  testConnection();
  redraw();
  return true;
}

void LexiriseSettingsActivity::redraw() {
  tapGate_.redrawAsked();
  requestUpdate();
}

void LexiriseSettingsActivity::render(RenderLock&& lock) {
  UiListActivity::render(std::move(lock));  // builds the rows, then the panel refresh
  tapGate_.frameShown();
}

void LexiriseSettingsActivity::activateIndex(const int index) {
  if (!tapGate_.accepts()) return;  // the frame of the last edit isn't on the panel yet
  const std::optional<Row> tapped = settings_screen::rowAt(drawnRows(), index);
  if (!tapped) return;
  const Row row = *tapped;
  switch (settings_screen::editFor(row)) {
    case settings_screen::Edit::Patch:
      if (const auto patch = settings_screen::tapPatch(row, settingsStore().snapshot(), dictionaries_))
        apply(row, *patch);
      break;
    case settings_screen::Edit::Keyboard:
      app.clearTapFlash();  // leaves the screen
      editWithKeyboard(row);
      return;
    case settings_screen::Edit::Test:
      app.clearTapFlash();  // the check blocks: its result mustn't show the row still grayed
      setNotice({});
      testConnection();
      break;
    case settings_screen::Edit::None:
      return;
  }
  redraw();
}

std::string LexiriseSettingsActivity::valueFor(const Row row, const Settings& settings) const {
  switch (row) {
    case Row::Lookups:
      return onOff(settings.enabled);
    case Row::ApiKey:
      return settings.hasApiKey() ? maskApiKey(settings.apiKey) : tr(STR_NOT_SET);
    case Row::Account: {
      std::lock_guard<std::mutex> lock(statusMutex_);
      return accountValue(settings.hasApiKey(), status_);
    }
    case Row::TestConnection:
      return {};
    case Row::JaLookups:
      return onOff(settings.japanese.enabled);
    case Row::JaReading:
      return settings.japaneseReading == Reading::Kana ? tr(STR_LEXI_SET_KANA) : tr(STR_LEXI_SET_ROMAJI);
    case Row::JaDictionary:
      return dictionaryValue(settings.japanese.stardict);
    case Row::ZhLookups:
      return onOff(settings.chinese.enabled);
    case Row::ZhDictionary:
      return dictionaryValue(settings.chinese.stardict);
    case Row::DefaultLanguage:
      return settings.defaultLanguage == Language::Japanese ? tr(STR_LEXI_SET_JAPANESE) : tr(STR_LEXI_SET_CHINESE);
    case Row::Tags:
      return settings.tags;
    case Row::WifiIdle:
      break;
  }
  if (settings.wifiIdleMin == 0) return tr(STR_STATE_OFF);
  char minutes[kMinutesValueBytes];
  std::snprintf(minutes, sizeof(minutes), tr(STR_LEXI_SET_MINUTES), settings.wifiIdleMin);
  return minutes;
}

void LexiriseSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints (as KOReaderSettingsActivity).
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  tapGate_.building();
  const Settings settings = settingsStore().snapshot();
  const auto rows = settings_screen::visibleRows(settings);
  {
    std::lock_guard<std::mutex> lock(rowsMutex_);
    drawnRows_ = rows;  // before syncListViewport reads listCount()
  }
  for (size_t i = 0; i < rows.count; i++) {
    const Row row = rows[i];
    auto& item = rowItems_[i];
    item = fui::ListItem{};
    item.label = I18N.get(labelFor(row));
    item.actionValue = static_cast<int16_t>(i);
    if (settings_screen::startsGroup(rows, i))
      item.sectionHeading = I18N.get(headingFor(settings_screen::groupOf(row)));
    const char* notice = noticeFor(row);
    rowValues_[i] = notice ? std::string(notice) : valueFor(row, settings);
    item.value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

#if LEXIPOINT_DEV_HARNESS
  LOG_INF(kLogTag, "rows %u", static_cast<unsigned>(rows.count));  // lxctl settings-smoke
#endif

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(rows.count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = kValueInset;
  // Label at the value's font size, up to 2 lines (as KOReaderSettingsActivity).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

}  // namespace lexipoint

#endif  // LEXIRISE
