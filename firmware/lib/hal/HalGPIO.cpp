#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <esp_sleep.h>

// Global HalGPIO instance
HalGPIO gpio;

void HalGPIO::begin() { inputMgr.begin(); }

void HalGPIO::update() {
  inputMgr.update();
#if LEXIPOINT_DEV_HARNESS
  devOverlay.promote(inputMgr.wasTouchReleased());
#endif
  const bool connected = isUsbConnected();
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return inputMgr.wasPressed(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed(); }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return inputMgr.wasReleased(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased(); }

bool HalGPIO::rawInputActive() {
  if (inputMgr.isPowerButtonPhysicallyPressed()) return true;
  InputManager::ButtonAdcSample g1{}, g2{};
  inputMgr.readButtonAdc(g1, g2);
  // The Xteink ladder idles at the ADC full-scale rail (~4095); every button band sits below 3900.
  constexpr int kIdleRailMin = 4000;
  return (g1.raw >= 0 && g1.raw < kIdleRailMin) || (g2.raw >= 0 && g2.raw < kIdleRailMin);
}

int HalGPIO::rawTouchLevel() {
  const auto& touch = BoardConfig::ACTIVE.touch;
  if (touch.controller == BoardConfig::TouchController::None || touch.irq < 0) return -1;
  return digitalRead(touch.irq) == HIGH ? 1 : 0;
}

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

unsigned long HalGPIO::getPowerButtonHeldTime() const { return inputMgr.getPowerButtonHeldTime(); }

bool HalGPIO::hasTouch() const { return inputMgr.hasTouch(); }

bool HalGPIO::hasHomeKey() const { return BoardConfig::hasHomeKey(); }

bool HalGPIO::wasHomeKeyTapped() const {
#if LEXIPOINT_DEV_HARNESS
  if (devOverlay.homeTap()) return true;
#endif
  return inputMgr.wasHomeKeyTapped();
}

bool HalGPIO::wasHomeKeyLongPressed() const {
#if LEXIPOINT_DEV_HARNESS
  if (devOverlay.homeHold()) return true;
#endif
  return inputMgr.wasHomeKeyLongPressed();
}

bool HalGPIO::wasTouchTap(float& nx, float& ny) const {
#if LEXIPOINT_DEV_HARNESS
  // LEXIPOINT: an injected frame with touch answers every touch query alone.
  if (devOverlay.ownsTouch()) return devOverlay.tap(nx, ny);
#endif
  return inputMgr.wasTouchTap(nx, ny);
}

bool HalGPIO::wasTouchDown(float& nx, float& ny) const {
#if LEXIPOINT_DEV_HARNESS
  // LEXIPOINT: an injected frame with touch answers every touch query alone.
  if (devOverlay.ownsTouch()) return devOverlay.down(nx, ny);
#endif
  return inputMgr.wasTouchPressedAt(nx, ny);
}

bool HalGPIO::wasTouchReleased() const {
#if LEXIPOINT_DEV_HARNESS
  // LEXIPOINT: an injected frame with touch answers every touch query alone.
  if (devOverlay.ownsTouch()) return devOverlay.released();
#endif
  return inputMgr.wasTouchReleased();
}

bool HalGPIO::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
#if LEXIPOINT_DEV_HARNESS
  if (devOverlay.ownsTouch()) return devOverlay.tapCandidate(nx, ny, heldMs);
#endif
  return inputMgr.isTouchTapCandidate(nx, ny, heldMs);
}

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const {
#if LEXIPOINT_DEV_HARNESS
  // LEXIPOINT: an injected frame with touch answers every touch query alone.
  if (devOverlay.ownsTouch()) return devOverlay.heldAt(nx, ny);
#endif
  return inputMgr.isTouchHeldAt(nx, ny);
}

bool HalGPIO::wasTouchLongPress(float& nx, float& ny) const {
#if LEXIPOINT_DEV_HARNESS
  // LEXIPOINT: an injected frame with touch answers every touch query alone.
  if (devOverlay.ownsTouch()) return devOverlay.longPress(nx, ny);
#endif
  return inputMgr.wasTouchLongPress(nx, ny);
}

void HalGPIO::suppressTouchContact() {
#if LEXIPOINT_DEV_HARNESS
  devOverlay.suppress();
#endif
  inputMgr.suppressTouchContact();
}

unsigned long HalGPIO::lastTouchHeldMs() const {
#if LEXIPOINT_DEV_HARNESS
  if (devOverlay.heldLatched()) return devOverlay.lastHeldMs();
#endif
  return inputMgr.lastTouchHeldMs();
}

bool HalGPIO::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
#if LEXIPOINT_DEV_HARNESS
  // LEXIPOINT: an injected frame with touch answers every touch query alone.
  if (devOverlay.ownsTouch()) return devOverlay.swipe(nxStart, nyStart, nxEnd, nyEnd);
#endif
  return inputMgr.wasSwipe(nxStart, nyStart, nxEnd, nyEnd);
}

bool HalGPIO::wasTouchActivity() const {
#if LEXIPOINT_DEV_HARNESS
  if (devOverlay.anyActivity()) return true;
#endif
  return inputMgr.wasTouchActivity();
}

void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(const bool enabled) {
  InputManager::setSharedConfirmPowerShortPressEmitsPower(enabled);
}

bool HalGPIO::verifyPowerButtonWakeup() {
  constexpr unsigned long POWER_WAKE_STABILITY_MS = 10;
  const bool heldAtFirstSample = inputMgr.isPowerButtonPhysicallyPressed();
  const unsigned long sampleStart = millis();
  inputMgr.update();
  while (millis() - sampleStart < POWER_WAKE_STABILITY_MS || inputMgr.isDebouncePending()) {
    delay(1);
    inputMgr.update();
  }
  return heldAtFirstSample && inputMgr.isPowerButtonPhysicallyPressed();
}

bool HalGPIO::isUsbConnected() const {
  if (BoardConfig::ACTIVE.usbDetect >= 0) {
    return digitalRead(BoardConfig::ACTIVE.usbDetect) == HIGH;
  }
  // No digital USB-detect line (e.g. Sticky, whose PWR_IN_VOLT is an analog
  // divider): infer external power from charging state instead. BatteryMonitor
  // picks the board's best source — charger IC status, gauge Current() sign, or
  // a /STAT pin — and reports false on boards with no battery telemetry at all.
  // Caveat: charge termination at 100% reads as "not connected".
  static const BatteryMonitor battery;
  return battery.isCharging();
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  const bool usbConnected = isUsbConnected();

  if (resetReason == ESP_RST_DEEPSLEEP &&
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}
