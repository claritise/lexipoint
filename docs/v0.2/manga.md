# Manga: tap a word in a speech bubble

**Status:** proposed 2026-09-25 (backlog item **C18** in `00-overview.md`). The Mac-side pipeline is built
as a spike (`tools/manga/`, §3) and was run on one real volume (§4). **Nothing on the device is built.**
The device part needs claritise's call on the card's orientation (§6, Q1).

Related: `../reference/manga-on-x4-research.md` (what others do, with sources),
`../v0.1/lookup-flow.md` (the lookup the device side reuses), `../v0.1/popup-ui.md` (the card),
`../reference/lexirise-api-notes.md` (`analyze/text`), `../v0.1/sentence-extraction.md` (D5: what a
"sentence" is; here it's the bubble).

---

## 0. Why, and the idea

Manga is where many learners read Japanese, and the lookup gap is worse there than in books: the page
is a picture, so no reader can look anything up. On the X4 Pro a full page is also too small to read.

The idea (claritise, 2026-09-25): **hold the reader sideways and read each page as 2–4 landscape
strips, top to bottom. The Mac OCRs the volume ahead of time and splits the text into words, so a
long-press on a word in a bubble opens the normal card.**

Three properties make it work on this device:

1. **All the heavy lifting runs on the Mac.** The ESP32 can't do Japanese OCR. The Mac does OCR, word
   splitting and image work once per volume. The device only shows pages and looks up word boxes.
2. **The words come from Lexirise's own tokenizer** (`analyze/text`, one call per page, when the volume
   is converted). Word boundaries, lemmas and entry IDs match what the card and saving expect, so D4
   holds: the server decides the word, just ahead of time.
3. **Strips are cut around the text, not at fixed positions.** Every tool in use today cuts blind and
   overlaps strips to avoid slicing a balloon (research §2). Our cutter knows where each text box is,
   so every bubble is shown whole on at least one strip, and every word gets a box on that strip.

## 1. What already exists (summary of the research)

Full notes with sources: `../reference/manga-on-x4-research.md`.

- **The device side is display-only everywhere.** Stock firmware and CrossPoint show pre-rendered XTC
  (1-bit) / XTCH (2-bit) pages. CrossPoint's `XtcReaderActivity` is portrait-locked, with no zoom, pan
  or reading-direction logic. CrossPoint declined on-device CBZ (too slow, unreadable at full page).
- **Converters do the manga work:** XTC.js (browser, the most used) and `srokl/cbz2xtc` (the most
  options). The common recipe is ours: rotate 90° and cut each page into 2–3 landscape strips, right
  piece first for spreads. **All their cuts are geometric**, with up to 70 % overlap as the safety net.
- **Manga OCR on e-ink stops at the bubble.** mokuro (comic-text-detector + manga-ocr) is the standard
  pipeline. It gives line outlines, not character or word boxes. The one e-ink integration (a
  KOReader plugin) opens a bubble's text and has you drag across characters. **Nobody precomputes word
  boxes with lemmas**, on any device.
- **Closest prior art:** `matcha-reader`, a CrossPoint fork for Japanese books and manga with its own
  panel-by-panel format. Not yet inspected. Look before building the device side.

## 2. The pipeline

```
volume.cbz ─► mokuro (OCR) ─► volume.mokuro ─► wordboxes.py ─► words.json ─┐
     │                                       (analyze/text, 1 call/page)   │
     └──────────────────────────────────────────► mangastrips.py ◄─────────┘
                                                   │           │
                                         volume.xtch      volume.lexi.json
                                     (strips, 480×800)    (words per strip)
```

### 2.1 OCR (mokuro, unchanged)

`mokuro <volume folder>` writes one `.mokuro` JSON per volume: per page, text **blocks** (a box, a
`vertical` flag, the text as `lines`, and one four-corner outline per line in `lines_coords`).
Furigana is left out of the text, which is what we want (the card shows the reading).

### 2.2 Words (`tools/manga/wordboxes.py`)

- **One `analyze/text` per page**, default mode (not `fast`: it drops lemmas). The page's blocks are
  joined with `\n`; a block's lines are one run of text (a bubble is one utterance).
- **Characters get positions by splitting each line's outline evenly along its reading axis**
  (top → bottom for vertical, left → right for horizontal). The split follows the outline's slant, so
  tilted handwriting works (a first version used the upright bounding box and failed on it).
- Each word-like occurrence (`isWordLike`) maps back through its UTF-16 `[charStart, charEnd)` to its
  characters, and gets **one box per OCR line it touches** (a word wrapping to the next column gets two).
- Kept per word: `word`, `lemma` (falls back to `word`), `reading` (`transliteration`, romaji),
  `entryId` (`lemmaEntryId` first), `sentence` (the word's whole block: its bubble) and `offset` (the
  word's UTF-16 start in the sentence), plus the boxes in source-image pixels.
- Responses are cached per page (`--cache`), so reruns cost no calls. **The cache is account data:**
  it stays outside the repo (`.gitignore` covers `api-cache/`).

### 2.3 Strips (`tools/manga/mangastrips.py`)

Per page, in order:

1. **Trim** white margins (anything lighter than 235 counts as paper).
2. **Split spreads:** an image wider than tall is two pages, **right half first**.
3. **Scale to 800 px wide** (the landscape width). A typical page becomes ~1,270 px tall.
4. **Cut into strips of at most 480 px**, choosing all cuts at once (a small dynamic program):
   - **fewest strips first**;
   - **never inside a text box** (6 px margin), when any other cut fits;
   - among allowed rows, **prefer quiet ones**: panel gutters (rows that are ≥ 98.5 % one tone for
     3+ rows) cost nothing, other rows cost their mean vertical gradient (a panel border is cheap,
     busy art is dear);
   - **when every row in reach is inside a box** (a tall column of text), cut anyway and start the
     next strip **at the top of the highest box that was cut**, so every box is whole on some strip;
   - strips are at least 160 px; a shorter remainder is centred on the screen.
5. **Quantize:** 4 greys with Floyd–Steinberg dithering for XTCH (2 greys for XTC).
6. **Rotate** the 800×480 strip to the stored 480×800 page (`--hold ccw`: the reader is turned
   counter-clockwise, content rotated clockwise; `cw` for the other hand).
7. **Encode** as XTH/XTG exactly as `XtcReaderActivity::renderPage` reads it: XTH is two bit planes,
   column-major from the right, 8 vertical pixels per byte, values 0 white · 1 dark grey · 2 light grey
   · 3 black. Every XTH page is decoded again with the firmware's `getPixelValue` and compared
   (the tool stops on a mismatch).

The container: 56-byte header (`XTCH` magic, `hasMetadata` 1, `currentPage` 1), the 256-byte metadata
block at `0x38` (title = the CBZ name), the page table, then the pages. No chapters or thumbnails yet.

Also written, for looking at the result without the device: `strips/` (each strip as you'd see it,
landscape), `sheets/` (each page with its text boxes and cuts drawn on: blue = clean cut, red = a cut
through a box), `taps/` (each strip with its word boxes drawn on) and `cuts.json`.

### 2.4 The sidecar (`<volume>.lexi.json`, spike format)

One entry per XTC page, in page order (values illustrative):

```json
{"hold": "ccw", "pages": [
  {"strip": "page0010_2", "words": [
    {"block": 4, "sentence": "わかってはいたのだ", "offset": 0,
     "word": "わかって", "lemma": "わかる", "reading": "wakatte", "entryId": 1234,
     "boxes": [[217, 52, 244, 128]]}]}]}
```

`boxes` are in the **stored page's pixel frame** (480×800, what touch reports once the reader's
orientation is accounted for), so the device never needs the source geometry. A word appears on every
strip where it's shown whole (overlapping strips repeat it).

**For the device** this becomes a compact binary file next to the book (`<volume>.lexi`): a page index,
then per page its words (boxes as `uint16`, strings UTF-8 with lengths), sentences stored once per
block. The JSON above is the spike's inspection format only.

## 3. Using the tools

Requirements: Python 3 with Pillow and NumPy; mokuro in its own venv (`pip install mokuro`, ~1.4 GB
with PyTorch; the OCR model downloads on first run); a Lexirise key in `~/.lexirise_key` for
`wordboxes.py` (read, never printed). **Keep volumes and every output outside this repo.**

```
mokuro "<volume folder>" --disable_confirmation=true         # writes "<volume folder>.mokuro"
python3 tools/manga/wordboxes.py "<volume>.mokuro" --out words.json --cache api-cache
python3 tools/manga/mangastrips.py "<volume>.cbz" --out <dir> \
    --mokuro "<volume>.mokuro" --words words.json            # --bits 1 for XTC; --pages 5-20
```

Without `--mokuro` the cutter only avoids busy rows (it will cut through text). Without `--words`
no sidecar is written.

## 4. Spike results (2026-09-25, one volume)

The test volume: a 184-page Japanese shōjo volume from the early 2000s, scans at **760×1200** (low
resolution for a digital release; a better scan only helps). Mostly vertical dialogue in balloons,
plus narration boxes, handwritten asides and one handwritten letter.

| Measure | Result |
|---|---|
| OCR (mokuro, Apple Silicon Mac, no GPU acceleration) | **~7 min** for the volume. 2,030 text blocks. Balloon dialogue read correctly on every page checked; furigana correctly excluded. Weak on handwriting, logos and a scan watermark |
| Word splitting | **178 calls** (pages with text), ~1–5 s each. **10,364 words.** Lemmas right where it matters (落ちて来た → 落ちる, めがけて → めがける, 早く → 早い). Server quirks shown as-is: the particle は read `ha`; 一九九九年 split into single characters |
| Strips | 185 pages (cover included) → **613 strips** (3.3 per page: 2 pages ×1, 122 ×3, 60 ×4, 1 ×5) |
| Cuts through text | **48 of 613**, on ~40 pages: tall text columns with no room to cut around them. Each is followed by a strip that restarts above the cut box |
| Words shown whole | **All but 9 of 10,364.** The 9 are logo lettering and the scanner's watermark |
| Sizes | XTCH **58.9 MB** (96 KB per strip, uncompressed by design). Sidecar (JSON, with sentences) 2.4 MB; the binary form would be far smaller |
| Readability (Mac preview, 2-bit) | Sideways, the kanji are clear. Furigana is ~8 px tall: legible only just, and not needed for lookup. **Not yet seen on the panel** |
| Word boxes | On vertical balloon text they sit on the characters (`taps/` images). On a tilted handwritten letter they follow the lines but overlap a little: fine for nearest-word taps, not for exact hits |

Findings that changed the design:

- **Gutters alone can't place the cuts.** Many panels bleed to the page edge or sit on screentone, so
  there is often no uniform row near where a cut must go. Cutting on "quiet rows" alone put cuts
  through the white insides of text boxes. **The OCR boxes are needed for cutting, not just lookup.**
- **Some pages need a 4th strip** only to keep a tall text column whole. That's the price of never
  cutting a bubble; it's still far below the 70 % overlap other tools use.
- **Short tail strips happen** (a page 1,272 px tall needs 3 strips even if the last holds only
  134 px of art). Acceptable; a later version could fold a text-free tail into the previous strip's
  overlap.

## 5. The device side (proposed, not built)

- **Where:** `XtcReaderActivity`, a hook like the reader's (`firmware-base.md` §3): if a
  `<book>.lexi` sits next to the `.xtc`/`.xtch`, a long-press is Lexipoint's.
- **Hit-test:** the long-press point (in the stored page frame) → the nearest word box on this page,
  within ~16 px; no word in reach does nothing (the same rule as a long-press off the text, P10).
  The side buttons step through the words of the same block (D15, D16: the block is the sentence).
- **Lookup:** the sidecar gives the word, lemma, reading and entry ID at once, so the highlight and the
  card's phase A come **instantly, without `analyze/text`**. The card still needs the live parts:
  your level for the entry (`stateByEntryId`) and the meaning (`dictionary/lookup`). Simplest: one
  `analyze/text` of the bubble (fills the state, as today) + `dictionary/lookup`, i.e. the normal
  flow, with the sidecar's word forcing the match. With the vocab mirror (C13) the state is local.
- **Saving:** unchanged (D9): the lemma, the translation, `notes` = the bubble text.
- **Highlight:** the word's boxes, inverted, on the strip as drawn.
- **Memory:** a 2-bit page needs one 96 KB buffer. P0 measured ~8.2 MB PSRAM free with a book open on
  our fork, so the upstream 1.6.x PSRAM bug (research §3) doesn't apply. The sidecar for one page is
  a few KB.
- **Offline:** without WiFi the card can still show the word, lemma and reading from the sidecar
  (no meaning; StarDict by lemma is the fallback, as D3).

## 6. Needs claritise

1. **The card's orientation on a strip (blocking for §5).** The strips are drawn rotated, so the
   reader is held sideways, but the card is measured for 480×800 portrait (D12), and in P4 a landscape
   reader switches to portrait while the card is open. On a manga strip that card would appear turned
   90°. Options: **(a)** the card opens in portrait and you turn the reader upright to read it (no new
   design; the page under it is sideways); **(b)** a landscape card (800×480), a new design needing
   your sign-off; **(c)** a compact card for strips only (word, reading, meaning, T L F K).
2. **1-bit or 2-bit by default.** 2-bit looks better in previews and memory allows it; XTC is half the
   size and turns pages faster. Decide on the panel after the readability check (§7 step 1).
3. **Where the tools live after phase M** (`../v0.1/standalone-repo.md`): `tools/manga/` at the root
   for now; `firmware/scripts/lexipoint/manga/` is the other choice.

## 7. Suggested order

1. **Panel check (no firmware change):** copy one XTCH and one XTC to the SD card, read a chapter
   sideways. Judge text size, furigana, screentone, page-turn time and ghosting on the UC8279 panel.
2. **Inspect `matcha-reader`** (its panel format and any lookup), before building the device side.
3. **Q1 answered**, then the device side (§5) as its own phase, with the sidecar's binary format and
   host tests (hit-test, stepping, reading the file).
4. **Converter polish:** chapters in the XTC (from the CBZ's folders), a text-free tail folded away,
   full-width → half-width punctuation before `analyze/text`, an optional whole-page overview strip.
5. **Later:** Chinese manhua (a different OCR model; mostly horizontal text, often long-strip webtoon
   pages, where `--manhwa`-style cutting applies).
