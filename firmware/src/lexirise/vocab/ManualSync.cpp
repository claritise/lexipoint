#if LEXIRISE

#include "ManualSync.h"

#include <Logging.h>

#include <algorithm>

#include "lexirise/api/RequestWindow.h"

namespace lexipoint::vocab {
namespace {

// The progress bar's share of a language once its incremental pass has started with no count to go by (a probe is
// most of a quiet pass).
constexpr unsigned kProbeShare = 50;

}  // namespace

bool syncRowShown(const Settings& settings) {
  return settings.enabled && settings.hasApiKey() && settings.enabledLanguageCount() > 0;
}

ManualSync::ManualSync(VocabStore& store, api::LexiriseApi& api, const Join join, const Settings& settings,
                       const WallClock wall, const Blocked blocked, const KeyUse keyUse)
    : store_(store), api_(api), join_(join), wall_(wall), blocked_(blocked), keyUse_(keyUse) {
  languages_.reserve(std::size(kLanguages));
  for (const Language l : kLanguages) {
    if (settings.language(l).enabled) languages_.push_back(l);
  }
}

ManualSync::Result ManualSync::finish(const Result r) {
  result_ = r;
  LOG_INF(kLogTag, "home sync: %s after %u pages, %u words changed",
          r == Result::UpToDate  ? "up to date"
          : r == Result::Synced  ? "synced"
          : r == Result::NoWifi  ? "no WiFi"
          : r == Result::Stopped ? "stopped"
                                 : "failed",
          pages_, changed());
  return r;
}

bool ManualSync::keyHourLeft() const {
  if (!keyUse_) return true;
  const KeyHour hour = keyUse_();
  return api::belowPercent(hour.used, hour.limit, config::kVocabManualSyncStopPercent);
}

void ManualSync::stop() {
  if (result_ == Result::Running) finish(Result::Stopped);
}

ManualSync::Result ManualSync::step(const unsigned long nowMs, const uint32_t epochS,
                                    const api::VocabPageReader::Cancel cancel) {
  if (result_ != Result::Running) return result_;
  if (!joinTried_) {
    joinTried_ = true;
    if (!store_.manualBudgetLeft(nowMs) || !keyHourLeft()) {  // the hour spent: no radio, "rate limited"
      error_ = api::ApiError::RateLimited;
      return finish(Result::Failed);
    }
    if (const api::ApiError refused = blocked_ ? blocked_() : api::ApiError::None; refused != api::ApiError::None) {
      error_ = refused;  // a 429's wait or a rejected key: no radio, the card's words for it
      return finish(Result::Failed);
    }
    const api::ApiError joined = join_ ? join_() : api::ApiError::None;
    joined_ = joined == api::ApiError::None && join_ != nullptr;
    if (joined == api::ApiError::NoWifi || joined == api::ApiError::NoWifiSaved) return finish(Result::NoWifi);
    if (joined != api::ApiError::None) {
      error_ = joined;
      return finish(Result::Failed);
    }
    for (const Language l : languages_) store_.load(l);  // SD I/O: read once per boot
    return result_;
  }
  while (at_ < languages_.size()) {
    const Language language = languages_[at_];
    if (pages_ >= config::kVocabManualSyncPagesMax || !store_.manualBudgetLeft(nowMs) || !keyHourLeft()) break;
    const std::optional<PagePlan> plan = store_.manualNext(language, epochS, incStarted_);
    if (!plan) {  // this language is done
      at_++;
      incStarted_ = false;
      continue;
    }
    if (plan->startsIncremental) incStarted_ = true;
    PageCall call = sendPage(api_, *plan, cancel);
    call.manual = true;
    pages_ += call.sent ? 1 : 0;
    const uint32_t readS = wall_ ? wall_() : 0;  // after the call: it may have set the clock
    const PageApplied applied = store_.apply(call, nowMs, readS != 0 ? readS : epochS);
    for (const uint32_t id : applied.changedEntries) {
      if (std::find(changed_.begin(), changed_.end(), id) == changed_.end()) changed_.push_back(id);
    }
    if (call.cancelled) return finish(Result::Stopped);
    if (call.error == api::ApiError::NoWifi) return finish(Result::NoWifi);
    if (call.error != api::ApiError::None) {
      error_ = call.error;
      return finish(Result::Failed);
    }
    return result_;  // one page per step
  }
  // Capped with a pass still under way: not "up to date" (there's more to read, at the next press or on idle cards).
  const bool capped = at_ < languages_.size();
  return finish(changed_.empty() && !capped ? Result::UpToDate : Result::Synced);
}

unsigned ManualSync::percent() {
  if (languages_.empty() || result_ != Result::Running) return 100;
  unsigned within = 0;
  if (at_ < languages_.size()) {
    // A pass with a count says how far it is; else a probe's page is most of an incremental pass.
    within = store_.passPercent(languages_[at_]).value_or(incStarted_ ? kProbeShare : 0);
  }
  return static_cast<unsigned>((at_ * 100 + within) / languages_.size());
}

}  // namespace lexipoint::vocab

#endif  // LEXIRISE
