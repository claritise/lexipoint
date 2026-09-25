# Dev harness: driving the reader over USB

**Status:** built 2026-09-24 on branch `lexi/dev-harness` (the pre-P1 phase). It lets the builder
drive and observe the X4 Pro with nobody touching it: synthetic taps, swipes and buttons, screenshots,
memory stats, reboot. Asked for by claritise so the build can run while they're away from the device.

Related: `01-build-order.md` (the phase), `firmware-base.md` §3 (hooks), `popup-ui.md` (the design
conformance gate that the screenshots feed).

---

## 0. Safety

- Compiled **only** when `LEXIPOINT_DEV_HARNESS` is defined, which **only `[env:x4pro]`** does
  (`platformio.ini`). The `*-gh_release` envs never define it. **Verified:** the `x4pro-gh_release`
  binary contains no harness strings.
- It only listens on the USB serial port, so it needs a cable and a computer. There's no network surface.
- **Never add `LEXIPOINT_DEV_HARNESS` to a release env.** `test_lxctl.py` checks it (`ReleaseEnvsExcludeHarness`),
  in the uniform gate's script tests.

## 1. Protocol

One command per line on the USB serial port (115200), prefixed `LX:`. Replies are `LX:OK <cmd>`,
`LX:ERR <reason>`, or a data line. Ordinary log lines are interleaved, and hosts skip them.

| Command | Does | Reply |
|---|---|---|
| `LX:PING` | Liveness + context | `LX:PONG <version> orientation=<n> screen=<w>x<h>` |
| `LX:TAP x y` | Down → held (120 ms) → tap + release, over 3 frames | `LX:OK TAP` (queued, not yet delivered: use `SYNC`) |
| `LX:LONG x y` | Down → held → long-press (800 ms) → **tap + release**, over 4 frames. Like the SDK, an unconsumed long-press taps on lift, and a screen that consumes it suppresses that tap | `LX:OK LONG` |
| `LX:SWIPE x1 y1 x2 y2` | Down → held **at the end point, moved** (not a tap candidate, so no touch-down fires at the start) → swipe + release, over 3 frames | `LX:OK SWIPE` |
| `LX:BTN LEFT\|RIGHT\|POWER [ms]` | Holds a page key (LEFT = previous, RIGHT = next) or Power for `ms` (default 150, clamped 120–5000, and at least 3 input updates) | `LX:OK BTN`, or `LX:ERR button busy` while a previous press is still held |
| `LX:HOME` / `LX:HOME HOLD` | Capacitive Home tap / long-press | `LX:OK HOME` |
| `LX:SYNC` | Waits until queued input is delivered, buttons are released and no render has run for 3 polls | `LX:OK SYNC` (or `LX:ERR SYNC timeout` after 15 s) |
| `LX:SHOT` | Raw framebuffer, read **under the render lock** (never torn) | `LX:SHOT <bytes> <w> <h> <crc32>`, then the raw bytes, a newline, then `LX:OK SHOT` (or `LX:ERR SHOT short write`) |
| `LX:MEM` | Heap stats | `LX:MEM heap_free=… heap_min=… heap_maxalloc=… psram_free=…` |
| `LX:SELFTEST` | Round-trips a grid of points through the real `tapToLogical()` | `LX:SELFTEST coords checked=… failed=…`, then OK/ERR |
| `LX:AWAKE 0\|1\|2` | Keep-awake mode: 0 = off, 1 = always, **2 = lease (default)**: while **a USB host is on the line** (`HWCDC::isPlugged()`, false on battery or a charger) and has sent a command in the last **10 minutes** (the lease also starts at boot), the device skips power saving and auto-sleep. Log chatter doesn't renew it | `LX:OK AWAKE` |
| `LX:REBOOT` | `ESP.restart()`, after waiting for any render to finish | `LX:OK REBOOT` |

- **Coordinates are logical screen pixels** (portrait 480×800 by default), the same space the UI
  draws in. They're converted to panel-native normalised coordinates by `DevCoords.h`, the exact
  inverse of `GfxRenderer::tapToLogical()`.
- **Frames:** the screenshot is the panel-native 800×480 1-bpp buffer (MSB = leftmost pixel,
  1 = white, 48,000 bytes). `lxctl.py` rotates it to portrait. **The CRC32 is checked**, so a dump
  corrupted by interleaved log output is rejected, not saved.
- **Screen tracking:** CrossPoint already logs `[ACT] Entering activity: <Name>` on every screen change.
  `lxctl.py wait "<text>" <secs>` blocks on it.
- CrossPoint's legacy `CMD:SCREENSHOT` keeps working, answered by the harness. In harness builds, the base's own serial reader is compiled out, so it can't block for 1 s and swallow half of an `LX:` line.
- **Replies:** each command's reply is `LX:OK <VERB>` (HOME HOLD answers `LX:OK HOME`). `lxctl.py` matches replies to the command that sent them, so stale ones are skipped.
- **All tunables** live in `src/lexirise/dev/DevConfig.h`. `lxctl.py`'s timeouts are kept in step with them.

## 2. How input is injected

- **Touch:** `lib/hal/LexiDevInput.h` holds a pure `lexidev::Overlay`. `HalGPIO` owns one, and
  `HalGPIO::devInject()` queues a `Frame`, which `HalGPIO::update()` promotes for exactly one frame.
  **While a frame carries touch, every touch query answers from it alone**, so an injected gesture never
  mixes with a real finger. Two latches mirror the SDK:
  - **Suppress:** when a screen calls `suppressTouchContact()` on an injected long-press, the rest of that
    gesture, **including its release**, is swallowed.
  - **Held time:** `lastTouchHeldMs()` reports the injected gesture's duration after its release, until
    a real finger lifts. A leftover real long hold then can't turn an injected tap into a long-press
    (the reader treats ≥ 700 ms as a long-press).
- **Gestures** (`DevGesture.h`) are queued in **logical** coordinates and converted when fed, using the
  orientation and panel size at that moment. The queue takes a gesture whole or not at all.
- **Buttons:** the SDK's own `InputManager::setButtonHook()`. The mask stays asserted for the
  requested time, so presses get the real debounce, held time and edges.
- **Keep-awake:** `main.cpp` treats `keepAwake()` as activity, so there's no power saving and no auto-sleep during
  the lease (or always, after `LX:AWAKE 1`, which is what an unattended build session uses while the
  device stays on the cable).

## 3. Host tool: `scripts/lexipoint/lxctl.py`

Needs `pyserial`. Examples: `lxctl.py ping`, `lxctl.py tap 240 400`, `lxctl.py swipe 240 600 240 200`,
`lxctl.py btn next`, `lxctl.py home`, `lxctl.py shot out.png`, `lxctl.py log 10`,
`lxctl.py wait "Entering activity: Home" 15`. Opening the port leaves DTR/RTS low, since toggling them
resets the ESP32-S3.

## 4. Tests

- `test/lexirise_dev_coords/`: every pixel, all four orientations, round-trips against a reference
  copy of `tapToLogical()` (5 tests).
- `test/lexirise_dev/`: the parser and line assembly (boundaries, malformed input, legacy command),
  gesture frame sequences and the all-or-nothing queue, the overlay (one-frame promotion, every query's
  mapping, the suppress latch, the held-time latch including suppressed releases, moved fingers), and
  timing (the lease incl. no-host and millis() wrap, button presses incl. the min-samples floor and busy) (39 tests).
- `scripts/lexipoint/test_lxctl.py` (`cd firmware && python3 -m unittest discover -s scripts/lexipoint`): PNG rotation,
  reply matching against a fake serial port (log noise, stale replies, errors, CRC mismatch, short write),
  and a **release guard** that follows `extends` and `build_src_flags`: no `*release*` env defines the
  harness, a synthetic sneaky env is caught, and no built release binary contains it (11 tests).
- `lxctl.py smoke`: an on-device end-to-end check using only theme-independent gestures: ping, selftest, mem,
  Home, a **top-edge swipe** into the frontlight panel, a **left-edge swipe** back, a page key. Screenshots
  after each step, exits non-zero on the first failure.
- On device: `LX:SELFTEST` against the real renderer, plus the smoke checks in `01-build-order.md`.

## 5. Known limits

- **A sleeping device can't be woken over USB.** Deep sleep drops the USB port. The harness's
  keep-awake prevents this, but only once a harness build is running. The first flash after a stock
  firmware needs one Power press.
- Injected touches don't pass through the SDK's gesture classifier. The harness rejects the one easy-to-get-wrong
  case (swipes under the SDK's 60 px minimum, as `LX:ERR swipe too short`), and gesture timings are
  compile-time checked against the reader's long-press threshold. Beyond that, tests must use realistic
  gestures.
- **Holds measured in real time** (keyboard key alternates, sliders, auto-repeat) aren't drivable yet.
  Each gesture reports fixed durations. A future `LX:HOLD x y ms` would repeat held frames.
- **A blocked main loop stretches button presses**, since the mask is released on the first update after the
  deadline. Keep `BTN POWER` short: a long one can reach the power-hold sleep.
- Events injected on a frame where the main loop returns early (e.g. USB-drive mode) are lost, the same
  as real one-shot events.
