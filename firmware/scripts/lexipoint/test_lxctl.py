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
    return {name: int(value) for name, value in re.findall(pattern, text)}


class HostConstantsMatchTheFirmware(unittest.TestCase):
    def test_lexi_wait_outlasts_the_longest_call(self):
        c = header_constants("src/lexirise/LexiriseConfig.h")
        max_call_ms = c["kWifiConnectMs"] + c["kNtpWaitMs"] + 2 * c["kHttpTimeoutMs"] + c["kRequestDeadlineMs"]
        self.assertGreater(lxctl.LEXI_CALL_TIMEOUT_S * 1000, max_call_ms)

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
        elif verb in ("TAP", "BTN", "SYNC", "HOME"):
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
        states = sorted(self.goldens())  # card-smoke's order
        h = FakeCardHarness([s["word"] - s["steps"] for _, s in states], direction)
        with tempfile.TemporaryDirectory() as out:
            h.done = lxctl.card_smoke(h, out, sleep=h.sleep, shot=h.shot)
        return h

    def test_replay_paces_the_buttons_and_runs_every_state(self):
        h = self.replay()
        self.assertEqual(len(h.done), len(list(self.goldens())))
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
