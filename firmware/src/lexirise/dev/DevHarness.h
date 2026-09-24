#pragma once

// LEXIPOINT dev harness: a line-based command interface on the USB serial port that lets a host
// drive and observe the reader without touching it (synthetic taps, swipes and buttons, a raw
// framebuffer dump, reboot, keep-awake). Compiled only when LEXIPOINT_DEV_HARNESS is set, which
// only env:x4pro does. Release environments never contain it.
//
// Host side: scripts/lexipoint/lxctl.py. Protocol: one command per line, prefixed "LX:", answered
// with "LX:OK <command>" or "LX:ERR <reason>". See docs/v0.1/dev-harness.md in the lexipoint repo.

#if LEXIPOINT_DEV_HARNESS

class GfxRenderer;
class HalDisplay;

namespace lexipoint::dev {

// Registers the button hook. Call once after gpio.begin().
void begin(GfxRenderer& renderer, HalDisplay& display);

// Reads pending serial input without blocking, runs complete commands, and feeds queued input
// steps into the next frame. Call once per loop, after input update() and before activities run.
void poll();

// True while the harness wants the device kept out of power saving and auto-sleep: in the default
// lease mode, while a USB host is on the line and has sent a command within config::kKeepAwakeLeaseMs
// (the lease also starts at boot); or always / never after LX:AWAKE 1 / LX:AWAKE 0.
bool keepAwake();

}  // namespace lexipoint::dev

#endif
