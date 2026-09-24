#!/usr/bin/env python3
"""Host side of the Lexipoint dev harness (src/lexirise/dev/DevHarness.cpp).

Drives a dev build (env:x4pro) of the reader over its USB serial port: synthetic touch and
buttons, screenshots, memory stats, reboot. Needs pyserial (`pip install pyserial`).

Examples:
  lxctl.py ping
  lxctl.py tap 240 400
  lxctl.py long 120 300
  lxctl.py lexi card ja [low]    # the card bench (P4); then tap / button / home to drive it
  lxctl.py swipe 240 600 240 200
  lxctl.py btn next            # right page key; also: prev, power; optional ms
  lxctl.py home [hold]
  lxctl.py sync                # wait until injected input has landed and rendering settled
  lxctl.py shot out.png        # syncs, then saves a portrait PNG of the current frame
  lxctl.py selftest            # exits non-zero if injected taps would miss
  lxctl.py mem
  lxctl.py awake 0|1|2         # off / always on / lease (default)
  lxctl.py reboot
  lxctl.py log 10              # print device log for N seconds
  lxctl.py wait "Entering activity: Home" 15
  lxctl.py smoke [outdir]      # end-to-end harness check with screenshots
  lxctl.py card-smoke [outdir] [name]  # every reference card state on the device, a screenshot each (P4 gate);
                                       # upright portrait, default side buttons
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import struct
import sys
import time
import zlib

try:
    import serial  # type: ignore
except ImportError:  # pragma: no cover
    serial = None

# --- Protocol constants (keep in step with src/lexirise/dev/DevConfig.h) ---------------------------
BAUD = 115200
REPLY_TIMEOUT_S = 5.0
SYNC_TIMEOUT_S = 20.0  # > DevConfig kSyncTimeoutMs (15 s), so the device reports its own timeout
SHOT_TIMEOUT_S = 10.0  # > DevConfig kShotWriteDeadlineMs (3 s) plus the render lock wait
PORT_GLOBS = ("/dev/cu.usbmodem*", "/dev/ttyACM*")
EDGE_INSET = 2  # px from an edge: satisfies any FreeInkUI edgeSwipe edge fraction
ACTIVITY_WAIT_S = 10.0  # screen transitions (log line "Entering activity: <Name>")
# LX:LEXI: the device bounds one call by config::kMaxCallMs (LexiriseConfig.h, 38 s); wait longer.
# test_lxctl checks these against the headers.
LEXI_CALL_TIMEOUT_S = 45.0
LEXI_SOAK_MAX = 50  # DevConfig kLexiSoakMax
LEAK_BYTES_PER_CALL = 64  # a free-heap trend steeper than this, per call, fails the soak
LEAK_MIN_SAMPLES = 5
# card-smoke: the bench's phases end by config::kBenchPhaseBMs (900 ms); wait for them before tapping.
CARD_PHASES_S = 1.5
CARD_HOME_PRESSES_MAX = 3  # Home: expanded → card → closed, plus one spare
# A close redraws the reader, a half or full refresh on every 5th card (~1.34 s measured, popup-ui.md §2);
# wait that out with a margin before the next Home, or a spare Home would reach the screen underneath.
PANEL_FULL_REFRESH_S = 1.34
CARD_CLOSE_WAIT_S = 3.0
CARD_WORD_LOG = re.compile(r"\[LXCARD\] word (\d+)")  # LexiriseCardActivity, smoke mode
GOLDEN_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "test", "lexirise_card", "golden")


def find_port() -> str:
    ports = sorted(p for g in PORT_GLOBS for p in glob.glob(g))
    if not ports:
        sys.exit("no reader found on USB (is it plugged in and awake?)")
    return ports[0]


def open_serial(port: str | None):
    if serial is None:
        sys.exit("pyserial is required: pip install pyserial")
    ser = serial.Serial()
    ser.port = port or find_port()
    ser.baudrate = BAUD
    ser.timeout = 0.2
    # Keep DTR/RTS low: toggling them resets the ESP32-S3 over USB-Serial/JTAG.
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.reset_input_buffer()
    return ser


class Harness:
    def __init__(self, ser):
        self.ser = ser

    def send(self, cmd: str) -> None:
        self.ser.write(f"LX:{cmd}\n".encode())
        self.ser.flush()

    def read_line(self, deadline: float) -> str | None:
        buf = b""
        while time.time() < deadline:
            ch = self.ser.read(1)
            if not ch:
                continue
            if ch == b"\n":
                return buf.decode("utf-8", "replace").rstrip("\r")
            buf += ch
        return None

    def command(self, cmd: str, expect: str | None = None, timeout: float = REPLY_TIMEOUT_S,
                seen: list[str] | None = None) -> str:
        """Send a command and return its reply. Log lines and stale replies are skipped (and appended to
        `seen` when given, for a caller that checks the log the command caused).

        expect defaults to "LX:OK <VERB>", the device's acknowledgement for this command.
        """
        verb = cmd.split()[0]
        expect = expect or f"LX:OK {verb}"
        self.send(cmd)
        deadline = time.time() + timeout
        while True:
            line = self.read_line(deadline)
            if line is None:
                raise TimeoutError(f"no reply to {cmd!r}")
            if line.startswith(expect):
                return line
            if line.startswith("LX:ERR"):
                raise RuntimeError(f"{cmd}: {line}")
            if seen is not None:
                seen.append(line)

    def collect(self, cmd: str, prefix: str, timeout: float) -> list[str]:
        """Send a command and return every line starting with prefix until its LX:OK."""
        verb = cmd.split()[0]
        self.send(cmd)
        deadline = time.time() + timeout
        lines = []
        while True:
            line = self.read_line(deadline)
            if line is None:
                raise TimeoutError(f"no reply to {cmd!r}")
            if line.startswith(prefix):
                lines.append(line)
            elif line.startswith(f"LX:OK {verb}"):
                return lines
            elif line.startswith("LX:ERR"):
                raise RuntimeError(f"{cmd}: {line}")

    def screenshot(self) -> tuple[int, int, bytes]:
        """Return (width, height, raw 1bpp panel-native frame), verified by CRC32."""
        self.send("SHOT")
        deadline = time.time() + SHOT_TIMEOUT_S
        while True:
            line = self.read_line(deadline)
            if line is None:
                raise TimeoutError("no SHOT header")
            if line.startswith("LX:ERR"):
                raise RuntimeError(line)
            if line.startswith("LX:SHOT "):
                break
        _, size, width, height, crc = line.split()
        size, width, height, crc = int(size), int(width), int(height), int(crc, 16)
        data = b""
        while len(data) < size and time.time() < deadline:
            data += self.ser.read(size - len(data))
        if len(data) != size:
            raise TimeoutError(f"short frame: {len(data)}/{size} bytes")
        # Consume the frame's trailing newline and its "LX:OK SHOT" (or a short-write error).
        while True:
            line = self.read_line(deadline)
            if line is None:
                raise TimeoutError("no SHOT acknowledgement")
            if line.startswith("LX:ERR"):
                raise RuntimeError(line)
            if line.startswith("LX:OK SHOT"):
                break
        if zlib.crc32(data) & 0xFFFFFFFF != crc:
            raise RuntimeError("frame CRC mismatch; retry")
        return width, height, data

    def wait_for(self, pattern: str, timeout: float) -> str:
        deadline = time.time() + timeout
        while True:
            line = self.read_line(deadline)
            if line is None:
                raise TimeoutError(f"timed out waiting for {pattern!r}")
            if pattern in line:
                return line


def frame_to_portrait_png(width: int, height: int, data: bytes, path: str) -> None:
    """Rotate the panel-native landscape frame to portrait (as the reader shows it) and save a PNG.

    Panel bytes: row-major, MSB = leftmost pixel, bit 1 = white. Portrait logical (x, y) maps to
    panel (px, py) = (y, height - 1 - x), matching GfxRenderer::rotateCoordinates for Portrait.
    """
    row_bytes = width // 8

    def pixel(px: int, py: int) -> int:
        b = data[py * row_bytes + (px >> 3)]
        return (b >> (7 - (px & 7))) & 1

    out_w, out_h = height, width  # portrait: 480 x 800
    rows = []
    for y in range(out_h):
        row = bytearray([0])  # PNG filter byte
        for x in range(out_w):
            row.append(255 if pixel(y, height - 1 - x) else 0)
        rows.append(bytes(row))
    raw = zlib.compress(b"".join(rows), 9)

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", out_w, out_h, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", raw)
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def save_shot(h: Harness, path: str) -> None:
    h.command("SYNC", timeout=SYNC_TIMEOUT_S)
    w, hh, data = h.screenshot()
    frame_to_portrait_png(w, hh, data, path)


def smoke(h: Harness, outdir: str) -> None:
    """End-to-end harness check using only theme-independent gestures. Raises on the first failure
    and leaves the device on the Home screen."""
    os.makedirs(outdir, exist_ok=True)
    pong = h.command("PING", "LX:PONG")
    print(pong)
    w, hgt = map(int, pong.split("screen=")[1].split("x"))
    print(h.command("SELFTEST"))
    print(h.command("MEM", "LX:MEM"))
    save_shot(h, os.path.join(outdir, "01-start.png"))

    h.command("HOME")
    save_shot(h, os.path.join(outdir, "02-home.png"))

    # Top-edge swipe down opens the frontlight panel (X4 Pro has a frontlight).
    h.command(f"SWIPE {w // 2} {EDGE_INSET} {w // 2} {hgt // 2}")
    h.wait_for("Entering activity: FrontlightPanel", ACTIVITY_WAIT_S)
    save_shot(h, os.path.join(outdir, "03-frontlight-panel.png"))

    # Left-edge swipe right is Back.
    h.command(f"SWIPE {EDGE_INSET} {hgt // 2} {w // 2} {hgt // 2}")
    h.wait_for("Exiting activity: FrontlightPanel", ACTIVITY_WAIT_S)
    save_shot(h, os.path.join(outdir, "04-back.png"))

    h.command("BTN RIGHT")
    save_shot(h, os.path.join(outdir, "05-after-next-key.png"))

    h.command("HOME")
    save_shot(h, os.path.join(outdir, "06-home-again.png"))
    print(f"smoke OK, screenshots in {outdir}")


def card_state_commands(state: dict, lang: str, low: bool) -> list[str]:
    """The harness commands that reach one golden card state (test/lexirise_card/golden, written by
    LexiriseCardRender from the same controller inputs): open the bench in kana without saving the reading
    (KANA: the goldens start in kana, and the user's setting stays as it was), step to the word with the
    side buttons, then the taps. Pure, so test_lxctl checks it against the device grammar."""
    cmds = [f"LEXI CARD {lang}{' LOW' if low else ''} KANA"]
    steps = int(state["steps"])
    cmds += ["BTN RIGHT" if steps > 0 else "BTN LEFT"] * abs(steps)
    cmds += [f"TAP {x} {y}" for x, y in state["taps"]]
    return cmds


def check_stepped_word(log: list[str], expected: int, name: str) -> None:
    """The card's last "[LXCARD] word n" log line (smoke mode) must be the golden's word: the side buttons'
    direction follows the reader settings and orientation, so a mapped-the-other-way press is caught here
    instead of shooting the wrong word."""
    words = [int(m.group(1)) for line in log if (m := CARD_WORD_LOG.search(line))]
    if not words:
        raise RuntimeError(f"{name}: the card logged no word after the side-button steps")
    if words[-1] != expected:
        raise RuntimeError(f"{name}: stepped to word {words[-1]}, not {expected} (side-button mapping? "
                           "card-smoke needs upright portrait and the default side buttons)")


def card_smoke(h: Harness, outdir: str, only: str = "", sleep=time.sleep, shot=None,
               golden_dir: str = GOLDEN_DIR) -> list[str]:
    """Every reference card state on the device, one screenshot each, for the P4 design conformance gate
    (compare with cardshots.py's panels). Starts and ends over the current screen. Returns the states shot."""
    shot = shot or save_shot
    os.makedirs(outdir, exist_ok=True)
    names = sorted(f[:-5] for f in os.listdir(golden_dir) if f.endswith(".json"))
    done = []
    for name in names:
        if only and only not in name:
            continue
        with open(os.path.join(golden_dir, name + ".json"), encoding="utf-8") as f:
            state = json.load(f)
        if state.get("extra"):  # an error state (P6): the bench can't be driven into it from the harness
            continue
        cmds = card_state_commands(state, name.split("-")[0], "-low" in name)
        h.command(cmds[0], "LX:OK LEXI")
        h.wait_for("Entering activity: LexiriseCard", ACTIVITY_WAIT_S)
        steps = [c for c in cmds[1:] if c.startswith("BTN")]
        taps = [c for c in cmds[1:] if c.startswith("TAP")]
        log: list[str] = []
        for cmd in steps:
            h.command(cmd, seen=log)
            # A press is held for a while and the device refuses the next one until it's released: wait
            # until the input has landed and the card has redrawn.
            h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
        if steps:
            check_stepped_word(log, int(state["word"]), name)
        for cmd in taps:
            sleep(CARD_PHASES_S)  # the phases end before a finger would tap
            h.command(cmd)
            h.command("SYNC", timeout=SYNC_TIMEOUT_S)
        # The last tap's SYNC already waited for its redraw; sleeping again would outlast a toast
        # (config::kToastMs) the reference shows. Without taps, the phases still have to end.
        if not state["taps"]:
            sleep(CARD_PHASES_S)
        shot(h, os.path.join(outdir, f"{name}.device.png"))
        print(name)
        done.append(name)
        for _ in range(CARD_HOME_PRESSES_MAX):
            h.command("HOME")
            try:
                h.wait_for("Exiting activity: LexiriseCard", CARD_CLOSE_WAIT_S)
                break
            except TimeoutError:
                continue
        else:
            raise RuntimeError(f"{name}: the card didn't close on Home")
    if not done:
        raise RuntimeError(f"no golden state matches {only!r}")
    print(f"card-smoke OK: {len(done)} states, screenshots in {outdir}")
    return done


def parse_fields(line: str) -> dict[str, str]:
    """The key=value fields of an LX:LEXI line."""
    return dict(tok.split("=", 1) for tok in line.split() if "=" in tok)


def heap_slope(values: list[int]) -> float:
    """Least-squares trend of free heap, in bytes per call (negative = losing memory)."""
    n = len(values)
    if n < 2:
        return 0.0
    mean_x = (n - 1) / 2
    mean_y = sum(values) / n
    num = sum((i - mean_x) * (v - mean_y) for i, v in enumerate(values))
    den = sum((i - mean_x) ** 2 for i in range(n))
    return num / den


def heap_leaks(values: list[int]) -> bool:
    """True when free heap trends down by more than LEAK_BYTES_PER_CALL per call (P1 gate: no
    monotonic loss). A trend, not a strict series, so one noisy recovery can't hide a slow leak."""
    return len(values) >= LEAK_MIN_SAMPLES and heap_slope(values) < -LEAK_BYTES_PER_CALL


def lexi(h: Harness, args: list[str]) -> None:
    sub = (args[0] if args else "").lower()
    if sub == "me":
        print(h.command("LEXI ME", "LX:LEXI me", timeout=LEXI_CALL_TIMEOUT_S))
    elif sub == "analyze":
        lang = args[1] if len(args) > 1 else "ja"
        for line in h.collect(f"LEXI ANALYZE {lang}", "LX:LEXI", LEXI_CALL_TIMEOUT_S):
            print(line)
    elif sub == "soak":
        n = int(args[1]) if len(args) > 1 else 20
        cold = len(args) > 2 and args[2].lower() == "cold"
        if not 1 <= n <= LEXI_SOAK_MAX:
            sys.exit(f"soak count must be 1..{LEXI_SOAK_MAX}")
        lines = h.collect(f"LEXI SOAK {n}{' COLD' if cold else ''}", "LX:LEXI analyze", LEXI_CALL_TIMEOUT_S * n)
        for line in lines:
            print(line)
        fields = [parse_fields(line) for line in lines]
        failed = [f for f in fields if f.get("parsed") != "1"]
        heap = [int(f["heap_free"]) for f in fields if "heap_free" in f]
        stack = [int(f["stack_free"]) for f in fields if "stack_free" in f]
        print(f"calls={len(fields)} failed={len(failed)} heap_first={heap[0] if heap else '-'} "
              f"heap_last={heap[-1] if heap else '-'} heap_min={min(heap) if heap else '-'} "
              f"heap_trend={heap_slope(heap):+.0f}B/call stack_free_min={min(stack) if stack else '-'}")
        if failed or heap_leaks(heap):
            sys.exit("soak FAILED" + (" (free heap trending down)" if heap_leaks(heap) else ""))
    elif sub == "card":  # the card bench (P4): opens over the current screen; drive it with tap/button/home
        lang = args[1] if len(args) > 1 else "ja"
        low = len(args) > 2 and args[2].lower() == "low"
        print(h.command(f"LEXI CARD {lang}{' LOW' if low else ''}", "LX:OK LEXI"))
    else:
        sys.exit("usage: lexi me | analyze ja|zh | soak [n] [cold] | card ja|zh [low]")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    ser = open_serial(a.port)
    h = Harness(ser)
    try:
        c = a.cmd.lower()
        if c == "ping":
            print(h.command("PING", "LX:PONG"))
        elif c in ("tap", "long"):
            x, y = map(int, a.args[:2])
            print(h.command(f"{c.upper()} {x} {y}"))
        elif c == "swipe":
            x1, y1, x2, y2 = map(int, a.args[:4])
            print(h.command(f"SWIPE {x1} {y1} {x2} {y2}"))
        elif c == "btn":
            name = a.args[0].upper()
            ms = f" {int(a.args[1])}" if len(a.args) > 1 else ""  # omitted: the device default wins
            print(h.command(f"BTN {name}{ms}"))
        elif c == "home":
            print(h.command("HOME HOLD" if a.args[:1] == ["hold"] else "HOME"))
        elif c == "sync":
            print(h.command("SYNC", timeout=SYNC_TIMEOUT_S))
        elif c == "shot":
            path = a.args[0] if a.args else "shot.png"
            save_shot(h, path)
            print(f"saved {path}")
        elif c == "selftest":
            try:
                print(h.command("SELFTEST"))
            except RuntimeError as e:
                sys.exit(str(e))
        elif c == "mem":
            print(h.command("MEM", "LX:MEM"))
        elif c == "awake":
            print(h.command(f"AWAKE {int(a.args[0])}"))
        elif c == "reboot":
            print(h.command("REBOOT"))
        elif c == "log":
            secs = float(a.args[0]) if a.args else 10
            end = time.time() + secs
            while time.time() < end:
                line = h.read_line(end)
                if line is not None:
                    print(line)
        elif c == "wait":
            print(h.wait_for(a.args[0], float(a.args[1]) if len(a.args) > 1 else 15))
        elif c == "lexi":
            lexi(h, a.args)
        elif c == "card-smoke":
            try:
                card_smoke(h, a.args[0] if a.args else "card-shots", a.args[1] if len(a.args) > 1 else "")
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"card-smoke FAILED: {e}")
        elif c == "smoke":
            try:
                smoke(h, a.args[0] if a.args else "smoke-shots")
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"smoke FAILED: {e}")
        else:
            sys.exit(f"unknown command {a.cmd!r}")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
