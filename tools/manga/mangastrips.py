#!/usr/bin/env python3
"""Spike: cut manga pages into landscape strips for the X4 Pro XTC reader.

Each page is trimmed, spreads are split right-to-left, the page is scaled to
the landscape width (800), and cut into strips of at most 480 px. Cuts prefer
panel gutters (uniform rows); where no gutter fits, the strip is cut at full
height and the next one overlaps it. Strips are stored rotated to the panel's
portrait 480x800 so CrossPoint's portrait-locked XTC reader shows them
sideways.

Outputs: <name>.xtch (2-bit) or .xtc (1-bit), strips/*.png (what you see,
landscape), sheets/*.png (page with cut lines), cuts.json.
"""

import argparse
import io
import json
import struct
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

LAND_W, LAND_H = 800, 480  # what the reader sees when holding it sideways
PANEL_W, PANEL_H = 480, 800  # what the XTC stores

GUTTER_UNIFORM = 0.985  # share of a row that must be one tone to count as a gutter
LIGHT, DARK = 225, 40  # tone thresholds for "white" / "black" gutter rows
MIN_GUTTER_RUN = 3  # rows; thinner uniform bands are usually art, not gutters
OVERLAP = 72  # px repeated when a strip has to be cut through art
BOX_MARGIN = 6  # px kept clear around a text box
MIN_STRIP = 160  # px; shorter strips waste a page turn
MIN_PROGRESS = 96  # px a strip must advance past the previous start after a blind cut
TRIM_INK = 235  # a row/col with any pixel darker than this is content
TRIM_PAD = 6

IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".webp", ".bmp"}


def load_pages(cbz):
    with zipfile.ZipFile(cbz) as zf:
        names = sorted(n for n in zf.namelist() if Path(n).suffix.lower() in IMAGE_EXTS)
        for n in names:
            yield Path(n).stem, Image.open(io.BytesIO(zf.read(n))).convert("L")


def trim(img):
    """Crop white margins. Returns (image, (dx, dy)) where dx/dy is the crop origin."""
    a = np.asarray(img)
    ink = a < TRIM_INK
    rows, cols = np.where(ink.any(axis=1))[0], np.where(ink.any(axis=0))[0]
    if len(rows) == 0:
        return img, (0, 0)
    x0, x1 = max(cols[0] - TRIM_PAD, 0), min(cols[-1] + TRIM_PAD + 1, a.shape[1])
    y0, y1 = max(rows[0] - TRIM_PAD, 0), min(rows[-1] + TRIM_PAD + 1, a.shape[0])
    return img.crop((x0, y0, x1, y1)), (int(x0), int(y0))


def split_spread(stem, img):
    """Wide images are two-page spreads: right page first (manga order).
    Returns [(name, image, x offset within img)]."""
    w, h = img.size
    if w <= h:
        return [(stem, img, 0)]
    return [(f"{stem}R", img.crop((w // 2, 0, w, h)), w // 2), (f"{stem}L", img.crop((0, 0, w // 2, h)), 0)]


def load_mokuro(path):
    """{image stem: [block, ...]} from a .mokuro file."""
    if not path:
        return {}
    data = json.loads(Path(path).read_text())
    return {Path(p["img_path"]).stem: p["blocks"] for p in data["pages"]}


def map_box(box, crop, xoff, width, scale):
    """An original-image box in the scaled page frame, or None if it belongs to the other half."""
    x0, y0, x1, y1 = box
    x0, x1 = x0 - crop[0] - xoff, x1 - crop[0] - xoff
    y0, y1 = y0 - crop[1], y1 - crop[1]
    if not 0 <= (x0 + x1) / 2 < width:
        return None
    return [x0 * scale, y0 * scale, x1 * scale, y1 * scale]


def to_panel(box, hold):
    """Landscape strip box -> the stored portrait page's pixel frame (what touch reports)."""
    x0, y0, x1, y1 = box
    if hold == "ccw":  # content rotated clockwise: (x, y) -> (H - y, x)
        return [LAND_H - y1, x0, LAND_H - y0, x1]
    return [y0, LAND_W - x1, y1, LAND_W - x0]


def load_words(path):
    return json.loads(Path(path).read_text()) if path else {}


def gutter_rows(a):
    light = (a > LIGHT).mean(axis=1) >= GUTTER_UNIFORM
    dark = (a < DARK).mean(axis=1) >= GUTTER_UNIFORM
    g = light | dark
    # Drop runs thinner than MIN_GUTTER_RUN.
    out = np.zeros_like(g)
    y = 0
    while y < len(g):
        if g[y]:
            e = y
            while e < len(g) and g[e]:
                e += 1
            if e - y >= MIN_GUTTER_RUN:
                out[y:e] = True
            y = e
        else:
            y += 1
    return out


def row_cost(a, gutter):
    """0 for gutters, else how busy the row is (0..1): cutting quiet rows hurts less."""
    grad = np.abs(np.diff(a.astype(np.int16), axis=0)).mean(axis=1)
    grad = np.append(grad, grad[-1]) / 64.0
    return np.where(gutter, 0.0, np.clip(grad, 0.05, 1.0))


def plan_cuts(cost, boxes, height):
    """Cover [0, height) with strips <= LAND_H, cutting only outside text boxes.

    Minimises strip count first, then the busyness of the cut rows. A cut
    through a text box is allowed only when nothing else fits; that strip is
    marked blind and the next one overlaps it.
    Returns [(y0, y1, blind)].
    """
    banned = np.zeros(height + 1, bool)
    # For each row inside a box: the highest box top cut there, so the next strip
    # can restart above it and show every sliced box whole.
    restart = np.full(height + 1, height, int)
    for _, y0, _, y1 in boxes:
        lo, hi = max(int(y0) - BOX_MARGIN, 0), min(int(y1) + BOX_MARGIN, height)
        banned[lo : hi + 1] = True
        restart[lo : hi + 1] = np.minimum(restart[lo : hi + 1], lo)
    STRIP = 100.0  # one strip outweighs any sum of row costs
    best = {}

    def solve(s):
        if s in best:
            return best[s][0]
        if height - s <= LAND_H:
            best[s] = (STRIP, (height, height, False))
            return STRIP
        cand = []
        for e in range(s + MIN_STRIP, s + LAND_H + 1):
            if not banned[e]:
                cand.append((STRIP + cost[e] + solve(e), (e, e, False)))
        if not cand:
            for e in range(s + MIN_STRIP, s + LAND_H + 1):
                nxt = restart[e]
                if nxt >= s + MIN_PROGRESS:
                    cand.append((STRIP * 1.3 + cost[e] + solve(nxt), (e, nxt, True)))
        if not cand:  # a box taller than a strip: plain overlap
            e = s + LAND_H
            cand.append((STRIP * 1.5 + solve(e - OVERLAP), (e, e - OVERLAP, True)))
        best[s] = min(cand, key=lambda c: c[0])
        return best[s][0]

    import sys

    sys.setrecursionlimit(10000)
    solve(0)
    cuts, s = [], 0
    while s < height:
        _, (e, nxt, blind) = best[s]
        cuts.append((int(s), int(e), bool(blind)))
        s = int(nxt)
    return cuts


def quantize(img, bits):
    levels = [0, 255] if bits == 1 else [0, 85, 170, 255]
    pal = Image.new("P", (1, 1))
    flat = []
    for v in levels:
        flat += [v, v, v]
    pal.putpalette(flat + [0] * (768 - len(flat)))
    q = img.convert("RGB").quantize(palette=pal, dither=Image.Dither.FLOYDSTEINBERG)
    idx = np.asarray(q)
    return np.array(levels, dtype=np.uint8)[idx]  # grey values


def encode_page(grey, bits):
    """grey: PANEL_H x PANEL_W array of palette greys. Returns XTG/XTH bytes."""
    h, w = grey.shape
    if bits == 1:
        white = grey >= 128
        data = np.packbits(white, axis=1).tobytes()  # 1 = white, MSB first
        magic = 0x00475458
    else:
        # XTH value: 0 white, 1 dark grey, 2 light grey, 3 black.
        val = np.select([grey == 255, grey == 85, grey == 170], [0, 1, 2], default=3).astype(np.uint8)
        cols = val[:, ::-1].T  # column-major, rightmost column first
        pad = (-h) % 8
        if pad:
            cols = np.pad(cols, ((0, 0), (0, pad)))
        p1 = np.packbits((cols >> 1) & 1, axis=1).tobytes()
        p2 = np.packbits(cols & 1, axis=1).tobytes()
        data = p1 + p2
        magic = 0x00485458
    header = struct.pack("<IHHBBIQ", magic, w, h, 0, 0, len(data), 0)
    return header + data


def write_xtc(path, pages, bits, title):
    META = 256
    n = len(pages)
    meta_off = 0x38
    table_off = meta_off + META
    data_off = table_off + 16 * n
    magic = 0x00435458 if bits == 1 else 0x48435458
    header = struct.pack("<IBBHBBBBIQQQQII", magic, 1, 0, n, 0, 1, 0, 0, 1, meta_off, table_off, data_off, 0, 0, 0)
    meta = bytearray(META)
    t = title.encode("utf-8")[:127]
    meta[0 : len(t)] = t
    table, off = bytearray(), data_off
    for p in pages:
        table += struct.pack("<QIHH", off, len(p), PANEL_W, PANEL_H)
        off += len(p)
    with open(path, "wb") as f:
        f.write(header + meta + table + b"".join(pages))


def decode_xth(page):
    """Round-trip check using the firmware's getPixelValue()."""
    _, w, h, _, _, size, _ = struct.unpack("<IHHBBIQ", page[:22])
    data = page[22:]
    col_bytes = (h + 7) // 8
    plane = col_bytes * w
    p1 = np.unpackbits(np.frombuffer(data[:plane], np.uint8).reshape(w, col_bytes), axis=1)[:, :h]
    p2 = np.unpackbits(np.frombuffer(data[plane:], np.uint8).reshape(w, col_bytes), axis=1)[:, :h]
    val = (p1 << 1) | p2  # [col_from_right, y]
    val = val[::-1].T
    return np.array([255, 85, 170, 0], np.uint8)[val]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cbz", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--bits", type=int, choices=[1, 2], default=2)
    ap.add_argument("--pages", help="page range, e.g. 5-20 (1-based, after sorting)")
    ap.add_argument("--mokuro", type=Path, help=".mokuro OCR file for this volume (text-aware cuts)")
    ap.add_argument("--words", type=Path, help="wordboxes.py output (sidecar + tap overlay)")
    ap.add_argument("--hold", choices=["ccw", "cw"], default="ccw", help="which way the device is turned")
    args = ap.parse_args()

    out = args.out
    (out / "strips").mkdir(parents=True, exist_ok=True)
    (out / "sheets").mkdir(exist_ok=True)

    pages = list(load_pages(args.cbz))
    if args.pages:
        a, b = (int(x) for x in args.pages.split("-"))
        pages = pages[a - 1 : b]

    ocr = load_mokuro(args.mokuro)
    words = load_words(args.words)
    sidecar = []  # per XTC page: the words fully shown on it
    encoded, report = [], []
    for stem, raw in pages:
        trimmed, crop = trim(raw)
        for sub, img, xoff in split_spread(stem, trimmed):
            w, h = img.size
            scale = LAND_W / w
            scaled = img.resize((LAND_W, round(h * scale)), Image.Resampling.LANCZOS)
            a = np.asarray(scaled)
            gut = gutter_rows(a)
            boxes = [m for b in ocr.get(stem, []) if (m := map_box(b["box"], crop, xoff, w, scale))]
            page_words = []
            for wd in words.get(stem, []):
                wb = [map_box(b, crop, xoff, w, scale) for b in wd["boxes"]]
                if all(wb):
                    page_words.append({**wd, "boxes": wb})
            cuts = plan_cuts(row_cost(a, gut), boxes, a.shape[0])
            report.append({"page": sub, "height": a.shape[0], "boxes": len(boxes), "cuts": cuts})

            sheet = scaled.convert("RGB")
            d = ImageDraw.Draw(sheet)
            for y in np.where(gut)[0]:
                d.line([(0, y), (12, y)], fill=(0, 160, 0))
            for bx in boxes:
                d.rectangle(bx, outline=(255, 140, 0), width=2)
            for i, (y0, y1, blind) in enumerate(cuts):
                strip = Image.new("L", (LAND_W, LAND_H), 255)
                part = scaled.crop((0, y0, LAND_W, y1))
                strip.paste(part, (0, (LAND_H - part.height) // 2))
                grey = quantize(strip, args.bits)
                Image.fromarray(grey).save(out / "strips" / f"{sub}_{i}.png")
                dy = (LAND_H - part.height) // 2 - y0
                shown = []
                for wd in page_words:
                    if all(y0 <= b[1] and b[3] <= y1 for b in wd["boxes"]):
                        lb = [[b[0], b[1] + dy, b[2], b[3] + dy] for b in wd["boxes"]]
                        shown.append({**wd, "boxes": [[round(v) for v in to_panel(b, args.hold)] for b in lb], "land": lb})
                sidecar.append({"strip": f"{sub}_{i}", "words": [{k: v for k, v in x.items() if k != "land"} for x in shown]})
                if shown:
                    ov = Image.fromarray(grey).convert("RGB")
                    od = ImageDraw.Draw(ov)
                    for k, x in enumerate(shown):
                        col = [(230, 60, 60), (40, 120, 230), (30, 160, 60), (200, 120, 0)][k % 4]
                        for b in x["land"]:
                            od.rectangle(b, outline=col, width=2)
                    (out / "taps").mkdir(exist_ok=True)
                    ov.save(out / "taps" / f"{sub}_{i}.png")
                rot = Image.fromarray(grey).transpose(
                    Image.Transpose.ROTATE_270 if args.hold == "ccw" else Image.Transpose.ROTATE_90
                )
                page = encode_page(np.asarray(rot), args.bits)
                if args.bits == 2:
                    assert (decode_xth(page) == np.asarray(rot)).all(), "XTH round-trip failed"
                encoded.append(page)
                d.line([(0, y1), (LAND_W, y1)], fill=(220, 0, 0) if blind else (0, 90, 255), width=3)
                d.text((20, y0 + 6), f"{i}{' blind' if blind else ''}", fill=(220, 0, 0))
            sheet.save(out / "sheets" / f"{sub}.png")

    ext = "xtc" if args.bits == 1 else "xtch"
    title = args.cbz.stem
    write_xtc(out / f"{title}.{ext}", encoded, args.bits, title)
    (out / "cuts.json").write_text(json.dumps(report, indent=1))
    if words:
        (out / f"{title}.lexi.json").write_text(json.dumps({"hold": args.hold, "pages": sidecar}, ensure_ascii=False))
    blind = sum(c[2] for r in report for c in r["cuts"])
    print(f"{len(report)} pages -> {len(encoded)} strips ({blind} blind cuts), {out / (title + '.' + ext)}")


if __name__ == "__main__":
    main()
