#!/usr/bin/env python3
"""Card conformance pre-check (popup-ui.md §1.1, P4): every reference state, side by side.

For each state, three panels at the device's 480×800:
  device  the card layout (src/lexirise/card) with the device fonts' metrics, rasterized here
  ref     reference/card-reference.html in that state, rendered by headless Chrome and scaled to the panel
          (480/340, bottom-anchored: the ~10 px of extra height goes to the top)
  overlay the two at 50%

Box positions come from the real layout and metrics. Glyph shapes don't: they use the fonts' TTF/OTF
sources (UI) and the Mac's Mincho/Songti (CJK), a stand-in for the device's NotoSerifCJK (sanctioned
deviation 2). The binding check is still the on-device screenshot pair (01-build-order.md, the Design
conformance gate).

  python3 scripts/lexipoint/cardshots.py --out <dir> [--docs ~/Projects/lexipoint/docs/v0.1] [--only ja-card]

Needs: the host tool (cmake --build build/test --target LexiriseCardRender), Pillow, Google Chrome.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

from cardstates import render_args, states

REPO = pathlib.Path(__file__).resolve().parents[2]
TOOL = REPO / "build/test/lexirise_card/LexiriseCardRender"
FONT_SRC = REPO / "lib/EpdFont/builtinFonts/source"
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
MINCHO = "/System/Library/Fonts/ヒラギノ明朝 ProN.ttc"
SONGTI = "/System/Library/Fonts/Supplemental/Songti.ttc"

W, H = 480, 800
SCALE = 480 / 340  # popup-ui.md §1.1

# The device fonts' pixel sizes (150 dpi): the preview draws their glyphs at the same size.
UI_FONTS = {
    "UiSmall": (FONT_SRC / "NotoSans/NotoSans-Regular.ttf", 8 * 150 / 72),
    "Ui": (FONT_SRC / "Ubuntu/Ubuntu-Regular.ttf", 10 * 150 / 72),
    "UiBold": (FONT_SRC / "Ubuntu/Ubuntu-Bold.ttf", 10 * 150 / 72),
}
READER_PT = {"ReaderSmall": 8, "ReaderMedium": 10, "ReaderLarge": 18, "Page": 12}


# Put card-reference.html in a state and show only its screen, scaled and bottom-anchored to 480×800.
REF_SCRIPT = r"""
<script>
(function(){
  const st = JSON.parse(decodeURIComponent(location.hash.slice(1)));
  const click = q => { const e = document.querySelector(q); if (e) e.click(); };
  try { localStorage.removeItem('lexipoint.reading'); } catch (e) {}
  s.rd = 'kana';
  click('#lang button[data-v=' + st.lang + ']');
  click('#pos button[data-v=' + (st.pos === 'low' ? 'lo' : 'hi') + ']');
  click('#view button[data-v=' + (st.view === 'expanded' ? 'd' : 'c') + ']');
  const start = B[st.lang].start;
  for (let i = start; i < st.word; i++) click('#sr');
  for (let i = start; i > st.word; i--) click('#sl');
  if (st.view === 'expanded') { s.tab = st.tab; draw(); }
  for (const a of st.actions) {
    if (a === 'reading') click('#rd');
    else if (a.startsWith('level:')) click('[data-lv="' + a.slice(6) + '"]');
  }
  clearTimeout(toast.t);
  const scr = document.getElementById('scr');
  document.body.style.cssText = 'margin:0;padding:0;background:#fff;overflow:hidden';
  document.body.replaceChildren(scr);
  scr.style.cssText += ';border:0;position:fixed;left:0;transform-origin:0 0;transform:scale(%SCALE%);' +
    'top:%TOP%px';
})();
</script>
"""


def render_reference(docs: pathlib.Path, state: dict, out: pathlib.Path, tmp: pathlib.Path) -> None:
    src = (docs / "reference/card-reference.html").read_text()
    top = H - 560 * SCALE
    page = src.replace("</body>", REF_SCRIPT.replace("%SCALE%", repr(SCALE)).replace("%TOP%", repr(top)) + "</body>")
    html = tmp / "ref.html"
    html.write_text(page)
    url = html.as_uri() + "#" + json.dumps(state)
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", f"--window-size={W},{H}",
                    "--force-device-scale-factor=1", "--virtual-time-budget=400", f"--screenshot={out}", url],
                   check=True, capture_output=True)


def font_for(name: str, lang: str) -> ImageFont.FreeTypeFont:
    if name in UI_FONTS:
        path, px = UI_FONTS[name]
        return ImageFont.truetype(str(path), size=px)
    px = READER_PT[name] * 150 / 72
    return ImageFont.truetype(MINCHO if lang == "ja" else SONGTI, size=px)


def draw_list(img: Image.Image, commands: list[dict], lang: str) -> None:
    d = ImageDraw.Draw(img)
    for c in commands:
        ink = 0 if c["black"] else 255
        x, y, w, h = c["x"], c["y"], c["w"], c["h"]
        k = c["kind"]
        if k == "fill":
            if w > 0 and h > 0:
                d.rectangle([x, y, x + w - 1, y + h - 1], fill=ink)
        elif k == "frame":
            for i in range(c["t"]):
                d.rectangle([x + i, y + i, x + w - 1 - i, y + h - 1 - i], outline=ink)
        elif k == "rframe":
            d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=c["r"], outline=ink, width=c["t"])
        elif k == "line":
            d.rectangle([x, y, x + w - 1, y + c["t"] - 1], fill=ink)
        elif k == "text":  # runs (TextRuns.h): CJK inside UI text is set in the reader family
            for r in c["runs"]:
                d.text((r["x"], r["y"] + r["asc"]), r["text"], font=font_for(r["font"], lang), fill=ink, anchor="ls")
        elif k == "ink":  # ShapeGeometry.h, expanded by the host tool
            for poly in c["polygons"]:
                d.polygon([tuple(p) for p in poly], fill=ink)
            for x0, y0, x1, y1 in c["strokes"]:
                d.line([x0, y0, x1, y1], fill=ink, width=c["sw"])
            for dx, dy, dw, dh in c["dots"]:
                d.rectangle([dx, dy, dx + dw - 1, dy + dh - 1], fill=ink)


def render_device(state: dict, out: pathlib.Path, tmp: pathlib.Path) -> None:
    js = tmp / "state.json"
    subprocess.run([str(TOOL), str(js), *render_args(state)], check=True)
    lists = json.loads(js.read_text())
    img = Image.new("L", (W, H), 255)
    draw_list(img, lists["page"], state["lang"])
    draw_list(img, lists["card"], state["lang"])
    img.point(lambda v: 0 if v < 128 else 255).convert("1").save(out)  # pure black and white, as the panel


def compose(device: pathlib.Path, ref: pathlib.Path, out: pathlib.Path, title: str) -> None:
    a = Image.open(device).convert("RGB")
    b = Image.open(ref).convert("RGB").resize((W, H))
    b = b.point(lambda v: 0 if v < 160 else 255)  # the reference in black and white too
    overlay = Image.blend(a, b, 0.5)
    label_h = 28
    sheet = Image.new("RGB", (W * 3 + 20, H + label_h), (255, 255, 255))
    d = ImageDraw.Draw(sheet)
    f = ImageFont.truetype(str(UI_FONTS["Ui"][0]), 16)
    for i, (img, name) in enumerate(((a, "device layout"), (b, "reference"), (overlay, "overlay 50%"))):
        sheet.paste(img, (i * (W + 10), label_h))
        d.rectangle([i * (W + 10), label_h, i * (W + 10) + W - 1, label_h + H - 1], outline=(200, 0, 0))
        d.text((i * (W + 10) + 4, 5), f"{title} · {name}", font=f, fill=(0, 0, 0))
    sheet.save(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    ap.add_argument("--docs", type=pathlib.Path, default=pathlib.Path.home() / "Projects/lexipoint/docs/v0.1")
    ap.add_argument("--only", help="states whose name contains this")
    args = ap.parse_args()
    if not TOOL.exists():
        print(f"build the host tool first: cmake --build build/test --target LexiriseCardRender", file=sys.stderr)
        return 1
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as t:
        tmp = pathlib.Path(t)
        for st in states():
            if args.only and args.only not in st["name"]:
                continue
            dev, ref = tmp / "dev.png", tmp / "ref.png"
            render_device(st, dev, tmp)
            render_reference(args.docs, st, ref, tmp)
            compose(dev, ref, args.out / f"{st['name']}.png", st["name"])
            shutil.copy(dev, args.out / f"{st['name']}.device.png")
            print(st["name"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
