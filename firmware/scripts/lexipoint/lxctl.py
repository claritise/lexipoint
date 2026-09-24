#!/usr/bin/env python3
"""Host side of the Lexipoint dev harness (src/lexirise/dev/DevHarness.cpp).

Drives a dev build (env:x4pro) of the reader over its USB serial port: synthetic touch and
buttons, screenshots, memory stats, reboot. Needs pyserial (`pip install pyserial`).

Examples:
  lxctl.py ping
  lxctl.py tap 240 400
  lxctl.py long 120 300
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
"""

from __future__ import annotations

import argparse
import glob
import os
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

    def command(self, cmd: str, expect: str | None = None, timeout: float = REPLY_TIMEOUT_S) -> str:
        """Send a command and return its reply. Log lines and stale replies are skipped.

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
