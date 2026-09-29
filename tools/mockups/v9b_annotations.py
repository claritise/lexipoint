#!/usr/bin/env python3
"""Build docs/v0.2/reference/v9b-annotations.html: the V9b mockups (page-annotations.md A5, A10, A11) for
claritise's sign-off (parked, not signed off, 2026-09-29: page-annotations.md §2 "Maybe later"; kept as the record),
drawn in the X4 Pro's 480x800 device pixels with the firmware's own fonts.

  python3 tools/mockups/v9b_annotations.py [--out docs/v0.2/reference/v9b-annotations.html]

Built on tools/mockups/v9a_annotations.py (the reader's page, the marks, the status bar, the list screens) and needs
what it needs (Pillow, the built-in font headers, the reader's CJK family in the gitignored sd-card/ folder). The
card screens are the host layout's own display lists (firmware/test/lexirise_card/golden/*.json, the approved card
pinned pixel for pixel) rasterized here with the device fonts. The list, popup and dialog screens are approximate
(FreeInkUI's list and popups, not re-derived pixel for pixel). The book text, the words, their readings, meanings and
states are made up here (nothing came from Lexirise).
"""

from __future__ import annotations

import argparse
import html
import json
from pathlib import Path

from PIL import ImageDraw

import v9a_annotations as v
from v9a_annotations import (BLACK, CJK8, CJK12, CJK18, CSS, INSET_L, INSET_T, JS, SMALL, UI10, UI12B, W, H,
                             Screen, blank, draw_header, draw_rows, fill, frame, parse, page_screen, png)
from epdfont import BuiltinFont, CpFont

REPO = v.REPO
GOLDEN = REPO / "firmware/test/lexirise_card/golden"
UI12 = BuiltinFont(v.BUILTIN / "ubuntu_12_regular.h")  # UI_12: the popups' text
CJK10 = CpFont(v.CJK / "NotoSerifCJK_10.cpfont")  # a CJK label in a UI_10 row: the SD family at 10 pt

# A5's option: an unsaved word within the language's most common words isn't marked (grammar words included);
# saved words keep their marks. In the made-up page, class C is "within the 3,000 most common".
v.RULES["a5"] = {"N": "new", "C": None, "F": None, "L": "learn", "K": None, "P": None}


# ---- The card, from the host layout's goldens ----
GOLDEN_FONTS = {"Page": CJK12, "ReaderSmall": CJK8, "ReaderLarge": CJK18, "UiSmall": SMALL, "Ui": UI10,
                "UiBold": v.UI10B}


def draw_ops(img, ops, drop_box=None):
    """Rasterize a golden display list; ops entirely inside drop_box (x0, y0, x1, y1) are left out."""
    d = ImageDraw.Draw(img)

    def inside(o):
        if not drop_box:
            return False
        x0, y0, x1, y1 = drop_box
        return x0 <= o["x"] < x1 and y0 <= o["y"] < y1

    for o in ops:
        if inside(o):
            continue
        k, col = o["kind"], (BLACK if o["black"] else 255)
        if k == "fill":
            if o["w"] and o["h"]:
                d.rectangle([o["x"], o["y"], o["x"] + o["w"] - 1, o["y"] + o["h"] - 1], fill=col)
        elif k == "frame":
            for i in range(o["t"]):
                d.rectangle([o["x"] + i, o["y"] + i, o["x"] + o["w"] - 1 - i, o["y"] + o["h"] - 1 - i], outline=col)
        elif k == "rframe":
            d.rounded_rectangle([o["x"], o["y"], o["x"] + o["w"] - 1, o["y"] + o["h"] - 1], radius=o["r"],
                                outline=col, width=o["t"])
        elif k == "text":
            for r in o["runs"]:
                GOLDEN_FONTS[r["font"]].draw(img, r["x"], r["y"], r["text"], black=o["black"])
        elif k == "ink":
            for poly in o["polygons"]:
                d.polygon([tuple(p) for p in poly], fill=col)
            for x0, y0, x1, y1 in o["strokes"]:
                d.line([x0, y0, x1, y1], fill=col, width=o["sw"])
            for x, y, w, h in o["dots"]:
                d.rectangle([x, y, x + w - 1, y + h - 1], fill=col)


def golden_screen(name: str, drop_box=None) -> Screen:
    g = json.loads((GOLDEN / f"{name}.json").read_text())
    img = blank()
    draw_ops(img, g["page"])
    draw_ops(img, g["card"], drop_box)
    return Screen(img)


# ---- Popups (BaseTheme::drawPopup with Lyra's metrics; the dialog approximate) ----
def popup(img, message: str, progress: int | None = None):
    mx, my, t, r = 16, 12, 2, 6
    y = int(H * 0.165)
    tw, th = UI12.width(message), UI12.advance_y
    w, h = tw + 2 * mx, th + 2 * my
    x = (W - w) // 2
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([x - t, y - t, x + w + t - 1, y + h + t - 1], radius=r + t, fill=BLACK)
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r, fill=255)
    UI12.draw(img, x + (w - tw) // 2, y + my - 2, message)
    if progress:
        bw = w - 2 * mx
        by = y + h - my // 2 - 2 - 1
        fill(img, x + mx, by, bw * progress // 100, 4)
    return (x - t, y - t, w + 2 * t, h + 2 * t)


def dialog(img, caption: str, headline: str, options: list[str], selected: int):
    """The confirm dialog (FreeInkUI's confirm popup, as ConfirmationActivity shows it): approximate."""
    side, pad, sel_v, gap = 20, 20, 12, 8
    x, w = side, W - 2 * side
    lines = [headline]
    if UI12B.width(headline) > w - 2 * pad:
        words = headline.split(" ")
        for k in range(len(words) - 1, 0, -1):
            if UI12B.width(" ".join(words[:k])) <= w - 2 * pad:
                lines = [" ".join(words[:k]), " ".join(words[k:])]
                break
    opt_h = UI10.advance_y + 2 * sel_v
    h = pad + CJK8.advance_y + 6 + len(lines) * UI12B.advance_y + 16 + len(options) * (opt_h + gap) - gap + pad
    y = (H - h) // 2
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([x - 2, y - 2, x + w + 1, y + h + 1], radius=8, fill=BLACK)
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=6, fill=255)
    cy = y + pad
    (CJK8 if any(ord(c) > 0x2FFF for c in caption) else SMALL).draw(img, x + pad, cy, caption)
    cy += CJK8.advance_y + 6
    for line in lines:
        UI12B.draw(img, x + pad, cy, line)
        cy += UI12B.advance_y
    cy += 16
    for i, opt in enumerate(options):
        if i == selected:
            d.rounded_rectangle([x + pad, cy, x + w - pad - 1, cy + opt_h - 1], radius=6, fill=BLACK)
            UI10.draw(img, x + pad + 14, cy + sel_v, opt, black=False)
        else:
            d.rounded_rectangle([x + pad, cy, x + w - pad - 1, cy + opt_h - 1], radius=6, outline=BLACK, width=1)
            UI10.draw(img, x + pad + 14, cy + sel_v, opt)
        cy += opt_h + gap
    return (x - 2, y - 2, w + 4, h + 4)


# ---- The words list (proposed; approximate: FreeInkUI's list with a word row of two lines) ----
ROW_X, ROW_W = v.ROW_X, v.ROW_W


def ui_or_cjk(text: str, size: int):
    cjk = any(ord(c) > 0x2FFF for c in text)
    return {10: CJK10 if cjk else UI10, 8: CJK8 if cjk else SMALL}[size]


def fit(font, text: str, room: int) -> str:
    if font.width(text) <= room:
        return text
    while text and font.width(text + "…") > room:
        text = text[:-1]
    return text + "…"


def words_list(rows, title="Looked-up words", sub="灯台守の手帳"):
    """rows: ('action', label, value) | ('head', chapter, button or '') | ('word', word, reading, meaning, state)."""
    img = blank()
    draw_header(img, title)
    CJK8.draw(img, INSET_L + 18, INSET_T + 5 + 84 + (48 - CJK8.advance_y) // 2, sub)
    y = INSET_T + 5 + 84 + 48 + 8
    rects = {}
    for i, r in enumerate(rows):
        if r[0] == "action":
            _, label, value = r
            h = 56
            ImageDraw.Draw(img).rounded_rectangle([ROW_X, y, ROW_X + ROW_W - 1, y + h - 1], radius=6, outline=BLACK,
                                                  width=1)
            UI10.draw(img, ROW_X + 14, y + (h - UI10.advance_y) // 2, label)
            if value:
                UI10.draw(img, ROW_X + ROW_W - 14 - UI10.width(value), y + (h - UI10.advance_y) // 2, value)
            rects[i] = (ROW_X, y, ROW_W, h)
            y += h + 14
        elif r[0] == "head":
            _, chapter, button = r
            h = 44
            ui_or_cjk(chapter, 10).draw(img, ROW_X + 8, y + (h - UI10.advance_y) // 2, chapter)
            if button:
                bw = SMALL.width(button) + 24
                bx, bh = ROW_X + ROW_W - 8 - bw, 32
                by = y + (h - bh) // 2
                ImageDraw.Draw(img).rounded_rectangle([bx, by, bx + bw - 1, by + bh - 1], radius=6, outline=BLACK,
                                                      width=2)
                SMALL.draw(img, bx + 12, by + (bh - SMALL.advance_y) // 2 + 1, button)
                rects[(i, "button")] = (bx, by, bw, bh)
            fill(img, ROW_X + 8, y + h - 2, ROW_W - 16, 2)
            rects[i] = (ROW_X, y, ROW_W, h)
            y += h + 4
        else:
            _, word, reading, meaning, state = r
            h = 12 + CJK12.advance_y + UI10.advance_y + 10
            x = ROW_X + 8
            CJK12.draw(img, x, y + 10, word)
            x += CJK12.width(word) + 14
            rf = ui_or_cjk(reading, 8)
            rf.draw(img, x, y + 10 + (CJK12.ascender - rf.ascender), reading)
            sw = SMALL.width(state)
            SMALL.draw(img, ROW_X + ROW_W - 8 - sw, y + 10 + (CJK12.ascender - SMALL.ascender), state)
            UI10.draw(img, ROW_X + 8, y + 10 + CJK12.advance_y, fit(UI10, meaning, ROW_W - 16))
            fill(img, ROW_X + 8, y + h - 1, ROW_W - 16, 1)
            rects[i] = (ROW_X, y, ROW_W, h)
            y += h
    return img, rects


LIST_ROWS = [
    ("action", "Look up 1 waiting word", ""),
    ("head", "第一章　灯台", "Save 4"),
    ("word", "窓辺", "まどべ", "window side; by the window", "not saved"),
    ("word", "螺旋", "らせん", "spiral, helix", "not saved"),
    ("word", "水平線", "すいへいせん", "horizon", "learning"),
    ("word", "漁船", "ぎょせん", "fishing boat", "not saved"),
    ("word", "几帳面", "", "Not looked up yet (you were offline)", "waiting"),
    ("head", "序章", ""),
    ("word", "灯台守", "とうだいもり", "lighthouse keeper", "tracked"),
    ("word", "手帳", "てちょう", "notebook, pocket diary", "known"),
]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=REPO / "docs/v0.2/reference/v9b-annotations.html")
    args = ap.parse_args()

    ja = parse(v.JA)
    ja_title, ja_prog = "第一章　灯台", "3/12  8%"
    S = {}

    # A5: the page as built (V9a) and with the option (for comparison: not recommended).
    S["a5_built"], _, _ = page_screen(ja, "approved", "off", ja_title, ja_prog)
    S["a5_rank"], _, _ = page_screen(ja, "a5", "off", ja_title, ja_prog)
    general = [("head", "On the page"), ("Mark words on the page", "On"), ("Side buttons on a card", "Marked words")]
    img = blank()
    draw_header(img, "Lexirise")
    _, rects = draw_rows(img, 5 + 84 + 16, general + [("New words to mark", "Less common only")], {3})
    S["a5_setting"] = Screen(img, rects)

    # The reader menu: as built (V9a) and with the new row after Page marks.
    menu = [("Select Chapter", ""), ("Toggle Bookmark", ""), ("Text Settings", ""), ("Night Mode", "Off"),
            ("Frontlight", "Off"), ("Lookup language", "Auto"), ("Page marks", "On"),
            ("Reading Orientation", "Portrait"), ("Auto Turn (Pages Per Minute)", "Off"), ("Go to %", "")]
    progress_line = "Chapter: 3/12 pages  |  Book: 8%"

    def menu_screen(rows, outlined=None):
        img = blank()
        draw_header(img, "灯台守の手帳")
        SMALL.draw(img, INSET_L + 18, INSET_T + 5 + 84 + (48 - SMALL.advance_y) // 2, progress_line)
        _, rects = draw_rows(img, INSET_T + 5 + 84 + 48 + 16, rows, outlined)
        return Screen(img, rects)

    S["menu_built"] = menu_screen(menu)
    S["menu_new"] = menu_screen(menu[:7] + [("Looked-up words", "1 waiting")] + menu[7:], {7})

    # The list, and its flows.
    img, rects = words_list(LIST_ROWS)
    S["list"] = Screen(img, [rects[0], rects[(1, "button")], rects[6]])
    img, rects = words_list(LIST_ROWS)
    box = dialog(img, "第一章　灯台", "Save 4 words as tracked?", ["Cancel", "Save"], 0)
    S["confirm"] = Screen(img, [box])
    img, _ = words_list(LIST_ROWS)
    S["saving"] = Screen(img, [popup(img, "Saving words...", 50)])
    done_rows = [LIST_ROWS[1][:2] + ("",)] + [r[:4] + ("tracked",) if r[0] == "word" and r[4] in ("not saved", "waiting")
                                             else r for r in LIST_ROWS[2:]]
    done_rows[5] = ("word", "几帳面", "きちょうめん", "methodical, meticulous", "tracked")
    img, _ = words_list(done_rows)
    S["saved"] = Screen(img, [popup(img, "Saved 4 words")])
    img, _ = words_list(LIST_ROWS)
    S["looking"] = Screen(img, [popup(img, "Looking up words...", 0)])

    # A11 on the card: an offline lookup draws nothing new; the ⋯ tab as built and without its Look up later row.
    S["offline_card"] = golden_screen("ja-card-unanswered")
    S["dots_built"] = golden_screen("ja-expanded-actions-new")
    S["dots_built"].ann.append((34, 344, 412, 49))
    S["dots_new"] = golden_screen("ja-expanded-actions-new", drop_box=(20, 340, 460, 400))

    page = render_html(S)
    args.out.write_text(page)
    print(f"wrote {args.out} ({len(page) // 1024} KB)")


def fig(s: Screen, title: str, tag: str, caption: str) -> str:
    tags = {"built": '<span class="tag ok">as built</span>', "rec": '<span class="tag rec">proposed</span>',
            "alt": '<span class="tag alt">alternative, not recommended</span>',
            "approx": '<span class="tag rec">proposed &middot; approximate</span>'}
    over = "".join(f'<div class="chg" style="left:{x}px;top:{y}px;width:{w}px;height:{h}px"></div>'
                   for x, y, w, h in s.ann)
    return (f'<figure><figcaption><b>{title}</b>{tags[tag]}<br>{caption}</figcaption><div class="fit"><div class="dev">'
            f'<div class="scr ann"><img alt="" src="{png(s.img)}">{over}</div><div class="home"></div></div></div>'
            f'</figure>')


def render_html(S) -> str:
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>V9b words and levels</title>
<!--
  PARKED, NOT SIGNED OFF (2026-09-29, claritise: A5 dropped, A10 and A11 parked, page-annotations.md §2 "Maybe later"; kept as the record). Was: proposed (v0.2 phase V9b, page-annotations.md A5, A10, A11). Generated by
  tools/mockups/v9b_annotations.py: edit that and regenerate, not this file. Every screen is drawn at the X4 Pro's
  native 480x800 device pixels, then scaled to fit the page. The reader's page and status bar follow the firmware as
  in v9a-annotations.html (the same code draws them); the card screens are the host layout's own display lists
  (firmware/test/lexirise_card/golden) rasterized with the device fonts' glyph bitmaps. The reader menu, the words
  list, the popups and the confirm dialog are approximate (FreeInkUI's list and popups, not re-derived pixel for
  pixel). Red dashed outlines mark what V9b adds or removes and are never drawn on the device. The book text, words,
  readings, meanings and states are made up. Self-contained: no external requests.
-->
<style>{CSS}</style>
</head>
<body>
<main>
<h1>V9b: looked-up words, look up later, above-level marks</h1>
<p class="lead"><b>Parked, not signed off (2026-09-29):</b> claritise dropped A5 and parked A10 and A11 (<code>page-annotations.md</code> &sect;2 &ldquo;Maybe later&rdquo;). Kept as the record. <b>Was proposed for sign-off.</b> V9b is the second part of
<code>page-annotations.md</code>&rsquo;s page annotations: <b>A10</b> a recap of the words you looked up, chapter by chapter,
with <i>Save all</i>; <b>A11</b> look up later, for words you tried to look up while offline; and <b>A5</b> marking only
words above your level. A10 and A11 share one list. A5 is measured below and <b>recommended to drop</b>. Screens are
the X4 Pro&rsquo;s 480&times;800 pixels in the firmware&rsquo;s fonts.</p>
<div class="ctl">
  <span><span class="legend"></span>what V9b adds or removes (never drawn on the device)
    <span class="seg" id="annSeg"><button data-v="1" class="on">Show</button><button data-v="0">Hide</button></span></span>
  <span>Screens <span class="seg" id="sizeSeg"><button data-v="fit" class="on">Fit</button><button data-v="1">1:1 device px</button></span></span>
</div>
<nav class="toc"><a href="#list">1 &middot; The looked-up words list</a><a href="#save">2 &middot; Save all</a><a href="#later">3 &middot; Look up later</a><a href="#a5">4 &middot; Above-level marks (A5)</a><a href="#store">What&rsquo;s kept</a><a href="#q">Questions</a></nav>

<h2 id="list">1 &middot; The looked-up words list (A10 and A11)</h2>
<p>One new row in the reader menu opens a list of the words you looked up in this book, the chapter you&rsquo;re in
first, then earlier ones. A word is listed once per chapter: every word you <b>tapped</b> to open a card (not the words
you stepped through with the side buttons), and every word you tried to look up while offline (&sect;3). Ignored words
aren&rsquo;t listed. Each row shows the word, its reading and first meaning (kept on the SD card when the card showed
them), and its state now, from the vocabulary copy. Rows are for reading: tapping one does nothing in V9b.</p>
<div class="frames">
{fig(S["menu_built"], "A &middot; Reader menu today", "built", "As built after V9a (the list style; approximate).")}
{fig(S["menu_new"], "B &middot; With V9b", "approx", "<i>Looked-up words</i> after Page marks, shown while Lexirise is on for the book. Its value says how many words wait to be looked up (none: blank).")}
{fig(S["list"], "C &middot; The list", "approx", "The chapter on screen first, with a <i>Save 4</i> button for its unsaved words (the waiting one included); a chapter with none has no button. The top row shows only while words wait to be looked up.")}
</div>

<h2 id="save">2 &middot; Save all</h2>
<p><i>Save N</i> asks first (the list can hold many words, and an Undo for all of them would be two calls a word).
Then each unsaved word is saved as a card save is (the word&rsquo;s dictionary form, its first meaning, the sentence it was
in as the note, your tags and the book&rsquo;s tag, so it lands in the book&rsquo;s deck), at <b>tracked</b>, one after
another behind a progress popup like Sync Vocabulary&rsquo;s. It&rsquo;s your own request, so it may join WiFi. Any
button stops it. A waiting word is looked up first. A saved word&rsquo;s level can still be changed from its card.</p>
<div class="frames">
{fig(S["confirm"], "D &middot; Confirm", "approx", "The chapter as the caption. Cancel is selected first.")}
{fig(S["saving"], "E &middot; Saving", "approx", "The home screen&rsquo;s popup and progress bar (as Sync Vocabulary). Any button stops it: &ldquo;Save stopped&rdquo;.")}
{fig(S["saved"], "F &middot; Done", "approx", "&ldquo;Saved 4 words&rdquo; (&ldquo;Saved 1 word&rdquo;), then the list with their new state. Failures: &ldquo;Save failed &middot; No Wi-Fi&rdquo;, &ldquo;Saved 2 of 4 &middot; Save failed&rdquo;, and the card&rsquo;s own &ldquo;Lexirise key rejected&rdquo; and &ldquo;Lexirise: rate limited&rdquo;.")}
</div>

<h2 id="later">3 &middot; Look up later (A11)</h2>
<p>The page annotations spec says &ldquo;offline, long-press flags the word&rdquo;, but a long-press already opens the
card (or the offline dictionary). The proposal needs <b>no new gesture</b>: a lookup Lexirise couldn&rsquo;t answer
(offline, a timeout, the rate limit) is kept as <b>waiting</b>, with its sentence, and nothing new is drawn at the time
(the card or the offline dictionary shows as today). A waiting word is looked up later, <b>only while WiFi is already on</b>
(as the page analysis: after a card brought it up, one word at a time while you read), or at once from the list&rsquo;s
top row, which may join WiFi (your own request). Looked up, it shows its reading and meaning in the list.</p>
<div class="frames">
{fig(S["offline_card"], "G &middot; An offline lookup", "built", "Unchanged: the card on an analyzed page, offline (its meaning couldn&rsquo;t load). This word is now kept as waiting. Offline on a page not analyzed, the offline dictionary answers, as today.")}
{fig(S["looking"], "H &middot; Look up now", "approx", "The list&rsquo;s top row: the same popup. Results: &ldquo;Looked up 1 word&rdquo;, &ldquo;Lookup failed &middot; No Wi-Fi&rdquo;, &ldquo;Lookup stopped&rdquo;.")}
</div>
<p>With that, the <code>&#8943;</code> tab&rsquo;s <i>Look up later</i> row (drawn by the approved card, today &ldquo;Not in
this version yet&rdquo;) has nothing left to do: a word you can see the card for is already looked up and listed.
The proposal <b>removes the row</b> (a change to the approved card, so it&rsquo;s asked).</p>
<div class="frames">
{fig(S["dots_built"], "I &middot; The &#8943; tab today", "built", "The approved card&rsquo;s actions (the host layout&rsquo;s golden).")}
{fig(S["dots_new"], "J &middot; Without Look up later", "rec", "The two rows that do something stay where they are (Undo save appears above them once a word is saved).")}
</div>

<h2 id="a5">4 &middot; Above-level marks (A5): measured, recommended to drop</h2>
<p>A5 would mark only words above a target level (N2, HSK 4). Measured read-only on 2026-09-29
(<code>tools/lexirise/probe_v9b.py</code>, <code>lexirise-api-notes.md</code> &ldquo;Levels for A5&rdquo;):</p>
<ul>
<li>The page analysis still carries <b>no level</b>. Only a dictionary lookup does, one word per call. A novel-like page
has 105&ndash;129 different words, a manga page ~43; even with repeats, a word&rsquo;s first lookup costs more calls
an hour than the key allows next to the page analysis.</li>
<li>The vocabulary copy <i>could</i> keep the level for free (each saved word carries it), but A5 is about the words
you haven&rsquo;t saved.</li>
<li>Many words have no level at all: 35% of a Japanese page&rsquo;s words (particles, and compounds like 港町, 坂道) and
22% of a Chinese page&rsquo;s (老街, 店铺, 屋檐).</li>
<li>The word&rsquo;s frequency rank, which the page analysis does carry, is a poor stand-in for JLPT (a rank cut agrees with
&ldquo;above N3&rdquo; for 74% of a page&rsquo;s words) and a fair one for HSK (91&ndash;92%).</li>
</ul>
<p>So an honest A5 can&rsquo;t say &ldquo;above N2&rdquo;. What&rsquo;s possible for free is a <b>frequency</b> option, shown
here for comparison: unsaved words among the language&rsquo;s most common ~3,000 aren&rsquo;t marked. On the probe&rsquo;s
pages that leaves 39 of 105 and 42 of 129 words marked. It goes against &ldquo;Every unsaved word&rdquo; (V9a), so the
recommendation is to drop A5.</p>
<div class="frames">
{fig(S["a5_built"], "K &middot; As built (V9a)", "built", "Every unsaved word underlined, particles included.")}
{fig(S["a5_rank"], "L &middot; Frequency option", "alt", "Unsaved words outside the most common marked; saved words (dotted) as before. Particles and everyday words lose their underline.")}
{fig(S["a5_setting"], "M &middot; Its setting", "alt", "One row in On the page: <i>New words to mark</i>: Every one / Less common only.")}
</div>

<div class="box" id="store">
<h3>What&rsquo;s kept, and when it calls Lexirise</h3>
<div class="tw"><table>
<tr><th>What</th><th>Proposal</th></tr>
<tr><td>The list&rsquo;s words</td><td>One file per book on the SD card (<code>/.lexirise/looked-up/&lt;book&gt;.bin</code>): per word its chapter, the dictionary form and the entry, reading, first meaning, the sentence (for the save&rsquo;s note and a waiting word&rsquo;s lookup), when. Capped per book (oldest first out) and in number of books. Written as the card&rsquo;s other files are (idle, and as it closes). Never sent anywhere.</td></tr>
<tr><td>A word&rsquo;s state</td><td>From the vocabulary copy (V7a) when the list opens, so saves made on the card or in Lexirise show.</td></tr>
<tr><td>Calls</td><td>Opening the list: none. Save all: one save per word (the card&rsquo;s save), plus a lookup for a waiting word. A waiting word: its sentence&rsquo;s analysis (none on an analyzed page) and a dictionary lookup, over WiFi already on, a few an hour, inside the page analysis&rsquo;s budget.</td></tr>
<tr><td>New strings</td><td><code>Looked-up words</code>, <code>N waiting</code>, <code>Look up N waiting words</code>, <code>Save N</code>, <code>Save N words as tracked?</code>, <code>Saving words...</code>, <code>Saved N words</code>, <code>Save failed &middot; No Wi-Fi</code>, <code>Saved N of M &middot; Save failed</code>, <code>Save stopped</code>, <code>Looking up words...</code>, <code>Looked up N words</code>, <code>Lookup failed &middot; No Wi-Fi</code>, <code>Lookup stopped</code>, <code>waiting</code>, <code>Not looked up yet (you were offline)</code>; singulars picked by the count.</td></tr>
</table></div>
</div>

<h2 id="q">Questions for claritise</h2>
<ol class="q">
<li><b>A5 (above-level marks):</b> drop it, or build the frequency option (L, M) instead? <span class="rec">Recommended: drop.</span> No cheap source of JLPT/HSK levels for unsaved words; a frequency cut is a different feature and undoes &ldquo;Every unsaved word&rdquo;.</li>
<li><b>Where the recap lives:</b> a reader-menu row only (B), or also shown by itself when you finish a chapter? <span class="rec">Recommended: the menu row only.</span> Nothing pops up between chapters.</li>
<li><b>Which words are listed:</b> the words you tapped, or also the ones you stepped to with the side buttons? <span class="rec">Recommended: tapped only</span> (stepping through a sentence would fill the list, and Save all would save them).</li>
<li><b>Save all:</b> ask first, save at tracked, no Undo (D&ndash;F)? <span class="rec">Recommended: yes.</span></li>
<li><b>Look up later:</b> keep every lookup Lexirise couldn&rsquo;t answer as waiting (no new gesture), look them up only while WiFi is already on or from the list&rsquo;s row, and remove the card&rsquo;s <i>Look up later</i> row (I, J)? <span class="rec">Recommended: yes.</span></li>
<li><b>The wording:</b> &ldquo;Looked-up words&rdquo; for the menu row and screen, and the strings above? <span class="rec">Recommended: as drawn.</span></li>
</ol>
</main>
<script>{JS}</script>
</body>
</html>
"""


if __name__ == "__main__":
    main()
