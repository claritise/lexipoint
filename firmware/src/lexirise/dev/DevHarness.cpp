#if LEXIPOINT_DEV_HARNESS

#include "DevHarness.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <InputManager.h>
#include <Logging.h>

#include "DevConfig.h"
#include "DevCoords.h"
#include "DevGesture.h"
#include "DevProtocol.h"
#include "DevTiming.h"
#include "activities/Activity.h"  // complete type needed by ActivityManager.h (via ReaderUtils.h)
#include "activities/RenderLock.h"
#include "activities/reader/ReaderUtils.h"
#if LEXIRISE
#include "lexirise/LexiriseService.h"
#include "lexirise/api/Responses.h"
#endif

namespace lexipoint::dev {
namespace {

static_assert(static_cast<int>(Orientation::Portrait) == GfxRenderer::Portrait &&
                  static_cast<int>(Orientation::LandscapeClockwise) == GfxRenderer::LandscapeClockwise &&
                  static_cast<int>(Orientation::PortraitInverted) == GfxRenderer::PortraitInverted &&
                  static_cast<int>(Orientation::LandscapeCounterClockwise) == GfxRenderer::LandscapeCounterClockwise,
              "DevCoords Orientation must mirror GfxRenderer::Orientation");
static_assert(config::kLongHeldMs >= ReaderUtils::SKIP_HOLD_MS,
              "an injected long-press must be long enough for the reader to treat it as one");

GfxRenderer* gRenderer = nullptr;
HalDisplay* gDisplay = nullptr;
LineAssembler gAssembler;
GestureQueue gQueue;

KeepAwake gKeepAwake;  // see DevTiming.h
ButtonPress gButton;   // fed to the SDK through its button hook (normal debounce, held time, edges)

// LX:SYNC.
bool gSyncPending = false;
unsigned long gSyncStartMs = 0;
unsigned gSyncStablePolls = 0;
unsigned gFramesSinceFeed = 0;

uint8_t buttonHook() { return gButton.tick(millis()); }

Orientation orientation() { return static_cast<Orientation>(gRenderer->getOrientation()); }
int panelWidth() { return gDisplay->getDisplayWidth(); }
int panelHeight() { return gDisplay->getDisplayHeight(); }

bool inScreen(const int x, const int y) {
  return x >= 0 && y >= 0 && x < gRenderer->getScreenWidth() && y < gRenderer->getScreenHeight();
}

uint32_t crc32(const uint8_t* data, const size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

void ok(const Verb verb) { logSerial.printf("LX:OK %s\n", verbName(verb)); }
void err(const char* reason) { logSerial.printf("LX:ERR %s\n", reason); }

// Writes everything or gives up at the deadline (a host that stopped reading).
bool writeAll(const uint8_t* data, const size_t len) {
  const unsigned long start = millis();
  size_t sent = 0;
  while (sent < len) {
    const size_t n = logSerial.write(data + sent, len - sent);
    sent += n;
    if (n == 0) {
      if (millis() - start > config::kShotWriteDeadlineMs) return false;
      delay(1);
    }
  }
  return true;
}

void queueGesture(const Gesture& g, const Verb verb) {
  if (!gQueue.enqueue(g)) return err("queue full");
  ok(verb);
}

void pressButton(const ButtonName name, const int ms) {
  uint8_t bit = InputManager::BTN_UP;                           // X4 Pro left page key
  if (name == ButtonName::Right) bit = InputManager::BTN_DOWN;  // right page key
  if (name == ButtonName::Power) bit = InputManager::BTN_POWER;
  if (!gButton.start(bit, millis(), static_cast<unsigned long>(ms))) return err("button busy");
  ok(Verb::Button);
}

void screenshot(const bool legacy) {
  RenderLock lock;  // no render may draw into (or borrow) the buffer while it's read
  if (!gRenderer->hasFrameBuffer()) return err("framebuffer unavailable, retry");
  const uint8_t* buf = gDisplay->getFrameBuffer();
  const uint32_t size = gDisplay->getBufferSize();
  if (legacy) {
    logSerial.printf("SCREENSHOT_START:%lu\n", static_cast<unsigned long>(size));
    if (!writeAll(buf, size)) LOG_ERR("LXDEV", "CMD:SCREENSHOT short write (host stopped reading)");
    logSerial.printf("SCREENSHOT_END\n");
    return;
  }
  // Raw panel-native 1bpp frame, MSB = leftmost pixel, 1 = white. The host rotates it.
  logSerial.printf("LX:SHOT %lu %d %d %08lx\n", static_cast<unsigned long>(size), panelWidth(), panelHeight(),
                   static_cast<unsigned long>(crc32(buf, size)));
  const bool complete = writeAll(buf, size);
  logSerial.printf("\n");
  if (!complete) return err("SHOT short write");
  ok(Verb::Shot);
}

void memoryStats() {
  const auto heap = HalMemory::getDefaultHeap();
#ifdef BOARD_HAS_PSRAM
  const auto psram = HalMemory::getPsramHeap();
  logSerial.printf("LX:MEM heap_free=%u heap_min=%u heap_maxalloc=%u psram_free=%u\n",
                   static_cast<unsigned>(heap.freeBytes), static_cast<unsigned>(heap.minFreeBytes),
                   static_cast<unsigned>(heap.largestBlockBytes), static_cast<unsigned>(psram.freeBytes));
#else
  logSerial.printf("LX:MEM heap_free=%u heap_min=%u heap_maxalloc=%u\n", static_cast<unsigned>(heap.freeBytes),
                   static_cast<unsigned>(heap.minFreeBytes), static_cast<unsigned>(heap.largestBlockBytes));
#endif
  ok(Verb::Mem);
}

#if LEXIRISE
// Bytes of this task's stack never used so far (the loop task runs the TLS handshake and JSON parse).
unsigned stackFree() { return static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)); }

// LX:LEXI. Blocks the main loop for the call(s), as a lookup will; the key status is printed by name
// only (never the account name or plan).
void lexiAnalyze(const bool chinese, const int index) {
  const unsigned long started = millis();
  const auto response = service().analyze(chinese ? Language::Chinese : Language::Japanese,
                                          chinese ? config::kLexiSampleZh : config::kLexiSampleJa);
  const unsigned long ms = millis() - started;
  api::AnalyzeResult parsed;
  const bool parsedOk = response.ok() && api::parseAnalyze(response.body, parsed) == api::ParseStatus::Ok;
  const auto heap = HalMemory::getDefaultHeap();
  logSerial.printf(
      "LX:LEXI analyze %d %s status=%d occ=%u parsed=%d ms=%lu heap_free=%u heap_maxalloc=%u stack_free=%u\n", index,
      api::apiErrorName(response.error), response.status, static_cast<unsigned>(parsed.occurrences.size()),
      parsedOk ? 1 : 0, ms, static_cast<unsigned>(heap.freeBytes), static_cast<unsigned>(heap.largestBlockBytes),
      stackFree());
  if (index != 0) return;  // the soak prints one line per call
  for (const auto& occ : parsed.occurrences) {
    logSerial.printf("LX:LEXI occ %u-%u word=%s lemma=%s reading=%s wordlike=%d\n",
                     static_cast<unsigned>(occ.charStart), static_cast<unsigned>(occ.charEnd), occ.word.c_str(),
                     occ.lemma.c_str(), occ.reading.c_str(), occ.wordLike ? 1 : 0);
  }
}

void lexi(const Command& c) {
  switch (c.lexi) {
    case LexiAction::Me: {
      const auto status = service().checkKey();
      logSerial.printf("LX:LEXI me %s %s stack_free=%u\n", api::keyStateName(status.state),
                       api::apiErrorName(status.error), stackFree());
      break;
    }
    case LexiAction::Analyze:
      lexiAnalyze(c.chinese, 0);
      break;
    case LexiAction::Soak:
      for (int i = 1; i <= c.count; i++) {
        lexiAnalyze(false, i);
        if (c.cold) service().releaseWifi();  // every call pays a WiFi join and a new TLS session
        gKeepAwake.renew(millis());
      }
      break;
  }
  ok(Verb::Lexi);
}
#endif

// Round-trips a grid of logical points through the real GfxRenderer::tapToLogical() for the current
// orientation, proving injected taps land exactly where they're aimed.
void selfTest() {
  const int w = gRenderer->getScreenWidth();
  const int h = gRenderer->getScreenHeight();
  int checked = 0;
  int failed = 0;
  for (int y = 0; y < h; y += config::kSelfTestStride) {
    for (int x = 0; x < w; x += config::kSelfTestStride) {
      float nx = 0, ny = 0;
      logicalToNormalised(orientation(), x, y, panelWidth(), panelHeight(), nx, ny);
      int rx = -1, ry = -1;
      gRenderer->tapToLogical(nx, ny, rx, ry);
      checked++;
      if (rx != x || ry != y) {
        if (failed < config::kSelfTestMaxMisses) {
          logSerial.printf("LX:SELFTEST miss (%d,%d) -> (%d,%d)\n", x, y, rx, ry);
        }
        failed++;
      }
    }
  }
  logSerial.printf("LX:SELFTEST coords checked=%d failed=%d\n", checked, failed);
  if (failed) return err("SELFTEST coords");
  ok(Verb::SelfTest);
}

void reboot() {
  RenderLock lock;  // never restart mid-render
  ok(Verb::Reboot);
  logSerial.flush();
  delay(config::kRebootFlushMs);
  ESP.restart();
}

void handle(const Command& c) {
  if (c.verb == Verb::None && !c.error) return;  // ordinary chatter: not a command
  gKeepAwake.renew(millis());                    // any command renews the keep-awake lease
  if (c.error) return err(c.error);
  switch (c.verb) {
    case Verb::None:
      return;
    case Verb::Ping:
      logSerial.printf("LX:PONG %s orientation=%d screen=%dx%d\n", CROSSPOINT_VERSION, static_cast<int>(orientation()),
                       gRenderer->getScreenWidth(), gRenderer->getScreenHeight());
      return;
    case Verb::Tap:
    case Verb::Long:
      if (!inScreen(c.x, c.y)) return err("off screen");
      return queueGesture(c.verb == Verb::Tap ? makeTap(c.x, c.y) : makeLongPress(c.x, c.y), c.verb);
    case Verb::Swipe:
      if (!inScreen(c.x, c.y) || !inScreen(c.x2, c.y2)) return err("off screen");
      if (!swipeIsLongEnough(c.x, c.y, c.x2, c.y2)) return err("swipe too short");
      return queueGesture(makeSwipe(c.x, c.y, c.x2, c.y2), c.verb);
    case Verb::Button:
      return pressButton(c.button, c.ms);
    case Verb::Home:
    case Verb::HomeHold:
      return queueGesture(makeHome(c.verb == Verb::HomeHold), c.verb);
    case Verb::Shot:
      return screenshot(false);
    case Verb::LegacyScreenshot:
      return screenshot(true);
    case Verb::Mem:
      return memoryStats();
    case Verb::SelfTest:
      return selfTest();
    case Verb::Sync:
      gSyncPending = true;
      gSyncStartMs = millis();
      gSyncStablePolls = 0;
      return;  // answered from poll() once settled
    case Verb::Awake:
      gKeepAwake.setMode(c.awake);
      return ok(Verb::Awake);
    case Verb::Reboot:
      return reboot();
    case Verb::Lexi:
#if LEXIRISE
      return lexi(c);
#else
      return err("built without LEXIRISE");
#endif
  }
}

void checkSync() {
  if (!gSyncPending) return;
  const bool idle =
      gQueue.empty() && !gButton.busy() && gFramesSinceFeed >= config::kSyncSettleFrames && !RenderLock::peek();
  gSyncStablePolls = idle ? gSyncStablePolls + 1 : 0;
  if (gSyncStablePolls >= config::kSyncStablePolls) {
    gSyncPending = false;
    ok(Verb::Sync);
  } else if (millis() - gSyncStartMs > config::kSyncTimeoutMs) {
    gSyncPending = false;
    err("SYNC timeout");
  }
}

}  // namespace

void begin(GfxRenderer& renderer, HalDisplay& display) {
  gRenderer = &renderer;
  gDisplay = &display;
  gKeepAwake.renew(millis());  // the lease starts at boot, so a freshly flashed device stays reachable
  InputManager::setButtonHook(&buttonHook);
  LOG_INF("LXDEV", "Dev harness ready (send LX:PING)");
}

void poll() {
  if (!gRenderer) return;

  // Feed at most one queued frame; HalGPIO::update() promotes it at the start of the next frame.
  LogicalFrame f;
  if (gQueue.pop(f)) {
    gpio.devInject(toPanelFrame(f, orientation(), panelWidth(), panelHeight()));
    gFramesSinceFeed = 0;
  } else if (gFramesSinceFeed < config::kSyncSettleFrames) {
    gFramesSinceFeed++;
  }

  while (logSerial.available() > 0) {
    const int ch = logSerial.read();
    if (ch < 0) break;
    if (gAssembler.feed(static_cast<char>(ch))) handle(parseLine(gAssembler.line()));
  }

  checkSync();
}

// HWCDC::isPlugged(): a USB host is sending start-of-frame packets (false on battery or a charger).
bool keepAwake() { return gKeepAwake.active(millis(), logSerial.isPlugged()); }

}  // namespace lexipoint::dev

#endif  // LEXIPOINT_DEV_HARNESS
