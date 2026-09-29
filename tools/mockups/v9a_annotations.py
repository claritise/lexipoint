#!/usr/bin/env python3
"""Build docs/v0.2/reference/v9a-annotations.html: the V9a mockups (page-annotations.md A1, A2, A3, A4) for
claritise's sign-off, drawn in the X4 Pro's 480x800 device pixels from the firmware's own geometry and fonts.

  python3 tools/mockups/v9a_annotations.py [--out docs/v0.2/reference/v9a-annotations.html]

Needs Pillow, the firmware's built-in font headers (firmware/lib/EpdFont/builtinFonts) and the reader's CJK
family on the gitignored SD staging folder (sd-card/fonts/NotoSerifCJK/*.cpfont, the font the reader uses).
Every screen is a PNG in the page (self-contained, no external requests). The book text is made up here; the
words' saved states, ranks and meanings are made up too (nothing came from Lexirise).

Where each number comes from is listed in the page's "Drawn from" table; the reader's page and status bar follow
the firmware exactly (EpubReaderActivity::renderBook, BaseTheme::drawStatusBar, ParsedText's CJK line breaking
and justification), the card follows the host layout's rules (src/lexirise/card/CardLayout.cpp, pinned by
test/lexirise_card/golden), and the two list screens are approximate (FreeInkUI's list, not re-derived pixel for
pixel).
"""

from __future__ import annotations

import argparse
import base64
import html
import io
import math
import re
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image, ImageDraw

from epdfont import BLACK, WHITE, BuiltinFont, CpFont

REPO = Path(__file__).resolve().parents[2]
BUILTIN = REPO / "firmware/lib/EpdFont/builtinFonts"
CJK = REPO / "sd-card/fonts/NotoSerifCJK"

SMALL = BuiltinFont(BUILTIN / "notosans_8_regular.h")  # SMALL_FONT_ID: the status bar, the card's small text
UI10 = BuiltinFont(BUILTIN / "ubuntu_10_regular.h")  # UI_10: the card's meaning, list rows
UI10B = BuiltinFont(BUILTIN / "ubuntu_10_bold.h")
UI12B = BuiltinFont(BUILTIN / "ubuntu_12_bold.h")  # UI_12 bold: screen titles
PAGE = CpFont(CJK / "NotoSerifCJK_14.cpfont")  # the reader's default: the SD family at 14 pt
CJK8 = CpFont(CJK / "NotoSerifCJK_8.cpfont")  # the card's reading (ReaderSmall) and the status bar's CJK fallback
CJK18 = CpFont(CJK / "NotoSerifCJK_18.cpfont")  # the card's word (ReaderLarge)
CJK12 = CpFont(CJK / "NotoSerifCJK_12.cpfont")  # a CJK screen title: UI_12's size-matched fallback

W, H = 480, 800

# ---- The reader's page (EpubReaderActivity::renderBook, X4 Pro, default settings) ----
INSET_T, INSET_R, INSET_B, INSET_L = 9, 7, 3, 7  # BoardConfig XTEINK_X4_PRO viewableInsets (portrait)
SCREEN_MARGIN = 5  # CrossPointSettings::screenMargin default (SCREEN_MARGIN_MIN)
STATUS_V = 19  # Lyra statusBarVerticalMargin: the status bar's height (text lane only, no progress bar)
PAGE_X = INSET_L + SCREEN_MARGIN  # 12
PAGE_Y = INSET_T + SCREEN_MARGIN  # 14
PAGE_W = W - PAGE_X - (INSET_R + SCREEN_MARGIN)  # 456
PAGE_H = H - PAGE_Y - (INSET_B + max(SCREEN_MARGIN, STATUS_V))  # 764
LINE_H = PAGE.advance_y  # 42: line compression 1.0 (Normal) for an SD family
BASE = PAGE.ascender  # 34: baseline below the line's top
PARA_GAP = LINE_H // 2  # extra paragraph spacing (on by default): half a line after each paragraph
CHAR_W = (PAGE.glyph(0x65E5)[2] + 8) >> 4  # 29: one CJK advance, snapped

# ---- The marks (proposed) ----
UL_Y = BASE + 6  # underline rows: baseline + 6 and + 7 (CJK ink ends at baseline + 4; the next line's at + 16)
UL_T = 2
UL_INSET = 2  # each end pulled in 2 px, so two marked words side by side read as two
DOT_PITCH = 4  # dotted: 2x2 squares every 4 px
SEEN_D = 4  # A4: a 4x4 round dot above the word's last character, at the line's top + 3


def no_break_before(c: str) -> bool:
    return c in ".,:;!?)]}»’”、。〉》」』】〕〗〙〛！），．：；？］｝"


def no_break_after(c: str) -> bool:
    return c in "([{«‘“〈《「『【〔〖〘〚（［｛"


# ---- Book text and the words' states (made up) ----
# Classes: N a rare word never saved; C a common word never saved (rank within the top 2,000); L saved at
# level 1-2 (tracked, learning); K saved at 3-4 (fresh, known); F a grammar word (particle, auxiliary,
# conjunction, copula) never saved; P punctuation and spaces. A key after ':' is the entry (the lemma) when the
# page's form differs; '!' marks a learning word due for review (A4's alternative).

JA = """
　/P 灯台守/N の/F 祖父/L は/F 、/P 毎朝/C 五/C 時/C に/F 起きて/K:起きる 海/C を/F 眺めて/C:眺める いた/F 。/P
天気/C が/F 荒れ/C:荒れる そう/F な/F 日/C に/F は/F 、/P 窓辺/N に/F 座った/K:座る まま/C 、/P じっと/L! 雲/K の/F
動き/C を/F 追って/K:追う いた/F 。/P
¶
　/P 「/P 海/C は/F な/F 、/P 嘘/K を/F つかない/C:つく ん/F だ/F 」/P と/F 祖父/L は/F よく/C 言った/C:言う 。/P けれど/F 、/P
その/C 言葉/K の/F 本当/C の/F 意味/C を/F 、/P 私/C は/F まだ/C 知らなかった/C:知る 。/P
¶
　/P 夏休み/K の/F 終わり/C に/F 、/P 私/C は/F 初めて/C 一人/C で/F 灯台/L! に/F 登った/C:登る 。/P 螺旋/N 階段/C は/F 思った/C:思う
より/F 狭く/K:狭い 、/P 足音/N が/F 壁/C に/F 響いて/L:響く 、/P 何度/C も/F 振り返り/C:振り返る たく/F なった/C:なる 。/P
¶
　/P てっぺん/C に/F 着く/C と/F 、/P 水平線/N が/F ゆるやか/N に/F 曲がって/C:曲がる 見えた/C:見える 。/P 遠く/C の/F 漁船/N が/F 、/P
小さな/C 光/C の/F 粒/K の/F よう/F に/F 揺れて/C:揺れる いる/F 。/P
¶
　/P 祖父/L の/F 手帳/K に/F は/F 、/P 毎日/C の/F 風向き/N と/F 波/C の/F 高さ/C が/F 、/P 几帳面/N な/F 字/C で/F 記されて/N:記す いた/F 。/P
"""

ZH = """
　/P 雨/C 下/C 了/F 整整/L 一/C 个/C 下午/C ，/P 老街/C 上/F 的/F 行人/K 越来越/K 少/C 。/P 我/C 躲/C 进/C 一/C 家/C
不起眼/N 的/F 旧/C 书店/K ，/P 门口/C 的/F 风铃/N 轻轻/L! 响/C 了/F 一/C 声/C 。/P
¶
　/P 店主/K 是/F 一/C 位/C 头发/C 花白/C 的/F 老人/C ，/P 正/C 戴/K 着/F 眼镜/K 修补/C 一/C 本/C 破旧/N 的/F 字典/K 。/P
他/C 抬头/L 看/C 了/F 我/C 一/C 眼/C ，/P 没有/C 说话/C ，/P 只是/C 指/C 了/F 指/C 靠/L! 窗/C 的/F 椅子/C 。/P
¶
　/P 书架/K 上/F 堆满/N 了/F 泛黄/C 的/F 小说/K 和/F 杂志/C ，/P 空气/C 里/F 有/C 一/C 种/C 淡淡/L 的/F 纸/C 香/C 。/P
我/C 随手/C 抽出/C 一/C 本/C 诗集/K ，/P 翻/C 到/C 中间/C ，/P 发现/C 里面/C 夹/L 着/F 一/C 张/C 褪色/N 的/F 车票/K 。/P
¶
　/P 车票/K 上/F 的/F 日期/C 是/F 二十/C 年/C 前/F 的/F 春天/C ，/P 终点站/N 的/F 名字/C 我/C 从来/C 没有/C 听说/C 过/F 。/P
¶
　/P 我/C 拿/C 着/F 诗集/K 走/C 到/C 柜台/K 前/F ，/P 老人/C 接/C 过/F 去/F ，/P 看/C 了/F 看/C 那/C 张/C 车票/K ，/P
忽然/C 笑/C 了/F 。/P 他/C 说/C ，/P 这/C 本/C 书/C 他/C 等/C 了/F 很/C 久/C 。/P
"""


@dataclass
class Token:
    text: str
    cls: str
    key: str
    due: bool = False


def parse(src: str) -> list[list[Token]]:
    paras: list[list[Token]] = [[]]
    for part in re.split(r"[ \n]+", src.strip(" \n")):  # not str.split(): U+3000 is whitespace to Python
        if part == "¶":
            paras.append([])
            continue
        text, _, rest = part.partition("/")
        due = rest.endswith("!")
        rest = rest.rstrip("!")
        cls, _, key = rest.partition(":")
        paras[-1].append(Token(text, cls, key or text, due))
    return [p for p in paras if p]


# ---- Rules: what each class draws (the proposal's options) ----
RULES = {
    # "approved" (claritise 2026-09-29, "Every unsaved word"): A1 as written.
    "approved": {"N": "new", "C": "new", "F": "new", "L": "learn", "K": None, "P": None},
    # "literal": page-annotations.md §2 A1 as written: anything not saved (or level 0) is new.
    "literal": {"N": "new", "C": "new", "F": "new", "L": "learn", "K": None, "P": None},
    # "content": grammar words and punctuation carry no mark and don't count.
    "content": {"N": "new", "C": "new", "F": None, "L": "learn", "K": None, "P": None},
    # "recommended": content words only, and an unsaved word in the language's 2,000 most frequent isn't marked.
    "recommended": {"N": "new", "C": None, "F": None, "L": "learn", "K": None, "P": None},
}


def counted(rule: str, cls: str) -> bool:
    if cls == "P":
        return False
    return not (cls == "F" and rule not in ("literal", "approved"))


def stats(paras: list[list[Token]], rule: str, shown_tokens: set[int]) -> str:
    """A2's text for the page: distinct new and learning entries, and the share of counted words unmarked."""
    new, learn = set(), set()
    total = unmarked = 0
    i = 0
    for p in paras:
        for t in p:
            if i in shown_tokens and counted(rule, t.cls):
                total += 1
                mark = RULES[rule][t.cls]
                if mark == "new":
                    new.add(t.key)
                elif mark == "learn":
                    learn.add(t.key)
                else:
                    unmarked += 1
            i += 1
    pct = math.floor(100 * unmarked / total) if total else 100
    return f"{len(new)} new · {len(learn)} learning · {pct}% known"


# ---- Layout (ParsedText: CJK break opportunities, justified lines, half-line paragraph gaps) ----
@dataclass
class Placed:
    ch: str
    x: int
    line_top: int
    tok: int  # global token index


@dataclass
class PageLayout:
    chars: list[Placed] = field(default_factory=list)
    shown_tokens: set[int] = field(default_factory=set)


def layout(paras: list[list[Token]]) -> PageLayout:
    out = PageLayout()
    y = PAGE_Y
    tok_base = 0
    for p in paras:
        chars = [(c, tok_base + ti) for ti, t in enumerate(p) for c in t.text]
        tok_base += len(p)
        units: list[list[tuple[str, int]]] = []
        for c, ti in chars:
            if units and (no_break_before(c) or no_break_after(units[-1][-1][0])):
                units[-1].append((c, ti))
            else:
                units.append([(c, ti)])
        lines: list[list[list[tuple[str, int]]]] = [[]]
        width = 0
        for u in units:
            uw = CHAR_W * len(u)
            if lines[-1] and width + uw > PAGE_W:
                lines.append([])
                width = 0
            lines[-1].append(u)
            width += uw
        for li, line in enumerate(lines):
            if y + LINE_H > PAGE_Y + PAGE_H:
                return out
            last = li == len(lines) - 1
            used = sum(CHAR_W * len(u) for u in line)
            gaps = len(line) - 1
            extra = (PAGE_W - used) // gaps if (not last and gaps >= 1) else 0
            x = PAGE_X
            for u in line:
                for c, ti in u:
                    out.chars.append(Placed(c, x, y, ti))
                    out.shown_tokens.add(ti)
                    x += CHAR_W
                x += extra
            y += LINE_H
        y += PARA_GAP
    return out


def token_segments(lay: PageLayout, ti: int) -> list[tuple[int, int, int]]:
    """(x0, x1, line_top) per line a token occupies: x1 is the last character's advance end."""
    segs: dict[int, list[int]] = {}
    for pc in lay.chars:
        if pc.tok == ti:
            s = segs.setdefault(pc.line_top, [pc.x, pc.x + CHAR_W])
            s[0] = min(s[0], pc.x)
            s[1] = max(s[1], pc.x + CHAR_W)
    return [(a, b, top) for top, (a, b) in sorted(segs.items())]


# ---- Drawing ----
def blank() -> Image.Image:
    return Image.new("L", (W, H), WHITE)


def fill(img, x, y, w, h, black=True):
    ImageDraw.Draw(img).rectangle([x, y, x + w - 1, y + h - 1], fill=BLACK if black else WHITE)


def frame(img, x, y, w, h, t):
    for i in range(t):
        ImageDraw.Draw(img).rectangle([x + i, y + i, x + w - 1 - i, y + h - 1 - i], outline=BLACK)


def invert(img, x, y, w, h):
    px = img.load()
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            px[xx, yy] = 255 - px[xx, yy]


def draw_marks(img, lay: PageLayout, paras, rule: str, seen: str):
    """A1 underlines and A4 dots. seen: 'all' (A4 as specified), 'due' (the alternative) or 'off'."""
    toks = [t for p in paras for t in p]
    for ti in sorted(lay.shown_tokens):
        t = toks[ti]
        mark = RULES[rule][t.cls] if counted(rule, t.cls) else None
        segs = token_segments(lay, ti)
        for x0, x1, top in segs:
            y = top + UL_Y
            a, b = x0 + UL_INSET, x1 - UL_INSET
            if mark == "new":
                fill(img, a, y, b - a, UL_T)
            elif mark == "learn":
                for x in range(a, b - 1, DOT_PITCH):
                    fill(img, x, y, 2, 2)
        if mark == "learn" and (seen == "all" or (seen == "due" and t.due)):
            x0, x1, top = segs[-1]
            dx, dy = x1 - SEEN_D, top + 3
            fill(img, dx + 1, dy, 2, SEEN_D)
            fill(img, dx, dy + 1, SEEN_D, 2)


def draw_page(img, paras, lay: PageLayout):
    toks = [t for p in paras for t in p]
    del toks
    for pc in lay.chars:
        PAGE.draw(img, pc.x, pc.line_top, pc.ch)


def draw_status(img, center: str, right: str = "", battery=87):
    """BaseTheme::drawStatusBar: battery and percent left, the title (or A2's text) centred, progress right."""
    text_y = H - STATUS_V - INSET_B - 4  # 774
    left_x = 5 + INSET_L + 1  # 13
    right_x = W - 5 - INSET_R  # 468
    # Battery (BaseTheme::drawBatteryLeft, Lyra's fill: a bar per 30%)
    by = text_y + 6
    d = ImageDraw.Draw(img)
    d.line([left_x + 1, by, left_x + 16 - 3, by], fill=BLACK)
    d.line([left_x + 1, by + 11, left_x + 16 - 3, by + 11], fill=BLACK)
    d.line([left_x, by + 1, left_x, by + 10], fill=BLACK)
    d.line([left_x + 14, by + 1, left_x + 14, by + 10], fill=BLACK)
    d.point([(left_x + 15, by + 3), (left_x + 15, by + 8)], fill=BLACK)
    d.line([left_x + 16, by + 4, left_x + 16, by + 7], fill=BLACK)
    for i, thr in enumerate((10, 40, 70)):
        if battery > thr:
            fill(img, left_x + 2 + 4 * i, by + 2, 3, 8)
    pct = f"{battery}%"
    SMALL.draw(img, left_x + 4 + 16, text_y, pct)
    left_w = 16 + 4 + SMALL.width(pct)
    right_w = 0
    if right:
        rw = SMALL.width(right)
        SMALL.draw(img, right_x - rw, text_y, right)
        right_w = rw
    if center:
        screen_w = W - 10 - INSET_L - INSET_R
        ml, mr = left_w + 30, right_w + 30
        adj = max(ml, mr)
        avail = screen_w - 2 * adj
        font = SMALL
        y = text_y
        if any(ord(c) > 0x2FFF for c in center):  # a CJK title: the SD family at the UI size (resolveTextFontId)
            font = CJK8
            y = text_y + (SMALL.advance_y - CJK8.advance_y) // 2
        tw = font.width(center)
        if tw > avail:
            avail = screen_w - ml - mr
            adj = ml
        img_x = adj + 5 + INSET_L + (avail - tw) // 2
        font.draw(img, img_x, y, center)
        return (img_x, text_y, tw, SMALL.advance_y)
    return None


# ---- The card (CardLayout.cpp's rules; positions as in test/lexirise_card/golden) ----
@dataclass
class CardWord:
    reading: str
    word: str
    badge: str
    level: int | None  # 0..3 = T L F K, None = not saved
    pos: str
    meaning: list[str]
    rank: str
    bars: int
    surface: str = ""
    conj: str = ""


LEVELS = ["T", "L", "F", "K"]
LEVEL_NAMES = ["tracked", "learning", "fresh", "known"]


def draw_card(img, w: CardWord) -> int:
    top = 578 - 24 * len(w.meaning)
    fill(img, 14, top, 452, 786 - top, black=False)
    frame(img, 14, top, 452, 786 - top, 3)
    CJK8.draw(img, 34, top + 14, w.reading)
    CJK18.draw(img, 34, top + 37, w.word)
    if w.badge:
        bx = 34 + round(37.5 * len(w.word)) + 11
        bw = SMALL.width(w.badge) + 20
        frame(img, bx, top + 48, bw, 31, 3)
        SMALL.draw(img, bx + 10, top + 52, w.badge)
    frame(img, 271, top + 14, 175, 41, 2)
    for i, lab in enumerate(LEVELS):
        cx = 273 + i * 43
        if i:
            fill(img, cx - 1, top + 16, 1, 37)
        on = w.level == i
        if on:
            fill(img, cx, top + 16, 42, 37)
        lx = cx + (42 - SMALL.width(lab)) // 2
        if on:
            SMALL.draw(img, lx, top + 23, lab, black=False)
        else:
            SMALL.draw(img, lx, top + 23, lab)
    state = "not saved" if w.level is None else LEVEL_NAMES[w.level]
    SMALL.draw(img, 446 - SMALL.advance(state), top + 61, state)
    x = 34
    if w.surface:
        CJK8.draw(img, x, top + 92, w.surface)
        x += round(16.6667 * len(w.surface)) + SMALL.advance(" ")
        if w.conj:
            SMALL.draw(img, x, top + 93, w.conj)
            x += SMALL.advance(w.conj) + SMALL.advance(" ")
    pw = SMALL.advance(w.pos) + 16
    ImageDraw.Draw(img).rounded_rectangle([x, top + 92, x + pw - 1, top + 92 + 24], radius=4, outline=BLACK, width=1)
    SMALL.draw(img, x + 8, top + 93, w.pos)
    fill(img, 17, top + 128, 446, 1)
    for i, line in enumerate(w.meaning):
        UI10.draw(img, 34, top + 140 + 24 * i, line)
    fill(img, 17, 729, 446, 1)
    fill(img, 400, 730, 1, 53)
    for i, bh in enumerate((8, 13, 17, 21, 25)):
        bx = 34 + 9 * i
        if i < w.bars:
            fill(img, bx, 769 - bh, 6, bh)
        else:
            frame(img, bx, 769 - bh, 6, bh, 1)
    SMALL.draw(img, 83, 745, w.rank)
    d = ImageDraw.Draw(img)
    d.polygon([(369, 752), (379, 752), (374, 760)], fill=BLACK)
    d.line([425, 750, 437, 762], fill=BLACK, width=2)
    d.line([437, 750, 425, 762], fill=BLACK, width=2)
    return top


# ---- List screens (approximate: FreeInkUI list, Lyra tokens) ----
def draw_header(img, title: str, battery=87):
    band_y, band_h = INSET_T + 5, 84
    font = CJK12 if any(ord(c) > 0x2FFF for c in title) else UI12B
    ty = band_y + band_h - 22 - UI12B.advance_y + 4
    font.draw(img, INSET_L + 18, ty + (UI12B.advance_y - font.advance_y) // 2, title)
    fill(img, INSET_L + 18, band_y + band_h - 3, W - 2 * (INSET_L + 18), 3)
    pct = f"{battery}%"
    bx = W - INSET_R - 18 - 18
    SMALL.draw(img, bx - 4 - SMALL.width(pct), band_y + 4, pct)
    by = band_y + 10
    frame(img, bx, by, 15, 12, 1)
    fill(img, bx + 15, by + 4, 2, 4)
    for i in range(3):
        fill(img, bx + 2 + 4 * i, by + 2, 3, 8)


ROW_H, ROW_GAP, ROW_X, ROW_W = 56, 6, 20, 440


def draw_rows(img, y: int, rows: list, outlined: set[int] | None = None) -> tuple[int, list]:
    """rows: ('head', label) or (label, value). Returns the next y and the rects of `outlined` rows."""
    rects = []
    first = True
    for i, r in enumerate(rows):
        if r[0] == "head":
            if not first:
                y += 16
            UI10.draw(img, ROW_X + 8, y, r[1])
            fill(img, ROW_X + 8, y + UI10.advance_y + 2, ROW_W - 16, 1)
            if outlined and i in outlined:
                rects.append((ROW_X, y - 2, ROW_W, UI10.advance_y + 6))
            y += UI10.advance_y + 4 + ROW_GAP
        else:
            label, value = r
            room = ROW_W - 16 - (UI10.width(value) + 8 + 8 if value else 0)
            lines = [label]
            if UI10.width(label) > room:  # two lines at most (labelText.maxLines = 2), split at a space
                words = label.split(" ")
                for k in range(len(words) - 1, 0, -1):
                    if UI10.width(" ".join(words[:k])) <= room:
                        lines = [" ".join(words[:k]), " ".join(words[k:])]
                        break
            rh = max(ROW_H, len(lines) * UI10.advance_y + 16)
            ty = y + (rh - len(lines) * UI10.advance_y) // 2
            for k, line in enumerate(lines):
                UI10.draw(img, ROW_X + 8, ty + k * UI10.advance_y, line)
            if value:
                UI10.draw(img, ROW_X + ROW_W - 8 - 8 - UI10.width(value), y + (rh - UI10.advance_y) // 2, value)
            if outlined and i in outlined:
                rects.append((ROW_X, y, ROW_W, rh))
            y += rh + ROW_GAP
        first = False
    return y, rects


def png(img: Image.Image) -> str:
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


# ---- Screens ----
@dataclass
class Screen:
    img: Image.Image
    ann: list[tuple[int, int, int, int]] = field(default_factory=list)  # red dashed outlines: what's new
    notes: list[tuple[int, int, str]] = field(default_factory=list)  # small red labels (never on the device)


def page_screen(paras, rule: str | None, seen: str, title: str, progress: str, card: CardWord | None = None,
                card_tok: int | None = None, next_tok: int | None = None,
                keep_title: bool = True) -> tuple[Screen, str, PageLayout]:
    img = blank()
    lay = layout(paras)
    draw_page(img, paras, lay)
    ann = []
    text = title  # claritise 2026-09-29: "Keep the chapter title" (A2 draws nothing)
    if rule:
        draw_marks(img, lay, paras, rule, seen)
        text = title if keep_title else stats(paras, rule, lay.shown_tokens)
    sb = draw_status(img, text, progress) if card is None else None  # the card draws the page body only
    if rule and sb and not keep_title:
        ann.append((sb[0] - 2, sb[1] + 2, sb[2] + 4, sb[3] - 2))
    notes = []
    if card is not None:
        for x0, x1, top in token_segments(lay, card_tok):
            invert(img, x0 - 1, top, x1 - x0 + 2, LINE_H)
        draw_card(img, card)
        if next_tok is not None:
            for x0, x1, top in token_segments(lay, next_tok):
                ann.append((x0 - 1, top, x1 - x0 + 2, LINE_H))
    return Screen(img, ann, notes), text, lay


def tok_index(paras, text: str, nth: int = 0) -> int:
    i = 0
    seen = 0
    for p in paras:
        for t in p:
            if t.text == text:
                if seen == nth:
                    return i
                seen += 1
            i += 1
    raise KeyError(text)


def skipped_between(paras, a: int, b: int) -> list[str]:
    toks = [t for p in paras for t in p]
    return [t.text for t in toks[a + 1:b] if t.cls != "P"]


def zoom(img: Image.Image, box, scale=4) -> Image.Image:
    return img.crop(box).resize(((box[2] - box[0]) * scale, (box[3] - box[1]) * scale), Image.NEAREST)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=REPO / "docs/v0.2/reference/v9a-annotations.html")
    args = ap.parse_args()

    ja, zh = parse(JA), parse(ZH)
    ja_title, zh_title = "第一章　灯台", "第三章　旧书店"
    ja_prog, zh_prog = "3/12  8%", "5/14  21%"

    S = {}
    S["ja_plain"], _, _ = page_screen(ja, None, "off", ja_title, ja_prog)
    S["ja_marked"], _, _ = page_screen(ja, "approved", "off", ja_title, ja_prog)
    S["zh_plain"], _, _ = page_screen(zh, None, "off", zh_title, zh_prog)
    S["zh_marked"], _, _ = page_screen(zh, "approved", "off", zh_title, zh_prog)

    # A3: the card on じっと (learning); Side Right goes to the next marked word, の, skipping 雲 (known).
    jitto = tok_index(ja, "じっと")
    no = tok_index(ja, "の", 1)
    assert [t for t in skipped_between(ja, jitto, no)] == ["雲"]
    jitto_card = CardWord("じっと", "じっと", "N2", 1, "adverb", ["still, motionless; intently, fixedly"],
                          "#3,105 common", 3)
    no_card = CardWord("の", "の", "N5", None, "particle", ["possessive particle: of, 's"], "#2 very common", 5)
    S["a3_on"], _, _ = page_screen(ja, "approved", "off", ja_title, ja_prog, jitto_card, jitto, no)
    S["a3_next"], _, _ = page_screen(ja, "approved", "off", ja_title, ja_prog, no_card, no)
    skipped = skipped_between(ja, jitto, no)

    # The marks up close: the first three lines of the marked Japanese page, 3x.
    close = zoom(S["ja_marked"].img, (PAGE_X, PAGE_Y - 4, PAGE_X + 300, PAGE_Y + 3 * LINE_H + 2), 3)

    # Settings → System → Lexirise, scrolled to General (as built) and with the signed-off group.
    general = [("head", "General"), ("Language when a book doesn't say", "Japanese"), ("Tags", "xteink"),
               ("Tag with book title", "On"), ("Deck per book", "On"), ("Keep WiFi on after a lookup", "5 min")]
    proposed = [("head", "On the page"), ("Mark words on the page", "On"), ("Side buttons on a card", "Marked words")]
    img = blank()
    draw_header(img, "Lexirise")
    draw_rows(img, 5 + 84 + 16, general)
    S["set_built"] = Screen(img)
    img = blank()
    draw_header(img, "Lexirise")
    _, rects = draw_rows(img, 5 + 84 + 16, general + proposed, set(range(len(general), len(general) + len(proposed))))
    S["set_new"] = Screen(img, rects)

    # The reader menu (the default list style), from its top.
    menu = [("Select Chapter", ""), ("Toggle Bookmark", ""), ("Text Settings", ""), ("Night Mode", "Off"),
            ("Frontlight", "Off"), ("Lookup language", "Auto"), ("Reading Orientation", "Portrait"),
            ("Auto Turn (Pages Per Minute)", "Off"), ("Go to %", ""), ("Screenshot", "")]
    progress_line = "Chapter: 3/12 pages  |  Book: 8%"

    def menu_screen(rows, outlined=None):
        img = blank()
        draw_header(img, "灯台守の手帳")
        SMALL.draw(img, INSET_L + 18, INSET_T + 5 + 84 + (48 - SMALL.advance_y) // 2, progress_line)
        _, rects = draw_rows(img, INSET_T + 5 + 84 + 48 + 16, rows, outlined)
        return Screen(img, rects)

    S["menu_built"] = menu_screen(menu)
    menu_new = menu[:6] + [("Page marks", "On")] + menu[6:]
    S["menu_new"] = menu_screen(menu_new, {6})

    page = render_html(S, dict(skipped=skipped, close=png(close), close_size=close.size))
    args.out.write_text(page)
    print(f"wrote {args.out} ({len(page) // 1024} KB)")


# ---- The page ----
CSS = """
:root{color-scheme:light;--ink:#000;--paper:#fff;--frame:#e9e7e1;--frame-line:#c9c6bd;--note:#555;--bg:#faf9f6;--text:#1f1f1f;
 --surface:#fff;--code:#efede7;--rule:#e3e0d8;--on:#1f1f1f;--on-text:#fff;--ann:#c8102e;--ann-text:#c8102e;--rec:#1d6b3a;
 --font-sans:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
/* Dark host theme: only the page around the screens changes. A screen is e-ink paper: black on white in both themes. */
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){color-scheme:dark;--frame:#3a3936;--frame-line:#5a5852;--note:#a9a69e;--bg:#171716;--text:#e8e6e1;
 --surface:#222120;--code:#2c2b29;--rule:#34332f;--on:#e8e6e1;--on-text:#171716;--ann-text:#ff6b7f;--rec:#5cc07e}}
:root[data-theme="dark"]{color-scheme:dark;--frame:#3a3936;--frame-line:#5a5852;--note:#a9a69e;--bg:#171716;--text:#e8e6e1;
 --surface:#222120;--code:#2c2b29;--rule:#34332f;--on:#e8e6e1;--on-text:#171716;--ann-text:#ff6b7f;--rec:#5cc07e}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font-family:var(--font-sans);font-size:15px;line-height:1.55}
main{max-width:1200px;margin:0 auto;padding:24px 16px 72px}
h1{font-size:23px;line-height:1.25;margin:0 0 6px}
h2{font-size:19px;line-height:1.3;margin:44px 0 6px;padding-top:18px;border-top:1px solid var(--frame-line)}
h3{font-size:15px;margin:18px 0 4px}
p{margin:6px 0;max-width:800px}
ul{margin:6px 0;padding-left:20px;max-width:800px}
li{margin:3px 0}
code{font-size:13px;background:var(--code);padding:0 3px;border-radius:3px}
.lead{color:var(--note);max-width:800px}
.ctl{display:flex;flex-wrap:wrap;gap:10px 18px;align-items:center;margin:14px 0 4px;font-size:13px;color:var(--note)}
.seg{display:inline-flex;border:1px solid var(--frame-line);border-radius:8px;overflow:hidden;vertical-align:middle}
.seg button{border:0;padding:5px 11px;font-size:12px;background:transparent;cursor:pointer;font-family:inherit;color:var(--text)}
.seg button.on{background:var(--on);color:var(--on-text)}
nav.toc{display:flex;flex-wrap:wrap;gap:6px 8px;margin:12px 0 0}
nav.toc a{font-size:13px;color:var(--text);text-decoration:none;border:1px solid var(--frame-line);border-radius:14px;padding:3px 10px;background:var(--surface)}
.frames{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:26px 22px;margin:16px 0 8px}
figure{margin:0;min-width:0;display:flex;flex-direction:column}
figcaption{font-size:13px;line-height:1.45;margin:0 0 8px;flex:1}
figcaption b{font-size:14px}
.tag{display:inline-block;font-size:11px;line-height:16px;padding:0 6px;border-radius:4px;border:1px solid var(--frame-line);margin-left:4px;vertical-align:1px;background:var(--surface);color:var(--note)}
.tag.rec{border-color:var(--rec);color:var(--rec);font-weight:600}
.tag.ok{border-color:var(--text);color:var(--text)}
.tag.alt{border-style:dashed}
.fit{position:relative;width:100%;overflow:hidden}
.dev{position:absolute;left:0;top:0;transform-origin:0 0;width:546px;background:var(--frame);border:1px solid var(--frame-line);border-radius:34px;padding:28px 31px 20px}
.home{margin:15px auto 0;width:65px;height:14px;border-radius:7px;border:1px solid var(--frame-line)}
.box{border:1px solid var(--frame-line);background:var(--surface);border-radius:8px;padding:10px 14px;margin:12px 0;max-width:860px}
table{border-collapse:collapse;font-size:13px;margin:8px 0;width:100%;max-width:860px}
th,td{text-align:left;vertical-align:top;border-bottom:1px solid var(--rule);padding:5px 8px 5px 0}
th{font-weight:600;color:var(--note)}
.tw{overflow-x:auto;max-width:100%}
.new{color:var(--ann-text);font-weight:600}
ol.q>li{margin:12px 0;max-width:860px}
ol.q .rec{color:var(--rec);font-weight:600}
.legend{display:inline-block;width:22px;height:12px;border:2px dashed var(--ann-text);vertical-align:-2px;margin-right:4px}
.zoomwrap{overflow-x:auto;max-width:100%;margin:10px 0}
.zoomwrap img{display:block;image-rendering:pixelated;border:1px solid var(--frame-line);background:#fff}
.scr,.scr *{box-sizing:content-box}
.scr{width:480px;height:800px;background:var(--paper);border:1px solid var(--frame-line);position:relative;overflow:hidden;user-select:none}
.scr img{display:block;width:480px;height:800px}
.px .scr img{image-rendering:pixelated}
.chg{position:absolute;outline:3px dashed var(--ann);outline-offset:2px;pointer-events:none}
.scr:not(.ann) .chg{display:none}
.lbl{position:absolute;font:600 15px/1.2 var(--font-sans);color:var(--ann);background:#fff;padding:1px 4px;border:1px solid var(--ann);white-space:nowrap}
.scr:not(.ann) .lbl{display:none}
"""

JS = """
let SIZE="fit";
function scale(){
 document.querySelectorAll('.fit').forEach(f=>{
  const dev=f.firstElementChild,w=dev.offsetWidth,h=dev.offsetHeight;
  const s=SIZE==='fit'?Math.min(1,f.clientWidth/w):1;
  dev.style.transform='scale('+s+')';f.style.height=Math.ceil(h*s)+'px';
  f.style.overflowX=SIZE==='fit'?'hidden':'auto';
  dev.classList.toggle('px',s===1);
 });
}
function seg(id,f){document.querySelectorAll('#'+id+' button').forEach(b=>b.onclick=()=>{document.querySelectorAll('#'+id+' button').forEach(x=>x.classList.toggle('on',x===b));f(b.dataset.v);});}
seg('annSeg',v=>{document.querySelectorAll('.scr').forEach(s=>s.classList.toggle('ann',v==='1'));});
seg('sizeSeg',v=>{SIZE=v;scale();});
let rt;addEventListener('resize',()=>{clearTimeout(rt);rt=setTimeout(scale,60);});
addEventListener('load',scale);scale();
"""


def fig(s: Screen, title: str, tag: str, caption: str, labels: list[tuple[int, int, str]] = ()) -> str:
    tags = {"built": '<span class="tag ok">as built</span>', "rec": '<span class="tag rec">signed off</span>',
            "alt": '<span class="tag alt">for comparison</span>', "approx": '<span class="tag rec">proposal</span>'}
    over = "".join(f'<div class="chg" style="left:{x}px;top:{y}px;width:{w}px;height:{h}px"></div>'
                   for x, y, w, h in s.ann)
    over += "".join(f'<div class="lbl" style="left:{x}px;top:{y}px">{html.escape(t)}</div>' for x, y, t in labels)
    return (f'<figure><figcaption><b>{title}</b>{tags[tag]}<br>{caption}</figcaption><div class="fit"><div class="dev">'
            f'<div class="scr ann"><img alt="" src="{png(s.img)}">{over}</div><div class="home"></div></div></div>'
            f'</figure>')


def render_html(S, d) -> str:
    e = html.escape
    skipped = " ".join(d["skipped"])
    parts = []
    parts.append(f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>V9a page marks</title>
<!--
  SIGNED OFF by claritise 2026-09-29 (v0.2 phase V9a, page-annotations.md A1 and A3; A2 and A4 dropped; the answers
  are at the end of the page). Generated by tools/mockups/v9a_annotations.py: edit that and regenerate, not this file.
  Every screen is drawn at the X4 Pro's native 480x800 device pixels, then scaled to fit the page. The reader's page
  and status bar follow the firmware (EpubReaderActivity::renderBook, BaseTheme::drawStatusBar, ParsedText's CJK
  line breaking and justification) with its fonts' own glyph bitmaps: the reader's SD family (NotoSerifCJK .cpfont,
  2-bit, anti-aliased at 14 pt, so glyph edges show grey as on the panel) and the built-in UI fonts (1-bit). The card
  follows the host layout's rules (src/lexirise/card/CardLayout.cpp and its goldens). The Settings and reader-menu
  screens are approximate (FreeInkUI's list; text in the firmware's fonts). The red dashed outlines and red labels
  are annotations marking what the proposal adds or points at, never drawn on the device. The book text, the words'
  saved states, ranks and meanings are made up. Self-contained: no external requests.
-->
<style>{CSS}</style>
</head>
<body>
<main>
<h1>V9a page marks: signed off</h1>
<p class="lead"><b>Signed off by claritise, 2026-09-29</b> (the answers are at the end). V9a is the first part of
<code>page-annotations.md</code>: <b>A1</b> marks under words (solid = not saved, particles included; dotted =
tracked or learning; none = fresh, known, ignored or suspended) and <b>A3</b> the side buttons stepping only between
marked words while a card is open. A2&rsquo;s page stats and A4&rsquo;s seen-again dot were dropped. Nothing here calls
Lexirise: the marks come from V7b&rsquo;s page analysis (cached on the SD card) and V7a&rsquo;s copy of the vocabulary.
Screens are the X4 Pro&rsquo;s 480&times;800 pixels, drawn from the firmware&rsquo;s own geometry and font bitmaps.</p>
<div class="ctl">
  <span><span class="legend"></span>what V9a adds (never drawn on the device)
    <span class="seg" id="annSeg"><button data-v="1" class="on">Show</button><button data-v="0">Hide</button></span></span>
  <span>Screens <span class="seg" id="sizeSeg"><button data-v="fit" class="on">Fit</button><button data-v="1">1:1 device px</button></span></span>
</div>
<nav class="toc"><a href="#ja">1 &middot; A Japanese page</a><a href="#close">2 &middot; The marks up close</a><a href="#zh">3 &middot; A Chinese page</a><a href="#a3">4 &middot; Stepping with the card open</a><a href="#rows">5 &middot; Settings and the reader menu</a><a href="#unanalyzed">6 &middot; Pages without an analysis</a><a href="#drawn">Drawn from</a><a href="#q">The answers</a></nav>

<h2 id="ja">1 &middot; A Japanese page</h2>
<p>The reader at its defaults (NotoSerifCJK 14 pt, Normal spacing, justified, extra paragraph spacing, the status bar
with battery, chapter title and progress). The marks are drawn over the page after it renders, from the page&rsquo;s
cached analysis (which word is where) and the vocabulary copy (what you know), so the text doesn&rsquo;t move. The
made-up reader here has a young vocabulary, so most words are unsaved and underlined.</p>
<div class="frames">
{fig(S["ja_plain"], "A &middot; Today", "built", "As the reader draws the page now: no marks.")}
{fig(S["ja_marked"], "B &middot; With V9a", "rec", "Solid underline: not saved (or level 0), particles included. Dotted: tracked or learning. None: fresh, known, ignored or suspended, and punctuation. The status bar keeps the chapter title.")}
</div>

<h2 id="close">2 &middot; The marks up close</h2>
<p>The first three lines of B, three times the device&rsquo;s pixels. Black only: grey would need the slow grayscale
refresh (<code>page-annotations.md</code> &sect;3).</p>
<div class="zoomwrap"><img alt="The marks at 3x" width="{d['close_size'][0]}" height="{d['close_size'][1]}" src="{d['close']}"></div>
<div class="box"><table>
<tr><th>Mark</th><th>Geometry (device px)</th><th>Why</th></tr>
<tr><td>Solid underline (not saved)</td><td>2 px tall, rows baseline + 6 and + 7; from the word&rsquo;s first character + 2 to its last character&rsquo;s advance &minus; 2</td><td>The CJK ink ends at baseline + 4 and the next line&rsquo;s starts at baseline + 16 (14 pt, 42 px lines): the line sits in the white between them. The 2 px pulled in at each end keep two marked words side by side apart (CJK has no spaces)</td></tr>
<tr><td>Dotted underline (tracked, learning)</td><td>2&times;2 squares every 4 px, same rows and ends</td><td>Reads as dotted at arm&rsquo;s length, and is still black-only</td></tr>
<tr><td>A word across a line end</td><td>Marked on both lines</td><td>&mdash;</td></tr>
<tr><td>The card&rsquo;s highlight</td><td>unchanged (inverted, 1 px side padding); the mark under the highlighted word disappears into it</td><td>The approved card stays as it is</td></tr>
</table></div>

<h2 id="zh">3 &middot; A Chinese page</h2>
<p>The same rules; the reader&rsquo;s font is the same NotoSerifCJK family (Japanese letterforms, slimming question S1).</p>
<div class="frames">
{fig(S["zh_plain"], "C &middot; Today", "built", "The page as drawn now.")}
{fig(S["zh_marked"], "D &middot; With V9a", "rec", "了, 的, 着 are unsaved here, so they&rsquo;re underlined too.")}
</div>

<h2 id="a3">4 &middot; Stepping with the card open (A3)</h2>
<p>Today the side buttons step to the previous or next word of the sentence, then on through the page&rsquo;s next
sentences (<code>popup-ui.md</code> &sect;3.3). With A3 (on by default) they step only between <b>marked</b> words
(solid or dotted), across the page: a fresh, known, ignored or suspended word is skipped. Past the page&rsquo;s last marked
word a press stops. The card itself doesn&rsquo;t change. On a page the analysis hasn&rsquo;t reached there are no marks,
so the buttons step every word as today. With a young vocabulary nearly every word is marked, so A3 skips little.</p>
<div class="frames">
{fig(S["a3_on"], "E &middot; The card on じっと", "rec",
     f"Tapped じっと (learning, dotted). The outlined word is where <b>Side Right</b> goes next: の, the next marked word, skipping {e(skipped)} (known). Without A3 it would go to 雲. The page under the card keeps its marks (the card draws the page body only, no status bar, as today).")}
{fig(S["a3_next"], "F &middot; After Side Right", "rec",
     "The card on の (not saved). Side Left goes back to じっと.")}
</div>

<h2 id="rows">5 &middot; Settings and the reader menu</h2>
<p>Two rows in <b>Settings &rarr; System &rarr; Lexirise</b>, in a new group after General; the web page&rsquo;s Lexirise
section shows the same rows, as it follows the device&rsquo;s. For one book, the reader menu gets one row that turns
the marks and A3 off for that book (kept like its Lookup language). These two screens are approximate (the
list&rsquo;s spacing isn&rsquo;t re-derived pixel for pixel).</p>
<div class="frames">
{fig(S["set_built"], "G &middot; Settings today", "built", "Lexirise&rsquo;s General group (scrolled to it).")}
{fig(S["set_new"], "H &middot; Settings with V9a", "rec", "A new group, <i>On the page</i>: Mark words on the page (On), Side buttons on a card: Marked words / Every word (Marked words). Shown while Lexirise is on and a key is set.")}
{fig(S["menu_built"], "I &middot; Reader menu today", "built", "The list menu (the default style), from the top.")}
{fig(S["menu_new"], "J &middot; Reader menu with V9a", "rec", "<i>Page marks: On / Off</i> after Lookup language: this book only. Off draws no marks in this book, and the side buttons step every word. The toolbar menu&rsquo;s More panel gets the same row, as it has Lookup language.")}
</div>

<h2 id="unanalyzed">6 &middot; Pages without an analysis</h2>
<p>A page is marked only once V7b has analyzed it, which happens only while WiFi is already up (claritise&rsquo;s
&ldquo;only if already on&rdquo;): the next page is analyzed while you read this one, for about 5 minutes after a card
(<code>wifi_idle_min</code>), and every analyzed page is kept on the SD card. A page without an analysis looks exactly
like frame A (and C): no marks, and the side buttons step every word. It stays plain until the next turn (no extra
refresh when its analysis lands).</p>

<h2>What changes</h2>
<ul>
<li><span class="new">Marks</span> drawn over the page after it renders (A1): no reflow, no layout change, no new font.</li>
<li><span class="new">The side buttons</span>, with a card open, skip unmarked words (A3). The card draws nothing new.</li>
<li><span class="new">Settings</span>: a group of two rows. <span class="new">Reader menu</span>: one row per book.</li>
<li>New strings: <code>On the page</code>, <code>Mark words on the page</code>, <code>Side buttons on a card</code>,
<code>Marked words</code>, <code>Every word</code>, <code>Page marks</code>.</li>
<li>Refresh: a marked page costs nothing extra (the marks are drawn with the page). A save or level change on the card
shows on the page when the card closes (the page is redrawn then anyway).</li>
</ul>

<div class="box" id="drawn">
<h3>Drawn from</h3>
<div class="tw"><table>
<tr><th>What</th><th>Device px</th><th>Source</th></tr>
<tr><td>Viewable insets</td><td>top 9, right 7, bottom 3, left 7</td><td><code>BoardConfig.h</code> <code>XTEINK_X4_PRO</code>, <code>GfxRenderer::getOrientedViewableTRBL</code></td></tr>
<tr><td>Text area</td><td>x 12, y 14, 456 &times; 764 (screen margin 5; bottom: the status bar&rsquo;s 19)</td><td><code>EpubReaderActivity::renderBook</code>, <code>CrossPointSettings.h</code> (<code>screenMargin</code>), <code>UITheme::getStatusBarHeight</code></td></tr>
<tr><td>Lines</td><td>42 px (NotoSerifCJK 14 pt&rsquo;s line, compression 1.0), baseline at + 34; half a line after a paragraph; each CJK character 29 px, justified by whole pixels per gap between break units</td><td><code>getReaderLineCompression</code>, <code>ChapterHtmlSlimParser.cpp</code> (paragraph spacing), <code>ParsedText.cpp</code> (break rules, <code>computeJustifyExtra</code>)</td></tr>
<tr><td>Status bar</td><td>text top 774, Noto Sans 8; battery at x 13; the chapter title centred (a CJK title in the SD family at 8 pt); progress right-aligned to 468</td><td><code>BaseTheme::drawStatusBar</code>, <code>drawBatteryLeft</code>, <code>LyraTheme::fillBatteryIcon</code></td></tr>
<tr><td>Card</td><td>popup-ui.md &sect;1.1; rows as the host layout places them</td><td><code>src/lexirise/card/CardLayout.cpp</code>, <code>test/lexirise_card/golden/ja-card-*.json</code></td></tr>
<tr><td>Fonts</td><td>page: NotoSerifCJK 14 pt (2-bit); card word 18 pt, reading 8 pt; UI: Noto Sans 8, Ubuntu 10 and 12</td><td><code>sd-card/fonts/NotoSerifCJK/*.cpfont</code> (the reader&rsquo;s family), <code>firmware/lib/EpdFont/builtinFonts/*.h</code>; decoded by <code>tools/mockups/epdfont.py</code></td></tr>
<tr><td>Settings and reader menu</td><td>approximate: header band 84, rows 56 with a 6 px gap, 20 px inset</td><td><code>LyraTheme.h</code>, <code>UIThemeTokens.h</code>, FreeInkUI <code>FreeInkApp.h</code>, <code>list.h</code></td></tr>
</table></div>
</div>

<h2 id="q">The answers (2026-09-29)</h2>
<ol class="q">
<li><b>New words</b>, claritise: &ldquo;Every unsaved word&rdquo;. A1 as written: every word not saved (or at level 0)
gets the solid underline, particles included.</li>
<li><b>The seen-again dot (A4)</b>, claritise: &ldquo;Drop it&rdquo;.</li>
<li><b>The status bar</b>, claritise: &ldquo;Keep the chapter title&rdquo;. No page stats (A2 draws nothing).</li>
<li><b>A3</b>, claritise: &ldquo;Marked words by default&rdquo;. A Settings row switches back to every word; past the
page&rsquo;s last marked word a press stops.</li>
<li><b>The rest</b> (the recommended defaults, decided by the coordinator): the radio stays &ldquo;only if already
on&rdquo;; the underline 2 px at baseline + 6, dotted 2&times;2 every 4 px; a page not analyzed yet stays plain until the
next turn; settings: the two global rows above and the per-book <i>Page marks</i> row.</li>
</ol>
</main>
<script>{JS}</script>
</body>
</html>
""")
    return "".join(parts)


if __name__ == "__main__":
    main()
