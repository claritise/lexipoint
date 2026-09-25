# Manga on Xteink X4 / X4 Pro: Community Practices and User Experiences

Research date: 2026-09-25. The X4 has a 4.3" 480x800 panel. Most sources are from April to September 2026. I found no source that separates the X4 Pro from the X4 for manga, so the findings below are for the X4 family in general. Reddit, TikTok, Instagram and YouTube content was either not indexed by search or could not be fetched. First-person user reports are therefore thin, and most evidence comes from tool READMEs, PRs, one blog walkthrough and reviews.

## 1. Orientation and how pages are split into screen-sized chunks

### Takeaway
The standard community workflow is to convert CBZ/PDF on a computer into Xteink's pre-rendered XTC/XTCH format. The converters (cbz2xtc and its forks, and XTC.js in the browser) rotate each page 90 degrees so the device is held sideways. They cut each page into 2 or 3 overlapping landscape strips, and wide spreads are split into 3 pieces. Every cut is geometric (fixed fractions plus overlap). None of them is panel-aware.

### Cited Findings
- The original cbz2xtc (tazua) default: "Splits each page in half horizontally (unless --no-split)" and "Rotates 90° for optimal portrait reading (if split)". Output is padded to 480x800. The README gives no overlap or panel-aware logic. It claims "text magnification from splitting" as the benefit. `--no-split` shows the full page. — [tazua/cbz2xtc README](https://github.com/tazua/cbz2xtc/blob/main/README.md)
- The srokl cbz2xtc fork has these options:
  - `--landscape-page-split rtl|ltr` ("Split wide pages into 3 pieces", with overlap "to ensure no text is cut"; rtl starts the first segment on the right).
  - `--hsplit-count N` with `--hsplit-overlap` (default 70%).
  - `--vsplit-count N` (default vertical overlap 5%), which combines with hsplit into grids.
  - `--manhwa <overlap>` for long strips (default 40% overlap).
  - `--include-overviews` / `--sideways-overviews`, which insert the full page before its splits for context. — [srokl/cbz2xtc](https://github.com/srokl/cbz2xtc)
- XTC.js (browser converter) offers "No split", "Overlapping thirds", "Split by halves", and "Split wide pages (right to left)". Suggested orientation is "Landscape". It explicitly uses "geometric splitting, not panel detection". — [varo6/xtcjs](https://github.com/varo6/xtcjs)
- A blog walkthrough (2 Apr 2026) recommends cbz2xtc with `--landscape-page-split rtl`, 2-bit output, `zhoufang` dithering and bicubic downscaling. — [www-gem, "Reading more than books on the X4"](https://www-gem.codeberg.page/cli_x4_manga/)
- CrossPoint firmware reads .xtc/.xtch natively. Its orientation control plus button remapping lets users rotate the device and keep the page-turn keys under the thumb. — [crosspoint-reader GitHub](https://github.com/crosspoint-reader/crosspoint-reader); [Josh Finnie blog, 11 Jun 2026](https://www.joshfinnie.com/blog/xteink-x4-gets-good/)
- Explainer/aggregator pages say you "may need to adjust the file's orientation for horizontal reading". They also say CrossPoint/CrossInk firmware is recommended for manga and manhwa. — [TikTok discover page (aggregated)](https://www.tiktok.com/discover/how-to-read-manga-on-xteink-x4); [YouTube "Manga on XTEInk X4 Tutorial"](https://www.youtube.com/watch?v=M9slkU-Y1Fo) (video not transcribed)

### Inferences
- Community practice for a normal portrait manga page is 2 strips (halves) or 3 strips (thirds) read sideways. Overlap ranges from about 5% (vertical splits) to 70% (horizontal column splits). The heavy overlap exists because the cuts are blind: overlap is the only defence against slicing through a speech balloon.
- The "overview then segments" option shows users want page-layout context before reading zoomed strips.

### Gaps
- I found no user poll or thread saying which split count (2 vs 3) most people prefer. Reddit r/xteink content was not reachable through search.
- It is unclear whether CrossPoint's orientation setting applies to XTC pages or only to reflowed text. Converters bake the rotation into the images.

## 2. Panel- and gutter-aware cutting tools

### Takeaway
Panel detection exists in the wider ecosystem: kumiko (OpenCV contours), KCC Panel View (Kindle-only virtual panels), and panelizer (CV + YOLO, now archived). The X4 converters I could verify do not do gutter-aware cutting. One blog claims cbz2xtc has OpenCV panel-by-panel mode, but the current READMEs contradict it.

### Cited Findings
- The www-gem blog says cbz2xtc "intelligently detects individual panels" using OpenCV for panel-by-panel reading. — [www-gem](https://www-gem.codeberg.page/cli_x4_manga/). This is contradicted by the current srokl README, which has no panel or OpenCV option. — [srokl/cbz2xtc](https://github.com/srokl/cbz2xtc). XTC.js also states "geometric splitting, not panel detection". — [xtcjs](https://github.com/varo6/xtcjs)
- Kumiko ("the Comics Cutter") finds panel locations with OpenCV contour detection. Its authors say panel-by-panel reading suits small screens where page text is unreadable. — [njean42/kumiko](https://github.com/njean42/kumiko); [KumikoMangaPanelExtractor](https://github.com/avan06/KumikoMangaPanelExtractor) crops panels into a ZIP.
- Panelizer pipeline: CV (blur, adaptive threshold, contours), then a YOLO/SAM fallback, then optional VLM ordering. It supports RTL order with a heuristic of about 80%+ accuracy. Known failures are borderless panels, irregular layouts, art bleeding into gutters, and ambiguous order. It has a human-override editor and is archived ("paused indefinitely"). — [hummat/panelizer](https://github.com/hummat/panelizer)
- KCC (analogous, Kindle-oriented) has these options:
  - Panel View options (`--two-panel`, `--vertical4panel`).
  - `--splitter` 0 split / 1 rotate / 2 both.
  - `--manga-style` (RTL).
  - `--webtoon`.
  - `--cropping` (margins and page numbers).
  - `--gamma`.
  - `--customwidth` / `--customheight` for non-standard screens.
  - Panel View is a Kindle KF8 feature, not a pixel slicer. — [ciromattia/kcc](https://github.com/ciromattia/kcc)

### Inferences
- A gutter-snapping slicer would fill a real gap. It would pick cut lines near the geometric third/half positions that fall in white horizontal gutters, and fall back to overlap only when no gutter exists. The failure modes to design for are the ones panelizer lists (borderless panels, bleed art).
- A lightweight version is feasible without full panel detection: search the row-darkness profile near each target cut for an all-white band. I found no X4 tool that does this.

### Gaps
- I could not confirm whether any older or other cbz2xtc fork (e.g. pablohc/srokl_cbz2xtc, PyPI cbz2xtc) ships OpenCV panel mode. The blog claim is unverified.
- I found no report of kumiko or panelizer output actually being used on an X4.

## 3. Readability: text size, furigana, screentone, 1-bit vs 2-bit, full page vs strips

### Takeaway
Full portrait pages at 480x800 are widely considered too small for dialogue. Splitting (magnification) is what makes text readable, and 2-bit XTCH is considered the highest quality output. I found no evidence specifically about furigana legibility on the X4.

### Cited Findings
- A reviewer (26 May 2026) puts "comics, manga with small text, and anything layout-heavy" under "weak experience". Manga "lose too much impact on such a small panel". — [MobilesTalk X4 review](https://mobilestalk.net/xteink-x4-review-after-three-months-i-love-this-teeny-tiny-e-reader-more-than-ever/)
- The www-gem blog (Apr 2026) found the result "a surprisingly enjoyable reading experience", though it "won't perfectly handle every double-page spread". — [www-gem](https://www-gem.codeberg.page/cli_x4_manga/)
- srokl README: 1-bit .xtc is pure black/white dithering. 2-bit .xtch has "4 levels of gray. Highest overall quality if your device supports it". Dithering choices are stucki (default), atkinson (sharp line art), zhoufang ("recommended for e-ink"), floyd, ordered and none. `--gamma` and a contrast boost are also available. — [srokl/cbz2xtc](https://github.com/srokl/cbz2xtc)
- The tazua README recommends ordered dithering as "often clearer for dense text". — [tazua README](https://github.com/tazua/cbz2xtc/blob/main/README.md)
- XTC.js recommends Floyd-Steinberg for detailed art and Atkinson for text-heavy content. — [xtcjs](https://github.com/varo6/xtcjs)
- XTC.js PR #46 (19 Sep 2026): wide spreads squeezed onto one screen left dialogue unreadable. After adding RTL wide-page splitting, the page count went from 51 to 61 and "the affected dialogue is now readable" on real X4 hardware. — [xtcjs PR #46](https://github.com/varo6/xtcjs/pull/46)
- Maintainer view (CrossPoint, May 2026): on-device CBZ was declined, with PDFs "out-of-scope exactly because of this reason", citing 480x800 readability. A contributor found on-device CBZ "possible, but very slow". — [crosspoint-reader Discussion #2015](https://github.com/crosspoint-reader/crosspoint-reader/discussions/2015)
- Analogous (6" readers): speech-bubble text is "noticeably smaller" and dense panels need zoom. 7.8" is called the practical middle ground for manga. — [ereadersforum PocketBook screen-size guide](https://www.ereadersforum.com/blog/pocketbook-for-manga-picking-the-right-screen-size-from-6-inch-to-10-3-inch/)

### Inferences
- Furigana is roughly half the size of balloon text. At full-page 480x800 scale it is almost certainly illegible, and even halves may be marginal. Thirds with 2-bit output are the most plausible setup for furigana, but this is inference, not reported.
- Dithering choice matters more on 1-bit. Screentone is interpreted through error-diffusion dithering, and the community defaults to error diffusion (stucki/zhoufang/floyd) for art and to atkinson/ordered for text.

### Gaps
- No user report on furigana legibility, screentone moiré, or 1-bit vs 2-bit side-by-side on the X4.
- No X4 Pro specific data (panel, grayscale or refresh differences).

## 4. Right-to-left order and double-page spreads

### Takeaway
RTL is handled at conversion time. `rtl` modes emit the right-hand segment or page first, and wide spreads are split into 2 halves (XTC.js) or 3 overlapping pieces (cbz2xtc). Covers are kept whole. Users accept that spreads remain imperfect.

### Cited Findings
- cbz2xtc `--landscape-page-split rtl`: the first segment starts on the right. There is an `ltr` for western comics. — [srokl/cbz2xtc](https://github.com/srokl/cbz2xtc)
- XTC.js "Split wide pages (right to left)" splits a spread into two sides, applies the thirds/halves layout to each, reads the right side first, keeps the cover whole, and can be turned off for panoramic art. — [xtcjs PR #46](https://github.com/varo6/xtcjs/pull/46)
- KCC `--manga-style` gives RTL reading and splitting, and `--splitter` chooses split, rotate or both for spreads (analogous, Kindle). — [KCC](https://github.com/ciromattia/kcc)
- "Won't perfectly handle every double-page spread." — [www-gem](https://www-gem.codeberg.page/cli_x4_manga/)

### Inferences
- The order within a page matters for sideways strips. For an RTL page cut into horizontal bands, reading is top band then bottom band, and within each band right to left. Rotation direction (clockwise or counter-clockwise) decides which physical button is "next". The srokl `--sideways-overviews` rotates -90 degrees, which suggests a de facto convention.

### Gaps
- No documented convention for which way users rotate the device (left or right hand) or which way the strips advance.

## 5. Community verdicts and comparisons vs Kindle/Kobo/phone

### Takeaway
Reviewers treat the X4 as a text-first pocket reader and call manga a weak point. Hobbyists who invest in conversion (2-bit, RTL splits, thirds) report it as "surprisingly enjoyable". No direct comparative user study against Kindle, Kobo or phone was found.

### Cited Findings
- Negative: manga and comics are a "weak experience" on the X4. — [MobilesTalk, May 2026](https://mobilestalk.net/xteink-x4-review-after-three-months-i-love-this-teeny-tiny-e-reader-more-than-ever/)
- Positive, with effort: "surprisingly enjoyable". — [www-gem, Apr 2026](https://www-gem.codeberg.page/cli_x4_manga/)
- Custom firmware (CrossInk) "renders images natively", which enables manga and illustrated books on the 4.3" screen. — [Josh Finnie, Jun 2026](https://www.joshfinnie.com/blog/xteink-x4-gets-good/)
- matcha-reader is a CrossPoint fork "focused on reading Japanese books & manga with built-in learning tools". It is the closest analogue to a Japanese-learner manga use case. — [GitHub topic: xteink](https://github.com/topics/xteink?l=c)
- CrossPoint maintainers consider on-device comic formats not worth it on this screen. — [Discussion #2015](https://github.com/crosspoint-reader/crosspoint-reader/discussions/2015)
- Analogous: 6" is workable but small, and 7.8" or larger is preferred for manga. — [ereadersforum](https://www.ereadersforum.com/blog/pocketbook-for-manga-picking-the-right-screen-size-from-6-inch-to-10-3-inch/)

### Inferences
- The device class works for manga only with pre-sliced, pre-dithered content. The quality of the slicing (where the cuts fall, how much overlap, preserving reading order) is the main lever on the experience. That is the space where a gutter-aware slicer would differentiate.

### Gaps
- Reddit (r/xteink, r/ereader, r/manga), MobileRead and YouTube/TikTok user experiences could not be retrieved or verified. Any aggregate sentiment beyond the sources above is unknown.
- I found no quantitative comparison (for example words per screen or taps per page) versus Kindle/Kobo/phone.
