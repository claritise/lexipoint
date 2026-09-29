"""Host tests for the Lexipoint dev harness tooling. Run: `python3 -m unittest discover -s scripts/lexipoint`.

- Frame decoding: the portrait PNG rotation matches GfxRenderer::rotateCoordinates.
- Reply handling: log lines and stale replies are skipped, errors raised, frames CRC-checked.
- Build safety: no release environment in platformio.ini ever compiles the dev harness.
"""

import configparser
import json
import os
import re
import struct
import sys
import tempfile
import time
import unittest
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import lxctl  # noqa: E402

W, H = 800, 480  # panel-native


def blank_frame() -> bytearray:
    return bytearray([0xFF] * (W // 8 * H))  # bit 1 = white


def set_black(frame: bytearray, px: int, py: int) -> None:
    frame[py * (W // 8) + (px >> 3)] &= ~(1 << (7 - (px & 7))) & 0xFF


def read_png_gray(path: str):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            width, height = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width + 1
    return width, height, [raw[i * stride + 1 : (i + 1) * stride] for i in range(height)]


class FrameToPortraitPng(unittest.TestCase):
    def render(self, frame: bytearray):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "f.png")
            lxctl.frame_to_portrait_png(W, H, bytes(frame), p)
            return read_png_gray(p)

    def test_size_is_portrait(self):
        w, h, _ = self.render(blank_frame())
        self.assertEqual((w, h), (H, W))  # 480 x 800

    def test_logical_points_map_like_rotate_coordinates(self):
        for lx, ly in [(0, 0), (479, 0), (0, 799), (479, 799), (123, 456)]:
            frame = blank_frame()
            set_black(frame, ly, H - 1 - lx)  # Portrait logical (x, y) lives at panel (y, H - 1 - x)
            _, _, rows = self.render(frame)
            self.assertEqual(rows[ly][lx], 0, f"logical ({lx},{ly}) should be black")
            self.assertEqual(sum(1 for r in rows for v in r if v == 0), 1, "exactly one black pixel")


class FakeSerial:
    """Replays scripted device output; records what the host wrote."""

    def __init__(self, output: bytes):
        self.buf = bytearray(output)
        self.written = b""

    def write(self, data: bytes) -> None:
        self.written += data

    def flush(self) -> None:
        pass

    def read(self, n: int = 1) -> bytes:
        chunk, self.buf = bytes(self.buf[:n]), self.buf[n:]
        return chunk


def shot_bytes(frame: bytes, crc=None, ack=b"LX:OK SHOT\n") -> bytes:
    crc = zlib.crc32(frame) & 0xFFFFFFFF if crc is None else crc
    return f"LX:SHOT {len(frame)} {W} {H} {crc:08x}\n".encode() + frame + b"\n" + ack


class HarnessReplies(unittest.TestCase):
    def test_skips_log_lines_and_stale_replies(self):
        dev = FakeSerial(b"[1] [INF] [MAIN] noise\nLX:OK SHOT\nLX:SELFTEST miss (1,2)\nLX:OK TAP\n")
        h = lxctl.Harness(dev)
        self.assertEqual(h.command("TAP 1 2"), "LX:OK TAP")
        self.assertEqual(dev.written, b"LX:TAP 1 2\n")

    def test_error_raises(self):
        h = lxctl.Harness(FakeSerial(b"LX:ERR off screen\n"))
        with self.assertRaises(RuntimeError):
            h.command("TAP 9999 1")

    def test_timeout(self):
        h = lxctl.Harness(FakeSerial(b"just logs\n"))
        with self.assertRaises(TimeoutError):
            h.command("PING", "LX:PONG", timeout=0.2)

    def test_screenshot_roundtrip_consumes_ack(self):
        frame = bytes(blank_frame())
        dev = FakeSerial(b"log line\n" + shot_bytes(frame) + b"LX:OK HOME\n")
        h = lxctl.Harness(dev)
        w, hh, data = h.screenshot()
        self.assertEqual((w, hh, data), (W, H, frame))
        self.assertEqual(h.command("HOME"), "LX:OK HOME")  # the SHOT ack didn't leak into this reply

    def test_screenshot_crc_mismatch(self):
        frame = bytes(blank_frame())
        h = lxctl.Harness(FakeSerial(shot_bytes(frame, crc=0x12345678)))
        with self.assertRaises(RuntimeError):
            h.screenshot()

    def test_screenshot_short_write_error(self):
        frame = bytes(blank_frame())
        h = lxctl.Harness(FakeSerial(shot_bytes(frame, ack=b"LX:ERR SHOT short write\n")))
        with self.assertRaises(RuntimeError):
            h.screenshot()


class LexiCommands(unittest.TestCase):
    def test_collect_returns_prefixed_lines_until_ok(self):
        dev = FakeSerial(b"LX:LEXI analyze 0 ok parsed=1\n[INF] noise\nLX:LEXI occ 0-1 word=x\nLX:OK LEXI\n")
        h = lxctl.Harness(dev)
        lines = h.collect("LEXI ANALYZE ja", "LX:LEXI", 1.0)
        self.assertEqual(lines, ["LX:LEXI analyze 0 ok parsed=1", "LX:LEXI occ 0-1 word=x"])
        self.assertEqual(dev.written, b"LX:LEXI ANALYZE ja\n")

    def test_lexi_card_sends_the_bench_command(self):
        for args, sent in ((["card", "zh", "low"], b"LX:LEXI CARD zh LOW\n"), (["card"], b"LX:LEXI CARD ja\n")):
            dev = FakeSerial(b"LX:OK LEXI\n")
            lxctl.lexi(lxctl.Harness(dev), args)
            self.assertEqual(dev.written, sent)

    def test_collect_raises_on_error(self):
        h = lxctl.Harness(FakeSerial(b"LX:ERR unknown command\n"))
        with self.assertRaises(RuntimeError):
            h.collect("LEXI ME", "LX:LEXI", 1.0)

    def test_parse_fields(self):
        f = lxctl.parse_fields("LX:LEXI analyze 3 ok status=200 occ=6 parsed=1 heap_free=51000")
        self.assertEqual(f["status"], "200")
        self.assertEqual(f["heap_free"], "51000")

    def test_heap_slope(self):
        self.assertAlmostEqual(lxctl.heap_slope([100, 90, 80, 70]), -10.0)
        self.assertAlmostEqual(lxctl.heap_slope([5, 5, 5]), 0.0)
        self.assertEqual(lxctl.heap_slope([1]), 0.0)

    def test_heap_leaks_on_a_trend_not_noise(self):
        steady = [50000, 50400, 49800, 50200, 49900, 50100, 50000]
        self.assertFalse(lxctl.heap_leaks(steady))
        # A slow leak with one noisy recovery in the middle is still a leak.
        leaking = [50000, 49800, 49600, 49900, 49200, 49000, 48800, 48600]
        self.assertTrue(lxctl.heap_leaks(leaking))
        self.assertFalse(lxctl.heap_leaks([50000, 40000]))  # too few samples to call


def header_constants(path: str) -> dict[str, int]:
    """`constexpr <int type> kName = <integer>;` values from a C++ header (expressions skipped)."""
    with open(os.path.join(REPO, path)) as f:
        text = f.read()
    pattern = r"constexpr\s+(?:unsigned\s+long|u?int\d*_t|int|size_t|long)\s+(k\w+)\s*=\s*([0-9]+)(?:UL|U|L)?\s*;"
    values = {name: int(value) for name, value in re.findall(pattern, text)}
    # Sums and small products of those (kWifiJoinMaxMs = kWifiDirectJoinMs + kWifiConnectMs), in order.
    expr = r"constexpr\s+(?:unsigned\s+long|u?int\d*_t|int|size_t|long)\s+(k\w+)\s*=\s*([\w\s+*]+?)\s*;"
    for name, body in re.findall(expr, text):
        if name in values:
            continue
        total = 0
        for term in body.split("+"):
            product = 1
            for factor in term.split("*"):
                factor = factor.strip()
                if factor.isdigit():
                    product *= int(factor)
                elif factor in values:
                    product *= values[factor]
                else:
                    product = None
                    break
            if product is None:
                total = None
                break
            total += product
        if total is not None:
            values[name] = total
    return values


class HostConstantsMatchTheFirmware(unittest.TestCase):
    def test_lexi_wait_outlasts_the_longest_call(self):
        c = header_constants("src/lexirise/LexiriseConfig.h")
        # config::kMaxCallMs (the WiFi join: a direct attempt then the scan, NTP, two TCP timeouts, the request),
        # as the header computes it.
        self.assertEqual(c["kWifiJoinMaxMs"], c["kWifiDirectJoinMs"] + c["kWifiConnectMs"])
        self.assertGreaterEqual(lxctl.LEXI_CALL_TIMEOUT_S * 1000 - c["kMaxCallMs"], lxctl.LEXI_CALL_MARGIN_MS)

    def test_soak_limit_matches(self):
        c = header_constants("src/lexirise/dev/DevConfig.h")
        self.assertEqual(lxctl.LEXI_SOAK_MAX, c["kLexiSoakMax"])

    def test_card_smoke_waits_out_a_close(self):
        self.assertGreater(lxctl.CARD_CLOSE_WAIT_S, 2 * lxctl.PANEL_FULL_REFRESH_S)

    def test_card_smoke_waits_out_the_phases(self):
        c = header_constants("src/lexirise/LexiriseConfig.h")
        self.assertGreater(lxctl.CARD_PHASES_S * 1000, c["kBenchPhaseBMs"])


def usage_regex(usage: str) -> re.Pattern:
    """A device usage line ("CARD ja|zh [LOW] [KANA]") as a regex over one command."""
    parts = []
    for tok in usage.split():
        optional = tok.startswith("[") and tok.endswith("]")
        tok = tok.strip("[]")
        alt = "|".join(re.escape(a) if not a.islower() or a in ("ja", "zh") else r"-?\d+"
                       for a in tok.split("|"))
        parts.append(f"(?: (?:{alt}))?" if optional else f" (?:{alt})")
    return re.compile("^" + "".join(parts).lstrip() + "$")


def device_usages() -> dict[str, re.Pattern]:
    """The DevProtocol.cpp usage lines, by verb (LEXI split into its sub-verbs)."""
    src = open(os.path.join(REPO, "src/lexirise/dev/DevProtocol.cpp"), encoding="utf-8").read()
    out = {}
    for line in set(re.findall(r'"usage: ([^"]+)"', src)):
        verb, _, rest = line.partition(" ")
        if verb == "LEXI":
            for sub in rest.split(" | "):
                out["LEXI " + sub.split()[0]] = usage_regex("LEXI " + sub)
        elif verb in ("TAP", "LONG", "SWIPE", "BTN", "SYNC", "HOME"):
            out[verb] = usage_regex(line)
    out.setdefault("SYNC", re.compile("^SYNC$"))
    return out


class FakeCardHarness:
    """The device's side of card-smoke: refuses a BTN while the last press is still held (until a SYNC),
    logs the card's word after a step as smoke mode does, and keeps a clock the fake sleep advances."""

    def __init__(self, starts: list[int], direction: int = 1):
        self.sent = []
        self.held = False
        self.starts = list(starts)  # each state's opening word, in the order card-smoke opens them
        self.direction = direction  # -1: the side buttons mapped the other way round
        self.word = 0
        self.clock = 0.0
        self.taps_at = []
        self.shots_at = []

    def command(self, cmd, expect=None, timeout=0, seen=None):
        self.sent.append(cmd)
        if cmd.startswith("LEXI CARD"):
            self.word = self.starts.pop(0)
        elif cmd.startswith("BTN"):
            if self.held:
                raise RuntimeError("LX:ERR button busy")
            self.held = True
            self.word += self.direction * (1 if cmd == "BTN RIGHT" else -1)
        elif cmd.startswith("TAP"):
            self.taps_at.append(self.clock)
        elif cmd == "SYNC":
            if self.held and seen is not None:
                seen.append(f"[123] [INF] [LXCARD] word {self.word}")
            self.held = False
        return "LX:OK"

    def wait_for(self, pattern, timeout):
        return pattern

    def sleep(self, seconds):
        self.clock += seconds

    def shot(self, _h, _path):
        self.shots_at.append(self.clock)


class CardSmoke(unittest.TestCase):
    def test_commands_reach_the_state(self):
        state = {"steps": -2, "taps": [[240, 700], [120, 690]]}
        self.assertEqual(lxctl.card_state_commands(state, "ja", True),
                         ["LEXI CARD ja LOW KANA", "BTN LEFT", "BTN LEFT", "TAP 240 700", "TAP 120 690"])
        self.assertEqual(lxctl.card_state_commands({"steps": 1, "taps": []}, "zh", False),
                         ["LEXI CARD zh KANA", "BTN RIGHT"])

    def goldens(self):
        names = sorted(f for f in os.listdir(lxctl.GOLDEN_DIR) if f.endswith(".json"))
        self.assertTrue(names, "no golden files: python3 scripts/lexipoint/cardgolden.py --update")
        for name in names:
            with open(os.path.join(lxctl.GOLDEN_DIR, name), encoding="utf-8") as f:
                yield name[:-5], json.load(f)

    def test_every_command_matches_the_device_grammar(self):
        usages = device_usages()
        for name, state in self.goldens():
            for cmd in lxctl.card_state_commands(state, name.split("-")[0], "-low" in name) + ["SYNC", "HOME"]:
                key = " ".join(cmd.split()[:2]) if cmd.startswith("LEXI") else cmd.split()[0]
                self.assertIn(key, usages, cmd)
                self.assertRegex(cmd, usages[key], f"{name}: {cmd!r} isn't what DevProtocol.cpp accepts")

    def test_taps_are_on_screen(self):
        for name, state in self.goldens():
            self.assertIsInstance(state["steps"], int, name)
            for x, y in state["taps"]:
                self.assertTrue(0 <= x < 480 and 0 <= y < 800, name)

    def replay(self, direction: int = 1) -> FakeCardHarness:
        states = sorted(g for g in self.goldens() if not g[1].get("extra"))  # card-smoke's order and states
        h = FakeCardHarness([s["word"] - s["steps"] for _, s in states], direction)
        with tempfile.TemporaryDirectory() as out:
            h.done = lxctl.card_smoke(h, out, sleep=h.sleep, shot=h.shot)
        return h

    def test_replay_paces_the_buttons_and_runs_every_state(self):
        h = self.replay()
        self.assertEqual(len(h.done), len([g for g in self.goldens() if not g[1].get("extra")]))
        self.assertEqual(len(h.shots_at), len(h.done))
        self.assertTrue(all("KANA" in c for c in h.sent if c.startswith("LEXI CARD")))  # the setting untouched

    def test_a_toast_is_still_up_for_the_shot(self):
        toast_s = header_constants("src/lexirise/LexiriseConfig.h")["kToastMs"] / 1000
        refresh_s = 0.5  # a partial refresh on the X4 Pro (popup-ui.md §2), which the SYNC waits out
        h = self.replay()
        last_tap = {}
        for t in h.taps_at:
            later = [s for s in h.shots_at if s >= t]
            last_tap[min(later)] = t
        self.assertTrue(last_tap)
        for shot, tap in last_tap.items():
            self.assertLess(shot - tap + refresh_s, toast_s)

    def test_a_step_the_wrong_way_fails_instead_of_shooting_the_wrong_word(self):
        with self.assertRaisesRegex(RuntimeError, "side-button mapping"):
            self.replay(direction=-1)

    def test_replay_is_the_same_in_any_order(self):
        # Each state opens in kana (KANA), so what came before can't leak into it.
        for name, state in self.goldens():
            self.assertTrue(lxctl.card_state_commands(state, name.split("-")[0], "-low" in name)[0].endswith("KANA"))

LEXIRISE_FLAG = re.compile(r"-D\s*LEXIRISE\b")


class FakeGestureHarness:
    """The device's side of card-gestures: the card's view and tab after each swipe, logged at the SYNC as
    smoke mode does. `states`: what the card logs after each swipe that doesn't close it."""

    def __init__(self, states):
        self.states = list(states)
        self.sent = []
        self.closed = False

    def command(self, cmd, expect=None, timeout=0, seen=None):
        previous = self.sent[-1] if self.sent else ""
        self.sent.append(cmd)
        # A long-press on the bench is dropped: the card logs nothing for it.
        if cmd == "SYNC" and seen is not None and not previous.startswith("LONG"):
            view, tab = self.states.pop(0)
            seen.append(f"[123] [INF] [LXCARD] word 2 view {view} tab {tab}")
        return "LX:OK"

    def wait_for(self, pattern, timeout):
        if pattern.startswith("Exiting activity"):
            self.closed = True
        return pattern


class CardGestures(unittest.TestCase):
    def card_box(self, golden):
        with open(os.path.join(lxctl.GOLDEN_DIR, golden + ".json"), encoding="utf-8") as f:
            frame = next(c for c in json.load(f)["card"] if c["kind"] == "frame" and c.get("t") == 3)
        return frame["x"], frame["y"], frame["x"] + frame["w"], frame["y"] + frame["h"]

    def test_every_swipe_matches_the_device_grammar(self):
        usages = device_usages()
        for cmd, _, _ in lxctl.CARD_GESTURES:
            self.assertRegex(cmd, usages[cmd.split()[0]], cmd)

    def test_every_swipe_starts_on_the_card_clear_of_the_edges(self):
        """CardInput.h swipeClearOfEdges, over the SDK's own edge bands (FreeInkUICore.h edgeSwipe)."""
        margin = header_constants("src/lexirise/LexiriseConfig.h")["kCardSwipeEdgeMarginPx"]
        with open(os.path.join(REPO, "freeink-sdk/libs/ui/FreeInkUI/include/FreeInkUICore.h")) as f:
            fracs = dict(re.findall(r"constexpr float (EDGE_SWIPE_\w+) = ([0-9.]+)f;", f.read()))
        w, h = 480, 800
        side, top_bottom = int(w * float(fracs["EDGE_SWIPE_SIDE_FRAC"])), int(h * float(fracs["EDGE_SWIPE_TOP_BOTTOM_FRAC"]))
        view = "card"
        for cmd, after, kind in lxctl.CARD_GESTURES:
            if kind == "long":
                x, y = map(int, cmd.split()[1:3])
                left, top, right, bottom = self.card_box("ja-card-saved" if view == "card" else "ja-expanded-meaning")
                self.assertEqual(view, "card", f"{cmd}: the page is only under the card view")
                self.assertFalse(left <= x < right and top <= y < bottom, f"{cmd}: meant for the page")
                continue
            x, y, ex, ey = map(int, cmd.split()[1:5])
            dx, dy = ex - x, ey - y
            left, top, right, bottom = self.card_box("ja-card-saved" if view == "card" else "ja-expanded-meaning")
            on_card = left <= x < right and top <= y < bottom
            back = x <= side and dx > 0 and abs(dx) > abs(dy)
            if kind == "back":
                self.assertTrue(back, f"{cmd}: meant as Back, but the SDK wouldn't read it so")
            elif kind == "page":
                self.assertFalse(on_card, f"{cmd}: meant to start off the card")
            else:
                self.assertTrue(on_card, f"{cmd}: off the {view} view's card")
                self.assertTrue(x >= margin and margin <= y < h - margin, f"{cmd}: inside an edge gesture's margin")
                self.assertFalse(back, f"{cmd}: the SDK reads it as Back")
                self.assertFalse(y <= top_bottom and dy > 0 and abs(dy) > abs(dx), f"{cmd}: a top-edge gesture")
                self.assertFalse(y >= h - top_bottom and dy < 0 and abs(dy) > abs(dx), f"{cmd}: a bottom-edge gesture")
            view = after[0] if after else view

    def test_a_long_press_that_closes_the_card_fails(self):
        class Closing(FakeGestureHarness):
            def command(self, cmd, expect=None, timeout=0, seen=None):
                if cmd.startswith("LONG"):
                    self.long_pressed = True
                elif cmd == "SYNC" and getattr(self, "long_pressed", False) and seen is not None:
                    seen.append("[1] [INF] [ACT] Exiting activity: LexiriseCard")
                    self.long_pressed = False
                    return "LX:OK"
                return super().command(cmd, expect, timeout, seen)

        h = Closing([s for _, s, k in lxctl.CARD_GESTURES if s and k != "long"])
        with self.assertRaises(RuntimeError):
            lxctl.card_gestures(h, sleep=lambda _s: None)

    def test_replay_checks_each_state_and_ends_closed(self):
        h = FakeGestureHarness([s for _, s, k in lxctl.CARD_GESTURES if s and k != "long"])
        lxctl.card_gestures(h, sleep=lambda _s: None)
        self.assertTrue(h.closed)
        self.assertEqual([c for c in h.sent if c.startswith(("SWIPE", "LONG"))], [c for c, _, _ in lxctl.CARD_GESTURES])

    def test_a_swipe_that_lands_elsewhere_fails(self):
        states = [s for _, s, k in lxctl.CARD_GESTURES if s and k != "long"]
        states[1] = ("expanded", 0)  # the left swipe didn't change the tab
        with self.assertRaises(RuntimeError):
            lxctl.card_gestures(FakeGestureHarness(states), sleep=lambda _s: None)


class FakeSentenceHarness:
    """The bench card's side of card-sentence: `n` words from `start`; past the last it stays until the next
    SYNC after a sleep, then jumps to n (the "next sentence", n words again), and stops at 2n - 1."""

    def __init__(self, n=6, start=2, jumps=True):
        self.n, self.word, self.jumps = n, start, jumps
        self.waiting = self.slept = self.closed = False
        self.sent = []

    def command(self, cmd, expect=None, timeout=0, seen=None):
        self.sent.append(cmd)
        if cmd == "BTN RIGHT":
            if self.word == self.n - 1 and self.jumps:
                self.waiting = True
            elif self.word < 2 * self.n - 1 and self.word != self.n - 1:
                self.word += 1
        elif cmd == "SYNC" and seen is not None:
            if self.waiting and self.slept:
                self.waiting = False
                self.word = self.n
            seen.append(f"[1] [INF] [LXCARD] word {self.word} view card tab 0")
        return "LX:OK"

    def wait_for(self, pattern, timeout):
        self.closed = self.closed or pattern.startswith("Exiting")
        return pattern

    def sleep(self, _s):
        self.slept = self.waiting


class FakeReaderHarness:
    """The reader's side of reader-longpress: each LONG logs its use from `use` (x, y → taken | ignored | left),
    and a taken one (or one `opens` says) enters word select, which the `backs_needed`-th Back swipe closes."""

    def __init__(self, use, opens=None, backs_needed=1, logs=True, lift=None, retap="card", answer="late"):
        self.use, self.opens, self.backs_needed, self.logs = use, opens, backs_needed, logs
        self.lift = lift  # (x, y) → a log line the press's lift caused (a page turn, the menu), or None
        self.retap = retap  # what a tap above an open card does: "card" (another card), "reader", None (nothing)
        self.stream: list[str] = []  # lines read_line hands out
        # How the looked-up word's answer arrives: "sync" (in the long-press's own SYNC log, as on the device),
        # "late" (after it), or "stardict" (StarDict's definition, no card).
        self.answer = answer
        self.pending: list[str] = []
        self.backs = 0
        self.open = False
        self.sent: list[str] = []

    def command(self, cmd, expect=None, timeout=0, seen=None):
        self.sent.append(cmd)
        if cmd.startswith("LONG "):
            p = tuple(map(int, cmd.split()[1:]))
            use = self.use(p)
            if self.logs:
                self.pending.append(f"[1] [DBG] [LXLP] long-press {p[0]} {p[1]} {use}")
            if self.opens(p) if self.opens else use == "taken":
                self.open = True
                self.pending.append("[1] [INF] [ACT] Entering activity: DictionaryWordSelect")
                answer = {"stardict": "[1] [DBG] [ACT] Entering activity: DictionaryDefinition"}.get(
                    self.answer, "[1] [DBG] [ACT] Entering activity: LexiriseCard")
                (self.pending if self.answer in ("sync", "stardict") else self.stream).append(answer)
            if self.lift and (line := self.lift(p)):
                self.pending.append(line)
        elif cmd == "SYNC" and seen is not None:
            seen.extend(self.pending)
            self.pending = []
        elif cmd.startswith("SWIPE ") and self.open:
            self.backs += 1
        elif cmd.startswith("TAP ") and self.open and self.retap and self.answer != "stardict":
            self.stream.append("[3] [DBG] [ACT] Exiting activity: LexiriseCard")
            if self.retap == "card":
                self.stream.append("[3] [DBG] [ACT] Entering activity: LexiriseCard")
            else:
                self.stream.append("[3] [DBG] [ACT] Exiting activity: DictionaryWordSelect")
                self.open = False
        return "LX:OK"

    def read_line(self, deadline):
        return self.stream.pop(0) if self.stream else None

    def wait_for(self, pattern, timeout):
        if pattern.startswith("Exiting activity: DictionaryWordSelect") and self.backs >= self.backs_needed:
            self.open = False
            return pattern
        raise TimeoutError(pattern)


def uses(word="taken", margin="ignored", side="ignored", at=None):
    """A reader whose word point is `at` (default the page's middle)."""
    word_at = at or lxctl.READER_ON_TEXT
    return lambda p: word if p == word_at else (side if p == lxctl.READER_SIDE_OFF_TEXT else margin)


class ReaderLongPress(unittest.TestCase):
    def test_the_margin_is_ignored_and_a_word_opens_word_select(self):
        h = FakeReaderHarness(uses())
        self.assertEqual(lxctl.reader_longpress(h),
                         {"margin": "ignored", "side": "ignored", "word": "taken", "retap": "card"})
        self.assertFalse(h.open)
        tap = h.sent.index(f"TAP {lxctl.READER_RETAP[0]} {lxctl.READER_RETAP[1]}")
        first_back = next(i for i, c in enumerate(h.sent) if c.startswith("SWIPE"))
        self.assertIn("SYNC", h.sent[tap:first_back])  # the new card's lookup waited out before any Back

    def test_a_tap_above_the_card_with_no_word_goes_back_to_the_reader(self):
        h = FakeReaderHarness(uses(), retap="reader")
        self.assertEqual(lxctl.reader_longpress(h)["retap"], "reader")
        self.assertFalse(any(c.startswith("SWIPE") for c in h.sent))  # nothing left to close

    def test_the_card_already_in_the_long_press_log_is_not_waited_for(self):
        # On the device the card opens on word select's first loop, before the SYNC reply.
        h = FakeReaderHarness(uses(), answer="sync")
        self.assertEqual(lxctl.reader_longpress(h)["retap"], "card")

    def test_stardict_answering_skips_the_tap(self):
        h = FakeReaderHarness(uses(), answer="stardict")
        self.assertEqual(lxctl.reader_longpress(h)["retap"], "none")
        self.assertFalse(any(c.startswith("TAP") for c in h.sent))

    def test_a_tap_above_the_card_that_does_nothing_fails(self):
        with self.assertRaises(TimeoutError):
            lxctl.reader_longpress(FakeReaderHarness(uses(), retap=None))

    def test_the_side_left_to_crosspoints_hold_action_is_not_checked(self):
        h = FakeReaderHarness(uses(side="left"))
        self.assertEqual(lxctl.reader_longpress(h)["side"], "left")

    def test_an_ignored_press_whose_lift_turned_the_page_fails(self):
        # P10: "ignored" must mean nothing happened; a lift that wasn't consumed turns the page back.
        turned = lambda p: "[2] [DBG] [ERS] Rendered page in 700ms" if p == lxctl.READER_SIDE_OFF_TEXT else None
        with self.assertRaisesRegex(RuntimeError, r"at \(3, 400\) still did something: .*Rendered page"):
            lxctl.reader_longpress(FakeReaderHarness(uses(), lift=turned))

    def test_an_ignored_press_whose_lift_opened_the_menu_fails(self):
        menu = lambda p: "[2] [DBG] [ACT] Entering activity: EpubReaderMenu" if p == lxctl.READER_OFF_TEXT else None
        with self.assertRaisesRegex(RuntimeError, "still did something: .*EpubReaderMenu"):
            lxctl.reader_longpress(FakeReaderHarness(uses(), lift=menu))

    def test_a_margin_left_to_the_reader_fails(self):
        # P10: off the text a long-press does nothing; "left" means its lift would tap the page.
        with self.assertRaisesRegex(RuntimeError, "bottom margin was left, not ignored.*dictionary or a Lexirise key"):
            lxctl.reader_longpress(FakeReaderHarness(uses(margin="left")))

    def test_the_margin_taken_fails(self):
        with self.assertRaisesRegex(RuntimeError, "was taken, not ignored"):
            lxctl.reader_longpress(FakeReaderHarness(uses(margin="taken")))

    def test_a_word_press_not_taken_fails(self):
        with self.assertRaisesRegex(RuntimeError, r"at \(240, 400\) wasn't taken"):
            lxctl.reader_longpress(FakeReaderHarness(uses(word="ignored")))

    def test_the_word_can_be_given(self):
        h = FakeReaderHarness(uses(at=(100, 200)))
        self.assertEqual(lxctl.reader_longpress(h, (100, 200))["word"], "taken")
        self.assertIn("LONG 100 200", h.sent)

    def test_word_select_without_the_press_being_taken_fails(self):
        # The P9 bug: word select opened with no word under the finger.
        h = FakeReaderHarness(uses(word="ignored"), opens=lambda p: p == lxctl.READER_ON_TEXT)
        with self.assertRaisesRegex(RuntimeError, "still did something: .*DictionaryWordSelect"):
            lxctl.reader_longpress(h)

    def test_no_decision_logged_means_no_book_open(self):
        with self.assertRaisesRegex(RuntimeError, "is a book open"):
            lxctl.reader_longpress(FakeReaderHarness(uses(), logs=False))

    def test_word_select_is_closed_with_up_to_three_backs(self):
        h = FakeReaderHarness(uses(), backs_needed=3)
        lxctl.reader_longpress(h)
        self.assertFalse(h.open)
        with self.assertRaisesRegex(RuntimeError, "didn't close"):
            lxctl.reader_longpress(FakeReaderHarness(uses(), backs_needed=4))

    def test_the_page_drawn_line_matches_the_firmware(self):
        src = open(os.path.join(REPO, "src", "activities", "reader", "EpubReaderActivity.cpp")).read()
        self.assertIn('LOG_DBG("ERS", "Rendered page in %dms"', src)

    def test_the_log_matches_the_firmware(self):
        src = open(os.path.join(REPO, "src", "activities", "reader", "EpubReaderActivity.cpp")).read()
        self.assertIn('LOG_DBG("LXLP", "long-press %d %d %s", pressX, pressY, lexipoint::lookup::longPressUseName(use))',
                      src)
        names = open(os.path.join(REPO, "src", "lexirise", "lookup", "LongPress.h")).read()
        for name in ("left", "taken", "ignored"):
            self.assertIn(f'return "{name}";', names)
            self.assertTrue(lxctl.READER_LONGPRESS_LOG.search(f"[9] [DBG] [LXLP] long-press 240 796 {name}"))


class CardSentence(unittest.TestCase):
    def test_it_goes_on_into_the_next_sentence_and_stops_at_the_end(self):
        h = FakeSentenceHarness()
        words = lxctl.card_sentence(h, sleep=h.sleep)
        self.assertEqual(words, [3, 4, 5, 6, 7, 8, 9, 10, 11])
        self.assertTrue(h.closed)

    def test_a_jump_read_with_the_press_counts(self):
        # The last word's refresh held the SYNC past the jump: [n-1, n] arrive in one read.
        class Late(FakeSentenceHarness):
            def command(self, cmd, expect=None, timeout=0, seen=None):
                if cmd == "SYNC" and seen is not None and self.waiting:
                    self.waiting = False
                    seen.append(f"[1] [INF] [LXCARD] word {self.word} view card tab 0")
                    self.word = self.n
                    seen.append(f"[1] [INF] [LXCARD] word {self.word} view card tab 0")
                    self.sent.append(cmd)
                    return "LX:OK"
                return super().command(cmd, expect, timeout, seen)

        h = Late()
        self.assertEqual(lxctl.card_sentence(h, sleep=h.sleep), [3, 4, 5, 6, 7, 8, 9, 10, 11])

    def test_a_card_that_never_goes_on_fails(self):
        h = FakeSentenceHarness(jumps=False)
        with self.assertRaises(RuntimeError):
            lxctl.card_sentence(h, sleep=h.sleep)

    def test_the_wait_outlasts_the_benchs_analysis(self):
        c = header_constants("src/lexirise/LexiriseConfig.h")
        for name in ("kBenchPhaseBMs", "kBenchNextSentenceMs"):  # card-smoke's wait, and card-sentence's
            self.assertGreater(lxctl.CARD_PHASES_S * 1000, c[name] * 1.2, name)  # with some margin


class SettingsSmoke(unittest.TestCase):
    class Fake:
        def __init__(self, rows):
            self.rows, self.sent, self.shots, self.left = rows, [], 0, False

        def command(self, cmd, expect=None, timeout=0, seen=None):
            self.sent.append(cmd)
            if cmd == "SYNC" and seen is not None and self.rows is not None:
                seen.append(f"[1] [INF] [LXSET] rows {self.rows}")
            return "LX:OK LEXI" if cmd.startswith("LEXI") else "LX:OK"

        def wait_for(self, pattern, timeout):
            self.left = self.left or pattern.startswith("Exiting")
            return pattern

    def run_smoke(self, rows):
        h = self.Fake(rows)
        with tempfile.TemporaryDirectory() as out:
            n = lxctl.settings_smoke(h, out, shot=lambda _h, _p: None)
        return h, n

    def test_opens_checks_the_rows_and_leaves_with_back(self):
        h, n = self.run_smoke(14)
        self.assertEqual(n, 14)
        self.assertTrue(h.left)
        self.assertEqual(h.sent[0], "LEXI SETTINGS")
        self.assertTrue(any(c.startswith("SWIPE") for c in h.sent))

    def test_no_rows_or_too_many_fail(self):
        for rows in (None, 3, lxctl.SETTINGS_ROWS_MAX + 1):
            with self.assertRaises(RuntimeError):
                self.run_smoke(rows)

    def test_commands_match_the_device_grammar(self):
        usages = device_usages()
        self.assertRegex("LEXI SETTINGS", usages["LEXI SETTINGS"])
        self.assertRegex(f"SWIPE {lxctl.EDGE_INSET} 400 240 400", usages["SWIPE"])

    def test_row_bounds_match_the_screen(self):
        """SETTINGS_ROWS_MAX is every settings_screen::Row; MIN Lexirise off (visibleRows: the Account group, the
        two offline dictionaries and the default language)."""
        with open(os.path.join(REPO, "src/lexirise/settings/SettingsScreen.h"), encoding="utf-8") as f:
            body = re.search(r"enum class Row : uint8_t \{(.*?)\};", f.read(), re.S).group(1)
        rows = re.findall(r"^\s*(\w+),", body, re.M)
        self.assertEqual(lxctl.SETTINGS_ROWS_MAX, len(rows))
        account = rows.index("TestConnection") + 1  # the Account group: Lookups .. TestConnection
        self.assertEqual(rows[:account], ["Lookups", "ApiKey", "Account", "TestConnection"])
        self.assertEqual(lxctl.SETTINGS_ROWS_MIN, account + len(["JaDictionary", "ZhDictionary", "DefaultLanguage"]))


class ReleaseVersioning(unittest.TestCase):
    """firmware-base.md §6: Lexipoint's releases are <base>-lexi.<n>, the X4 Pro only."""

    def ini(self):
        cp = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";",))
        cp.read(os.path.join(REPO, "platformio.ini"))
        return cp

    def test_lexirise_envs_carry_the_lexi_version(self):
        cp = self.ini()
        self.assertTrue(cp["lexirise"]["release"].isdigit())
        for env, suffix in (("x4pro", "-x4pro"), ("x4pro-gh_release", ""),
                            ("x4pro-gh_release_rc", "-rc+${sysenv.CROSSPOINT_RC_HASH}")):
            flags = cp[f"env:{env}"]["build_flags"]
            self.assertIn('-DCROSSPOINT_VERSION=\\"${crosspoint.version}-lexi.${lexirise.release}' + suffix + '\\"',
                          flags, env)

    def test_ota_reads_lexipoints_releases(self):
        with open(os.path.join(REPO, "src/lexirise/LexiriseConfig.h"), encoding="utf-8") as f:
            self.assertIn("api.github.com/repos/claritise/lexipoint/releases/latest", f.read())


class LexiriseIsAlwaysBuilt(unittest.TestCase):
    """v0.2 V8 removed the LEXIRISE gate and the Lexirise-off build (slimming.md §2): every X4 Pro env carries
    [lexirise]'s flags (the TLS suites, session resumption, the product version) and nothing defines LEXIRISE."""

    def test_every_x4pro_env_has_the_lexirise_flags(self):
        with open(os.path.join(REPO, "platformio.ini")) as f:
            cfg = load_ini(f.read())
        x4pro = [s for s in cfg.sections() if s.startswith("env:x4pro")]
        self.assertEqual(sorted(x4pro), ["env:x4pro", "env:x4pro-gh_release", "env:x4pro-gh_release_rc"])
        lexirise = {line.split(";")[0].strip() for line in _expand(cfg.get("lexirise", "build_flags"), cfg).splitlines()}
        lexirise.discard("")
        self.assertTrue(lexirise)
        for env in x4pro:
            flags = resolved_build_flags(env, cfg)
            self.assertNotRegex(flags, LEXIRISE_FLAG, env)
            for flag in lexirise:
                self.assertIn(flag, flags, env)

    def test_no_source_is_gated_on_lexirise(self):
        gate = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b.*\bLEXIRISE\b", re.M)
        for top in ("src", "lib", "test"):
            for root, dirs, files in os.walk(os.path.join(REPO, top)):
                dirs[:] = [d for d in dirs if d not in ("_deps", "build")]
                for name in files:
                    if not name.endswith((".cpp", ".h", ".c", ".txt")):
                        continue
                    path = os.path.join(root, name)
                    with open(path, encoding="utf-8", errors="replace") as f:
                        self.assertIsNone(gate.search(f.read()), os.path.relpath(path, REPO))


HARNESS_FLAG = re.compile(r"-D\s*LEXIPOINT_DEV_HARNESS\b")
FLAG_KEYS = ("build_flags", "build_src_flags")


def _expand(value: str, cfg: configparser.ConfigParser) -> str:
    for _ in range(10):  # ${section.key} references, nested
        new = re.sub(r"\$\{([^}.]+)\.([^}]+)\}", lambda m: cfg.get(m.group(1), m.group(2), fallback=""), value)
        if new == value:
            break
        value = new
    return value


def resolved_build_flags(section: str, cfg: configparser.ConfigParser, seen=None) -> str:
    """All build flags a platformio section ends up with: its own build_flags and build_src_flags, plus
    everything inherited through `extends` (comma-separated, env: or plain sections), recursively."""
    seen = set() if seen is None else seen
    if section in seen or not cfg.has_section(section):
        return ""
    seen.add(section)
    parts = [_expand(cfg.get(section, k, fallback=""), cfg) for k in FLAG_KEYS]
    for parent in cfg.get(section, "extends", fallback="").split(","):
        parent = parent.strip()
        if parent:
            parts.append(resolved_build_flags(parent, cfg, seen))
    return "\n".join(parts)


def harness_envs(cfg: configparser.ConfigParser) -> set[str]:
    return {s[4:] for s in cfg.sections() if s.startswith("env:") and HARNESS_FLAG.search(resolved_build_flags(s, cfg))}


def load_ini(text: str) -> configparser.ConfigParser:
    cfg = configparser.ConfigParser(interpolation=None, strict=False)
    cfg.read_string(text)
    return cfg


class ReleaseEnvsExcludeHarness(unittest.TestCase):
    def test_platformio_ini(self):
        with open(os.path.join(REPO, "platformio.ini")) as f:
            cfg = load_ini(f.read())
        envs = {s[4:] for s in cfg.sections() if s.startswith("env:")}
        with_harness = harness_envs(cfg)
        self.assertIn("x4pro", with_harness)
        release = {e for e in envs if "release" in e}
        self.assertTrue(release)
        self.assertFalse(release & with_harness, f"release envs compile the dev harness: {release & with_harness}")

    def test_guard_follows_extends_and_src_flags(self):
        cfg = load_ini(
            "[base]\nbuild_flags = -DFOO\n"
            "[env:x4pro]\nextends = base\nbuild_flags = ${base.build_flags} -D LEXIPOINT_DEV_HARNESS=1\n"
            "[env:sneaky-release]\nextends = env:x4pro\n"
            "[env:srcflag-release]\nextends = base\nbuild_src_flags = -DLEXIPOINT_DEV_HARNESS\n"
            "[env:clean-release]\nextends = base\n"
        )
        self.assertEqual(harness_envs(cfg), {"x4pro", "sneaky-release", "srcflag-release"})

    def test_built_release_binaries_if_present(self):
        for path in sorted(__import__("glob").glob(os.path.join(REPO, ".pio/build/*release*/firmware.bin"))):
            with open(path, "rb") as f:
                self.assertNotIn(b"LX:PONG", f.read(), path)


def deck_line(ms: int, method: str, path: str, status: int) -> str:
    return f"[{ms}] [INF] [LXS] {method} {path} -> {status} (ok)"


def step_line(ms: int, kind: str, key: str = "ja:kokoro") -> str:
    return f"[{ms}] [INF] [LXDECK] step {kind} {key}"


def recorded_line(ms: int, key: str = "ja:kokoro") -> str:
    return f"[{ms}] [INF] [LXDECK] Deck {key}: recorded"


SAVE = deck_line(1000, "POST", "/v1/vocabulary", 200)
CLOSED = ["[9000] [INF] [ACT] Exiting activity: LexiriseCard"]


def new_book(list_at=4000, list_took=500, create_at=None):
    """A new book's card: the save, the list (idle 3 s after the save), the creation (idle 3 s after the list's
    answer), then "recorded"."""
    listed = list_at + list_took
    create_at = create_at if create_at is not None else listed + 3000
    return [SAVE, step_line(list_at, "list"), deck_line(listed, "GET", "/v1/decks?language=ja", 200),
            step_line(create_at, "create"), deck_line(create_at + 400, "POST", "/v1/decks", 200),
            recorded_line(create_at + 401)]


def checked_card(at=4100):
    return [SAVE, step_line(at, "check"), deck_line(at + 300, "GET", "/v1/decks/{id}", 200)]


class DeckSmokeRules(unittest.TestCase):
    """deck-smoke's rules (V3), on synthetic logs: never run against a device here."""

    def test_a_new_book_lists_creates_and_records_then_the_next_card_calls_nothing(self):
        self.assertEqual(lxctl.check_deck_cards([(new_book(), CLOSED), ([SAVE], CLOSED)]), ["created", "none"])

    def test_a_book_recorded_before_is_checked_once(self):
        self.assertEqual(lxctl.check_deck_cards([(checked_card(), CLOSED), ([SAVE], CLOSED)]), ["checked", "none"])

    def test_a_deck_from_another_device_is_found(self):
        found = [SAVE, step_line(4000, "list"), deck_line(4300, "GET", "/v1/decks?language=ja", 200),
                 recorded_line(4301)]
        self.assertEqual(lxctl.check_deck_cards([(found, CLOSED)]), ["found"])

    def test_a_deleted_deck_is_listed_and_made_again(self):
        gone = [SAVE, step_line(4000, "check"), deck_line(4200, "GET", "/v1/decks/{id}", 404),
                step_line(7300, "list"), deck_line(7600, "GET", "/v1/decks?language=ja", 200),
                step_line(10700, "create"), deck_line(11000, "POST", "/v1/decks", 200), recorded_line(11001)]
        self.assertEqual(lxctl.check_deck_cards([(gone, CLOSED)]), ["created"])

    def test_a_step_before_the_idle_time_fails(self):
        with self.assertRaisesRegex(RuntimeError, "list step started 1500 ms"):
            lxctl.check_deck_cards([(new_book(list_at=2500), CLOSED)])

    def test_a_slow_call_is_timed_from_its_start(self):
        # The list started on time and took 5 s: its answer line comes late, and that's fine.
        self.assertEqual(lxctl.check_deck_cards([(new_book(list_took=5000), CLOSED)]), ["created"])

    def test_a_creation_too_soon_after_the_list_fails(self):
        with self.assertRaisesRegex(RuntimeError, "create step started 1000 ms"):
            lxctl.check_deck_cards([(new_book(create_at=5500), CLOSED)])

    def test_a_deck_call_while_closing_fails(self):
        closing = [step_line(9000, "list"), deck_line(9100, "GET", "/v1/decks?language=ja", 200)] + CLOSED
        with self.assertRaisesRegex(RuntimeError, "while closing"):
            lxctl.check_deck_cards([(new_book(), closing)])

    def test_a_card_after_the_deck_settled_calling_again_fails(self):
        with self.assertRaisesRegex(RuntimeError, "after the deck was settled"):
            lxctl.check_deck_cards([(new_book(), CLOSED), (checked_card(), CLOSED)])

    def test_two_creations_or_checks_in_one_card_fail(self):
        twice = new_book() + [step_line(12000, "create"), deck_line(12300, "POST", "/v1/decks", 200)]
        with self.assertRaisesRegex(RuntimeError, "second deck creation"):
            lxctl.check_deck_cards([(twice, CLOSED)])
        checked_twice = checked_card() + [step_line(8000, "check"), deck_line(8300, "GET", "/v1/decks/{id}", 200)]
        with self.assertRaisesRegex(RuntimeError, "checked twice"):
            lxctl.check_deck_cards([(checked_twice, CLOSED)])

    def test_steps_out_of_order_fail(self):
        backwards = [SAVE, step_line(4000, "create"), deck_line(4300, "POST", "/v1/decks", 200),
                     step_line(7400, "list"), deck_line(7700, "GET", "/v1/decks?language=ja", 200)]
        with self.assertRaisesRegex(RuntimeError, "list after create"):
            lxctl.check_deck_cards([(backwards, CLOSED)])

    def test_a_creation_not_recorded_fails(self):
        with self.assertRaisesRegex(RuntimeError, "recorded none"):
            lxctl.check_deck_cards([(new_book()[:-1], CLOSED)])

    def test_no_save_or_no_deck_call_fails(self):
        with self.assertRaisesRegex(RuntimeError, "saved no new word"):
            lxctl.check_deck_cards([([deck_line(1000, "PATCH", "/v1/vocabulary/{id}", 200)], CLOSED)])
        with self.assertRaisesRegex(RuntimeError, "waited 12 s"):
            lxctl.check_deck_cards([([SAVE], CLOSED)], watch_s=12)

    def test_steps_for_two_book_decks_fail(self):
        zh = [SAVE, step_line(4000, "list", "zh:kokoro"), deck_line(4300, "GET", "/v1/decks?language=zh", 200),
              recorded_line(4301, "zh:kokoro")]
        with self.assertRaisesRegex(RuntimeError, "more than one book deck"):
            lxctl.check_deck_cards([(new_book(), CLOSED), (zh, CLOSED)])

    def test_a_step_exactly_at_the_idle_time_passes(self):
        self.assertEqual(lxctl.check_deck_cards([(new_book(list_at=4000), CLOSED)]), ["created"])
        with self.assertRaisesRegex(RuntimeError, "list step started 2999 ms"):
            lxctl.check_deck_cards([(new_book(list_at=3999), CLOSED)])

    def test_level_buttons_are_the_last_set_logged(self):
        log = ["[1] [INF] [LXCARD] targets 2", "[1] [INF] [LXCARD] level 0 273 570 42 37 0",
               "[1] [INF] [LXCARD] level 1 316 570 42 37 0",
               "[2] [INF] [LXCARD] targets 3", "[2] [INF] [LXCARD] level 0 273 546 42 37 1",
               "[2] [INF] [LXCARD] level 1 316 546 42 37 1", "[2] [INF] [LXCARD] target rank 0 17 700 446 50",
               "[3] [INF] [LXCARD] targets 2", "[3] [INF] [LXCARD] level 0 1 1 1 1 0"]  # cut short: not a set
        self.assertEqual(lxctl.level_buttons(log), {0: (273, 546, 42, 37, True), 1: (316, 546, 42, 37, True)})
        self.assertEqual(lxctl.level_buttons(["[1] [INF] [LXS] GET /v1/me -> 200 (ok)"]), {})

    def test_the_constants_and_log_lines_match_the_firmware(self):
        c = header_constants("src/lexirise/LexiriseConfig.h")
        self.assertEqual(lxctl.DECK_IDLE_MS, c["kDeckIdleMs"])
        model = open(os.path.join(REPO, "src/lexirise/card/CardModel.h"), encoding="utf-8").read()
        levels = re.search(r"enum class Level : \w+ \{([^}]*)\}", model).group(1)
        self.assertIn(f"Learning = {lxctl.DECK_SAVE_LEVEL}", levels)  # L, the card's level button index
        # The watch outlasts the Undo window and two idle windows, with time for three calls.
        self.assertGreater(lxctl.DECK_WATCH_S * 1000, c["kToastMs"] + 2 * c["kDeckIdleMs"] + 3000)
        usages = device_usages()
        for cmd in [f"LONG {x} {y}" for x, y in lxctl.DECK_WORDS] + ["TAP 337 588", "HOME"]:
            self.assertRegex(cmd, usages[cmd.split()[0]])
        service = open(os.path.join(REPO, "src/lexirise/LexiriseService.cpp"), encoding="utf-8").read()
        self.assertIn('"%s %s -> %d (%s)"', service)
        deck = open(os.path.join(REPO, "src/lexirise/deck/BookDeck.cpp"), encoding="utf-8").read()
        self.assertIn('"Deck %s: %s"', deck)
        self.assertIn('"step %s %s"', deck)
        for kind in ("check", "list", "create"):
            self.assertIn(f'return "{kind}";', deck)
        card = open(os.path.join(REPO, "src/lexirise/card/LexiriseCardActivity.cpp"), encoding="utf-8").read()
        self.assertIn('"level %d %d %d %d %d %d"', card)
        self.assertRegex(card, r"#if LEXIPOINT_DEV_HARNESS\s+logTapTargets")  # dev builds only

    def test_it_refuses_to_run_without_the_flag(self):
        argv = sys.argv
        sys.argv = ["lxctl.py", "--port", "/dev/null-none", "deck-smoke"]
        opened = []
        real = lxctl.open_serial
        lxctl.open_serial = lambda port: opened.append(port)
        try:
            with self.assertRaises(SystemExit) as e:
                lxctl.main()
            self.assertIn("claritise's OK", str(e.exception))
            self.assertEqual(opened, [])  # the port is never opened
        finally:
            lxctl.open_serial = real
            sys.argv = argv


class FakeDeckHarness:
    """The device's side of deck-smoke: a LONG opens a card (or StarDict's definition), its SYNC logs the level
    buttons, the TAP on L logs `after_tap` (the save and the deck's lines), HOME logs the close."""

    def __init__(self, after_tap, levels=True, saved=0, stardict=False, closing=None):
        self.after_tap, self.levels, self.saved, self.stardict = after_tap, levels, saved, stardict
        self.closing = closing or []
        self.stream: list[str] = []
        self.sent: list[str] = []

    def command(self, cmd, expect=None, timeout=0, seen=None):
        self.sent.append(cmd)
        if cmd.startswith("LONG "):
            self.stream.append("[1] [DBG] [ACT] Entering activity: "
                               + ("DictionaryDefinition" if self.stardict else "LexiriseCard"))
        elif cmd == "SYNC" and seen is not None and self.levels:
            seen += ["[2] [INF] [LXCARD] targets 4"]
            seen += [f"[2] [INF] [LXCARD] level {i} {273 + 43 * i} 546 42 37 {self.saved}" for i in range(4)]
        elif cmd.startswith("TAP "):
            self.stream += self.after_tap
        elif cmd == "HOME" and seen is not None:
            seen += self.closing
            self.stream.append("[9] [DBG] [ACT] Exiting activity: LexiriseCard")
        return "LX:OK"

    def read_line(self, deadline):
        return self.stream.pop(0) if self.stream else None


class DeckSmokeDriver(unittest.TestCase):
    def test_a_card_taps_l_where_the_card_drew_it_and_stops_once_recorded(self):
        h = FakeDeckHarness(new_book() + ["[20000] [INF] [LXS] GET /v1/me -> 200 (ok)"])
        opened, closing = lxctl.deck_card(h, (240, 150), watch_s=0.01)
        self.assertEqual([c.split()[0] for c in h.sent], ["LONG", "SYNC", "TAP", "HOME"])
        self.assertEqual(h.sent[2], f"TAP {273 + 43 + 21} {546 + 18}")  # L's centre, as logged
        self.assertTrue(lxctl.DECK_RECORDED_LOG.search(opened[-1]))  # stopped on "recorded"
        self.assertTrue(any("Exiting activity: LexiriseCard" in line for line in closing))  # the close's log
        self.assertTrue(any("GET /v1/me" in line for line in closing))  # what came after the stop, with the close
        self.assertFalse(any("GET /v1/me" in line for line in opened))

    def test_a_card_stops_on_a_found_check(self):
        h = FakeDeckHarness(checked_card() + ["[20000] [INF] [LXS] GET /v1/me -> 200 (ok)"])
        opened, _ = lxctl.deck_card(h, (240, 150), watch_s=0.01)
        self.assertIn("GET /v1/decks/{id} -> 200", opened[-1])

    def test_stardict_no_levels_or_a_saved_word_raise(self):
        for harness, message in ((FakeDeckHarness([], stardict=True), "StarDict"),
                                 (FakeDeckHarness([], levels=False), "no level buttons"),
                                 (FakeDeckHarness([], saved=1), "saved already")):
            with self.assertRaisesRegex(RuntimeError, message):
                lxctl.deck_card(harness, (240, 150), watch_s=0.01)

    def test_the_smoke_runs_both_words_and_checks_them(self):
        class TwoCards(FakeDeckHarness):
            def __init__(self):
                super().__init__(new_book())
                self.cards = 0

            def command(self, cmd, expect=None, timeout=0, seen=None):
                if cmd.startswith("TAP "):
                    self.cards += 1
                    self.after_tap = new_book() if self.cards == 1 else [SAVE]
                return super().command(cmd, expect, timeout, seen)

        h = TwoCards()
        self.assertEqual(lxctl.deck_smoke(h, watch_s=0.01), ["created", "none"])
        self.assertEqual([c.split()[0] for c in h.sent].count("LONG"), 2)


# ignore-smoke (v0.2 V5): synthetic logs in the firmware's formats.
IGNORE_ON = "[5] [INF] [LXCARD] ignore ja:6 on written"
IGNORE_OFF = "[6] [INF] [LXCARD] ignore ja:6 off written"
MET_BEFORE_GET = "[4] [INF] [LXS] GET /v1/vocabulary/{id} -> 200 (ok)"


class IgnoreSmokeRules(unittest.TestCase):
    def test_an_ignore_and_its_undo_both_written_pass(self):
        self.assertEqual(lxctl.check_ignore_log([MET_BEFORE_GET, IGNORE_ON, IGNORE_OFF]), "ja:6")

    def test_a_write_to_lexirise_fails(self):
        for call in ("POST /v1/vocabulary", "PATCH /v1/vocabulary/{id}", "DELETE /v1/vocabulary/{id}"):
            with self.assertRaisesRegex(RuntimeError, "wrote to Lexirise"):
                lxctl.check_ignore_log([IGNORE_ON, f"[5] [INF] [LXS] {call} -> 200 (ok)", IGNORE_OFF])

    def test_missing_unwritten_or_extra_changes_fail(self):
        cases = (([], "no ignore-list change"),
                 (["[5] [INF] [LXCARD] ignore ja:6 on failed", IGNORE_OFF], "wasn't written"),
                 (["[5] [INF] [LXCARD] ignore ja:6 on unchanged"], "wasn't written"),
                 ([IGNORE_ON], "then its Undo"),
                 ([IGNORE_ON, "[6] [INF] [LXCARD] ignore ja:7 off written"], "then its Undo"),
                 ([IGNORE_ON, IGNORE_OFF, IGNORE_ON], "then its Undo"))
        for log, message in cases:
            with self.assertRaisesRegex(RuntimeError, message):
                lxctl.check_ignore_log(log)

    def test_targets_are_the_last_complete_set(self):
        log = ["[1] [INF] [LXCARD] targets 2", "[1] [INF] [LXCARD] level 0 273 570 42 37 0",
               "[1] [INF] [LXCARD] target undo 0 60 85 360 35",
               "[2] [INF] [LXCARD] targets 2", "[2] [INF] [LXCARD] target rank 0 17 690 446 50",
               "[2] [INF] [LXCARD] target tab 5 400 650 42 38"]
        # The toast's Undo was in the first set only: gone once the next set is drawn.
        self.assertEqual(lxctl.tap_targets(log), {("rank", 0): (17, 690, 446, 50), ("tab", 5): (400, 650, 42, 38)})
        cut = log + ["[3] [INF] [LXCARD] targets 1"]  # a set whose lines haven't all come: not the current one
        self.assertEqual(lxctl.tap_targets(cut), lxctl.tap_targets(log))
        self.assertEqual(len(lxctl.target_sets(log)), 2)
        self.assertEqual(lxctl.tap_targets(["[1] [INF] [LXCARD] target rank 0 1 2 3 4"]), {})  # no header: no set

    def test_a_form_key_with_spaces_is_read_whole(self):
        log = ["[5] [INF] [LXCARD] ignore ja:~a b on written", "[6] [INF] [LXCARD] ignore ja:~a b off written"]
        self.assertEqual(lxctl.check_ignore_log(log), "ja:~a b")

    def test_the_constants_and_log_lines_match_the_firmware(self):
        model = open(os.path.join(REPO, "src/lexirise/card/CardModel.h"), encoding="utf-8").read()
        ids = re.search(r"enum : int \{([^}]*)\}", model).group(1)
        self.assertEqual([n.strip() for n in ids.split(",")].index(
            [n.strip() for n in ids.split(",") if n.strip().startswith("Ignore")][0]), lxctl.IGNORE_ACTION)
        card = open(os.path.join(REPO, "src/lexirise/card/LexiriseCardActivity.cpp"), encoding="utf-8").read()
        self.assertIn('"target %s %d %d %d %d %d"', card)
        self.assertIn('"targets %d"', card)
        # Logged once the frame is on screen (after ShownTargets::shown): a tap sent then lands on it.
        self.assertRegex(card, r"targets_\.shown\(millis\(\)\);\s+#if LEXIPOINT_DEV_HARNESS\s+logTapTargets")
        c = header_constants("src/lexirise/LexiriseConfig.h")
        self.assertEqual(lxctl.IGNORE_TOAST_MS, c["kIgnoreToastMs"])
        for kind in ("rank", "tab", "action", "undo"):
            self.assertIn(f'"{kind}"', card)
        session = open(os.path.join(REPO, "src/lexirise/card/CardSession.cpp"), encoding="utf-8").read()
        self.assertIn('"ignore %s %s %s"', session)
        for word in ("written", "unchanged", "failed"):
            self.assertIn(f'"{word}"', session)


class FakeIgnoreHarness:
    """The device's side of ignore-smoke: a LONG opens a card, each SYNC logs the targets the card now shows, a TAP
    on the Ignore row or the toast's Undo logs its write."""

    def __init__(self, stardict=False, targets=True, undo=True, undo_lands=True):
        self.stardict, self.targets, self.undo = stardict, targets, undo  # undo=False: the toast expired first
        self.undo_lands = undo_lands  # False: the Undo tap is swallowed (it landed on the frame before the toast)
        self.stream: list[str] = []
        self.sent: list[str] = []
        self.shown = "card"

    def command(self, cmd, expect=None, timeout=0, seen=None):
        self.sent.append(cmd)
        if cmd.startswith("LONG "):
            self.stream.append("[1] [DBG] [ACT] Entering activity: "
                               + ("DictionaryDefinition" if self.stardict else "LexiriseCard"))
        elif cmd == "SYNC" and seen is not None and self.targets:
            seen += self.frame()
        elif cmd == "TAP 240 725":
            self.shown = "expanded"
        elif cmd == "TAP 441 709":
            self.shown = "actions"
        elif cmd == "TAP 240 370":  # the write, then the toast's frame (or, too late, the frame after it)
            self.shown = "toast" if self.undo else "actions"
            self.stream += [IGNORE_ON] + self.frame()
        elif cmd == "TAP 240 102" and self.undo_lands:
            self.shown = "actions"
            self.stream += [IGNORE_OFF] + self.frame()
        elif cmd == "HOME":
            self.stream.append("[9] [DBG] [ACT] Exiting activity: LexiriseCard")
        return "LX:OK"

    def frame(self):
        """The targets set the card logs for what it shows: a header, then every target, as the firmware does."""
        actions = ["target rank 0 17 740 446 50", "target tab 5 420 690 42 38",
                   "target action 1 17 300 446 40", "target action 2 17 350 446 40"]
        lines = {"card": ["target rank 0 17 700 446 50"],
                 "expanded": ["target rank 0 17 740 446 50", "target tab 0 17 690 80 38",
                              "target tab 5 420 690 42 38"],
                 "actions": actions,
                 "toast": ["target undo 0 60 85 360 35"] + actions}[self.shown]
        return [f"[2] [INF] [LXCARD] targets {len(lines)}"] + [f"[2] [INF] [LXCARD] {line}" for line in lines]

    def read_line(self, deadline):
        return self.stream.pop(0) if self.stream else None


class IgnoreSmokeDriver(unittest.TestCase):
    def test_it_taps_each_target_where_the_card_drew_it(self):
        h = FakeIgnoreHarness()
        self.assertEqual(lxctl.ignore_smoke(h, (240, 400), watch_s=0.01), "ja:6")
        taps = [c for c in h.sent if c.startswith("TAP")]
        self.assertEqual(taps, ["TAP 240 725", "TAP 441 709", "TAP 240 370", "TAP 240 102"])  # rank, ⋯, Ignore, Undo
        self.assertEqual(h.sent[0], "LONG 240 400")
        self.assertEqual(h.sent[-1], "HOME")

    def test_stardict_no_targets_or_no_undo_raise(self):
        for harness, message in ((FakeIgnoreHarness(stardict=True), "StarDict"),
                                 (FakeIgnoreHarness(targets=False), "no tap targets"),
                                 (FakeIgnoreHarness(undo=False), r"toast expired before the Undo tap: remove ja:6 "),
                                 (FakeIgnoreHarness(undo_lands=False),
                                  r"the Undo tap took nothing off the list: remove ja:6 from /.lexirise/ignored.ini")):
            with self.assertRaisesRegex(RuntimeError, message):
                lxctl.ignore_smoke(harness, (240, 400), watch_s=0.01)

    def test_its_commands_are_the_devices(self):
        usages = device_usages()
        for cmd in ("LONG 240 400", "TAP 240 725", "SYNC", "HOME"):
            self.assertRegex(cmd, usages[cmd.split()[0]])


VOCAB_FLUSH = "[4000] [INF] [LXVOCAB] mirror file read or written in 35 ms"
VOCAB_GET = ("[13000] [INF] [LXS] GET /v1/vocabulary?language=ja&limit=50&offset=0&sortId=updated_at&sortDesc=true "
             "-> 200 (ok)")
VOCAB_PASS = "[13010] [INF] [LXVOCAB] full ja offset 0: 50 items, mirror 48 words"
VOCAB_PAGE = ("[13020] [INF] [LXVOCAB] page 50 items in 2410 ms (ok), applied in 40 ms (file written); heap 91000 "
              "free, 62000 min, 38000 largest")
VOCAB_GIVEN = "[13005] [INF] [LXVOCAB] full ja offset 0 given up: input came"
VOCAB_PAGE_GIVEN = ("[13006] [INF] [LXVOCAB] page 0 items in 700 ms (malformed, given up for input), applied in 0 "
                    "ms (file not written); heap 90000 free, 62000 min, 38000 largest")


class VocabSmokeRules(unittest.TestCase):
    def test_a_page_with_its_time_and_heap_passes(self):
        pages = lxctl.check_vocab_log([MET_BEFORE_GET, VOCAB_FLUSH, VOCAB_GET, VOCAB_PASS, VOCAB_PAGE])
        self.assertEqual(pages, [{"kind": "page", "items": 50, "ms": 2410, "error": "ok", "applied_ms": 40,
                                  "file": "written", "free": 91000, "min": 62000, "largest": 38000}])

    def test_a_synced_mirrors_card_probe_counts_as_the_page(self):
        probe = "[5100] [INF] [LXVOCAB] card probe: 5 items in 820 ms (ok), 0 entries changed"
        pages = lxctl.check_vocab_log([MET_BEFORE_GET, VOCAB_FLUSH, VOCAB_GET, probe])
        self.assertEqual(pages, [{"kind": "probe", "items": 5, "ms": 820, "error": "ok", "changed": 0}])
        with self.assertRaisesRegex(RuntimeError, "other than the vocabulary list"):
            lxctl.check_vocab_log([VOCAB_FLUSH, "[5000] [INF] [LXS] POST /v1/vocabulary -> 200 (ok)", probe])
        card = open(os.path.join(REPO, "src/lexirise/card/LexiriseCardActivity.cpp"), encoding="utf-8").read()
        self.assertIn('"card probe: %u items in %lu ms (%s%s), %u entries changed%s"', card)

    def test_no_page_fails(self):
        with self.assertRaisesRegex(RuntimeError, "no mirror page or card probe logged"):
            lxctl.check_vocab_log([MET_BEFORE_GET, VOCAB_FLUSH])

    def test_another_call_during_the_sync_fails(self):
        for call in ("POST /v1/vocabulary", "PATCH /v1/vocabulary/{id}", "GET /v1/vocabulary/{id}", "GET /v1/decks"):
            with self.assertRaisesRegex(RuntimeError, "other than the vocabulary list"):
                lxctl.check_vocab_log([VOCAB_FLUSH, f"[6000] [INF] [LXS] {call} -> 200 (ok)", VOCAB_GET, VOCAB_PAGE])
        # Before the sync (the card's own lookup and Met before) is fine.
        lxctl.check_vocab_log(["[1000] [INF] [LXS] POST /v1/analyze/text -> 200 (ok)", VOCAB_FLUSH, VOCAB_GET,
                               VOCAB_PAGE])

    def test_a_page_that_never_came_whole_fails(self):
        failed = VOCAB_PAGE.replace("(ok)", "(timeout)")
        with self.assertRaisesRegex(RuntimeError, "no page came whole"):
            lxctl.check_vocab_log([VOCAB_FLUSH, failed])

    def test_pressed_needs_a_page_given_up(self):
        mid_stream = [VOCAB_FLUSH, VOCAB_GET.replace("(ok)", "(malformed)"), VOCAB_GIVEN, VOCAB_PAGE_GIVEN]
        self.assertEqual(len(lxctl.check_vocab_log(mid_stream, pressed=True)), 1)
        # Given up before its request (the button already held): no GET line, so not a cancel mid-stream.
        with self.assertRaisesRegex(RuntimeError, "before its request"):
            lxctl.check_vocab_log([VOCAB_FLUSH, VOCAB_GIVEN, VOCAB_PAGE_GIVEN], pressed=True)
        # The GET before an earlier page doesn't count for a later one given up before its request.
        with self.assertRaisesRegex(RuntimeError, "before its request"):
            lxctl.check_vocab_log([VOCAB_FLUSH, VOCAB_GET, VOCAB_PAGE, VOCAB_GIVEN, VOCAB_PAGE_GIVEN], pressed=True)
        with self.assertRaisesRegex(RuntimeError, "no page was given up"):
            lxctl.check_vocab_log([VOCAB_FLUSH, VOCAB_GET, VOCAB_PAGE], pressed=True)
        # A card probe's GET before a page given up before its request doesn't make it look mid-stream (R8).
        probe = "[13003] [INF] [LXVOCAB] card probe: 5 items in 800 ms (ok), 0 entries changed"
        with self.assertRaisesRegex(RuntimeError, "before its request"):
            lxctl.check_vocab_log([VOCAB_FLUSH, VOCAB_GET, probe, VOCAB_GIVEN, VOCAB_PAGE_GIVEN], pressed=True)

    def test_the_constants_and_log_lines_match_the_firmware(self):
        c = header_constants("src/lexirise/LexiriseConfig.h")
        self.assertEqual(lxctl.VOCAB_IDLE_MS, c["kVocabIdleMs"])
        card = open(os.path.join(REPO, "src/lexirise/card/LexiriseCardActivity.cpp"), encoding="utf-8").read()
        self.assertIn('"page %u items in %lu ms (%s%s), applied in %lu ms (file %s); heap %u free, %u min, %u largest"',
                      card)
        for file in ('"written"', '"write failed"', '"not written"'):
            self.assertIn(file, card)
        self.assertIn('"mirror file read or written in %lu ms%s"', card)
        mirror = open(os.path.join(REPO, "src/lexirise/vocab/VocabMirror.cpp"), encoding="utf-8").read()
        self.assertIn('given up: input came"', mirror)
        self.assertIn('kLogTag = "LXVOCAB"', open(os.path.join(REPO, "src/lexirise/vocab/VocabMirror.h"),
                                                   encoding="utf-8").read())


class FakeVocabHarness:
    """The device's side of vocab-smoke: a LONG opens a card; the idle card then logs the mirror's file and a page
    (`probe_first`: a quick probe page, then a whole one given up for a button)."""

    def __init__(self, stardict=False, page=True, probe_first=False, card_probe=False):
        self.stardict, self.page, self.probe_first, self.card_probe = stardict, page, probe_first, card_probe
        self.stream: list[str] = []
        self.sent: list[str] = []

    def command(self, cmd, expect=None, timeout=0, seen=None):
        self.sent.append(cmd)
        if cmd.startswith("LONG "):
            self.stream.append("[1] [DBG] [ACT] Entering activity: "
                               + ("DictionaryDefinition" if self.stardict else "LexiriseCard"))
        elif cmd == "SYNC" and self.card_probe:  # a synced mirror: the card's probe, and no idle page after it
            self.stream += [VOCAB_FLUSH, VOCAB_GET, "[5100] [INF] [LXVOCAB] card probe: 5 items in 820 ms (ok), 0 entries changed"]
        elif cmd == "SYNC" and self.probe_first:
            probe = VOCAB_PAGE.replace("page 50 items in 2410 ms", "page 5 items in 310 ms")
            self.stream += [VOCAB_FLUSH, VOCAB_GET, VOCAB_PASS, probe,
                            VOCAB_GET.replace("(ok)", "(malformed)"), VOCAB_GIVEN, VOCAB_PAGE_GIVEN]
        elif cmd == "SYNC":
            self.stream += [VOCAB_FLUSH] + ([VOCAB_GET, VOCAB_PASS, VOCAB_PAGE] if self.page else [])
        elif cmd == "HOME":  # the card closes: what it would have logged after is never logged
            self.stream = ["[20000] [DBG] [ACT] Exiting activity: LexiriseCard"]
        return "LX:OK"

    def read_line(self, deadline):
        return self.stream.pop(0) if self.stream else None


class VocabSmokeDriver(unittest.TestCase):
    def test_it_keeps_the_reader_awake_first_then_opens_and_leaves_a_card(self):
        h = FakeVocabHarness()
        pages = lxctl.vocab_smoke(h, (240, 400), watch_s=0.01)
        self.assertEqual(pages[0]["items"], 50)
        self.assertEqual(h.sent, ["AWAKE 1", "LONG 240 400", "SYNC", "HOME"])

    def test_press_reads_past_a_probe_to_the_page_given_up(self):
        h = FakeVocabHarness(probe_first=True)
        pages = lxctl.vocab_smoke(h, (240, 400), press=True, watch_s=0.01)
        self.assertEqual([p["items"] for p in pages], [5, 0])  # the probe came whole, then the page given up
        self.assertIn("given up for input", pages[-1]["error"])

    def test_a_synced_mirrors_card_probe_passes(self):
        pages = lxctl.vocab_smoke(FakeVocabHarness(card_probe=True), (240, 400), watch_s=0.01)
        self.assertEqual(pages[-1]["kind"], "probe")

    def test_stardict_or_no_page_raise(self):
        for harness, message in ((FakeVocabHarness(stardict=True), "StarDict"),
                                 (FakeVocabHarness(page=False), "no mirror page or card probe logged")):
            with self.assertRaisesRegex(RuntimeError, message):
                lxctl.vocab_smoke(harness, (240, 400), watch_s=0.01)

    def test_its_commands_are_the_devices(self):
        usages = device_usages()
        for cmd in ("LONG 240 400", "SYNC", "HOME"):
            self.assertRegex(cmd, usages[cmd.split()[0]])
        protocol = open(os.path.join(REPO, "src/lexirise/dev/DevProtocol.cpp"), encoding="utf-8").read()
        self.assertIn("AWAKE", protocol)



# --- page-smoke (v0.2 V7b) ---

PAGE_DRAWN = "[{t}] [DBG] [ERS] Rendered page in 420ms"
PAGE_STEP = ("[{t}] [INF] [LXPAGE] {which} page ({page} of section 2): {kind} in {ms} ms (ok), 206 occurrences, "
             "{calls} calls in {ms} ms, written in 30 ms; heap 91000 free, 62000 min, 38000 largest")
PAGE_ANALYZE = "[{t}] [INF] [LXS] POST /v1/analyze/text -> 200 (ok)"
PAGE_CARD = "[{t}] [INF] [LXPAGE] card: page 2-1200 {found}: no analyze/text for its sentences"


def page_log(first_step_at=3000, card_found="analyzed", card_asks=False, write=False):
    log = [PAGE_DRAWN.format(t=1000),
           PAGE_STEP.format(t=first_step_at, which="this", page=3, kind="kept already", ms=20, calls=0),
           PAGE_ANALYZE.format(t=first_step_at + 2400),
           PAGE_STEP.format(t=first_step_at + 2500, which="next", page=4, kind="analyzed", ms=2400, calls=1),
           "[9000] [DBG] [ACT] Entering activity: LexiriseCard",
           PAGE_CARD.format(t=9010, found=card_found)]
    if card_asks:
        log.append("[9100] [INF] [LXS] POST /v1/analyze/text -> 200 (ok)")
    if write:
        log.append("[9200] [INF] [LXS] POST /v1/vocabulary -> 200 (ok)")
    log.append("[9500] [DBG] [ACT] Exiting activity: LexiriseCard")
    return log


class PageSmokeRules(unittest.TestCase):
    def test_this_page_and_the_next_after_the_dwell_pass(self):
        steps = lxctl.check_page_log(page_log())
        self.assertEqual([s["which"] for s in steps], ["this", "next"])
        self.assertEqual(steps[1]["ms"], 2400)
        self.assertEqual(steps[1]["calls"], 1)

    def test_a_step_right_after_a_card_closed_over_the_page_fails(self):
        log = page_log()
        log += [PAGE_DRAWN.format(t=9600), PAGE_STEP.format(t=9700, which="next", page=5, kind="kept already", ms=20,
                                                            calls=0)]
        with self.assertRaisesRegex(RuntimeError, "after the page was drawn"):
            lxctl.check_page_log(log)

    def test_a_step_before_the_dwell_fails(self):
        with self.assertRaisesRegex(RuntimeError, "after the page was drawn"):
            lxctl.check_page_log(page_log(first_step_at=2000))  # started 1000 ms after the page was drawn

    def test_a_step_with_no_drawing_before_it_fails(self):
        log = [line for line in page_log() if "Rendered page" not in line]
        with self.assertRaisesRegex(RuntimeError, "no page drawing logged"):
            lxctl.check_page_log(log)

    def test_no_next_page_fails(self):
        log = [line for line in page_log() if "next page" not in line]
        with self.assertRaisesRegex(RuntimeError, "this page and the next"):
            lxctl.check_page_log(log)

    def test_the_card_must_use_the_pages_analysis(self):
        with self.assertRaisesRegex(RuntimeError, "didn't find its analysis"):
            lxctl.check_page_log(page_log(card_found="not analyzed"))
        with self.assertRaisesRegex(RuntimeError, "still sent analyze/text"):
            lxctl.check_page_log(page_log(card_asks=True))

    def test_a_page_analyzed_twice_or_an_extra_call_fails(self):
        twice = page_log()
        twice.insert(3, PAGE_ANALYZE.format(t=5600))
        twice.insert(4, PAGE_STEP.format(t=5700, which="next", page=4, kind="analyzed", ms=50, calls=1))
        with self.assertRaisesRegex(RuntimeError, "more than once"):
            lxctl.check_page_log(twice)
        extra = page_log()
        extra.insert(1, PAGE_ANALYZE.format(t=1200))
        with self.assertRaisesRegex(RuntimeError, "analyze/text calls outside cards"):
            lxctl.check_page_log(extra)

    def test_a_call_given_up_before_it_was_sent_isnt_counted(self):
        log = page_log()
        log.insert(1, "[1500] [INF] [LXS] POST /v1/analyze/text -> 0 (cancelled)")
        lxctl.check_page_log(log)

    def test_a_call_given_up_after_it_was_sent_is_left_out_of_both_counts(self):
        # The step given up logs 0 calls (Prefetch counts answered calls), its call "(cancelled)": both leave it out.
        log = page_log()
        log.insert(1, "[3400] [INF] [LXS] POST /v1/analyze/text -> 0 (cancelled)")
        log.insert(2, PAGE_STEP.format(t=3500, which="this", page=2, kind="given up for input", ms=900, calls=0))
        lxctl.check_page_log(log)
        src = open(os.path.join(REPO, "src/lexirise/page/Prefetch.cpp"), encoding="utf-8").read()
        self.assertIn("response.status != 0 && response.error != api::ApiError::Cancelled ? 1 : 0", src)

    def test_a_call_failing_before_an_answer_is_left_out_of_both_counts(self):
        log = page_log()
        log.insert(1, "[3400] [INF] [LXS] POST /v1/analyze/text -> 0 (low-memory)")
        log.insert(2, PAGE_STEP.format(t=3500, which="this", page=2, kind="failed", ms=900, calls=0))
        lxctl.check_page_log(log)

    def test_a_write_fails(self):
        with self.assertRaisesRegex(RuntimeError, "write to Lexirise"):
            lxctl.check_page_log(page_log(write=True))

    def test_the_constants_and_log_lines_match_the_firmware(self):
        c = header_constants("src/lexirise/LexiriseConfig.h")
        self.assertEqual(lxctl.PAGE_DWELL_MS, c["kPagePrefetchDwellMs"])
        pages = open(os.path.join(REPO, "src/lexirise/page/ReaderPages.cpp"), encoding="utf-8").read()
        joined = re.sub(r'"\s+"', "", pages)  # adjacent string literals, as the compiler joins them
        self.assertIn('"%s page (%d of section %d): %s in %lu ms (%s), %u occurrences%s, %u calls in %lu ms, written '
                      'in %lu ms%s; heap %u free, %u min, %u largest"', joined)
        for kind in ("analyzed", "kept already", "failed", "unusable", "given up for input", "no text"):
            self.assertIn(f'"{kind}"', pages)
        sentences = open(os.path.join(REPO, "src/lexirise/page/PageSentences.cpp"), encoding="utf-8").read()
        self.assertIn('"card: page %u-%u %s"', sentences)
        self.assertIn('"analyzed: no analyze/text for its sentences"', sentences)


class HeldButton:
    """The device's side of a BTN (DevHarness pressButton -> ButtonPress::start): the press is held for
    kButtonDefaultMs, and another BTN before then is refused with "LX:ERR button busy". Time is a clock the fake
    sleep advances; `slow_s` holds one press that much longer (a loop slowed by a page render samples it late)."""

    HOLD_S = header_constants("src/lexirise/dev/DevConfig.h")["kButtonDefaultMs"] / 1000

    def __init__(self, slow_s: float = 0.0):
        self.clock = 0.0
        self.held_until = -1.0
        self.slow_s = slow_s
        self.busy = 0  # presses refused

    def sleep(self, seconds):
        self.clock += seconds

    def btn(self, cmd):
        if self.clock < self.held_until:
            self.busy += 1
            raise RuntimeError(f"{cmd}: LX:ERR button busy")
        self.held_until = self.clock + self.HOLD_S + self.slow_s
        self.slow_s = 0.0


class FakePageHarness(HeldButton):
    """The device's side of page-smoke: LONG opens a card, HOME closes it, each turn draws a page and, after the
    dwell, logs this page's and the next page's steps; the second card finds the page analyzed."""

    def __init__(self):
        super().__init__()
        self.stream: list[str] = []
        self.sent: list[str] = []
        self.t = 1000
        self.cards = 0
        self.page = 0

    def _page(self):
        self.t += 500
        self.stream.append(PAGE_DRAWN.format(t=self.t))
        self.t += 2000
        self.page += 1
        self.stream.append(PAGE_STEP.format(t=self.t, which="this", page=self.page, kind="kept already", ms=20,
                                            calls=0))
        self.t += 2500
        self.stream.append(PAGE_ANALYZE.format(t=self.t - 100))
        self.stream.append(PAGE_STEP.format(t=self.t, which="next", page=self.page + 1, kind="analyzed", ms=2400,
                                            calls=1))

    def command(self, cmd, expect=None, timeout=0, seen=None):
        self.sent.append(cmd)
        if cmd.startswith("LONG "):
            self.cards += 1
            self.stream.append("[1] [DBG] [ACT] Entering activity: LexiriseCard")
            if self.cards == 2:
                self.stream.append(PAGE_CARD.format(t=self.t + 10, found="analyzed"))
        elif cmd == "HOME":  # the card closes over the page: the reader draws it again
            self.stream.append("[2] [DBG] [ACT] Exiting activity: LexiriseCard")
            if self.cards == 1:
                self._page()
            else:
                self.t += 300
                self.stream.append(PAGE_DRAWN.format(t=self.t))
        elif cmd == lxctl.PAGE_TURN:
            self.btn(cmd)
            self._page()
        return "LX:OK"

    def read_line(self, deadline):
        return self.stream.pop(0) if self.stream else None


class PageSmokeDriver(unittest.TestCase):
    def test_it_brings_wifi_up_with_a_card_then_turns_and_opens_a_card_on_the_page(self):
        h = FakePageHarness()
        steps = lxctl.page_smoke(h, (240, 400), watch_s=0.01, sleep=h.sleep)
        self.assertGreaterEqual(len(steps), 4)
        self.assertEqual(h.busy, 0)
        self.assertEqual(h.sent[:2], ["AWAKE 1", "LONG 240 400"])
        self.assertEqual(h.sent.count(lxctl.PAGE_TURN), 1 + lxctl.PAGE_FAST_TURNS)
        self.assertEqual(h.sent[-3:], ["LONG 240 400", "SYNC", "HOME"])

    def test_its_commands_are_the_devices(self):
        usages = device_usages()
        for cmd in ("LONG 240 400", "SYNC", "HOME", lxctl.PAGE_TURN):
            self.assertRegex(cmd, usages[cmd.split()[0]])



# --- marks-smoke (v0.2 V9a) ---

MARKS_DRAW = "[{t}] [DBG] [LXPAGE] marks: 180 words, 190 fills in {ms} ms; heap {heap} free"
MARKS_READ = "[{t}] [DBG] [LXPAGE] marks: {which} page 2-{start} kept in {ms} ms"
MARKS_PEEK = "[{t}] [DBG] [LXPAGE] marks: page 2-0 read as drawn: kept in 35 ms"


def marks_log(turns=3, unmarked_back=None, write=False):
    """`turns` forward and back, from a page never drawn in the log (AWAKE draws nothing): each drawing marked (its
    draw line before its Rendered page), the pages around read after it; `unmarked_back`: the back drawing (0-based
    among the back turns) left unmarked."""
    log, t = [], 1000
    for i in range(2 * turns):
        t += 100
        log.append(MARKS_READ.format(t=t, which="next", start=1200 * (i + 1), ms=30))
        t += 900
        if i < turns or unmarked_back != i - turns:
            log.append(MARKS_DRAW.format(t=t - 50, ms=45 + i, heap=90000 - i))
        log.append(PAGE_DRAWN.format(t=t))
    if write:
        log.append("[9200] [INF] [LXS] POST /v1/vocabulary -> 200 (ok)")
    return log


class MarksSmokeRules(unittest.TestCase):
    def test_every_page_marked_forward_and_back_passes(self):
        r = lxctl.check_marks_log(marks_log())
        self.assertEqual(r, {"drawn": 6, "marked": 6, "draw_ms": 50, "read_ms": 30, "reads": 6, "peek_ms": 0,
                             "heap": 89995})

    def test_a_page_plain_on_the_way_back_fails(self):
        with self.assertRaisesRegex(RuntimeError, "not on the way back"):
            lxctl.check_marks_log(marks_log(unmarked_back=1))

    def test_an_unmarked_first_page_or_too_few_pages_fail(self):
        log = [line for line in marks_log() if "marks:" not in line]
        with self.assertRaisesRegex(RuntimeError, "first page turned to wasn't marked"):
            lxctl.check_marks_log(log)
        with self.assertRaisesRegex(RuntimeError, "pages drawn"):
            lxctl.check_marks_log(marks_log()[:8])

    def test_a_write_fails(self):
        with self.assertRaisesRegex(RuntimeError, "write to Lexirise"):
            lxctl.check_marks_log(marks_log(write=True))

    def test_the_log_lines_match_the_firmware(self):
        marks = "".join(open(os.path.join(REPO, f"src/lexirise/page/{name}"), encoding="utf-8").read()
                        for name in ("ReaderMarks.cpp", "MarkKeeper.cpp", "MarkGate.h"))
        self.assertIn('"marks: %u words, %u fills in %lu ms; heap %u free"', marks)
        self.assertIn('"marks: %s page %u-%u %s in %lu ms"', marks)
        self.assertIn('"marks: page %u-%u read as drawn: %s in %lu ms"', marks)
        for word in ('"this"', '"next"', '"previous"', '"kept"', '"not analyzed"'):
            self.assertIn(word, marks)


class FakeMarksHarness(HeldButton):
    """The device's side of marks-smoke: each turn draws a marked page, then the loop reads the pages around it; a
    turn while the last is still held is refused (HeldButton)."""

    def __init__(self, slow_s: float = 0.0):
        super().__init__(slow_s)
        self.stream: list[str] = []
        self.sent: list[str] = []
        self.t = 1000

    def _page(self):
        self.t += 500
        self.stream.append(MARKS_DRAW.format(t=self.t - 10, ms=42, heap=90500))
        self.stream.append(PAGE_DRAWN.format(t=self.t))
        self.stream.append(MARKS_READ.format(t=self.t + 50, which="previous", start=0, ms=0))

    def command(self, cmd, expect=None, timeout=0, seen=None):
        self.sent.append(cmd)
        if cmd in (lxctl.PAGE_TURN, lxctl.PAGE_BACK):  # AWAKE draws nothing (DevHarness: it only holds the reader awake)
            self.btn(cmd)
            self._page()
        return "LX:OK"

    def read_line(self, deadline):
        return self.stream.pop(0) if self.stream else None


class MarksSmokeDriver(unittest.TestCase):
    def test_it_turns_forward_then_back(self):
        h = FakeMarksHarness()
        r = lxctl.marks_smoke(h, settle_s=0.01, sleep=h.sleep)
        self.assertEqual(h.busy, 0)
        self.assertEqual(h.sent, ["AWAKE 1"] + [lxctl.PAGE_TURN] * lxctl.MARKS_TURNS +
                         [lxctl.PAGE_BACK] * lxctl.MARKS_TURNS)
        self.assertEqual(r["marked"], 2 * lxctl.MARKS_TURNS)

    def test_awake_draws_nothing(self):
        h = FakeMarksHarness()
        h.command("AWAKE 1")
        self.assertEqual(h.stream, [])
        src = open(os.path.join(REPO, "src/lexirise/dev/DevHarness.cpp"), encoding="utf-8").read()
        awake = src[src.index("case Verb::Awake:"):src.index("case Verb::Reboot:")]
        self.assertNotIn("requestUpdate", awake)  # it only keeps the reader awake: nothing is drawn

    def test_its_commands_are_the_devices(self):
        usages = device_usages()
        for cmd in (lxctl.PAGE_TURN, lxctl.PAGE_BACK):
            self.assertRegex(cmd, usages[cmd.split()[0]])


RENDER = "[{t}] [DBG] [ERS] Page render: prewarm=5ms bw_render=80ms display=300ms total={ms}ms"


class MarksFastSmoke(unittest.TestCase):
    def fast_log(self, pages=10):
        log = []
        for i in range(pages):
            log += [MARKS_PEEK.format(t=1000 + i), RENDER.format(t=1000 + i, ms=400 + i), PAGE_DRAWN.format(t=1001 + i)]
        return log

    def test_it_reports_the_reads_as_drawn_and_the_renders(self):
        r = lxctl.check_marks_fast_log(self.fast_log())
        self.assertEqual(r, {"drawn": 10, "peeks": 10, "peek_ms": 35, "render_ms": sum(400 + i for i in range(10)),
                             "render_max_ms": 409})

    def test_too_few_pages_or_a_write_fail(self):
        with self.assertRaisesRegex(RuntimeError, "pages drawn"):
            lxctl.check_marks_fast_log(self.fast_log(4))
        with self.assertRaisesRegex(RuntimeError, "write to Lexirise"):
            lxctl.check_marks_fast_log(self.fast_log() + ["[1] [INF] [LXS] PATCH /v1/vocabulary/1 -> 200 (ok)"])

    def test_the_render_lines_match_the_firmware(self):
        src = open(os.path.join(REPO, "src/activities/reader/EpubReaderActivity.cpp"), encoding="utf-8").read()
        joined = re.sub(r'"\s+"', "", src)  # adjacent string literals, as the compiler joins them
        formats = re.findall(r'"(Page render[^"]*total=%lums[^"]*)"', joined)
        # The plain render and the tiled ones (sync and async), each counted by PAGE_RENDER_LOG.
        self.assertIn("Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", formats)
        self.assertTrue(any(f.startswith("Page render (tiled): ") for f in formats), formats)
        self.assertTrue(any(f.startswith("Page render (tiled async): ") for f in formats), formats)
        for f in formats:
            line = "[1] [DBG] [ERS] " + re.sub(r"%lu", "7", f)
            m = lxctl.PAGE_RENDER_LOG.search(line)
            self.assertIsNotNone(m, f)
            self.assertEqual(m.group(1), "7", f)

    def test_tiled_renders_are_counted(self):
        log = self.fast_log(10)
        log.append(RENDER.format(t=2000, ms=500).replace("Page render:", "Page render (tiled):"))
        log.append(RENDER.format(t=2001, ms=600).replace("Page render:", "Page render (tiled async):"))
        r = lxctl.check_marks_fast_log(log)
        self.assertEqual(r["render_ms"], sum(400 + i for i in range(10)) + 1100)
        self.assertEqual(r["render_max_ms"], 600)

    def test_the_driver_turns_back_to_back(self):
        h = FakeMarksHarness()
        r = lxctl.marks_fast_smoke(h, turns=3, settle_s=0.01, sleep=h.sleep)
        self.assertEqual(h.sent, ["AWAKE 1"] + [lxctl.PAGE_TURN] * 3)
        self.assertEqual(r["drawn"], 3)
        self.assertEqual(h.busy, 0)


class ButtonHold(unittest.TestCase):
    """V9a: a BTN is refused while the last press is held, so every smoke that presses one after another spaces
    them by BUTTON_HOLD_S (press), and tries a refused press once more."""

    def test_the_hold_outlasts_the_devices(self):
        c = header_constants("src/lexirise/dev/DevConfig.h")
        self.assertGreaterEqual(lxctl.BUTTON_HOLD_S * 1000 - c["kButtonDefaultMs"], lxctl.BUTTON_HOLD_MARGIN_MS)
        src = open(os.path.join(REPO, "src/lexirise/dev/DevHarness.cpp"), encoding="utf-8").read()
        self.assertIn(f'err("{lxctl.BUTTON_BUSY}")', src)

    def test_the_fake_refuses_a_press_until_the_hold_has_passed(self):
        h = FakeMarksHarness()
        h.command(lxctl.PAGE_TURN)
        h.sleep(HeldButton.HOLD_S / 2)
        with self.assertRaisesRegex(RuntimeError, "button busy"):
            h.command(lxctl.PAGE_TURN)
        h.sleep(HeldButton.HOLD_S / 2)
        h.command(lxctl.PAGE_TURN)
        self.assertEqual(h.busy, 1)

    def test_back_to_back_presses_without_the_wait_are_refused(self):
        h = FakeMarksHarness()
        with self.assertRaisesRegex(RuntimeError, "button busy"):
            lxctl.marks_fast_smoke(h, turns=3, settle_s=0.01, sleep=lambda _s: None)

    def test_a_press_waits_the_hold_after_it(self):
        h = FakeMarksHarness()
        lxctl.press(h, lxctl.PAGE_TURN, sleep=h.sleep)
        self.assertEqual(h.clock, lxctl.BUTTON_HOLD_S)

    def test_a_busy_press_is_tried_once_more(self):
        h = FakeMarksHarness(slow_s=lxctl.BUTTON_HOLD_S)  # the first press is sampled late
        r = lxctl.marks_fast_smoke(h, turns=3, settle_s=0.01, sleep=h.sleep)
        self.assertEqual(h.busy, 1)
        self.assertEqual(h.sent, ["AWAKE 1"] + [lxctl.PAGE_TURN] * 4)
        self.assertEqual(r["drawn"], 3)

    def test_busy_twice_or_another_error_fails(self):
        h = FakeMarksHarness(slow_s=3 * lxctl.BUTTON_HOLD_S)
        lxctl.press(h, lxctl.PAGE_TURN, sleep=h.sleep)
        with self.assertRaisesRegex(RuntimeError, "button busy"):
            lxctl.press(h, lxctl.PAGE_TURN, sleep=h.sleep)

        class Refuses:
            def command(self, cmd, expect=None, timeout=0, seen=None):
                raise RuntimeError(f"{cmd}: LX:ERR no reader")

        with self.assertRaisesRegex(RuntimeError, "no reader"):
            lxctl.press(Refuses(), lxctl.PAGE_TURN, sleep=lambda _s: self.fail("slept after another error"))

    def test_every_smoke_spaces_its_presses(self):
        # No BTN is sent straight after another: each goes through press, or is followed by a SYNC or a shot.
        src = open(os.path.join(REPO, "scripts/lexipoint/lxctl.py"), encoding="utf-8").read()
        for name in ("page_smoke", "marks_smoke", "marks_fast_smoke"):
            body = src[src.index(f"def {name}("):]
            body = body[:body.index("\ndef ", 1)]
            self.assertNotIn("h.command(PAGE_TURN", body, name)
            self.assertNotIn("h.command(PAGE_BACK", body, name)
            self.assertNotIn("h.command(cmd, seen=log)", body, name)


class CardMarksChange(unittest.TestCase):
    """V9a: the page's marks under the card are drawn again after an Ignore (ignore-smoke reports it)."""

    @staticmethod
    def drawn(t, words):
        return MARKS_DRAW.format(t=t, ms=40, heap=90000).replace("180 words", f"{words} words")

    def test_the_marks_before_and_after_the_ignore(self):
        log = [self.drawn(1, 12), "[2] [INF] [LXCARD] ignore ja:6 on written", self.drawn(3, 10)]
        self.assertEqual(lxctl.card_marks_change(log, 1), (12, 10))

    def test_paired_by_the_tap_not_the_written_line(self):
        # The revision moves as the list is written, so the new marks can be drawn before "on written" is logged;
        # the Undo's change draws the old ones again, after it.
        log = [self.drawn(1, 12), "[2] [INF] [LXCARD] target undo 0 1 2 3 4", self.drawn(3, 10),
               "[4] [INF] [LXCARD] ignore ja:6 on written", "[5] [INF] [LXCARD] ignore ja:6 off written",
               self.drawn(6, 12)]
        self.assertEqual(lxctl.card_marks_change(log, 1), (12, 10))
        # Nothing drawn between the tap and the Undo: nothing to say.
        self.assertIsNone(lxctl.card_marks_change(log[:2] + log[3:], 1))

    def test_no_marks_or_no_ignore_says_nothing(self):
        self.assertIsNone(lxctl.card_marks_change(["[2] [INF] [LXCARD] ignore ja:6 on written"], 0))
        self.assertIsNone(lxctl.card_marks_change([self.drawn(1, 12)], 1))


# --- home-sync-smoke (v0.2 V7b) ---

HOME_RESULT = "[30000] [INF] [LXVOCAB] home sync: {result} after 4 pages, {n} words changed"
HOME_WIFI = "[32100] [INF] [LXVOCAB] home sync: WiFi given back in 180 ms"
HOME_GET = "[20000] [INF] [LXS] GET /v1/vocabulary?language=ja&limit=5&offset=0&sortId=updated_at&sortDesc=true -> 200 (ok)"


class HomeSyncSmokeRules(unittest.TestCase):
    def test_a_result_then_wifi_given_back_passes(self):
        r = lxctl.check_home_sync_log([HOME_GET, HOME_RESULT.format(result="synced", n=3), HOME_WIFI])
        self.assertEqual(r, {"result": "synced", "pages": 4, "changed": 3, "wifi": "given back", "release_ms": 180})

    def test_no_result_a_failure_or_no_release_fails(self):
        with self.assertRaisesRegex(RuntimeError, "no home sync result"):
            lxctl.check_home_sync_log([HOME_GET])
        with self.assertRaisesRegex(RuntimeError, "failed"):
            lxctl.check_home_sync_log([HOME_RESULT.format(result="failed", n=0), HOME_WIFI])
        with self.assertRaisesRegex(RuntimeError, "given back"):
            lxctl.check_home_sync_log([HOME_RESULT.format(result="up to date", n=0)])

    def test_another_call_fails(self):
        with self.assertRaisesRegex(RuntimeError, "other than the vocabulary list"):
            lxctl.check_home_sync_log(["[1] [INF] [LXS] POST /v1/vocabulary -> 200 (ok)",
                                       HOME_RESULT.format(result="synced", n=1), HOME_WIFI])

    def test_the_log_lines_match_the_firmware(self):
        sync = open(os.path.join(REPO, "src/lexirise/vocab/ManualSync.cpp"), encoding="utf-8").read()
        self.assertIn('"home sync: %s after %u pages, %u words changed"', sync)
        for word in ('"up to date"', '"synced"', '"no WiFi"', '"stopped"', '"failed"'):
            self.assertIn(word, sync)
        home = open(os.path.join(REPO, "src/lexirise/vocab/HomeSync.cpp"), encoding="utf-8").read()
        self.assertIn('"home sync: WiFi %s in %lu ms"', re.sub(r'"\s+"', "", home))
        self.assertIn('"given back"', home)


class FakeHomeHarness:
    def __init__(self, leaks=False):
        self.stream: list[str] = []
        self.sent: list[str] = []
        self.leaks = leaks  # the dismissing tap reaches the menu: a second sync
        self.result_read = False
        self.tapped = False
        self.waited_s = 0.0  # the longest read asked between the result's line and the popup's tap

    def command(self, cmd, expect=None, timeout=0, seen=None):
        self.sent.append(cmd)
        if cmd == "HOME" and seen is not None:
            seen.append("[1] [DBG] [ACT] Entering activity: Home")
        elif cmd == f"TAP {lxctl.HOME_SYNC_ROW[0]} {lxctl.HOME_SYNC_ROW[1]}":
            self.stream += [HOME_GET, HOME_RESULT.format(result="up to date", n=0)]
        elif cmd == f"TAP {lxctl.HOME_SYNC_POPUP[0]} {lxctl.HOME_SYNC_POPUP[1]}":
            self.tapped = True
            self.stream += [HOME_WIFI]
            if self.leaks:
                self.stream += [HOME_GET, HOME_RESULT.format(result="up to date", n=0)]
        return "LX:OK"

    def read_line(self, deadline):
        if self.result_read and not self.tapped:
            self.waited_s = max(self.waited_s, deadline - time.time())
        line = self.stream.pop(0) if self.stream else None
        if line and lxctl.HOME_SYNC_LOG.search(line):
            self.result_read = True
        return line


class HomeSyncSmokeDriver(unittest.TestCase):
    def test_it_waits_for_the_popup_before_tapping_it(self):
        h = FakeHomeHarness()
        lxctl.home_sync_smoke(h, watch_s=0.01)
        self.assertGreater(h.waited_s, lxctl.HOME_SYNC_TAP_AFTER_S - 0.2)
        self.assertLess(lxctl.HOME_SYNC_TAP_AFTER_S, 2.0)  # inside kVocabSyncResultMs: the popup is still up

    def test_it_goes_home_and_taps_the_row(self):
        h = FakeHomeHarness()
        r = lxctl.home_sync_smoke(h, watch_s=0.01)
        self.assertEqual(r["result"], "up to date")
        self.assertEqual(h.sent, ["AWAKE 1", "HOME", "SYNC", f"TAP {lxctl.HOME_SYNC_ROW[0]} {lxctl.HOME_SYNC_ROW[1]}",
                                  f"TAP {lxctl.HOME_SYNC_POPUP[0]} {lxctl.HOME_SYNC_POPUP[1]}"])

    def test_a_dismissing_tap_that_starts_a_second_sync_fails(self):
        with self.assertRaisesRegex(RuntimeError, "a second home sync|after the home sync ended"):
            lxctl.home_sync_smoke(FakeHomeHarness(leaks=True), watch_s=0.01)

    def test_a_failed_join_says_wifi_not_up(self):
        r = lxctl.check_home_sync_log([HOME_RESULT.format(result="no WiFi", n=0),
                                       "[32100] [INF] [LXVOCAB] home sync: WiFi not up in 1 ms"])
        self.assertEqual(r["wifi"], "not up")

    def test_its_commands_are_the_devices(self):
        usages = device_usages()
        for cmd in ("HOME", "SYNC", "TAP 240 562"):
            self.assertRegex(cmd, usages[cmd.split()[0]])


if __name__ == "__main__":
    unittest.main()


# cache-smoke (v0.2 V7c): three cards' logs, as LexiriseCardActivity, the service and TlsConnection write them.
CACHE_OPEN = "[{t}] [DBG] [ACT] Entering activity: LexiriseCard"
CACHE_EXIT = "[{t}] [DBG] [ACT] Exiting activity: LexiriseCard"
CACHE_READ = "[{t}] [INF] [LXLOOK] cache {outcome} in 12 ms ({h} of {n} hits this boot)"
CACHE_LOOKUP = "[{t}] [INF] [LXS] POST /v1/dictionary/lookup -> 200 (ok)"
CACHE_WRITE = "[{t}] [INF] [LXLOOK] cache: 1 answers {how} in 30 ms (closing)"
CACHE_TLS = "[{t}] [INF] [LXT] Verified api.lexirise.app (TLSv1.3, TLS_AES_256_GCM_SHA384, {kind}) in {ms} ms, " \
            "free heap 81234"


def cache_log(first_read="miss", first_lookups=1, written="written", again_read="hit", again_lookup=False,
              second_read="miss", resumed=True, write_call=False):
    log = [CACHE_OPEN.format(t=1000), CACHE_TLS.format(t=3500, kind="full", ms=2500),
           CACHE_READ.format(t=3600, outcome=first_read, h=0, n=1)]
    log += [CACHE_LOOKUP.format(t=4000 + i) for i in range(first_lookups)]
    log += [CACHE_WRITE.format(t=6000, how=written), CACHE_EXIT.format(t=6100)]
    log += [CACHE_OPEN.format(t=40000), CACHE_READ.format(t=40100, outcome=again_read, h=1, n=2)]
    if again_lookup:
        log.append(CACHE_LOOKUP.format(t=40500))
    log += [CACHE_EXIT.format(t=41000), CACHE_OPEN.format(t=42000)]
    if resumed:
        log.append(CACHE_TLS.format(t=43000, kind="resumed", ms=1100))
    log += [CACHE_READ.format(t=43100, outcome=second_read, h=1, n=3), CACHE_LOOKUP.format(t=43500)]
    if write_call:
        log.append("[43600] [INF] [LXS] POST /v1/vocabulary -> 200 (ok)")
    log.append(CACHE_EXIT.format(t=44000))
    return log


class CacheSmokeRules(unittest.TestCase):
    def test_a_miss_a_hit_and_a_resumed_session_pass(self):
        r = lxctl.check_cache_log(cache_log())
        self.assertEqual((r["miss_ms"], r["hit_ms"], r["write_ms"]), (12, 12, 30))
        self.assertEqual((r["resumed_ms"], r["full_ms"], r["resumed_heap"]), (1100, 2500, 81234))

    def test_a_first_card_that_hit_fails(self):
        with self.assertRaisesRegex(RuntimeError, "wasn't a miss"):
            lxctl.check_cache_log(cache_log(first_read="hit"))

    def test_the_first_card_sends_one_lookup(self):
        with self.assertRaisesRegex(RuntimeError, "not 1"):
            lxctl.check_cache_log(cache_log(first_lookups=2))

    def test_an_answer_not_written_fails(self):
        with self.assertRaisesRegex(RuntimeError, "wasn't written"):
            lxctl.check_cache_log(cache_log(written="not written"))

    def test_the_second_card_must_hit_without_a_lookup(self):
        with self.assertRaisesRegex(RuntimeError, "didn't hit"):
            lxctl.check_cache_log(cache_log(again_read="stale"))
        with self.assertRaisesRegex(RuntimeError, "still sent dictionary/lookup"):
            lxctl.check_cache_log(cache_log(again_lookup=True))

    def test_the_second_word_misses(self):
        with self.assertRaisesRegex(RuntimeError, "second word"):
            lxctl.check_cache_log(cache_log(second_read="hit"))

    def test_no_resumed_session_fails(self):
        with self.assertRaisesRegex(RuntimeError, "no resumed TLS session"):
            lxctl.check_cache_log(cache_log(resumed=False))

    def test_a_write_to_lexirise_fails(self):
        with self.assertRaisesRegex(RuntimeError, "write to Lexirise"):
            lxctl.check_cache_log(cache_log(write_call=True))

    def test_a_pending_line_is_no_cache_read(self):
        # The card's own unwritten answer (R8) logs "cache pending: ...": not a read, never parsed as one.
        card = open(os.path.join(REPO, "src/lexirise/card/LexiriseCardActivity.cpp"), encoding="utf-8").read()
        self.assertIn('"cache pending: this card\'s answer, not written yet"', card)
        line = "[5000] [INF] [LXLOOK] cache pending: this card's answer, not written yet"
        self.assertIsNone(lxctl.CACHE_READ_LOG.search(line))
        self.assertIsNone(lxctl.CACHE_WRITE_LOG.search(line))

    def test_fewer_than_three_cards_fail(self):
        with self.assertRaisesRegex(RuntimeError, "not 3"):
            lxctl.check_cache_log(cache_log()[:6])

    def test_the_constants_and_log_lines_match_the_firmware(self):
        c = header_constants("src/lexirise/LexiriseConfig.h")
        self.assertEqual(lxctl.TLS_IDLE_CLOSE_S * 1000, c["kTlsIdleCloseMs"])
        self.assertGreater(lxctl.CACHE_IDLE_WAIT_S, lxctl.TLS_IDLE_CLOSE_S)
        card = re.sub(r'"\s+"', "", open(os.path.join(REPO, "src/lexirise/card/LexiriseCardActivity.cpp"),
                                         encoding="utf-8").read())
        self.assertIn('"cache %s in %lu ms (%u of %u hits this boot)"', card)
        self.assertIn('"cache: %u answers %s in %lu ms%s"', card)
        for word in ('"written"', '"not written"', '" (closing)"'):
            self.assertIn(word, card)
        cache = open(os.path.join(REPO, "src/lexirise/lookup/LookupCache.cpp"), encoding="utf-8").read()
        for word in ('"hit"', '"miss"', '"stale"'):
            self.assertIn(word, cache)
        self.assertIn('kCacheLogTag = "LXLOOK"', open(os.path.join(REPO, "src/lexirise/lookup/LookupCache.h"),
                                                      encoding="utf-8").read())
        tls = re.sub(r'"\s+"', "", open(os.path.join(REPO, "src/lexirise/net/TlsConnection.cpp"),
                                        encoding="utf-8").read())
        self.assertIn('"Verified %s (%s, %s, %s) in %lu ms, free heap %u"', tls)
        self.assertIn('"resumed" : "full"', tls)
        self.assertIn('kLogTag = "LXT"', tls)


class FakeCacheHarness:
    """The device's side of cache-smoke: each LONG opens a card that reads the cache (a miss for a new word, a hit
    for one written), a miss looks up; HOME closes it, writing what it looked up."""

    def __init__(self):
        self.stream: list[str] = []
        self.sent: list[str] = []
        self.t = 1000
        self.cached: set[str] = set()
        self.pending: str | None = None
        self.sessions = 0

    def _line(self, fmt, **kw):
        self.t += 10
        self.stream.append(fmt.format(t=self.t, **kw))

    def command(self, cmd, expect=None, timeout=0, seen=None):
        self.sent.append(cmd)
        if cmd.startswith("LONG "):
            word = cmd
            self._line(CACHE_OPEN)
            if word in self.cached:
                self._line(CACHE_READ, outcome="hit", h=1, n=2)
            else:
                self.sessions += 1
                self._line(CACHE_TLS, kind="full" if self.sessions == 1 else "resumed", ms=900)
                self._line(CACHE_READ, outcome="miss", h=0, n=1)
                self._line(CACHE_LOOKUP)
                self.pending = word
        elif cmd == "HOME":
            if self.pending:
                self._line(CACHE_WRITE, how="written")
                self.cached.add(self.pending)
                self.pending = None
            self._line(CACHE_EXIT)
        return "LX:OK"

    def read_line(self, deadline):
        return self.stream.pop(0) if self.stream else None


class CacheSmokeDriver(unittest.TestCase):
    def test_it_drives_three_cards_and_checks_them(self):
        h = FakeCacheHarness()
        r = lxctl.cache_smoke(h, (100, 200), (100, 300), idle_s=0.01, watch_s=0.01)
        self.assertEqual(r["resumed_ms"], 900)
        self.assertEqual(h.sent[0], "AWAKE 1")
        self.assertEqual([c for c in h.sent if c.startswith("LONG")], ["LONG 100 200", "LONG 100 200", "LONG 100 300"])
