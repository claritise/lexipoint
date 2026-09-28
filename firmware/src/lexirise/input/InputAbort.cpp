#if LEXIRISE

#include "InputAbort.h"

#include <HalGPIO.h>
#include <Logging.h>

#include "TouchLine.h"

namespace lexipoint::input {
namespace {

TouchLine& line() {
  static TouchLine touch;
  return touch;
}

}  // namespace

void sampleIdle(const bool fingerDown) {
  if (fingerDown) return;
  TouchLine& touch = line();
  touch.idle(gpio.rawTouchLevel(), millis());
  if (touch.takeLearned()) LOG_INF("LXIN", "touch line idles %s", touch.idleLevel() ? "high" : "low");
  if (touch.takeIgnored()) LOG_ERR("LXIN", "touch line changes with no finger down: not used to give calls up");
}

bool inputCame() { return gpio.rawInputActive() || line().active(gpio.rawTouchLevel()); }

}  // namespace lexipoint::input

#endif  // LEXIRISE
