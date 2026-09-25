# The card: layout, progressive fill, input

**Status:** proposed 2026-09-24. Decisions D6, D12, D13 in `00-overview.md`.

> ## ⛔ BINDING DESIGN: BUILD EXACTLY THIS, PIXEL-PERFECT
>
> **The card and expanded view were approved by claritise on 2026-09-24 with no open complaints.**
> **`reference/card-reference.html` is the spec.** Open it in a browser: every state, tab,
> language and interaction is there. **§1.1 gives every measurement in device pixels.** The
> firmware must reproduce it **pixel-perfect** on the X4 Pro's 480×800 panel.
>
> - **Do not restyle, "improve", simplify, add chrome, re-order, rename or drop anything.** No
>   extra rows, labels, icons, borders, rounded corners, shadows or animations. No button legend.
> - **The only allowed deviations** are listed in §1.1 ("Sanctioned deviations"). Anything else,
>   however small, needs **claritise's explicit sign-off first**, and must update
>   `reference/card-reference.html` and §1.1 **in the same commit**.
> - Earlier layouts in this repo's history (the grid-of-cells, the rounded drawer, the button
>   legend, `︿ More` / `✕ Close` bars) were **rejected**. Don't revive them.
> - Build phases P4 and P5 **cannot pass** without the screenshot comparison in
>   `01-build-order.md` (the "Design conformance" gate).

Related: `lookup-flow.md` §5–7 (what fills it, and when), `offline-and-errors.md` (error states).

---

## 0. Constraints

- 4.3", **800×480** (~217 ppi), mono (SSD1677, or UC8179 / UC8279 depending on the batch). **A full refresh flashes and takes about
  half a second. A partial refresh is fast but leaves ghosting.**
- The reader page underneath is already drawn. The card is drawn **over the bottom of the page,
  always** (the controls must stay under the thumb, §3). **The page never moves.** If the card would
  cover the active word, the card gains the **context strip as its top row** (D17, in the reference:
  "Sentence: Low on page"). The test is geometric, re-run on every step: word box bottom > card top.
- Existing idioms: `DictionaryWordSelectActivity` already does **differential highlight repaint**
  (framebuffer snapshot under the box), and draws popups with `UITheme`. Re-use those. Don't invent
  new chrome.

## 1. Layout

**Visual style (decided 2026-09-24, claritise):** one 2px frame, dividers only between real sections,
a small T L F K control top right, and the rank row as the bottom row. **No button legend** (the X4 Pro has no buttons it could describe, D15). The grid-of-cells and
drawer variants tried the same day were rejected. Touch is added **without adding chrome** (§3).

```
┌───────────────────────────────────────────┐
│ わずらわしい                 [T][L][F][K] │  reading · level control (tap = save / set)
│ 煩わしい ┌──┐                 not saved   │  lemma, large · JLPT/HSK badge · state
│          │N1│                             │
│ 煩わしくて te-form ‹adjective›            │  surface form + conjugation · POS
├───────────────────────────────────────────┤
│ troublesome, annoying, bothersome,        │  translation, ≤ 2 lines
│ cumbersome                                │
├───────────────────────────────────────────┤
│ ▮▯▯▯▯ #29,774 rare                ▼  │ ✕ │  rank · tap the row (▼) = detail view · ✕ closes
└───────────────────────────────────────────┘
```

**Detail view:** the same card, taller. It stops one text line below the top of the screen, leaving a
**context strip**: ~~the line of the page that holds the active word~~ **the active word at the strip's
left edge, highlighted, then as much of its sentence after it as fits** (nothing before it; claritise,
2026-09-25, P9: the scrolled page line jumped about as words stepped). When the active word changes (side
buttons, §3.3), the next word takes the left edge. Before the sentence is analyzed (phase 0) the strip
shows the page line as before. A small `line 2/5` marker sits at the strip's right edge. (The card view's
strip, shown when the card covers the word, is still the page line.)
Same frame and header. Tab content in the middle. Then **the tab row**: Meaning · Examples · Context · Kanji/Chars · Form (Japanese only) · **`⋯`**, styled like T L F K
(the active tab is filled black). `⋯` is an icon-width tab holding the **actions** (Undo save, once saved · Save the sentence as a card ·
Ignore this word · Look up later: v0.2 C17), so the word tabs keep room for their labels. Examples fall back to your own sentences (this one, and
"met before") when Lexirise has none, or only Traditional ones in a Simplified book, then the **same rank row,
now pointing up, `▲`**, with ✕ at its end. Everything you tap stays in the bottom part of the screen.

### 1.1 Measurements (binding) — device pixels on the 480×800 panel

The reference is drawn at 340×560 CSS px, and the panel is 480×800. **Scale = 480 / 340 ≈ 1.412**,
rounded to whole pixels. The card is anchored to the bottom edge, so the ~10 px of extra panel height
goes to the page (card view) or the strip (expanded view). **Tolerance: ±1 px** on any position or
size. Colours are **exactly black or white**, never grey (see the deviations below).

| Element | Reference (CSS px) | **Device (px)** |
|---|---|---|
| Card inset from left / right / bottom edges | 10 | **14** |
| Card frame | 2 | **3** |
| Section dividers (header, meaning, tabs, rank row, ✕ cell) | 1 | **1** |
| Row padding (vertical / horizontal) | 8 / 12 | **11 / 17** |
| Reading line (kana / pinyin), UI or reader font | 12 | **17** |
| Word (lemma), reader font, line-height 1.2 | 30 | **42** (line box 51) |
| JLPT/HSK badge: text / frame / h-padding / line box / gap after word | 12 / 2 / 5 / 18 / 8 | **17 / 3 / 7 / 25 / 11** |
| T L F K: cell width / v-padding / text / outer frame / inner dividers | 30 / 5 / 13 / 1.5 / 1 | **42 / 7 / 18 / 2 / 1** |
| State text under T L F K ("not saved", "learning") / gap above | 11 / 4 | **16 / 6** |
| Reading-line tap area (Japanese): the reading text plus padding, left of T L F K | text + 6 top, 8 each side | **text + 8 top, 11 each side** (no visible border) |
| Surface form + conjugation line | 12 | **17** |
| POS pill: text / frame / corner radius / h-padding | 11 / 1 / 3 / 5 | **16 / 1 / 4 / 7** |
| Meaning line on the card | 14 | **20** |
| Rank row: v-padding / h-padding / text | 10 / 12 / 12 | **14 / 17 / 17** |
| Frequency bars: bar width incl. frame / gap / heights | 4 / 2 / 6·9·12·15·18 | **6 / 3 / 8·13·17·21·25** |
| Filled bars | ⌈frequency_score × 5⌉, min 1 | same |
| ✕ cell width / glyph | 44 / 15 | **62 / 21** |
| ▼ / ▲ glyph (right end of the rank row, before ✕), no text | 12 | **17** |
| **Expanded view:** context strip height = card top offset | 50 | **71** |
| Strip text | the page's own reader font and size | same (follows the user's settings) |
| Strip line marker (`line 2/5`): text / right / top | 10 / 8 / 4 | **14 / 11 / 6** |
| Strip text clip: ends before the marker (both strips) | 40 from the right padding edge | **56** |
| Strip auto-scroll (the card view's strip, and the detail view's before its word is known; P9's detail strip starts at the word and doesn't scroll): if the active word is past the clip, shift the line left so the word ends 8 px (**11**) inside the clip | — | — |
| **Card-view strip (D17)**, the card's first row, only when the word is covered: height / h-padding / divider under it | 36 / 12 / 1 | **51 / 17 / 1** |
| Card-view strip text / marker text / marker right / marker top | reader font (17 in the reference) / 10 / 6 / 2 | **reader font** / **14 / 8 / 3** |
| Tab row: text / v-padding | 11 / 8 | **16 / 11** |
| `⋯` tab: width / glyph | 30 / 14 | **42 / 20** |
| Meaning tab senses: text / line-height | 14 / 1.7 | **20** / 1.7 |
| Examples and Context sentences: text / line-height; small labels | 15 / 1.8; 11 | **21** / 1.8; **16** |
| Kanji/Chars tab: reading / character / gloss / gap | 11 / 28 / 14 / 14 | **16 / 40 / 20 / 20** |
| Form tab: forms / labels, line-height 2 | 16 / 14 | **23 / 20** |
| `⋯` action rows: frame / v-padding / h-padding / text / gap | 1.5 / 8 / 10 / 13 / 7 | **2 / 11 / 14 / 18 / 10** |
| Toast ("Saved as learning · Undo"): frame / text / padding / top | 2 / 12 / 4·10 / 60 | **3 / 17 / 6·14 / 85** |
| Word highlight on the page and in the strip | inverted, 1 px side padding | **inverted, 1 px side padding** |

**Typefaces:** Japanese and Chinese text (the word, the page, the strip, sentences and characters)
uses the **reader's current font**. Everything else uses **CrossPoint's UI sans font**. Where a font
can't render an exact size, use the nearest size and **log it in the P4 ledger note**.

**Sanctioned deviations (the only ones):**
1. The reference's **grey helper text** (e.g. "No Lexirise examples…", "Not inflected here…")
   is drawn **black** on the device. There's no fast grey on this panel.
2. **Glyph rasterization** differs between browser and device fonts. Glyph shapes may differ; box
   positions and sizes may not.
3. **Page text** (in card view and in the strip) follows the user's reader settings. The reference's
   19 px serif is a stand-in.
4. The reference's device frame, side-button and Home-pad drawings are illustration only.

- **Chinese:** the reading is pinyin with tone marks, and there is no surface-form line (`languages.md` §3).
- **Reading on top, small; lemma large.** Same order as furigana. Show the reading as **kana**. If
  `transliteration` comes back in romaji (the brief's example has `"neko"`), convert it or show it
  as is. P0 checks what `ja` actually returns.
- **Proficiency** is the level row (§3): the saved word's level is filled in. A new word has
  nothing filled, and tapping a level saves it. The state (`learning`, `not saved`) is also shown
  top right as text.
- **Level badge** (`N3` for Japanese, `HSK 4` for Chinese) sits **next to the word**, as a small
  boxed label, so it's visible without opening anything. Source (verified live 2026-09-24): **`dictionary/lookup` →
  `system_tags`**, matching `^JLPT-N[1-5]$` or `^HSK-(\d|7\+)$`. Lexirise uses **HSK 3.0**, with 7–9
  banded as `HSK-7+`. The level isn't in `analyze/text`, so the badge **appears in phase B**, with
  the translation. No tag means no badge (兄貴 has none). That's common past N1 or HSK 6, and it's
  information too. JLPT has had no official list since 2010, so say in the user docs that these are
  Lexirise's lists. No local list on SD is needed.
- **Japanese reading: kana ⇄ romaji, tap to switch** (approved 2026-09-24, claritise). Tapping the
  reading line switches every Japanese reading on the card: the header reading, the Kanji tab
  readings and the "also" readings. The existing toast confirms it (`Readings: romaji` /
  `Readings: kana`), and nothing else is drawn. **The choice is remembered** as the
  `reading` setting in `/.lexirise/config.ini` (`settings.md`), the same value as **Settings → Lexirise →
  Japanese readings**. Changing it in either place changes both. The default is `kana`. Romaji comes
  straight from the API. **Kana is converted on the device** (`languages.md` §3a), exactly and
  without guessing, and it falls back to romaji for any word it can't fully convert. Chinese pinyin
  doesn't toggle, and a tap there does nothing.
- In the detail view's Kanji tab, each character also gets its own level (JLPT kanji level, HSK
  character level) where the data has it.
- **Rank** shows as `#1,846` plus a single word (`very common`, `common`, `uncommon`, `rare`), with
  thresholds per language (`languages.md` §6: Japanese < 1k / 5k / 20k, Chinese < 1k / 10k / 30k). If
  rank is missing, the row shows only the state.
- **Translation** is the first sense, then the second after `;` if it fits on 2 lines. Longer text
  is cut with an ellipsis. There's no paging in v0.1: this is a glance card, not a dictionary.
- **Font:** the reader's current font (already loaded, and it has CJK glyphs) at two sizes. No new
  font assets.

## 2. Progressive fill (D6)

| Phase | When | Drawn |
|---|---|---|
| **0 · pending** | Tap (≤ 50 ms) | Card frame plus the tapped character, large, with `…`. Partial refresh. Gives instant feedback while WiFi and TLS come up |
| **A · analyzed** | After `analyze/text` | Reading, lemma, surface form, POS, proficiency, state or Save. **The page highlight grows from the character to the whole word** (`charStart..charEnd`). Partial refresh of the card and the highlight |
| **B · complete** | After `dictionary/lookup` | Translation and rank. Partial refresh of those two rows only |
| **B′ · pending translation** | `translation_status != "ready"` | `translation pending` in grey. No polling in v0.1 |

Save is **enabled from phase A**. If it's pressed before phase B, the save waits for B
(the translation is part of the payload), and the button shows `Saving…`.

**Measured on claritise's X4 Pro (UC8279 panel, 2026-09-24): a partial refresh takes ~0.49 s, a full one ~1.34 s.** So each progressive phase and each side-button word step costs about half a second on screen. Merge phases A and B into one refresh when B arrives within ~300 ms of A. (The bench does. The live card draws A before it asks for B, since a call blocks the loop while it runs and the screen mustn't change under a tap: `lookup-flow.md` §5b.)

**Refresh policy:** every phase uses a partial refresh. On dismiss, redraw the page with a partial
refresh, and schedule a **full refresh after every 5th card**, the same way the reader counts page
turns for ghost cleanup. Tune this on hardware.

## 3. Input: touch first, one-handed (D12, revised 2026-09-24)

The X4 Pro is held in one hand, and the thumb rests on the bottom of the screen. **Its only
physical inputs are the two side page buttons, the Home pad and Power (D15)**, so every card action
is a tap. There's no on-screen button legend.

### 3.1 Where the touch targets are

| Target | Card | Detail view |
|---|---|---|
| **T L F K** (top right of the card, ~28px cells, wider than the button-only version) | Save / set the level | Same |
| **Rank row**, full width, ending in an arrow (`▼` / `▲`, no text) | Open the detail view | Back to the card |
| **Tab row** | — | Switch tabs |
| **Reading line** (Japanese) | Switch kana ⇄ romaji (remembered) | Same |
| **`✕`** at the end of the rank row | Close | Close |
| The page outside the card | Close | — (the detail view covers it) |

The rank row (with ✕) doesn't move between the two views. T L F K sit at the top of the card, which
on the X4 Pro is still in the lower half of the screen. Check thumb reach on hardware in P4, and if
it's a stretch, the fallback is to let the rank row carry the level control instead.

### 3.2 Touch

| Gesture | Action |
|---|---|
| **Tap T / L / F / K** | **Saves the word at that level** (new word: `POST`), or **sets the level** (saved word: `PATCH`). One tap. A 2 s `Saved as learning · Undo` toast. Tapping Undo reverts it: for a new save, `DELETE`, then `PATCH {notes: null, customTranslation: null, tags: []}`, because Lexirise only resets dictionary words to *unknown* and keeps their notes and tags (tested 2026-09-24). For a level change, `PATCH` back to the old level |
| **Swipe up** on the card, or tap the rank row (`▼`) | Open the detail view |
| **Swipe down** on the detail view, or tap the rank row (`▲`) | Back to the card |
| Tap **`✕`** (either view), press **Home** or swipe **Back** (the left edge, as everywhere in CrossPoint: the detail view goes back to the card first), **swipe down** on the card, or tap the page outside the card | Close the dictionary |
| **Tap a tab**, or **swipe left/right** across the content | Change tab (the only way to change tabs, since the side buttons step words in both views) |
| **Long-press another word** on the page | Replace the card with a new lookup |
| **Tap the reading line** (Japanese only) | Switch all readings kana ⇄ romaji, with a toast. Remembered across lookups and reboots |

**As built (P7, `lookup-flow.md` §5c):** every row above works; the swipes need a start on the card, and a
long-press on another word replaces the card in card view only (the detail view covers the page). **Where a
close goes depends on the entry point:** a card opened by a long-press on the page closes to the reader; one
opened from word select (reader menu → Look Up) closes to word select. (P10: with Lexirise, word select is
only ever opened by a long-press, so a close always goes back to the reader: `lookup-flow.md` §5g.)

**Gesture safety:** CrossPoint reserves three edge swipes (`MappedInputManager`): left edge → right
is **Back**, top edge → down is the **frontlight panel**, and bottom edge → up is the **reader menu**
on Home-key boards like the X4 Pro. Card swipes are only recognised when they **start inside the
card and at least ~10 mm from the left and bottom edges** (as built, P7: the top edge too, and never inside
the SDK's own edge-gesture bands, `lookup-flow.md` §5c). Use `wasSwipe()` together with the touch
origin, and never `wasBackGesture()` territory.

### 3.3 Physical inputs on the X4 Pro

| Input | Card | Detail view |
|---|---|---|
| **Side Left / Right** (page buttons) | Previous / next word in the sentence. **Past its last word, on into the page's next sentence** (P9, claritise 2026-09-25): the card stays on the word it's on while that sentence is analyzed (one call), then moves to its first word, in either view and on the same tab; stepping back returns through the sentences seen. If it can't load (offline, a rejected key, a rate limit) a toast says so and the next press tries again. It stops at the page's end, and never goes back before the tapped sentence. Re-runs only `dictionary/lookup` within a sentence | **Same: previous / next word.** The context strip moves on with it (the word at its left edge, §1), and **the current tab stays open** (step through the Kanji tab word by word) |
| **Home** (capacitive pad) | Close | Back to the card |

The side buttons mean **"next / previous" everywhere**: a page when the card is closed, a word when it's open. They never switch tabs. That keeps one meaning per button (claritise, 2026-09-24).
| **Power** | Untouched (sleep, as everywhere) | — |

Saving and setting the level are **touch only**. That's deliberate: the page buttons are for moving,
and a save should never happen from a mis-press while turning pages.

## 4. Strings (I18n)

New `STR_LEXI_*` keys in `lib/I18n/translations/english.yaml` (and `japanese.yaml` if present):
`SAVE`, `SAVING`, `SAVED`, `PROF_0..4`, `RANK_*`, `TRANSLATION_PENDING`, `OFFLINE`, `NO_KEY`,
`NOT_FOUND`, `RATE_LIMITED`, `AUTH_FAILED`, `SAVE_FAILED_RETRY`.

**As built (P6):** every word on the card is a key named after its `CardStrings` field
(`STR_LEXI_CARD_*`), plus `STR_LEXI_NO_KEY`, `STR_LEXI_AUTH_FAILED`, `STR_LEXI_RATE_LIMITED`,
`STR_LEXI_OFFLINE` and `STR_LEXI_SAVE_FAILED` (`offline-and-errors.md` §3a).

## 5. Bench first

Before wiring the network, build the card as a **static activity fed from a recorded response**
(`01-build-order.md` P4). That's how layout, fonts, placement and refresh get tuned on hardware
without WiFi in the loop.
