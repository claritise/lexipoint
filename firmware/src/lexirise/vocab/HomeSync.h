#pragma once

// The home screen's side of Sync Vocabulary (v0.2 V7b; vocab/ManualSync.h decides, this drives it on the device): WiFi
// held up while it runs (the idle teardown can't close it between pages), one page per loop pass of the home screen,
// the popup's text and progress (GUI.drawPopup, GUI.fillPopupProgress: the look claritise signed off, 2026-09-28), and
// the result shown for config::kVocabSyncResultMs. Device only; the logic is ManualSync's.

#include <memory>
#include <string>

class GfxRenderer;

namespace lexipoint::vocab {

class ManualSync;

// Whether the home screen shows the row (syncRowShown over the settings now).
bool homeSyncRowShown();
// "Synced · N words changed", or "Synced · 1 word changed" for one (claritise, 2026-09-28).
std::string syncedText(unsigned changed);

class HomeSync {
 public:
  HomeSync();
  ~HomeSync();
  HomeSync(const HomeSync&) = delete;
  HomeSync& operator=(const HomeSync&) = delete;
  bool ok() const { return sync_ != nullptr; }
  // One step: the join first, then a page (blocking; any input gives it up). Outside RenderLock; then refresh().
  void step();
  void stop();  // input between steps (then refresh())
  // What draw() shows, worked out from the sync's state: under RenderLock, after each step and stop (the render task
  // reads only this, never the sync while a step changes it).
  void refresh();
  bool running() const;
  // The result has been up long enough: the home screen goes back to its menu.
  bool over(unsigned long nowMs) const;
  // Over the home screen as drawn: the popup, with the progress bar while it runs.
  void draw(const GfxRenderer& renderer) const;

 private:
  std::string message() const;
  std::unique_ptr<ManualSync> sync_;
  unsigned long doneMs_ = 0;
  struct Shown {
    std::string text;
    int percent = 0;
    bool bar = true;
  } shown_;
};

}  // namespace lexipoint::vocab
