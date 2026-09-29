"""The reader's fonts, read the way the firmware reads them, for mockups drawn in device pixels.

Two sources:
  BuiltinFont  a built-in header (firmware/lib/EpdFont/builtinFonts/*.h): 1-bit glyph bitmaps, 12.4 advances,
               sparse kerning, ligature pairs.
  CpFont       an SD-card .cpfont (version 4, firmware/lib/EpdFont/SdCardFont.cpp): the reader's CJK family,
               2-bit glyphs (text anti-aliasing), style 0 only, no kerning (the firmware never kerns a CJK
               breakable codepoint, EpdFont::getKerning).

Drawing follows GfxRenderer::drawText: `y` is the line's top, the baseline is y + ascender, each glyph step is
the previous advance plus the kern, snapped to a pixel (fp4 "differential rounding"); getTextWidth is the ink
bounds' width (EpdFont::getTextBounds). Pure Python and Pillow; nothing here talks to a device.
"""

from __future__ import annotations

import re
import struct
from pathlib import Path

WHITE, LIGHT, DARK, BLACK = 255, 170, 85, 0  # the four levels an anti-aliased glyph can leave


def fp_to_px(fp: int) -> int:
    return (fp + 8) >> 4


def _c_array(src: str, name: str) -> str:
    m = re.search(r"static const [A-Za-z0-9_]+ " + re.escape(name) + r"\[[0-9]*\] = \{(.*?)\n\};", src, re.S)
    if not m:
        raise ValueError(f"array {name} not found")
    return m.group(1)


def _ints(body: str) -> list[int]:
    body = re.sub(r"//[^\n]*", "", body)
    return [int(t, 0) for t in re.findall(r"-?0x[0-9A-Fa-f]+|-?\d+", body)]


class _Font:
    ascender: int
    advance_y: int
    is2bit: bool

    def glyph(self, cp: int):  # -> (w, h, adv_fp, left, top, data) or None
        raise NotImplementedError

    def kern(self, left: int, right: int) -> int:
        return 0

    def ligature(self, left: int, right: int) -> int | None:
        return None

    def _codepoints(self, text: str) -> list[int]:
        cps = [ord(c) for c in text]
        out: list[int] = []
        for cp in cps:
            if out:
                lig = self.ligature(out[-1], cp)
                if lig is not None and self.glyph(lig):
                    out[-1] = lig
                    continue
            out.append(cp)
        return out

    def layout(self, text: str, x: int = 0) -> list[tuple[int, int]]:
        """(codepoint, cursor x) for each drawn glyph, as drawText places them."""
        out = []
        cursor = x
        prev_cp = 0
        prev_adv = 0
        for cp in self._codepoints(text):
            g = self.glyph(cp)
            if not g:
                cursor += fp_to_px(prev_adv)
                prev_cp, prev_adv = 0, 0
                continue
            if prev_cp:
                cursor += fp_to_px(prev_adv + self.kern(prev_cp, cp))
            out.append((cp, cursor))
            prev_cp, prev_adv = cp, g[2]
        return out

    def width(self, text: str) -> int:
        """GfxRenderer::getTextWidth: the ink bounds, from the cursor's start."""
        min_x = max_x = 0
        for cp, cx in self.layout(text):
            w, h, adv, left, top, _ = self.glyph(cp)
            min_x = min(min_x, cx + left)
            max_x = max(max_x, cx + left + w)
        return max_x - min_x

    def advance(self, text: str) -> int:
        """The cursor's travel over the text (the next glyph's start)."""
        placed = self.layout(text)
        if not placed:
            return 0
        cp, cx = placed[-1]
        return cx + fp_to_px(self.glyph(cp)[2])

    def draw(self, img, x: int, y: int, text: str, black: bool = True, aa: bool = True) -> None:
        """Draw with the line's top at y (baseline y + ascender). `img` is an 'L' Pillow image."""
        px = img.load()
        W, H = img.size
        base = y + self.ascender
        for cp, cx in self.layout(text, x):
            w, h, adv, left, top, data = self.glyph(cp)
            pos = 0
            for gy in range(h):
                sy = base - top + gy
                for gx in range(w):
                    sx = cx + left + gx
                    if self.is2bit:
                        raw = (data[pos >> 2] >> ((3 - (pos & 3)) * 2)) & 3
                        level = (WHITE, LIGHT, DARK, BLACK)[raw] if aa else (WHITE if raw == 0 else BLACK)
                    else:
                        level = BLACK if (data[pos >> 3] >> (7 - (pos & 7))) & 1 else WHITE
                    pos += 1
                    if level == WHITE or not (0 <= sx < W and 0 <= sy < H):
                        continue
                    if black:
                        px[sx, sy] = min(px[sx, sy], level)
                    else:
                        px[sx, sy] = 255 if level <= DARK else px[sx, sy]

    def ink_box(self, cp: int) -> tuple[int, int, int, int]:
        """A glyph's ink relative to its cursor and baseline: (left, top_above_baseline, width, height)."""
        w, h, adv, left, top, _ = self.glyph(cp)
        return left, top, w, h


class BuiltinFont(_Font):
    def __init__(self, header: Path):
        src = header.read_text()
        name = header.stem
        self.name = name
        self.bitmap = bytes(_ints(_c_array(src, name + "Bitmaps")))
        g = _ints(_c_array(src, name + "Glyphs"))
        self.glyphs = [tuple(g[i:i + 7]) for i in range(0, len(g), 7)]
        iv = _ints(_c_array(src, name + "Intervals"))
        self.intervals = [tuple(iv[i:i + 3]) for i in range(0, len(iv), 3)]
        m = re.search(r"static const EpdFontData " + name + r" = \{(.*?)\n\};", src, re.S)
        fields = [f.strip() for f in re.sub(r"//[^\n]*", "", m.group(1)).split(",")]
        self.advance_y = int(fields[4])
        self.ascender = int(fields[5])
        self.descender = int(fields[6])
        self.is2bit = fields[7] == "true"

        def arr(suffix):
            try:
                return _ints(_c_array(src, name + suffix))
            except ValueError:
                return []

        self.kl_cp, self.kl_id = arr("KernLeftCodepoints"), arr("KernLeftClassIds")
        self.kr_cp, self.kr_id = arr("KernRightCodepoints"), arr("KernRightClassIds")
        self.k_rows, self.k_cols, self.k_vals = arr("KernRowOffsets"), arr("KernSparseCols"), arr("KernSparseValues")
        lig = arr("LigaturePairs")
        self.ligs = {lig[i]: lig[i + 1] for i in range(0, len(lig), 2)}
        self._cache: dict[int, tuple | None] = {}

    def glyph(self, cp: int):
        if cp in self._cache:
            return self._cache[cp]
        found = None
        for first, last, off in self.intervals:
            if first <= cp <= last:
                w, h, adv, left, top, length, doff = self.glyphs[off + cp - first]
                found = (w, h, adv, left, top, self.bitmap[doff:doff + length])
                break
        self._cache[cp] = found
        return found

    def _class(self, cps, ids, cp):
        import bisect
        i = bisect.bisect_left(cps, cp)
        return ids[i] if i < len(cps) and cps[i] == cp else 0

    def kern(self, left: int, right: int) -> int:
        if not self.k_rows:
            return 0
        lc = self._class(self.kl_cp, self.kl_id, left)
        rc = self._class(self.kr_cp, self.kr_id, right)
        if not lc or not rc:
            return 0
        for i in range(self.k_rows[lc - 1], self.k_rows[lc]):
            if self.k_cols[i] == rc - 1:
                return self.k_vals[i]
            if self.k_cols[i] > rc - 1:
                break
        return 0

    def ligature(self, left: int, right: int):
        return self.ligs.get((left << 16) | right)


class CpFont(_Font):
    def __init__(self, path: Path):
        self.name = path.stem
        b = path.read_bytes()
        if b[:8] != b"CPFONT\0\0" or struct.unpack_from("<H", b, 8)[0] != 4:
            raise ValueError(f"{path}: not a version 4 .cpfont")
        self.is2bit = bool(struct.unpack_from("<H", b, 10)[0] & 1)
        t = b[32:64]  # the first style's TOC entry
        ic, gc = struct.unpack_from("<II", t, 4)
        self.advance_y = t[12]
        self.ascender, self.descender = struct.unpack_from("<hh", t, 13)
        kl, kr = struct.unpack_from("<HH", t, 17)
        klc, krc, lig = t[21], t[22], t[23]
        off = struct.unpack_from("<I", t, 24)[0]
        self.intervals = [struct.unpack_from("<III", b, off + 12 * i) for i in range(ic)]
        goff = off + 12 * ic
        self.glyph_off = goff
        self.bitmap_off = goff + 16 * gc + 3 * kl + 3 * kr + klc * krc + 8 * lig
        self.b = b
        self._cache: dict[int, tuple | None] = {}

    def glyph(self, cp: int):
        if cp in self._cache:
            return self._cache[cp]
        found = None
        for first, last, off in self.intervals:
            if first <= cp <= last:
                w, h, adv, left, top, length, doff = struct.unpack_from("<BBHhhHxxI", self.b,
                                                                         self.glyph_off + 16 * (off + cp - first))
                start = self.bitmap_off + doff
                found = (w, h, adv, left, top, self.b[start:start + length])
                break
        self._cache[cp] = found
        return found
