#include "HomeSync.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <ctime>

#include "ManualSync.h"
#include "components/UITheme.h"
#include "lexirise/LexiriseService.h"
#include "lexirise/input/InputAbort.h"
#include "lexirise/settings/SettingsStore.h"
#include "lexirise/util/Epoch.h"

namespace lexipoint::vocab {
namespace {

api::ApiError join() { return service().joinForUser(); }

api::ApiError blocked() {
  switch (service().blocked()) {
    case api::AccessPolicy::Block::RateLimited:
      return api::ApiError::RateLimited;
    case api::AccessPolicy::Block::Rejected:
      return api::ApiError::Unauthorized;
    case api::AccessPolicy::Block::None:
    default:
      return api::ApiError::None;
  }
}

ManualSync::KeyHour keyUse() { return {service().requestsLastHour(), service().rateLimit()}; }

}  // namespace

bool homeSyncRowShown() { return syncRowShown(settingsStore().snapshot()); }

std::string syncedText(const unsigned changed) {
  char text[64];
  if (changed == 1) return tr(STR_LEXI_VOCABULARY_SYNCED_ONE);  // "1 word changed" (claritise, 2026-09-28)
  std::snprintf(text, sizeof(text), tr(STR_LEXI_VOCABULARY_SYNCED), changed);
  return text;
}

HomeSync::HomeSync() {
  sync_ = makeUniqueNoThrow<ManualSync>(vocabStore(), service(), join, settingsStore().snapshot(), timing::epochNowS,
                                        blocked, keyUse);
  if (sync_) {
    service().holdWifi(true);  // up between pages, whatever the idle setting
  } else {
    LOG_ERR(kLogTag, "home sync: OOM (%u bytes)", static_cast<unsigned>(sizeof(ManualSync)));
  }
  refresh();
}

HomeSync::~HomeSync() {
  if (!sync_) return;
  service().holdWifi(false);
  // Given back now, not after wifi_idle_min: nothing on the home screen uses it (offline-and-errors.md §5); a network
  // someone else brought up is left alone (only a lease Lexipoint holds is released).
  const unsigned long start = millis();
  const bool released = service().releaseWifi();
  const char* wifi = released ? "given back" : sync_->joined() ? "left up (not Lexipoint's)" : "not up";
  LOG_INF(kLogTag, "home sync: WiFi %s in %lu ms", wifi, millis() - start);
}

void HomeSync::step() {
  if (!sync_ || !running()) return;
  const uint32_t epochS = timing::epochNowS();
  if (sync_->step(millis(), epochS, input::inputCame) != ManualSync::Result::Running) doneMs_ = millis();
}

void HomeSync::stop() {
  if (!sync_ || !running()) return;
  sync_->stop();
  doneMs_ = millis();
}

bool HomeSync::running() const { return sync_ && sync_->result() == ManualSync::Result::Running; }

bool HomeSync::over(const unsigned long nowMs) const {
  return !sync_ || (!running() && nowMs - doneMs_ >= config::kVocabSyncResultMs);
}

std::string HomeSync::message() const {
  if (!sync_) return tr(STR_LEXI_SYNC_FAILED);
  switch (sync_->result()) {
    case ManualSync::Result::Running:
      return tr(STR_LEXI_SYNCING_VOCABULARY);
    case ManualSync::Result::UpToDate:
      return tr(STR_LEXI_VOCABULARY_UP_TO_DATE);
    case ManualSync::Result::Synced:
      return syncedText(sync_->changed());
    case ManualSync::Result::NoWifi:
      return tr(STR_LEXI_SYNC_NO_WIFI);
    case ManualSync::Result::Stopped:
      return tr(STR_LEXI_SYNC_STOPPED);
    case ManualSync::Result::Failed:
    default:
      // A rejected key or a rate limit in the words the card uses for them; anything else plainly (signed off).
      if (sync_->error() == api::ApiError::Unauthorized) return tr(STR_LEXI_AUTH_FAILED);
      if (sync_->error() == api::ApiError::RateLimited) return tr(STR_LEXI_RATE_LIMITED);
      return tr(STR_LEXI_SYNC_FAILED);
  }
}

void HomeSync::refresh() {
  shown_.text = message();
  shown_.bar = running();
  shown_.percent = shown_.bar ? static_cast<int>(sync_->percent()) : 100;
}

void HomeSync::draw(const GfxRenderer& renderer) const {
  const Rect popup = GUI.drawPopup(renderer, shown_.text.c_str());
  if (shown_.bar) GUI.fillPopupProgress(renderer, popup, shown_.percent);
}

}  // namespace lexipoint::vocab
