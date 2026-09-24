# Page annotations: marking the page with what you know

**Status:** proposed 2026-09-24. Supersedes backlog item C6 (`00-overview.md`) and folds in the
other page ideas. Nothing here is built. It depends on **page analysis** and the **vocab mirror**
(§1), which it shares with C5 (difficulty preview) and C11 (the SRS app).

Related: `../v0.1/sentence-extraction.md` (the page → text walk, reused in reverse),
`../v0.1/lookup-flow.md` (word select), `../reference/lexirise-api-notes.md`, `../v0.1/languages.md`.

---

## 0. Why

The card only helps once you've stopped on a word. Annotations help **while you read**. You see at a
glance what's new, what you're learning, and how hard the page is, and the reading support fades as
your level rises. This is what `analyze/text` was built for (it powers Lexirise's own reader).

## 1. Foundations (build these first, they pay off everywhere)

### 1.1 Page analysis

- **One `analyze/text` per page**, with the whole page's base text (ruby excluded, D5 rules), not
  per lookup. **Use the default mode, not `fast`**: `fast` drops lemmas, so inflected words wouldn't match saved lemmas (tested 2026-09-24).
- **Prefetch:** while page *n* is on screen, analyze page *n+1*. On a normal forward read, the
  annotations are ready when the page turns, so there's **never a second refresh**. On a jump (a TOC
  link, go-to-percent, turning backwards), the page is drawn plain and the marks come in with one
  partial refresh, and only if they arrive within 1.5 s. Otherwise the page stays plain until
  the next turn.
- **Debounce fast flipping:** don't prefetch while pages turn faster than 1 per 1.5 s.
- **Cache** results on SD: `/.lexirise/cache/<bookId>/<section>-<pageStart>.bin` (compact
  occurrences, no strings the vocab mirror already has). The key includes font and layout settings,
  since a page boundary moves with them. Invalidate on a vocab-mirror change for any entry on that page.
- **Lookups reuse it:** a tap on an analyzed page skips request ① entirely. The card goes straight to
  phase A, and only `dictionary/lookup` goes out.
- **Budget:** 60–150 pages/h is 5–13% of the 1200 req/h limit. Watch `rateLimitMax` from `/me`, and
  stop prefetching (keep on-demand lookups) above 70% of the window.

### 1.2 Vocab mirror

- Page through `GET /v1/vocabulary?language=<lang>&limit=200` once (1 request per 200 items) into
  `/.lexirise/vocab-<lang>.bin`: `entryId → {proficiency, savedId, seen, suspended, next_review_at}`.
- **Incremental sync** on each WiFi-up: `sortId=updated_at&sortDesc=true`, stopping at the first item
  older than the last sync.
- **Local writes** (save, level change, ignore) update the mirror straight away, so annotations
  reflect a save on the same page.
- Annotation state comes **from the mirror**, not from `stateByEntryId`. The page analysis only
  supplies the word → entry mapping, so marks still work offline for cached pages, and for uncached
  pages once the chapter is analyzed.

## 2. The annotations

Each one is a separate on/off setting (`/.lexirise/config.ini`), set per book from the reader menu.

| # | Annotation | What it shows | Default |
|---|---|---|---|
| A1 | **Proficiency marks** | New (not saved or level 0): **solid underline**. Tracked or learning (1–2): **dotted underline**. Fresh or known (3–4): no mark. Suspended/ignored: no mark | On |
| A2 | **Page stats** | Status bar: `6 new · 2 learning · 89% known` (running-token coverage, as in C5) | On |
| A3 | **Skip to unknown** | With the card open, the side buttons step between A1-marked (unknown or learning) words only, instead of every word (D16). A setting switches back to every word | On |
| A4 | **Seen-again marker** | A small dot after a word you're *learning* when it reappears. Tells you "you saved this, here it is again" | On |
| A5 | **Above-level only** | Restrict A1 to words above a target (`target_level=N2` / `HSK-4`), using `system_tags` from the mirror's embedded `dictionary_entry` | Off |
| A6 | **Adaptive furigana / pinyin** | Readings as ruby **only above words that aren't known** (level < 3). As you learn, the furigana fades on its own | Off |
| A7 | **Hide publisher ruby over known words** | The opposite of A6, for books that print ruby everywhere: suppress the EPUB's own ruby over known words | Off |
| A8 | **Page glossary** | New words on the page get superscript numbers, and a strip at the bottom lists `n word reading · meaning`, capped at 5 lines | Off |

Chapter-level extras (same data, no inline drawing):

| # | Feature | Notes |
|---|---|---|
| A9 | **Chapter primer** | On opening a chapter, the 10 most frequent unknown words in it, with meanings. `⏎` saves all of them. Requires analyzing the chapter (sampled, or all of it if the text limit allows) |
| A10 | **End-of-chapter recap** | Words looked up in this chapter, with `Save all` for the unsaved ones |
| A11 | **Look up later** | Offline: long-press flags the word (stored with its sentence). On the next WiFi-up, the flags are looked up, and a "Flagged words" list appears in the reader menu |

## 3. Drawing (the renderer constraints)

- **Overlay, don't reflow.** A1, A4 and A8's superscripts are drawn **after** the page renders, from
  our code, using word boxes (the same `WordBox` geometry as word select). Nothing inside
  `ParsedText` or the layout changes. That keeps the rebase cost low, as in v0.1.
- **Map spans to glyphs:** each occurrence's `[charStart, charEnd)` (UTF-16) is mapped back to page
  `(line, token)` ranges. It's the inverse of `SentenceBuilder`, shares its unit conversion, and gets
  host tests with the same fixtures.
- **A6 needs line space.** Ruby is laid out at parse time (`TextBlock::getRubyShift`). Rather than
  re-layout per annotation, **furigana mode reserves ruby space on every line** (the same line height
  as a ruby-bearing line), and the readings are overlaid into that space. The trade-off is fewer lines
  per page in exchange for zero reflow. Changing A6 re-lays out the book once, like a font change.
- **A7** is the one annotation that must act at layout: a known-word test while `ChapterHtmlSlimParser`
  collects ruby. It needs the mirror to be available when the chapter is parsed, and a fixed rule when
  it isn't (keep all ruby). It's the most invasive hook, so it's built last.
- **A8 takes space at the bottom** (up to 5 lines). The page reserves it only when A8 is on, which
  again means a one-time re-layout.
- **Mono only:** solid, dotted and double underlines, a small filled dot, superscript digits. No grey
  (grey needs grayscale refresh, which is slow and ghosts). Underline sits **below the descender
  line**, so it doesn't touch ruby from the line below.
- **Refresh:** annotations arrive with the page (prefetch) and cost nothing extra. The late-arrival
  case is one partial refresh of the text area. Count it in the reader's ghost-cleanup cadence.

## 4. Dependencies and blockers

| Needs | For | Status |
|---|---|---|
| Page analysis (§1.1) | Everything | Not built. v0.1 analyzes per sentence |
| Vocab mirror (§1.2) | Offline marks, A4, A5, immediate updates after a save | Not built |
| **Kana readings** | A6 for Japanese, A8 readings | **Solved:** converted on the device (`../v0.1/languages.md` §3a). Chinese pinyin works as is |
| `analyze/text` maximum text length | Page analysis in one request, and A9 | **No limit hit up to 20k chars** (tested). ~70 bytes of response per character, so ~20 KB per page |
| Whether analyze bumps `seen_count` | Whether prefetching inflates your stats | **Tested: it doesn't.** Prefetching is safe |
| Tokenizer quality | A1 accuracy (一日中雨 came back as one token) | Report issues to Lexirise |

## 5. Build order

1. **Page analysis + cache + prefetch.** Lookups get faster straight away. That alone is worth shipping.
2. **Vocab mirror.**
3. **A2 page stats** (no drawing, proves the data path).
4. **A1 marks + A4 seen-again** (the overlay drawing path, span → glyph mapping).
5. **A3 skip to unknown.**
6. **A5 above-level.**
7. **A10 recap, A11 look up later** (lists, no inline drawing).
8. **A6 furigana**, then **A8 glossary**.
9. **A9 primer**, then **A7 publisher-ruby hiding** (the most invasive).

Each step gets a bench phase (a static page plus recorded analysis) before it touches the network,
the same pattern as the v0.1 card.
