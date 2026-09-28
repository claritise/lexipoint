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
  lxctl.py card-gestures              # the card's swipes (up, down, tabs) checked from the log (P7)
  lxctl.py card-sentence              # the side button on into the next sentence, from the log (P9)
  lxctl.py settings-smoke [outdir]    # opens Settings → Lexirise, checks its rows, a screenshot (P7)
  lxctl.py reader-longpress [x y]     # a book open, upright portrait: a long-press looks up a word, and does
                                      # nothing off the text (P9, P10);
                                      # x y: a point on a word (default the page's middle)
  lxctl.py --creates-a-deck deck-smoke [x1 y1 x2 y2]
                                      # CREATES A REAL DECK and saves two words: only with claritise's OK.
                                      # A dev build, a book open (upright portrait, Deck per book on), two
                                      # unsaved words at x1 y1 and x2 y2; checks the book deck's calls from the
                                      # log (V3; not sleep). Other commands ignore --creates-a-deck
  lxctl.py ignore-smoke [x y]         # a dev build, a book open (upright portrait): long-press the word at x y (one
                                      # not ignored yet), ⋯ → Ignore this word, then the toast's Undo; checks
                                      # from the log it was written to ignored.ini and taken off again, with no
                                      # write to Lexirise (V5). Writes only the reader's SD card
  lxctl.py vocab-smoke [x y] [press]  # a dev build, a book open (upright portrait), WiFi saved: long-press the word
                                      # at x y and leave its card idle; checks from the log that the vocab mirror
                                      # synced a page (V7a) or, on a synced mirror, ran the card's probe (V7b:
                                      # `card probe: 5 items`, about a second after the card settles): its time,
                                      # and only GET /v1/vocabulary? went out meanwhile. press: you press a side
                                      # button (the real one) while a whole page streams, and it must be given
                                      # up: it needs a page that takes a while (a fresh mirror: move
                                      # /.lexirise/vocab-<lang>.bin aside so a full pass runs). Read-only; one
                                      # held session
  lxctl.py home-sync-smoke [x y]      # a dev build, Lexirise on with a key, WiFi saved: Home, then tap Sync
                                      # Vocabulary (x y: the row, default theme without OPDS); checks the result,
                                      # read-only calls, and WiFi given back after it (V7b). One held session
  lxctl.py cache-smoke [x1 y1 x2 y2]  # a dev build, a Japanese or Chinese book open at running text (upright
                                      # portrait), WiFi saved; two words not looked up before (x1 y1, x2 y2): a
                                      # card on the first (cache miss, one dictionary/lookup, the answer written
                                      # as it closes); past the TLS idle close, the first again (a hit, no lookup);
                                      # then the second (a miss); a TLS session after the first card resumed
                                      # (which call isn't said) (V7c). Read-only; one held session
  lxctl.py page-smoke [x y]           # a dev build, a Japanese or Chinese book open at running text (upright
                                      # portrait), WiFi saved: a card on the word at x y brings WiFi up; then the
                                      # page and the next are analyzed, a turn, fast turns, and a card on the page
                                      # reached; checks from the log the dwell, the steps, no analyze/text on that
                                      # card and no write (V7b). Read-only; one held session
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
# LX:LEXI: the device bounds one call by config::kMaxCallMs (LexiriseConfig.h, 45 s); wait longer. test_lxctl
# checks the margin against the header.
LEXI_CALL_TIMEOUT_S = 50.0
LEXI_CALL_MARGIN_MS = 5000
LEXI_SOAK_MAX = 50  # DevConfig kLexiSoakMax
LEAK_BYTES_PER_CALL = 64  # a free-heap trend steeper than this, per call, fails the soak
LEAK_MIN_SAMPLES = 5
# card-smoke: the bench's phases end by config::kBenchPhaseBMs (900 ms); wait for them before tapping.
# card-sentence also waits this long for the bench's "next sentence" (config::kBenchNextSentenceMs, 1000 ms).
# test_lxctl checks it outlasts both.
CARD_PHASES_S = 1.5
CARD_HOME_PRESSES_MAX = 3  # Home: expanded → card → closed, plus one spare
# A close redraws the reader, a half or full refresh on every 5th card (~1.34 s measured, popup-ui.md §2);
# wait that out with a margin before the next Home, or a spare Home would reach the screen underneath.
PANEL_FULL_REFRESH_S = 1.34
CARD_CLOSE_WAIT_S = 3.0
CARD_WORD_LOG = re.compile(r"\[LXCARD\] word (\d+)")  # LexiriseCardActivity, smoke mode
CARD_VIEW_LOG = re.compile(r"\[LXCARD\] word \d+ view (\w+) tab (\d+)")
# card-gestures (popup-ui.md §3.2), on the bench's ja card (card view: y 530-786; the detail view from 80, as
# in test/lexirise_card/golden, which test_lxctl checks these against): each swipe, the view and tab it leaves
# (None: it closes the card), and what it is. A card swipe starts on the card, at least
# config::kCardSwipeEdgeMarginPx (85) clear of the left, top and bottom edges and outside the SDK's
# edge-gesture bands (CardInput.h swipeClearOfEdges). "back" starts in the SDK's left band, so it's Back (the
# detail view goes back to the card, the tab kept); "page" starts on the page above the card: nothing. "long"
# is a long-press on the page: on the bench it's consumed and dropped (no word select under it), so its lift
# must not tap the page and close the card; nothing is logged for it.
CARD_GESTURES = [
    ("SWIPE 240 670 240 430", ("expanded", 0), "card"),  # up from the card: the detail view
    ("SWIPE 380 400 120 400", ("expanded", 1), "card"),  # left: the next tab
    ("SWIPE 380 400 120 400", ("expanded", 2), "card"),
    ("SWIPE 160 400 420 400", ("expanded", 1), "card"),  # right: the previous tab
    ("SWIPE 60 400 320 400", ("card", 1), "back"),       # right from the left edge: Back, not a tab
    ("SWIPE 240 300 240 100", ("card", 1), "page"),      # starts above the card: not the card's
    ("LONG 240 300", ("card", 1), "long"),               # a long-press on the page: its lift isn't a tap
    ("SWIPE 240 670 240 430", ("expanded", 1), "card"),  # up again: the tab was kept
    ("SWIPE 240 300 240 600", ("card", 1), "card"),      # down: back to the card
    ("SWIPE 240 560 240 760", None, "card"),             # down on the card: closes it
]
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
    words = logged_words(log)
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


CARD_SENTENCE_STEPS_MAX = 40  # side-button presses before card-sentence gives up (the bench has ~10 words)


def logged_words(log: list[str]) -> list[int]:
    """The card's "[LXCARD] word n" log lines (smoke mode), in order."""
    return [int(m.group(1)) for line in log if (m := CARD_WORD_LOG.search(line))]


def card_sentence(h: Harness, sleep=time.sleep) -> list[int]:
    """P9 on the device: the side button past the bench card's last word goes on into its "next sentence" (the
    bench's one sentence again, after the time an analysis takes). The card stays on the last word, then
    jumps to the next sentence's first on its own, and stops at the page's end. Checked from the smoke log;
    needs upright portrait and the default side buttons (as card-smoke). Returns the words it went through."""
    h.command("LEXI CARD ja KANA", "LX:OK LEXI")
    h.wait_for("Entering activity: LexiriseCard", ACTIVITY_WAIT_S)
    sleep(CARD_PHASES_S)
    seen: list[int] = []
    jumped = False
    for _ in range(CARD_SENTENCE_STEPS_MAX):
        log: list[str] = []
        h.command("BTN RIGHT", seen=log)
        h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
        words = logged_words(log)
        if not words:
            raise RuntimeError("the card logged no word after a side-button press")
        if seen and not jumped and words[-2:] == [seen[-1], seen[-1] + 1]:
            jumped = True  # it stayed, and the jump came before this read ended (a slow refresh held the SYNC)
        elif seen and words[-1] == seen[-1]:  # stayed: waiting for the next sentence, or the page's end
            if jumped:
                break
            sleep(CARD_PHASES_S)  # the "analysis"
            log = []
            h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
            later = logged_words(log)
            if not later or later[-1] != seen[-1] + 1:
                raise RuntimeError(f"stayed on word {seen[-1]} but didn't go on to {seen[-1] + 1}")
            jumped = True
            words = later
        seen.append(words[-1])
    else:
        raise RuntimeError(f"no end after {CARD_SENTENCE_STEPS_MAX} presses")
    if not jumped:
        raise RuntimeError("the card never went past its sentence's last word")
    for _ in range(CARD_HOME_PRESSES_MAX):
        h.command("HOME")
        try:
            h.wait_for("Exiting activity: LexiriseCard", CARD_CLOSE_WAIT_S)
            break
        except TimeoutError:
            continue
    print(f"card-sentence OK: words {seen}")
    return seen


# reader-longpress (lookup-flow.md §5e): the reader's "[LXLP] long-press x y taken|ignored|left"
# (EpubReaderActivity, dev builds; lookup::longPressUseName). Off the text: the bottom margin's middle is always
# the lookup's zone; the left margin at mid-page is where a lift that wasn't consumed would turn the page back
# (so it shows P10's "nothing"), and it's CrossPoint's own while its hold action (Long-press Behavior) is on.
# On a page of running text, its middle is a word. An ignored press must open nothing and redraw no page.
READER_LONGPRESS_LOG = re.compile(r"\[LXLP\] long-press (-?\d+) (-?\d+) (taken|ignored|left)")
READER_OFF_TEXT = (240, 796)
READER_SIDE_OFF_TEXT = (3, 400)
READER_ON_TEXT = (240, 400)
READER_PAGE_DRAWN = "[ERS] Rendered page"  # EpubReaderActivity::render, a page drawn (a turn, a reflow)
# P10: with the card open (it covers the bottom of the screen: CardMetrics.h, the card view's frame from about
# y 550), a tap on the page above it looks up the word there: the card closes and another opens, or, with no
# word there, word select goes back to the reader.
READER_RETAP = (240, 150)
CARD_OPENED = "Entering activity: LexiriseCard"
DEFINITION_OPENED = "Entering activity: DictionaryDefinition"  # StarDict answered instead of Lexirise


def collect_until(h: Harness, patterns: tuple[str, ...], timeout: float) -> list[str]:
    """Device log lines, in order, up to and including the first holding one of `patterns`; TimeoutError if
    none comes."""
    deadline = time.time() + timeout
    lines: list[str] = []
    while True:
        line = h.read_line(deadline)
        if line is None:
            raise TimeoutError(f"timed out waiting for any of {patterns}")
        lines.append(line)
        if any(p in line for p in patterns):
            return lines


def retap(h: Harness, seen: list[str], at: tuple[int, int] = READER_RETAP) -> str:
    """After a long-press on a word (`seen`: the log so far): once its card is open, a tap on the page above
    the card. Returns "card" (the word there looked up: another card), "reader" (no word there: back to the
    reader) or "none" (StarDict answered, no card: not checked)."""
    opened = next((line for line in seen if CARD_OPENED in line or DEFINITION_OPENED in line), None)
    if opened is None:
        opened = collect_until(h, (CARD_OPENED, DEFINITION_OPENED), LEXI_CALL_TIMEOUT_S)[-1]
    if DEFINITION_OPENED in opened:
        return "none"
    h.command("SYNC", timeout=SYNC_TIMEOUT_S)
    h.command(f"TAP {at[0]} {at[1]}")
    collect_until(h, ("Exiting activity: LexiriseCard",), CARD_CLOSE_WAIT_S)  # the tap closed the card
    after = collect_until(h, ("Entering activity: LexiriseCard", "Exiting activity: DictionaryWordSelect"),
                          LEXI_CALL_TIMEOUT_S)
    if CARD_OPENED not in after[-1]:
        return "reader"
    # The new card's lookup blocks its loop: wait it out, or a Back sent now would land after it and a spare
    # one would leave the book.
    h.command("SYNC", timeout=SYNC_TIMEOUT_S)
    return "card"
READER_BACKS_MAX = 3  # Back: the card or definition → the reader, plus spares


def reader_longpress(h: Harness, on_text: tuple[int, int] = READER_ON_TEXT) -> dict[str, str]:
    """P9/P10 on the device, with a book open in the reader (upright portrait) and something to look words up
    with: a long-press in the bottom margin is ignored (consumed, nothing opens; the P9 bug was word select
    with nothing under the finger), and one on a word (`on_text`) is taken and opens word select. Anything
    opened is closed with the Back swipe. Returns each press's use."""
    uses: dict[str, str] = {}
    for name, (x, y) in (("margin", READER_OFF_TEXT), ("side", READER_SIDE_OFF_TEXT), ("word", on_text)):
        log: list[str] = []
        h.command(f"LONG {x} {y}", seen=log)
        h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
        decisions = [m.group(3) for line in log if (m := READER_LONGPRESS_LOG.search(line))]
        if not decisions:
            raise RuntimeError(f"{name}: the reader logged no long-press (is a book open in the reader?)")
        uses[name] = decisions[-1]
        taken = uses[name] == "taken"
        opened = any("Entering activity: DictionaryWordSelect" in line for line in log)
        if name == "side" and uses[name] == "left":
            print(f"side ({x}, {y}): left to CrossPoint (its hold action owns the sides): not checked")
            continue
        if name in ("margin", "side") and uses[name] != "ignored":
            where = "bottom margin" if name == "margin" else "left margin"
            raise RuntimeError(f"a long-press in the {where} was {uses[name]}, not ignored"
                               + (" (is a dictionary or a Lexirise key set up?)" if uses[name] == "left" else ""))
        if uses[name] == "ignored":
            acted = [line for line in log if "Entering activity:" in line or READER_PAGE_DRAWN in line]
            if acted:
                raise RuntimeError(f"an ignored long-press at ({x}, {y}) still did something: {acted[0]}")
        if opened != taken:
            raise RuntimeError(f"{name}: taken={taken} but word select {'opened' if opened else 'did not open'}")
        if name == "word" and not taken:
            raise RuntimeError(f"a long-press at ({x}, {y}) wasn't taken: open a page with text there, or pass "
                               "the point of a word")
        if opened and name == "word":
            uses["retap"] = retap(h, log)
            found = {"card": "its word looked up", "reader": "no word there: back to the reader",
                     "none": "no card (StarDict answered): not checked"}[uses["retap"]]
            print(f"tap on the page above the card: {found}")
            if uses["retap"] == "reader":
                continue  # word select has already closed
        if opened:
            for _ in range(READER_BACKS_MAX):
                h.command(f"SWIPE {EDGE_INSET} 400 240 400")  # Back
                try:
                    h.wait_for("Exiting activity: DictionaryWordSelect", CARD_CLOSE_WAIT_S)
                    break
                except TimeoutError:
                    continue
            else:
                raise RuntimeError(f"{name}: word select didn't close")
        print(f"{name} ({x}, {y}): {uses[name]}")
    print("reader-longpress OK")
    return uses


def card_gestures(h: Harness, sleep=time.sleep) -> None:
    """The card's swipes on the device (P7): opens the bench card and checks where each CARD_GESTURES swipe
    leaves it, from the smoke log. Needs upright portrait. Ends over the screen it started on."""
    h.command("LEXI CARD ja KANA", "LX:OK LEXI")
    h.wait_for("Entering activity: LexiriseCard", ACTIVITY_WAIT_S)
    sleep(CARD_PHASES_S)  # the phases end before a finger would swipe
    for cmd, expected, kind in CARD_GESTURES:
        log: list[str] = []
        h.command(cmd, seen=log)
        if kind == "long":
            h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
            if any("Exiting activity: LexiriseCard" in line for line in log):
                raise RuntimeError(f"{cmd}: the card closed (the long-press's lift tapped the page)")
            print(f"{cmd}: consumed")
            continue
        if expected is None:
            h.wait_for("Exiting activity: LexiriseCard", CARD_CLOSE_WAIT_S)
            continue
        h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
        states = [(m.group(1), int(m.group(2))) for line in log if (m := CARD_VIEW_LOG.search(line))]
        if not states or states[-1] != expected:
            raise RuntimeError(f"{cmd}: the card is at {states[-1] if states else 'nothing logged'}, not {expected}")
        print(f"{cmd}: {expected[0]}, tab {expected[1]}")
    print("card-gestures OK")


# deck-smoke (v0.2 V3, C4): the book deck's calls, from the service's "[LXS] <METHOD> <path> -> <status>" lines
# (LexiriseService::send, ids masked, logged once answered), the deck steps' "[LXDECK] step <kind> <key>" (logged as
# each starts, deck::sendDeckStep) and the store's "[LXDECK] Deck <lang>:<slug>: recorded". Each line starts
# "[<millis>]" (lib/Logging). The card's level buttons come from its "[LXCARD] level <i> <x> <y> <w> <h> <saved>"
# lines (LexiriseCardActivity, dev builds), part of the target sets ignore-smoke reads too: a "[LXCARD] targets <n>"
# header, then n lines, "level …" and "target …", logged again as a whole once the frame is on screen whenever any
# target changes (target_sets).
DECK_CALL_LOG = re.compile(r"^\[(\d+)\] \[\w+\] \[LXS\] (GET|POST|PATCH|DELETE) (\S+) -> (-?\d+)")
DECK_STEP_LOG = re.compile(r"^\[(\d+)\] \[\w+\] \[LXDECK\] step (check|list|create) (\S+)")
DECK_RECORDED_LOG = re.compile(r"\[LXDECK\] Deck (\S+): recorded")
CARD_LEVEL_LOG = re.compile(r"\[LXCARD\] level (\d) (-?\d+) (-?\d+) (\d+) (\d+) ([01])")
DECK_IDLE_MS = 3000  # config::kDeckIdleMs (test_lxctl checks it)
# A new book's deck: the save's Undo window (2 s), the save, then two idle windows (3 s each) and two calls.
DECK_WATCH_S = 40.0
DECK_SAVE_LEVEL = 1  # L: Level::Learning (CardModel.h; test_lxctl checks it)
DECK_WORDS = ((240, 150), (240, 400))


def deck_calls(log: list[str]) -> list[tuple[int, str, str, int]]:
    """Every Lexirise call in `log`: (millis, method, masked path, status), logged as each was answered."""
    return [(int(m.group(1)), m.group(2), m.group(3), int(m.group(4))) for line in log
            if (m := DECK_CALL_LOG.search(line))]


def deck_steps(log: list[str]) -> list[tuple[int, str]]:
    """Every book deck step in `log`: (millis as it started, check | list | create)."""
    return [(int(m.group(1)), m.group(2)) for line in log if (m := DECK_STEP_LOG.search(line))]


def level_buttons(log: list[str]) -> dict[int, tuple[int, int, int, int, bool]]:
    """The card's level buttons in the last complete target set logged: index → (x, y, w, h, the word saved)."""
    sets = target_sets(log)
    last = sets[-1] if sets else {}
    return {i: (v[0], v[1], v[2], v[3], v[4] == 1) for (kind, i), v in last.items() if kind == "level"}


def check_deck_cards(cards: list[tuple[list[str], list[str]]], idle_ms: int = DECK_IDLE_MS,
                     watch_s: float = DECK_WATCH_S) -> list[str]:
    """V3's book deck on the device, from each card's log: (while open: the save and what followed, the close).
    Every card saved a new word, and every deck step is for one book deck (one key: pick both words in the book's
    language). Each deck step starts at least `idle_ms` after the save and after the deck call
    before it (each step waits its own idle time); steps go check → list → create; a creation is followed by
    "recorded". Nothing touches /v1/decks while a card closes. Over the run (one boot): at most one creation and
    one check; once the deck was recorded or checked, no card calls /v1/decks again; the first card made one.
    Returns what each card did ("created", "found", "checked", "none"); raises RuntimeError on the first broken
    rule. Sleep isn't covered: a run is one boot, awake."""
    posts = checks = 0
    settled = False
    done: list[str] = []
    order = {"check": 0, "list": 1, "create": 2}
    keys = {m.group(3) for opened, _ in cards for line in opened if (m := DECK_STEP_LOG.search(line))}
    keys |= {m.group(1) for opened, _ in cards for line in opened if (m := DECK_RECORDED_LOG.search(line))}
    if len(keys) > 1:  # one book, and both words in its language
        raise RuntimeError(f"deck steps for more than one book deck: {sorted(keys)}")
    for n, (opened, closing) in enumerate(cards, 1):
        calls = deck_calls(opened)
        saves = [c for c in calls if c[1] == "POST" and c[2] == "/v1/vocabulary" and 200 <= c[3] < 300]
        if not saves:
            raise RuntimeError(f"card {n} saved no new word (pick a word not saved yet)")
        decks = [c for c in calls if c[2].startswith("/v1/decks")]
        steps = deck_steps(opened)
        if [c for c in deck_calls(closing) if c[2].startswith("/v1/decks")] or deck_steps(closing):
            raise RuntimeError(f"card {n} called /v1/decks while closing")
        if (decks or steps) and settled:
            raise RuntimeError(f"card {n} called /v1/decks after the deck was settled this boot")
        since = saves[-1][0]
        last = -1
        for ms, kind in steps:
            # The idle time starts as a call's answer is applied, after its line is logged: never before the line.
            if ms - since < idle_ms:
                raise RuntimeError(f"card {n}: the {kind} step started {ms - since} ms after the save or the deck "
                                   f"call before it, before {idle_ms} ms of idle")
            if order[kind] < last:
                raise RuntimeError(f"card {n}: {kind} after {[k for k, v in order.items() if v == last][0]}")
            last = order[kind]
            answered = [c for c in decks if c[0] >= ms]
            since = answered[0][0] if answered else ms
        kinds = [k for _, k in steps]
        posts += kinds.count("create")
        checks += kinds.count("check")
        if posts > 1:
            raise RuntimeError("a second deck creation this boot")
        if checks > 1:
            raise RuntimeError("the recorded deck was checked twice this boot")
        recorded = any(DECK_RECORDED_LOG.search(line) for line in opened)
        if "create" in kinds and not recorded:
            raise RuntimeError(f"card {n} created a deck but recorded none (see the LXDECK lines)")
        checked = any(c[1] == "GET" and c[2].startswith("/v1/decks/") and 200 <= c[3] < 300 for c in decks)
        if n == 1 and not steps:
            raise RuntimeError(f"card 1 made no deck call (is Deck per book on? waited {watch_s:.0f} s)")
        did = ("created" if "create" in kinds else "found" if recorded else "checked" if checked
               else "none" if not steps else "failed")
        if did == "failed":
            raise RuntimeError(f"card {n}'s deck calls settled nothing: {decks}")
        settled = settled or did in ("created", "found", "checked")
        done.append(did)
    return done


def read_for(h: Harness, seconds: float, stop=None) -> list[str]:
    """Device log lines for up to `seconds`, or until `stop(line)`."""
    deadline = time.time() + seconds
    lines: list[str] = []
    while True:
        line = h.read_line(deadline)
        if line is None:
            return lines
        lines.append(line)
        if stop and stop(line):
            return lines


def deck_settled(line: str) -> bool:
    """A line that ends a card's deck work: the deck recorded, or the recorded one found (a 2xx check)."""
    m = DECK_CALL_LOG.search(line)
    return bool(DECK_RECORDED_LOG.search(line) or (m and m.group(2) == "GET" and m.group(3).startswith("/v1/decks/")
                                                    and 200 <= int(m.group(4)) < 300))


def deck_card(h: Harness, at: tuple[int, int], watch_s: float = DECK_WATCH_S) -> tuple[list[str], list[str]]:
    """One card in the open book: a long-press on the word at `at`, L (a new save) where the card logged it, then
    the log while it stays open (until its deck is settled, or `watch_s`), then Home and the close's log."""
    opened: list[str] = []
    h.command(f"LONG {at[0]} {at[1]}", seen=opened)
    opened += collect_until(h, (CARD_OPENED, DEFINITION_OPENED), LEXI_CALL_TIMEOUT_S)
    if DEFINITION_OPENED in opened[-1]:
        raise RuntimeError(f"StarDict answered the word at {at}, not Lexirise")
    h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=opened)  # its lookup drawn: the level buttons logged
    buttons = level_buttons(opened)
    if DECK_SAVE_LEVEL not in buttons:
        raise RuntimeError("the card logged no level buttons (a dev build, env:x4pro?)")
    x, y, w, hh, saved = buttons[DECK_SAVE_LEVEL]
    if saved:
        raise RuntimeError(f"the word at {at} is saved already: pick one that isn't")
    h.command(f"TAP {x + w // 2} {y + hh // 2}", seen=opened)
    opened += read_for(h, watch_s, deck_settled)
    closing: list[str] = []
    h.command("HOME", seen=closing)
    closing += collect_until(h, ("Exiting activity: LexiriseCard",), CARD_CLOSE_WAIT_S + LEXI_CALL_TIMEOUT_S)
    closing += read_for(h, CARD_CLOSE_WAIT_S)  # back to the reader
    return opened, closing


def deck_smoke(h: Harness, words=DECK_WORDS, watch_s: float = DECK_WATCH_S) -> list[str]:
    """V3's book deck on the device. CREATES A REAL DECK in the account (unless the book has one) and saves two
    words: run it only with claritise's OK (lxctl.py --creates-a-deck deck-smoke). A dev build (env:x4pro: the
    card logs its level buttons), a book open in the reader, upright portrait, Lexirise and Deck per book on, WiFi
    saved; `words`: two words not saved yet. Card 1 saves one and waits for the deck (a new book: list, create; a
    book recorded on an earlier boot: one check); card 2 saves the other and must make no deck call this boot.
    Checked from the log by check_deck_cards. Sleep isn't covered."""
    cards = [deck_card(h, at, watch_s) for at in words]
    done = check_deck_cards(cards, watch_s=watch_s)
    print(f"deck-smoke OK: {', '.join(done)}")
    return done


# ignore-smoke (v0.2 V5, C17): the reader's own ignore list. The card logs its tap targets as a whole set when they
# change (dev builds, LexiriseCardActivity: "[LXCARD] targets <n>", then n lines: "level …" and "[LXCARD] target
# <rank|tab|action|undo> <i> <x> <y> <w> <h>") and each change written to ignored.ini (CardSession::persistIgnores:
# "[LXCARD] ignore <key> <on|off> <written|unchanged|failed>"; a form key can hold spaces).
CARD_TARGETS_HEADER = re.compile(r"\[LXCARD\] targets (\d+)$")
CARD_SET_LINE = re.compile(r"\[LXCARD\] (level|target) ")
CARD_TARGET_LOG = re.compile(r"\[LXCARD\] target (rank|tab|action|undo) (\d+) (-?\d+) (-?\d+) (\d+) (\d+)")
IGNORE_LOG = re.compile(r"\[LXCARD\] ignore (.+) (on|off) (written|unchanged|failed)$")
IGNORE_ACTION = 2  # ActionId::Ignore (CardModel.h; test_lxctl checks it)
IGNORE_WATCH_S = 10.0  # the SD write after a tap: well under a second
IGNORE_TOAST_MS = 5000  # config::kIgnoreToastMs: how long the Ignore's Undo is offered (test_lxctl checks it)


def target_sets(log: list[str]) -> list[dict[tuple[str, int], tuple[int, int, int, int]]]:
    """Every complete set of tap targets in `log`, in order: (kind, index) → (x, y, w, h), and for a level button
    ("level", i) → (x, y, w, h, saved). A set is a "targets <n>" header and the n lines after it; one cut short (the
    log ended, or another set began) isn't complete."""
    sets: list[dict[tuple[str, int], tuple[int, int, int, int]]] = []
    expected = -1
    current: dict[tuple[str, int], tuple[int, int, int, int]] = {}
    seen = 0
    for line in log:
        if m := CARD_TARGETS_HEADER.search(line):
            expected, current, seen = int(m.group(1)), {}, 0
        elif expected >= 0 and CARD_SET_LINE.search(line):
            seen += 1
            if m := CARD_TARGET_LOG.search(line):
                current[(m.group(1), int(m.group(2)))] = tuple(int(v) for v in m.groups()[2:])  # type: ignore
            elif m := CARD_LEVEL_LOG.search(line):  # (x, y, w, h, saved)
                current[("level", int(m.group(1)))] = tuple(int(v) for v in m.groups()[1:])  # type: ignore
        else:
            continue
        if expected >= 0 and seen == expected:
            sets.append(current)
            expected = -1
    return sets


def tap_targets(log: list[str]) -> dict[tuple[str, int], tuple[int, int, int, int]]:
    """The card's tap targets now: the last complete set logged (none: empty)."""
    sets = target_sets(log)
    return sets[-1] if sets else {}


def ignore_events(log: list[str]) -> list[tuple[str, str, str]]:
    """Every ignore-list change in `log`: (key, on | off, written | unchanged | failed)."""
    return [(m.group(1), m.group(2), m.group(3)) for line in log if (m := IGNORE_LOG.search(line))]


def check_ignore_log(log: list[str]) -> str:
    """V5's Ignore on the device, from the card's log: one change "on written", then its Undo "off written" for the
    same key, nothing else to the list; and no write to Lexirise (no POST, PATCH or DELETE on /v1/vocabulary; a
    saved word's Met before GET is expected). Returns the key; raises RuntimeError on the first broken rule."""
    writes = [c for c in deck_calls(log) if c[1] in ("POST", "PATCH", "DELETE") and c[2].startswith("/v1/vocabulary")]
    if writes:
        raise RuntimeError(f"Ignore wrote to Lexirise: {writes}")
    events = ignore_events(log)
    if not events:
        raise RuntimeError("no ignore-list change logged (a dev build? was the word already ignored?)")
    key, on, result = events[0]
    if (on, result) != ("on", "written"):
        raise RuntimeError(f"the Ignore wasn't written: {events[0]}")
    if len(events) != 2 or events[1] != (key, "off", "written"):
        raise RuntimeError(f"expected the Ignore then its Undo, both written, got {events}")
    return key


def next_target_set(h: Harness, log: list[str], since: int,
                    watch_s: float) -> dict[tuple[str, int], tuple[int, int, int, int]] | None:
    """The first complete set of targets logged from log[since] on, reading more lines (appended to `log`) until one
    completes or `watch_s` passes; None if none does."""
    deadline = time.time() + watch_s
    while True:
        sets = target_sets(log[since:])
        if sets:
            return sets[0]
        line = h.read_line(deadline)
        if line is None:
            return None
        log.append(line)


def tap_centre(h: Harness, target: tuple[int, int, int, int], seen: list[str]) -> None:
    x, y, w, hh = target
    h.command(f"TAP {x + w // 2} {y + hh // 2}", seen=seen)


def ignore_smoke(h: Harness, at: tuple[int, int] = READER_ON_TEXT, watch_s: float = IGNORE_WATCH_S) -> str:
    """V5's Ignore on the device. A dev build (env:x4pro: the card logs its tap targets and its ignore-list
    changes), a book open in the reader, upright portrait, Lexirise on; `at`: a word not ignored yet. Opens its card,
    the detail view, the ⋯ tab, taps "Ignore this word", then the toast's Undo, and closes the card. Checked from the
    log by check_ignore_log. Writes only the reader's /.lexirise/ignored.ini (the word on, then off again)."""
    log: list[str] = []
    h.command(f"LONG {at[0]} {at[1]}", seen=log)
    log += collect_until(h, (CARD_OPENED, DEFINITION_OPENED), LEXI_CALL_TIMEOUT_S)
    if DEFINITION_OPENED in log[-1]:
        raise RuntimeError(f"StarDict answered the word at {at}, not Lexirise")

    def synced() -> dict[tuple[str, int], tuple[int, int, int, int]]:
        h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
        return tap_targets(log)

    targets = synced()
    if ("rank", 0) not in targets:
        raise RuntimeError("the card logged no tap targets (a dev build, env:x4pro?)")
    tap_centre(h, targets[("rank", 0)], log)  # the detail view
    targets = synced()
    tabs = [i for kind, i in targets if kind == "tab"]
    if not tabs:
        raise RuntimeError("the detail view logged no tabs")
    tap_centre(h, targets[("tab", max(tabs))], log)  # ⋯, the last tab
    targets = synced()
    if ("action", IGNORE_ACTION) not in targets:
        raise RuntimeError("the ⋯ tab logged no Ignore row")
    # The toast's Undo lasts IGNORE_TOAST_MS: no SYNC (it could outlast that); the first set of targets drawn after
    # the tap is the toast's, waited for no longer than the toast lasts, and its Undo is tapped at once.
    since = len(log)
    tap_centre(h, targets[("action", IGNORE_ACTION)], log)
    toast = next_target_set(h, log, since, min(watch_s, IGNORE_TOAST_MS / 1000))
    events = ignore_events(log)
    key = events[0][0] if events else "<the word's key>"
    if toast is None:
        raise RuntimeError(f"no frame drawn after the Ignore tap (check /.lexirise/ignored.ini for {key})")
    if ("undo", 0) not in toast:
        raise RuntimeError(f"toast expired before the Undo tap: remove {key} from /.lexirise/ignored.ini")
    tap_centre(h, toast[("undo", 0)], log)
    if not any(e[1] == "off" for e in ignore_events(log)):
        log += read_for(h, watch_s, lambda line: bool(IGNORE_LOG.search(line)) and " off " in line)
    if not any(e[1] == "off" for e in ignore_events(log)):  # the Undo tap didn't land (or wasn't written)
        raise RuntimeError(f"the Undo tap took nothing off the list: remove {key} from /.lexirise/ignored.ini")
    h.command("HOME", seen=log)
    log += collect_until(h, ("Exiting activity: LexiriseCard",), CARD_CLOSE_WAIT_S + LEXI_CALL_TIMEOUT_S)
    key = check_ignore_log(log)
    print(f"ignore-smoke OK: {key} on, then off")
    return key


# vocab-smoke (v0.2 V7a, C13): the vocab mirror's sync on an idle card, from the log. Each page logs
# "[LXVOCAB] <full|incremental> <lang> offset <n>: <items> items, mirror <words> words" (vocab::applyPage) and then
# "[LXVOCAB] page <items> items in <ms> ms (<error>[, given up for input]), applied in <ms> ms (file <written|not
# written|write failed>); heap <free> free, <min> min, <largest> largest" (LexiriseCardActivity::idleStep, every build); a page given up for a
# button logs "[LXVOCAB] <pass> <lang> offset <n> given up: input came" (VocabStore::apply); the mirror's file read or
# write, "[LXVOCAB] mirror file read or written in <ms> ms[ (closing)]" (only when it read or wrote). Lexirise calls:
# the service's "[LXS] ..." lines (DECK_CALL_LOG).
VOCAB_LOG = re.compile(r"^\[(\d+)\] \[\w+\] \[LXVOCAB\] ")
VOCAB_PAGE_LOG = re.compile(r"\[LXVOCAB\] page (\d+) items in (\d+) ms \(([^)]*)\), applied in (\d+) ms "
                            r"\(file (written|not written|write failed)\); heap (\d+) free, (\d+) min, (\d+) largest")
VOCAB_GIVEN_UP = "given up: input came"
VOCAB_IDLE_MS = 8000  # config::kVocabIdleMs (test_lxctl checks it)
# The card's first idle window reads the mirror's file (DECK_IDLE_MS), a page follows VOCAB_IDLE_MS later, and a page
# may take a whole request (config::kRequestDeadlineMs, 15 s) and its write: plenty past that.
VOCAB_WATCH_S = 60.0


# V7b: on a synced mirror the card's probe (IdleStep::Probe, about kVocabCardProbeIdleMs after the card settles)
# comes first and usually ends the incremental pass, so no idle page follows for kVocabSyncIntervalMs; it counts as
# the page ("[LXVOCAB] card probe: <items> items in <ms> ms (<error>[, given up for input]), <n> entries changed[, the
# card redrawn]", LexiriseCardActivity::idleStep).
CARD_PROBE_LOG = re.compile(r"\[LXVOCAB\] card probe: (\d+) items in (\d+) ms \(([^)]*)\), (\d+) entries changed")


def vocab_pages(log: list[str]) -> list[dict[str, int | str]]:
    """Every mirror page in `log`, in order: an idle page (kind "page": its items, time, error with ", given up for
    input", apply time, whether the file was written, heap) or the card's probe (kind "probe": items, time, error, the
    entries it changed)."""
    keys = ("items", "ms", "error", "applied_ms", "file", "free", "min", "largest")
    pages: list[dict[str, int | str]] = []
    for line in log:
        if m := VOCAB_PAGE_LOG.search(line):
            pages.append({"kind": "page", **{k: (v if k in ("error", "file") else int(v)) for k, v in zip(keys, m.groups())}})
        elif m := CARD_PROBE_LOG.search(line):
            pages.append({"kind": "probe", "items": int(m.group(1)), "ms": int(m.group(2)), "error": m.group(3),
                          "changed": int(m.group(4))})
    return pages


def is_mirror_page(line: str) -> bool:
    return bool(VOCAB_PAGE_LOG.search(line) or CARD_PROBE_LOG.search(line))


def check_vocab_log(log: list[str], pressed: bool = False) -> list[dict[str, int | str]]:
    """V7a's sync on the device, from the card's log: at least one page logged with its time and heap; from the first
    [LXVOCAB] line to the last page, the only Lexirise calls are GET /v1/vocabulary? (the list: no write, nothing else);
    `pressed`: a page was given up for the button, else one came whole ("ok"). Returns the pages; raises RuntimeError
    on the first broken rule."""
    pages = vocab_pages(log)
    if not pages:
        raise RuntimeError("no mirror page or card probe logged (a dev build? Lexirise on, WiFi up, the card left idle "
                           f"{VOCAB_IDLE_MS / 1000:.0f} s? a page due: see the [LXVOCAB] lines)")
    first = next(i for i, line in enumerate(log) if VOCAB_LOG.search(line))
    last = max(i for i, line in enumerate(log) if is_mirror_page(line))
    others = [c for c in deck_calls(log[first:last + 1]) if not (c[1] == "GET" and c[2].startswith("/v1/vocabulary?"))]
    if others:
        raise RuntimeError(f"a call other than the vocabulary list during the sync: {others}")
    if pressed:
        if not any(VOCAB_GIVEN_UP in line for line in log):
            raise RuntimeError("no page was given up for the button (pressed while a page streamed?)")
        # Mid-stream: its request went out (an [LXS] GET line after the page before it). A page given up before its
        # request (the button already held as it was due) logs none, and isn't the cancel this checks.
        # Every mirror call's line, pages and card probes alike: a probe's GET between two pages belongs to the probe.
        page_lines = [i for i, line in enumerate(log) if is_mirror_page(line)]
        mid_stream = False
        for n, i in enumerate(page_lines):
            m = VOCAB_PAGE_LOG.search(log[i])
            if not m or "given up for input" not in m.group(3):
                continue
            since = page_lines[n - 1] + 1 if n > 0 else first
            if any(c[1] == "GET" and c[2].startswith("/v1/vocabulary?") for c in deck_calls(log[since:i])):
                mid_stream = True
        if not mid_stream:
            raise RuntimeError("the page was given up before its request, not while it streamed (let go, then press "
                               "once the page has started)")
    elif not any(p["error"] == "ok" for p in pages):
        raise RuntimeError(f"no page came whole: {[p['error'] for p in pages]}")
    return pages


def vocab_smoke(h: Harness, at: tuple[int, int] = READER_ON_TEXT, press: bool = False,
                watch_s: float = VOCAB_WATCH_S) -> list[dict[str, int | str]]:
    """V7a's vocab mirror on the device, read-only. A dev build, one held serial session (opening the port resets the
    reader: dev-harness.md §3), a book open in the reader, upright portrait, Lexirise on, WiFi saved; `at`: a word
    Lexirise answers. Keeps the reader awake, opens the word's card and leaves it idle until a page is logged (or
    `watch_s`), then closes it. `press`: a person presses and releases a real side button while a page streams (an
    injected press isn't seen: the cancel reads the hardware), and the log is read until a page is given up (or
    `watch_s`), past pages that came whole first. On a synced mirror the card's probe (V7b: `card probe: 5 items`,
    about a second after the card settles) is the page and usually ends the pass, too short to press into: press
    needs a whole page, from a fresh mirror (/.lexirise/vocab-<lang>.bin moved aside, so a full pass runs) or more
    changes than the probe holds. Checked by check_vocab_log."""
    h.command("AWAKE 1")  # first: the whole run is one session, and the reader mustn't sleep during the idle wait
    log: list[str] = []
    h.command(f"LONG {at[0]} {at[1]}", seen=log)
    log += collect_until(h, (CARD_OPENED, DEFINITION_OPENED), LEXI_CALL_TIMEOUT_S)
    if DEFINITION_OPENED in log[-1]:
        raise RuntimeError(f"StarDict answered the word at {at}, not Lexirise")
    h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
    if press:
        print(f"leave the card; about {VOCAB_IDLE_MS / 1000:.0f} s after it settles an idle page starts (a synced "
              "mirror's card probe comes about a second after it settles, too short to press into): press and release "
              "a side button while a whole page streams (a fresh mirror, or more changes than the probe holds)")

        def done(line: str) -> bool:
            m = VOCAB_PAGE_LOG.search(line)
            return bool(m) and "given up for input" in m.group(3)
    else:
        def done(line: str) -> bool:
            return is_mirror_page(line)  # an idle page, or (a synced mirror) the card's probe
    log += read_for(h, watch_s, done)
    h.command("HOME", seen=log)
    log += collect_until(h, ("Exiting activity: LexiriseCard",), CARD_CLOSE_WAIT_S + LEXI_CALL_TIMEOUT_S)
    pages = check_vocab_log(log, press)
    p = pages[-1]
    if p["kind"] == "probe":
        print(f"vocab-smoke OK: the card's probe, {p['items']} items in {p['ms']} ms ({p['error']}), "
              f"{p['changed']} entries changed")
    else:
        print(f"vocab-smoke OK: {p['items']} items in {p['ms']} ms ({p['error']}), applied in {p['applied_ms']} ms "
              f"(file {p['file']}); heap {p['free']} free, {p['min']} min, {p['largest']} largest")
    return pages


# page-smoke (v0.2 V7b, C12): the page analysis, read-only, from the log. Each step logs "[LXPAGE] <this|next> page
# (<n> of section <s>): <kind> in <ms> ms (<error>), <n> occurrences[ (refined, merged)], <n> calls in <ms> ms,
# written in <ms> ms[ (failed)]; heap <free> free, <min> min, <largest> largest" (page::ReaderPages, every build) and a card on a page logs
# "[LXPAGE] card: page <s>-<start> analyzed: ..." or "... not analyzed" (page::PageSentences). A page is drawn:
# READER_PAGE_DRAWN.
PAGE_STEP_LOG = re.compile(r"^\[(\d+)\] \[\w+\] \[LXPAGE\] (this|next) page \((-?\d+) of section (-?\d+)\): "
                           r"(analyzed|kept already|failed|unusable|given up for input|no text|nothing) in (\d+) ms "
                           r"\(([^)]*)\), (\d+) occurrences(?: \(refined, merged\))?, (\d+) calls in (\d+) ms, "
                           r"written in (\d+) ms(?: \(failed\))?; heap (\d+) free, (\d+) min, (\d+) largest")
PAGE_CARD_LOG = re.compile(r"\[LXPAGE\] card: page (\d+)-(\d+) (analyzed|not analyzed)")
LOG_MILLIS = re.compile(r"^\[(\d+)\]")
PAGE_DWELL_MS = 1500  # config::kPagePrefetchDwellMs (test_lxctl checks it)
PAGE_WATCH_S = 30.0   # a step waits the dwell, then may open a TLS session and take a page's answer
PAGE_FAST_TURNS = 3   # turned back to back: no page is analyzed until they stop
PAGE_TURN = "BTN RIGHT"  # a side button: the reader turns the page (forward or back, by the side-button layout)
PAGE_WRITES = ("POST /v1/vocabulary", "PATCH ", "DELETE ", "POST /v1/decks")  # never during a read-only smoke


def page_steps(log: list[str]) -> list[dict[str, int | str]]:
    """Every page analysis step in `log`: when it ended (millis), which page, its kind, time, error, calls, heap."""
    keys = ("at", "which", "page", "section", "kind", "ms", "error", "occurrences", "calls", "call_ms", "write_ms",
            "free", "min", "largest")
    text = ("which", "kind", "error")
    return [{k: (v if k in text else int(v)) for k, v in zip(keys, m.groups())} for line in log
            if (m := PAGE_STEP_LOG.search(line))]


def check_page_log(log: list[str]) -> list[dict[str, int | str]]:
    """V7b's page analysis on the device, from the log: this page's and the next page's steps came (analyzed or kept
    already); every step started at least the dwell after the page on screen was drawn (pages turned faster are never
    analyzed); a card on an analyzed page sent no analyze/text; nothing written to Lexirise. Returns the steps; raises
    RuntimeError on the first broken rule."""
    steps = page_steps(log)
    done = [st for st in steps if st["kind"] in ("analyzed", "kept already")]
    if not any(st["which"] == "this" for st in done) or not any(st["which"] == "next" for st in done):
        raise RuntimeError("no page analyzed or kept for this page and the next (a dev build? WiFi up from the card? "
                           f"see the [LXPAGE] lines: {[st['kind'] + ' ' + str(st['error']) for st in steps]})")
    drawn = -1
    for line in log:
        m = LOG_MILLIS.search(line)
        if READER_PAGE_DRAWN in line and m:
            drawn = int(m.group(1))
        step = PAGE_STEP_LOG.search(line)
        if step and drawn < 0:
            raise RuntimeError(f"a page step with no page drawing logged before it (\"{READER_PAGE_DRAWN}\"): {line}")
        if step:
            started = int(step.group(1)) - int(step.group(6))
            if started < drawn + PAGE_DWELL_MS:
                raise RuntimeError(f"a page step started {started - drawn} ms after the page was drawn (the dwell is "
                                   f"{PAGE_DWELL_MS} ms): {line}")
    # One analyze/text per page: each page analyzed once, and every analyze/text outside a card is a step's call.
    pages = [(st["section"], st["page"]) for st in steps if st["kind"] == "analyzed"]
    if len(pages) != len(set(pages)):
        raise RuntimeError(f"a page was analyzed more than once: {pages}")
    in_card = False
    outside = 0
    for line in log:
        if CARD_OPENED in line:
            in_card = True
        elif "Exiting activity: LexiriseCard" in line:
            in_card = False
        elif not in_card and "[LXS] POST /v1/analyze/text" in line and "(cancelled)" not in line and \
                " -> 0 (" not in line:
            outside += 1  # only answered calls (an HTTP status): the step counts the same (Prefetch.cpp)
    step_calls = sum(int(st["calls"]) for st in steps)
    if outside != step_calls:
        raise RuntimeError(f"{outside} analyze/text calls outside cards, but the page steps sent {step_calls}")
    writes = [line for line in log if "[LXS] " in line and any(w in line for w in PAGE_WRITES)]
    if writes:
        raise RuntimeError(f"a write to Lexirise during a read-only smoke: {writes[0]}")
    cards = [i for i, line in enumerate(log) if PAGE_CARD_LOG.search(line)]
    analyzed = [i for i in cards if PAGE_CARD_LOG.search(log[i]).group(3) == "analyzed"]
    if not analyzed:
        raise RuntimeError("the card on an analyzed page didn't find its analysis (\"[LXPAGE] card: page ... "
                           f"analyzed\"): {[log[i] for i in cards]}")
    i = analyzed[-1]
    end = next((j for j in range(i, len(log)) if "Exiting activity: LexiriseCard" in log[j]), len(log))
    asked = [c for c in deck_calls(log[i:end]) if c[1] == "POST" and c[2] == "/v1/analyze/text"]
    if asked:
        raise RuntimeError("the card on an analyzed page still sent analyze/text")
    return steps


def page_smoke(h: Harness, at: tuple[int, int] = READER_ON_TEXT,
               watch_s: float = PAGE_WATCH_S) -> list[dict[str, int | str]]:
    """V7b's page analysis on the device, read-only. A dev build, one held serial session (dev-harness.md §3), a
    Japanese or Chinese book open in the reader at a page of running text, upright portrait, Lexirise on, WiFi saved;
    `at`: a word there. A card on the word brings WiFi up (the page analysis never does) and is closed; the page and
    the next are analyzed (or kept already); a turn at reading pace (the next page is kept already, its next
    analyzed); PAGE_FAST_TURNS turns back to back (nothing analyzed until they stop); then a card on the word at `at`
    of the page reached, which must take its sentence from the page's analysis. Checked by check_page_log."""
    h.command("AWAKE 1")
    log: list[str] = []

    def card() -> None:
        h.command(f"LONG {at[0]} {at[1]}", seen=log)
        log.extend(collect_until(h, (CARD_OPENED, DEFINITION_OPENED), LEXI_CALL_TIMEOUT_S))
        if DEFINITION_OPENED in log[-1]:
            raise RuntimeError(f"StarDict answered the word at {at}, not Lexirise")
        h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
        h.command("HOME", seen=log)
        log.extend(collect_until(h, ("Exiting activity: LexiriseCard",), CARD_CLOSE_WAIT_S + LEXI_CALL_TIMEOUT_S))

    def next_steps(count: int) -> None:
        seen = 0

        def stop(line: str) -> bool:
            nonlocal seen
            seen += 1 if PAGE_STEP_LOG.search(line) else 0
            return seen >= count

        log.extend(read_for(h, watch_s, stop))

    card()  # WiFi up
    next_steps(2)  # this page, the next
    h.command(PAGE_TURN, seen=log)
    next_steps(2)
    for _ in range(PAGE_FAST_TURNS):
        h.command(PAGE_TURN, seen=log)
    next_steps(2)
    card()  # on the page reached: from its analysis
    steps = check_page_log(log)
    analyzed = [st for st in steps if st["kind"] == "analyzed"]
    last = analyzed[-1] if analyzed else steps[-1]
    print(f"page-smoke OK: {len(steps)} steps; last analyzed in {last['ms']} ms ({last['calls']} calls in "
          f"{last['call_ms']} ms, written in {last['write_ms']} ms, {last['occurrences']} occurrences); heap "
          f"{last['free']} free, {last['min']} min, {last['largest']} largest")
    return steps


# home-sync-smoke (v0.2 V7b): the home screen's Sync Vocabulary, read-only, from the log: "[LXVOCAB] home sync:
# <up to date|synced|no WiFi|stopped|failed> after <n> pages, <n> words changed" (ManualSync), then "[LXVOCAB] home
# sync: WiFi <given back|left up (not Lexipoint's)> in <ms> ms" (HomeSync, as the result goes).
HOME_SYNC_LOG = re.compile(r"\[LXVOCAB\] home sync: (up to date|synced|no WiFi|stopped|failed) after (\d+) pages, "
                           r"(\d+) words changed")
HOME_SYNC_WIFI_LOG = re.compile(r"\[LXVOCAB\] home sync: WiFi (given back|left up \(not Lexipoint's\)|not up) in (\d+) ms")
HOME_OPENED = "Entering activity: Home"
# The row's centre in the default theme with no OPDS server (docs/v0.2/reference/v7b-home-sync.html): rows from y 314,
# 72 apart; Sync Vocabulary is the fourth (Browse Files, Library, File Transfer, it, Settings).
HOME_SYNC_ROW = (240, 314 + 72 * 3 + 32)
HOME_SYNC_WATCH_S = 120.0  # a join, then up to a few dozen pages on a first sync
# The result popup's centre (Lyra: y 132, 53 tall): the smoke taps it and a second sync mustn't follow (R9).
HOME_SYNC_POPUP = (240, 158)
# The result is logged before its popup is drawn: the tap waits this long (well inside kVocabSyncResultMs, 2 s), so it's
# a press on the popup shown, the one HomeSyncFlow dismisses on its release.
HOME_SYNC_TAP_AFTER_S = 0.8
HOME_SYNC_AFTER_S = 5.0  # watched after the result goes: nothing more may start


def check_home_sync_log(log: list[str]) -> dict[str, int | str]:
    """The home sync on the device, from the log: its result came (not failed), WiFi was given back (or left up when
    it wasn't Lexipoint's) after it, and the only Lexirise calls meanwhile were GET /v1/vocabulary? (read-only).
    Returns the result, pages, words changed and the release's time; raises RuntimeError on the first broken rule."""
    results = [(i, m) for i, line in enumerate(log) if (m := HOME_SYNC_LOG.search(line))]
    if not results:
        raise RuntimeError("no home sync result logged (the row tapped? Lexirise on with a key? a dev build?)")
    if len(results) > 1:
        raise RuntimeError("a second home sync started (the press dismissing the result reached the menu)")
    at, m = results[-1]
    if m.group(1) == "failed":
        raise RuntimeError(f"the home sync failed: {log[at]}")
    released = [(i, w) for i, line in enumerate(log) if (w := HOME_SYNC_WIFI_LOG.search(line)) and i > at]
    if not released:
        raise RuntimeError("WiFi wasn't given back after the home sync (no \"home sync: WiFi ...\" line)")
    after = [c for c in deck_calls(log[released[0][0] + 1:])]
    if after:
        raise RuntimeError(f"a Lexirise call after the home sync ended: {after}")
    others = [c for c in deck_calls(log[:at + 1]) if not (c[1] == "GET" and c[2].startswith("/v1/vocabulary?"))]
    if others:
        raise RuntimeError(f"a call other than the vocabulary list during the home sync: {others}")
    return {"result": m.group(1), "pages": int(m.group(2)), "changed": int(m.group(3)),
            "wifi": released[0][1].group(1), "release_ms": int(released[0][1].group(2))}


def home_sync_smoke(h: Harness, row: tuple[int, int] = HOME_SYNC_ROW,
                    watch_s: float = HOME_SYNC_WATCH_S) -> dict[str, int | str]:
    """V7b's Sync Vocabulary on the device, read-only. A dev build, one held serial session (dev-harness.md §3), the
    home screen reachable with the Home key, Lexirise on with a key, WiFi saved, the default theme, no OPDS server
    (else pass the row's point). Goes home, taps the row, and reads the log until WiFi is given back after the result
    (or `watch_s`). Checked by check_home_sync_log."""
    h.command("AWAKE 1")
    log: list[str] = []
    h.command("HOME", seen=log)
    if not any(HOME_OPENED in line for line in log):
        log.extend(collect_until(h, (HOME_OPENED,), ACTIVITY_WAIT_S))
    h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
    h.command(f"TAP {row[0]} {row[1]}", seen=log)
    log.extend(read_for(h, watch_s, lambda line: bool(HOME_SYNC_LOG.search(line))))
    log.extend(read_for(h, HOME_SYNC_TAP_AFTER_S))  # the popup drawn first
    h.command(f"TAP {HOME_SYNC_POPUP[0]} {HOME_SYNC_POPUP[1]}", seen=log)  # dismiss the result: on its release only
    log.extend(read_for(h, watch_s, lambda line: bool(HOME_SYNC_WIFI_LOG.search(line))))
    log.extend(read_for(h, HOME_SYNC_AFTER_S))
    r = check_home_sync_log(log)
    print(f"home-sync-smoke OK: {r['result']} after {r['pages']} pages, {r['changed']} words changed; WiFi "
          f"{r['wifi']} in {r['release_ms']} ms")
    return r


# cache-smoke (v0.2 V7c, C21): the lemma cache and TLS session resumption, read-only, from the log. Before each phase B
# the card logs "[LXLOOK] cache <hit|miss|stale> in <ms> ms (<h> of <n> hits this boot)"; the idle Flush step and the
# close "[LXLOOK] cache: <n> answers <written|not written> in <ms> ms[ (closing)]" (LexiriseCardActivity); each TLS
# session "[LXT] Verified <host> (<version>, <cipher>, <resumed|full>) in <ms> ms, free heap <n>" (TlsConnection).
CACHE_READ_LOG = re.compile(r"\[LXLOOK\] cache (hit|miss|stale) in (\d+) ms \((\d+) of (\d+) hits this boot\)")
CACHE_WRITE_LOG = re.compile(r"\[LXLOOK\] cache: (\d+) answers (written|not written) in (\d+) ms( \(closing\))?")
TLS_VERIFIED_LOG = re.compile(r"\[LXT\] Verified (\S+) \(([^,]+), ([^,]+), (resumed|full)\) in (\d+) ms, "
                              r"free heap (\d+)")
CACHE_LOOKUP = ("POST", "/v1/dictionary/lookup")
CACHE_SECOND_WORD = (READER_ON_TEXT[0], READER_ON_TEXT[1] + 120)  # another line of the page: pass a real word
TLS_IDLE_CLOSE_S = 30.0  # config::kTlsIdleCloseMs (test_lxctl checks it)
CACHE_IDLE_WAIT_S = TLS_IDLE_CLOSE_S + 5.0  # past it: the session closed, the next call handshakes again
CARD_CLOSED = "Exiting activity: LexiriseCard"


def card_spans(log: list[str]) -> list[tuple[int, int]]:
    """Each card's lines as (first, past the last) in `log`, from its opening to its exit (the close's flush is logged
    before the exit)."""
    spans: list[tuple[int, int]] = []
    start: int | None = None
    for i, line in enumerate(log):
        if CARD_OPENED in line:
            start = i
        elif start is not None and CARD_CLOSED in line:
            spans.append((start, i + 1))
            start = None
    return spans


def check_cache_log(log: list[str]) -> dict[str, int]:
    """V7c on the device, from the log of three cards: the first word's card missed the cache, sent one
    dictionary/lookup and wrote its answer; the same word's second card hit, with no lookup; the second word's card
    missed, and a TLS session after the first card was resumed; nothing written to Lexirise. Returns the read, write
    and handshake times; raises RuntimeError on the first broken rule."""
    spans = card_spans(log)
    if len(spans) < 3:
        raise RuntimeError(f"{len(spans)} cards opened and closed, not 3 (a dev build? Lexirise, not StarDict?)")
    first, again, second = (log[a:b] for a, b in spans[-3:])

    def reads(card: list[str]) -> list[re.Match]:
        return [m for line in card if (m := CACHE_READ_LOG.search(line))]

    def lookups(card: list[str]) -> int:
        return sum(1 for c in deck_calls(card) if (c[1], c[2]) == CACHE_LOOKUP)

    r1 = reads(first)
    if not r1 or r1[0].group(1) != "miss":
        raise RuntimeError(f"the first card's cache read wasn't a miss: {[m.group(0) for m in r1]} (a word looked up "
                           "before stays cached for 30 days: pass the coordinates of words never looked up, "
                           "cache-smoke x1 y1 x2 y2)")
    if lookups(first) != 1:
        raise RuntimeError(f"the first card sent {lookups(first)} dictionary/lookup calls, not 1")
    writes = [m for line in first if (m := CACHE_WRITE_LOG.search(line))]
    written = [m for m in writes if m.group(2) == "written"]
    if not written:
        raise RuntimeError(f"the first card's answer wasn't written to the cache: {[m.group(0) for m in writes]}")
    r2 = reads(again)
    if not r2 or r2[0].group(1) != "hit":
        raise RuntimeError(f"the same word's second card didn't hit the cache: {[m.group(0) for m in r2]}")
    if lookups(again):
        raise RuntimeError("the same word's second card still sent dictionary/lookup")
    r3 = reads(second)
    if not r3 or r3[0].group(1) != "miss":
        raise RuntimeError(f"the second word's cache read wasn't a miss: {[m.group(0) for m in r3]} (cached for 30 "
                           "days once looked up: pass fresh coordinates, cache-smoke x1 y1 x2 y2)")
    after = log[spans[-3][1]:]
    handshakes = [m for line in after if (m := TLS_VERIFIED_LOG.search(line))]
    resumed = [m for m in handshakes if m.group(4) == "resumed"]
    if not resumed:
        raise RuntimeError(f"no resumed TLS session after the idle close: {[m.group(0) for m in handshakes]}")
    bad = [line for line in log if "[LXS] " in line and any(w in line for w in PAGE_WRITES)]
    if bad:
        raise RuntimeError(f"a write to Lexirise during a read-only smoke: {bad[0]}")
    full = [m for line in log if (m := TLS_VERIFIED_LOG.search(line)) and m.group(4) == "full"]
    return {"miss_ms": int(r1[0].group(2)), "hit_ms": int(r2[0].group(2)), "write_ms": int(written[-1].group(3)),
            "resumed_ms": int(resumed[0].group(5)), "resumed_heap": int(resumed[0].group(6)),
            "full_ms": int(full[0].group(5)) if full else -1}


def cache_smoke(h: Harness, first: tuple[int, int] = READER_ON_TEXT, second: tuple[int, int] = CACHE_SECOND_WORD,
                idle_s: float = CACHE_IDLE_WAIT_S, watch_s: float = LEXI_CALL_TIMEOUT_S) -> dict[str, int]:
    """V7c's lemma cache and TLS resumption on the device, read-only. A dev build, one held serial session
    (dev-harness.md §3), a Japanese or Chinese book open at running text, upright portrait, Lexirise on, WiFi saved;
    `first` and `second`: two words not looked up before. A card on the first (its phase B's lookup answered, then
    closed: the answer written as it closes); `idle_s` past the TLS idle close, a card on the first again (a hit); then
    a card on the second (a miss). Some TLS session after the first card must have resumed (the log doesn't say for
    which call). Checked by check_cache_log."""
    h.command("AWAKE 1")
    log: list[str] = []

    def card(at: tuple[int, int], done) -> None:
        h.command(f"LONG {at[0]} {at[1]}", seen=log)
        log.extend(collect_until(h, (CARD_OPENED, DEFINITION_OPENED), LEXI_CALL_TIMEOUT_S))
        if DEFINITION_OPENED in log[-1]:
            raise RuntimeError(f"StarDict answered the word at {at}, not Lexirise")
        log.extend(read_for(h, watch_s, done))
        h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
        h.command("HOME", seen=log)
        log.extend(collect_until(h, (CARD_CLOSED,), CARD_CLOSE_WAIT_S + LEXI_CALL_TIMEOUT_S))

    def looked_up(line: str) -> bool:
        m = DECK_CALL_LOG.search(line)
        return bool(m) and (m.group(2), m.group(3)) == CACHE_LOOKUP

    def cache_hit(line: str) -> bool:
        m = CACHE_READ_LOG.search(line)
        return bool(m) and m.group(1) == "hit"

    card(first, looked_up)
    log.extend(read_for(h, idle_s))  # the TLS session closes
    card(first, cache_hit)
    card(second, looked_up)
    r = check_cache_log(log)
    full = f"{r['full_ms']} ms" if r["full_ms"] >= 0 else "not seen"
    print(f"cache-smoke OK: miss read {r['miss_ms']} ms, hit read {r['hit_ms']} ms, written in {r['write_ms']} ms; "
          f"TLS resumed in {r['resumed_ms']} ms (free heap {r['resumed_heap']}), full {full}")
    return r


SETTINGS_ROWS_LOG = re.compile(r"\[LXSET\] rows (\d+)")  # LexiriseSettingsActivity, dev builds
# settings_screen::visibleRows: Lexirise off (the Account group, the two offline dictionaries and the default
# language) .. every row
SETTINGS_ROWS_MIN, SETTINGS_ROWS_MAX = 7, 14


def settings_smoke(h: Harness, outdir: str, shot=None) -> int:
    """The device's Lexirise settings screen (P7): opens it (LEXI SETTINGS), checks it built its rows (the
    count depends on which languages are on), saves a screenshot, and leaves with the Back swipe. Changes no
    setting. Returns the row count."""
    shot = shot or save_shot
    os.makedirs(outdir, exist_ok=True)
    log: list[str] = []
    h.command("LEXI SETTINGS", "LX:OK LEXI", seen=log)
    h.wait_for("Entering activity: LexiriseSettings", ACTIVITY_WAIT_S)
    h.command("SYNC", timeout=SYNC_TIMEOUT_S, seen=log)
    rows = [int(m.group(1)) for line in log if (m := SETTINGS_ROWS_LOG.search(line))]
    if not rows or not SETTINGS_ROWS_MIN <= rows[-1] <= SETTINGS_ROWS_MAX:
        raise RuntimeError(f"the settings screen built {rows[-1] if rows else 'no'} rows")
    shot(h, os.path.join(outdir, "lexirise-settings.png"))
    h.command(f"SWIPE {EDGE_INSET} 400 240 400")  # Back
    h.wait_for("Exiting activity: LexiriseSettings", ACTIVITY_WAIT_S)
    print(f"settings-smoke OK: {rows[-1]} rows, screenshot in {outdir}")
    return rows[-1]


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
    elif sub == "settings":  # Settings → System → Lexirise (P7), over the current screen
        print(h.command("LEXI SETTINGS", "LX:OK LEXI"))
    elif sub == "card":  # the card bench (P4): opens over the current screen; drive it with tap/button/home
        lang = args[1] if len(args) > 1 else "ja"
        low = len(args) > 2 and args[2].lower() == "low"
        print(h.command(f"LEXI CARD {lang}{' LOW' if low else ''}", "LX:OK LEXI"))
    else:
        sys.exit("usage: lexi me | analyze ja|zh | soak [n] [cold] | card ja|zh [low] | settings")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--creates-a-deck", action="store_true",
                    help="deck-smoke only (other commands ignore it): confirms claritise's OK to create a real "
                         "deck and save two words")
    ap.add_argument("cmd")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    if a.cmd.lower() == "deck-smoke" and not a.creates_a_deck:  # before the port: nothing touches the device
        sys.exit("deck-smoke creates a real deck and saves two words: run it only with claritise's OK, "
                 "as lxctl.py --creates-a-deck deck-smoke [x1 y1 x2 y2]")
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
        elif c == "settings-smoke":
            try:
                settings_smoke(h, a.args[0] if a.args else "settings-shots")
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"settings-smoke FAILED: {e}")
        elif c == "card-sentence":
            try:
                card_sentence(h)
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"card-sentence FAILED: {e}")
        elif c == "reader-longpress":
            try:
                reader_longpress(h, tuple(map(int, a.args[:2])) if len(a.args) >= 2 else READER_ON_TEXT)
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"reader-longpress FAILED: {e}")
        elif c == "deck-smoke":
            words = DECK_WORDS
            if len(a.args) >= 4:
                v = list(map(int, a.args[:4]))
                words = ((v[0], v[1]), (v[2], v[3]))
            try:
                deck_smoke(h, words)
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"deck-smoke FAILED: {e}")
        elif c == "ignore-smoke":
            try:
                ignore_smoke(h, tuple(map(int, a.args[:2])) if len(a.args) >= 2 else READER_ON_TEXT)
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"ignore-smoke FAILED: {e}")
        elif c == "vocab-smoke":
            nums = [x for x in a.args if x.lstrip("-").isdigit()]
            try:
                vocab_smoke(h, tuple(map(int, nums[:2])) if len(nums) >= 2 else READER_ON_TEXT,
                            press="press" in a.args)
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"vocab-smoke FAILED: {e}")
        elif c == "home-sync-smoke":
            nums = [x for x in a.args if x.lstrip("-").isdigit()]
            try:
                home_sync_smoke(h, tuple(map(int, nums[:2])) if len(nums) >= 2 else HOME_SYNC_ROW)
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"home-sync-smoke FAILED: {e}")
        elif c == "cache-smoke":
            nums = [int(x) for x in a.args if x.lstrip("-").isdigit()]
            try:
                cache_smoke(h, tuple(nums[:2]) if len(nums) >= 2 else READER_ON_TEXT,
                            tuple(nums[2:4]) if len(nums) >= 4 else CACHE_SECOND_WORD)
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"cache-smoke FAILED: {e}")
        elif c == "page-smoke":
            nums = [x for x in a.args if x.lstrip("-").isdigit()]
            try:
                page_smoke(h, tuple(map(int, nums[:2])) if len(nums) >= 2 else READER_ON_TEXT)
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"page-smoke FAILED: {e}")
        elif c == "card-gestures":
            try:
                card_gestures(h)
            except (RuntimeError, TimeoutError) as e:
                sys.exit(f"card-gestures FAILED: {e}")
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
