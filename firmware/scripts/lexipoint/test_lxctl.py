"""Host tests for the Lexipoint dev harness tooling. Run: `python3 -m unittest discover -s scripts/lexipoint`.

- Frame decoding: the portrait PNG rotation matches GfxRenderer::rotateCoordinates.
- Reply handling: log lines and stale replies are skipped, errors raised, frames CRC-checked.
- Build safety: no release environment in platformio.ini ever compiles the dev harness.
"""

import configparser
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
