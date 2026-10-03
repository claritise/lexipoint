# matcha-reader: the closest prior art

**Read 2026-10-04** at commit `3a5210d` of [eszter007/matcha-reader](https://github.com/eszter007/matcha-reader)
(MIT, a CrossPoint fork): the README, `lib/Dict/` (lookup and deinflection),
`src/activities/reader/WordSelectionScan.*` and `tools/manga_convert/convert_manga.py`. Not built or run. No code or
rule data is taken from it; where an idea is worth having, it's listed as such and would be written for Lexipoint.

## 1. What it is

A CrossPoint fork for reading Japanese, on every CrossPoint device (X4, X3, X4C, X4 Pro, Papermono, Sticky), buttons
or touch. Everything works offline except page translation and manga OCR (both Gemini, under the user's own key).

| | matcha-reader | Lexipoint |
|---|---|---|
| Devices | All CrossPoint devices, buttons or touch | X4 Pro, touch only (D20) |
| Languages | Japanese segmented; other languages by spaces, through StarDict | Japanese and Simplified Chinese |
| The word | Found on the device by dictionary longest match (§2) | Lexirise's `analyze/text` (D4) |
| The meaning | Yomitan dictionaries converted for the device (Jitendex, JMnedict, a grammar dictionary); StarDict elsewhere | Lexirise; StarDict on the card when Lexirise doesn't answer |
| Saving | A CSV per language on the card, for Anki import (word, reading, sentence, entry, book, tags) | The Lexirise account, with a level, tags, a deck per book |
| Vertical text | Yes (columns, kinsoku, furigana, emphasis marks), with a word cursor down the column | No |
| Manga | Built: panel by panel, lookup in speech bubbles, a translation per panel (§3) | Specced (`../v0.2/manga.md`), Mac pipeline only |
| Also | Page translation, reading stats by language, a cover-grid library, OTA from its own releases, a browser converter, a simulator | The card's own (other readings, conjugation, met before, page marks) |

## 2. How it finds the word

- **Longest match against the dictionary** (`WordLookup::lookup`): from a position, windows of 8 characters down to
  1; each window is looked up as written, then as each deinflected form. The first hit, the longest, wins.
- **Deinflection** (`Deinflector`): about 150 suffix rules for verbs and い-adjectives, chained, each tagged with the
  word class it produces. A deinflected form counts only if its entry has that class (a godan rule must land on a
  godan verb), which kills collisions like って → う (鵜). Skipped for windows not ending in hiragana.
- **Guards learned in use** (`WordSelectionScan`): no word starts on a small kana (っ, ゃ…), except the quotative
  って, and then only as an exact entry; hiragana-only text doesn't match a kanji headword unless the entry is marked
  usually-kana (ました → 真下 is dropped; ちょっと → 一寸 kept); a trailing particle is stripped from a match; names
  are searched only for windows with kanji or katakana.
- **The page is pre-scanned** in slices while the page is read (thousands of SD lookups, ~2.5–4 s a page by its own
  comments), so the cursor only lands on real words. It reads 8 characters past the page end, so a word split by the
  page break still matches.
- **No Chinese segmentation.**

**What it means for Lexipoint.** Online, Lexirise stays the segmenter: it does Chinese (where a dictionary longest
match is weak: 研究生命起源 would take 研究生 first), its lemmas were right on every conjugation measured
(`lexirise-api-notes.md`, "How `analyze/text` splits conjugated verbs"), and the answer carries rank and the user's
state. A longest match would likely keep whole some words Lexirise splits (とびら, 深深, 一九九九年), but the two
haven't been compared on the same text. Offline, our StarDict fallback tries prefixes as written and finds no
conjugated word; dictionary forms for it are candidate C26 (`../v0.2/00-overview.md`).

## 3. The manga format

The converter finds panels with a YOLO model trained on Manga109 (a white-gutter heuristic without it), sends each
panel crop to Gemini for its text, line boxes and an English translation, and writes, per book folder:

- **Page images** as `page_NNNN.jpg/png`, copied as they are (the device decodes them), renamed so they sort.
- **Panel crops** in a `panels/` subfolder: a flat folder made opening a book slow (6.5 s for ~2,400 files, measured
  there, since the device walks the whole folder).
- **`meta.bin`:** title, author and an optional language, versioned; new fields are only ever appended, so older
  firmware reads the start and ignores the rest.
- **`panels.idx` / `panels.dat`** (format v3): per page, its image size and its panels; per panel, the box, the crop
  rectangle (to map a tap on a zoomed panel back to the page) and the translation; per text block, the box, the text,
  a vertical flag and one box per printed line.

**No word boxes are stored.** On the device, a tap along a line maps to a character (manga lettering is one cell per
character), and §2's longest match finds the word around it. So lookup in speech bubbles exists already: the claim
that Lexipoint would be the first is withdrawn (`manga-on-x4-research.md` §7). What stays ours: words and lemmas fixed
at conversion by Lexirise (`../v0.2/manga.md` §2), and the Lexirise card on top.

**Worth having in ours:** the append-only metadata rule; keeping per-panel or per-strip files out of the folder the
device lists; line boxes as a fallback where a word box is missing.
