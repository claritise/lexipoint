#!/usr/bin/env python3
"""Spike: word boxes for a mokuro-OCR'd volume, segmented by Lexirise.

One analyze/text request per page: the page's text blocks are joined with
newlines (lines inside a block are one run of text). Occurrences come back
with UTF-16 offsets; each is mapped to characters, and each character to an
even slice of its OCR line box. A word that wraps gets one box per line.

Output: {image stem: [{block, sentence, offset, word, lemma, reading, entryId,
boxes}]}: `sentence` is the word's text block (its bubble), `offset` the
word's UTF-16 start in it, boxes in the original image's pixel coordinates. Responses are cached (local only; they
are account data and never go in a repo).
"""

import argparse
import json
import time
import urllib.request
from pathlib import Path

API = "https://api.lexirise.app/v1/analyze/text"
KEY_FILE = Path.home() / ".lexirise_key"
REQUEST_GAP_S = 0.5  # stay far below 1200 req/h


def utf16_len(ch):
    return 2 if ord(ch) > 0xFFFF else 1


def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def char_cells(coords, n, vertical):
    """Split a line's quad into n character cells along its long axis (tilt-aware).

    Returns each cell's axis-aligned bbox, in reading order (top->bottom for
    vertical text, left->right for horizontal)."""
    p0, p1, p2, p3 = coords
    d = lambda a, b: ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5
    # Long axis runs a0->a1 on one side and b0->b1 on the other.
    if d(p0, p1) >= d(p0, p3):
        a0, a1, b0, b1 = p0, p1, p3, p2
    else:
        a0, a1, b0, b1 = p0, p3, p1, p2
    axis = 1 if vertical else 0
    if a0[axis] > a1[axis]:  # make t=0 the start of the line
        a0, a1, b0, b1 = a1, a0, b1, b0
    cells = []
    for i in range(n):
        q = [lerp(a0, a1, i / n), lerp(a0, a1, (i + 1) / n), lerp(b0, b1, (i + 1) / n), lerp(b0, b1, i / n)]
        xs, ys = [v[0] for v in q], [v[1] for v in q]
        cells.append((min(xs), min(ys), max(xs), max(ys)))
    return cells


def page_text(blocks):
    """Returns (text, chars) where chars[utf16 offset] = (block, line id, bbox) or None."""
    text, chars = [], []
    for bi, b in enumerate(blocks):
        if bi:
            text.append("\n")
            chars.append(None)
        for li, (line, coords) in enumerate(zip(b["lines"], b["lines_coords"])):
            for ch, box in zip(line, char_cells(coords, len(line), b["vertical"])):
                text.append(ch)
                for _ in range(utf16_len(ch)):
                    chars.append((bi, (bi, li), box))
    return "".join(text), chars


def analyze(text, lang, key):
    req = urllib.request.Request(
        API,
        data=json.dumps({"text": text, "language": lang}).encode(),
        headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def union_by_line(cells):
    """One box per OCR line the word touches: cells arrive as (line id, bbox)."""
    boxes, last = [], None
    for line, c in cells:
        if line == last:
            b = boxes[-1]
            boxes[-1] = [min(b[0], c[0]), min(b[1], c[1]), max(b[2], c[2]), max(b[3], c[3])]
        else:
            boxes.append(list(c))
        last = line
    return [[round(v, 1) for v in b] for b in boxes]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mokuro", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--cache", type=Path, required=True)
    ap.add_argument("--lang", default="ja")
    ap.add_argument("--pages", help="only these image stems' range, e.g. 1-30 (1-based)")
    args = ap.parse_args()

    key = KEY_FILE.read_text().strip()
    args.cache.mkdir(parents=True, exist_ok=True)
    pages = json.loads(args.mokuro.read_text())["pages"]
    if args.pages:
        a, b = (int(x) for x in args.pages.split("-"))
        pages = pages[a - 1 : b]

    result, calls, words = {}, 0, 0
    for p in pages:
        stem = Path(p["img_path"]).stem
        text, chars = page_text(p["blocks"])
        if not text.strip():
            continue
        cached = args.cache / f"{stem}.json"
        if cached.exists():
            resp = json.loads(cached.read_text())
        else:
            resp = analyze(text, args.lang, key)
            cached.write_text(json.dumps(resp, ensure_ascii=False))
            calls += 1
            time.sleep(REQUEST_GAP_S)
        block_text = ["".join(b["lines"]) for b in p["blocks"]]
        block_start = {}  # block -> UTF-16 offset of its first character in the page text
        for off, c in enumerate(chars):
            if c and c[0] not in block_start:
                block_start[c[0]] = off
        out = []
        for o in resp.get("occurrences", []):
            if not o.get("isWordLike", True):
                continue
            cells = [c for c in chars[o["charStart"] : o["charEnd"]] if c]
            if not cells:
                continue
            out.append(
                {
                    "block": cells[0][0],
                    "sentence": block_text[cells[0][0]],
                    "offset": o["charStart"] - block_start[cells[0][0]],
                    "word": o.get("word"),
                    "lemma": o.get("lemma") or o.get("word"),
                    "reading": o.get("transliteration"),
                    "entryId": o.get("lemmaEntryId") or o.get("entryId"),
                    "boxes": union_by_line([(c[1], c[2]) for c in cells]),
                }
            )
        result[stem] = out
        words += len(out)

    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=1))
    print(f"{len(result)} pages, {words} words, {calls} API calls -> {args.out}")


if __name__ == "__main__":
    main()
