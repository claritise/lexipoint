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
        h = lxctl.Harness(FakeSerial(b"LX:ERR built without LEXIRISE\n"))
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

LEXIRISE_FLAG = re.compile(r"-D\s*LEXIRISE=1\b")


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
        h, n = self.run_smoke(12)
        self.assertEqual(n, 12)
        self.assertTrue(h.left)
        self.assertEqual(h.sent[0], "LEXI SETTINGS")
        self.assertTrue(any(c.startswith("SWIPE") for c in h.sent))

    def test_no_rows_or_too_many_fail(self):
        for rows in (None, 3, 13):
            with self.assertRaises(RuntimeError):
                self.run_smoke(rows)

    def test_commands_match_the_device_grammar(self):
        usages = device_usages()
        self.assertRegex("LEXI SETTINGS", usages["LEXI SETTINGS"])
        self.assertRegex(f"SWIPE {lxctl.EDGE_INSET} 400 240 400", usages["SWIPE"])

    def test_row_bounds_match_the_screen(self):
        """SETTINGS_ROWS_MAX is every settings_screen::Row; MIN the Account group (visibleRows, Lexirise off)."""
        with open(os.path.join(REPO, "src/lexirise/settings/SettingsScreen.h"), encoding="utf-8") as f:
            body = re.search(r"enum class Row : uint8_t \{(.*?)\};", f.read(), re.S).group(1)
        rows = re.findall(r"^\s*(\w+),", body, re.M)
        self.assertEqual(lxctl.SETTINGS_ROWS_MAX, len(rows))
        self.assertEqual(rows[:lxctl.SETTINGS_ROWS_MIN], ["Lookups", "ApiKey", "Account", "TestConnection"])


class ReleaseVersioning(unittest.TestCase):
    """firmware-base.md §6: the fork's releases are <upstream>-lexi.<n>, the X4 Pro only."""

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

    def test_release_workflow_builds_the_x4pro_only(self):
        with open(os.path.join(REPO, ".github/workflows/release.yml"), encoding="utf-8") as f:
            text = f.read()
        self.assertEqual(re.findall(r"^\s+device: (\S+)", text, re.M), ["x4pro"])
        self.assertIn("python3 scripts/lexipoint/release_tag.py check", text)

    def test_ota_reads_the_forks_releases(self):
        with open(os.path.join(REPO, "src/lexirise/LexiriseConfig.h"), encoding="utf-8") as f:
            self.assertIn("api.github.com/repos/claritise/crosspoint-reader/releases/latest", f.read())


class X4ProEnvsBuildLexirise(unittest.TestCase):
    def test_every_x4pro_env_has_lexirise(self):
        with open(os.path.join(REPO, "platformio.ini")) as f:
            cfg = load_ini(f.read())
        x4pro = [s for s in cfg.sections() if s.startswith("env:x4pro")]
        self.assertGreaterEqual(len(x4pro), 3)  # dev, release, release candidate
        for env in x4pro:
            self.assertRegex(resolved_build_flags(env, cfg), LEXIRISE_FLAG, env)


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


if __name__ == "__main__":
    unittest.main()
