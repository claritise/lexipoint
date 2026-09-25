# Manga/comic support in Xteink X4 / X4 Pro firmwares (stock, CrossPoint Reader, forks)

Research date: 2026-09-25. CrossPoint Reader state: latest stable **v1.6.0 (2026-09-05)**, pre-release **1.6.5rc (2026-09-14)**; earlier v1.5.0 (2026-08-07), v1.4.1 (2026-06-26), v1.3.0 (2026-05-15) — [CrossPoint releases](https://github.com/crosspoint-reader/crosspoint-reader/releases/). Repo: ~8,000 stars, ~1,840 forks, default branch `develop`, last push 2026-09-24 (GitHub API, queried 2026-09-25) — [repo](https://github.com/crosspoint-reader/crosspoint-reader).

## Stock Xteink firmware: comic formats and viewing features

### Takeaway
Stock firmware reads Xteink's own pre-rendered page-image formats (XTC 1-bit, XTCH 2-bit, and reportedly XTCZ compressed) plus JPG/BMP (and possibly PNG) images; comics must be converted off-device (CBZ/PDF → XTC). No direct evidence was found of stock zoom, pan, panel view, or right-to-left page-order settings. Official format lists conflict with each other.

### Cited Findings
- Official X4 Pro FAQ (dated 2026-07-23) lists only Documents "EPUB and TXT", Images "JPG and BMP", Fonts "BIN and XTF"; it says PDF, MOBI and PNG must be confirmed with support. It does not mention comics, XTC, landscape or zoom — [Xteink X4 Pro FAQ](https://www.xteink.com/blogs/product/x4-pro-faq-specs-support)
- A retail listing summarized by search gives a wider list: "Books/Comics: xtc, xtch, xtcz, txt, epub; Images: xtg, xth, jpg, png, bmp; Fonts: bin", with mobi/pdf "automatically converted" when sent through the app — [Amazon X4 listing via search summary](https://www.amazon.com/XTEINK-Pocket-Ink-Book-Reader/dp/B0GR4BCJK3) (not verified first-hand; conflicts with the official FAQ above)
- readme.club guide: the device supports "EPUB, PDF, MOBI, TXT, JPG, PNG, BMP, BIN, XTF", but PDF/MOBI must be converted through Xteink's app/XT-Cloud first and cannot be copied straight to the SD card — [readme.club X4 guide](https://www.readme.club/guide/x4)
- `.xtcz` is an LZ4-compressed wrapper around .xtc/.xtch that saves about 30% of file size. CrossPoint does not support it yet (open feature request #2573, 2026-07-11) — [CrossPoint #2573](https://github.com/crosspoint-reader/crosspoint-reader/issues/2573)
- Stock CN firmware 3.1.4+ added a faster 2-bit refresh mode. A community member extracted two grayscale LUTs from `V3.1.9_CH_X4_0117.bin`: LUT2 ("quality") for standalone XTH wallpapers and covers, and LUT1 ("fast") for "XTH in container", which the extractor labels "Comic reading - fast page turns". The English firmware 3.1.1 had only one very slow LUT at the time (Jan 2026). Tested on CrossPoint, the fast LUT turned pages at the same speed as stock — [CrossPoint #215](https://github.com/crosspoint-reader/crosspoint-reader/issues/215)
- Official X4 update V3.0.2 (2025-11-04) changelog: "Improved JPG image refresh rate for smoother image display". It says nothing about comics, XTC, landscape or PDF — [Xteink X4 System Update](https://www.xteink.com/pages/xteink-x4-system-update)
- A July 2026 X4 Pro review says a recent stock update removed settings (button customization, cover as sleep screen, progress bar settings). Its companion app provides fonts, wallpapers, covers and RSS — [svartling.net, 2026-07-23](https://www.svartling.net/2026/07/xteink-x4-pro-my-thoughts-on-original.html)
- CrossPoint issue #3580 ("Tilt function of 1.6.0 is up and down not left and right as Xteink") suggests stock has a tilt page-turn gesture (X3 gyroscope) — [CrossPoint #3580](https://github.com/crosspoint-reader/crosspoint-reader/issues/3580)
- Reviewers and community say manga on Xteink means converting to XTC/XTCH with tools such as xtcjs / cbz2xtc. Rotation, splitting and dithering happen in the converter, not on the device — [www-gem, 2026-04-02](https://www-gem.codeberg.page/cli_x4_manga/); [xtcjs](https://github.com/varo6/xtcjs)

### Inferences
- XTC/XTCH are Xteink-native containers, so stock reads them natively. Features like RTL order and landscape are baked into the file by the converter (for example xtcjs's "Split wide pages (right to left)" and auto-rotation), not selected on the device.
- The stock firmware tunes a dedicated fast grayscale waveform for XTCH comic pages. That makes 2-bit manga page turns a competitive point for stock.

### Gaps
- No primary source found for stock on-device zoom, pan, landscape toggle for XTC, CBZ, or direct PDF reading on the X4 Pro's current English firmware. The latest English/X4 Pro stock version number for Sept 2026 was not found.
- No official stock changelog mentioning XTCH or comics was found. The Amazon listing could not be fetched directly.

## CrossPoint Reader (upstream): XTC reader, CBZ, orientation, EPUB images, grayscale, RTL

### Takeaway
CrossPoint reads pre-rendered XTC/XTCH natively with page turning, chapter jump (if the file has a TOC) and 1-bit/2-bit rendering. The XTC reader is forced to portrait. It does not read CBZ, PDF or fixed-layout EPUB. It has no zoom, pan, panel view or right-to-left page order. Zoom and pan requests stay open or unaddressed, and PDF is explicitly out of scope.

### Cited Findings
**Formats and scope**
- README: "native handling for `.epub`, `.xtc/.xtch`, `.txt`, and `.bmp`". Reader engine: EPUB 2/3 "with … image handling … go-to-percent, auto page turn, orientation control" — [CrossPoint README (develop)](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/README.md)
- SCOPE.md lists PDF rendering as out of scope: fixed-layout pages must be shown as images, which causes "constant panning and zooming that makes for a poor reading experience on e-ink. Out of scope on the current hardware class." Current priorities are memory and flash footprint and EPUB typography. The ESP32-C3 "is the tightest target and sets the ceiling" — [SCOPE.md](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/SCOPE.md)
- A searched keyword ("cbz") turns up no CBZ reader PR or issue in CrossPoint. The only CBZ mentions are issue #753 (CBZ converted to XTC via xtcjs) and a closed "python xtc viewer" PR #448 — [#753](https://github.com/crosspoint-reader/crosspoint-reader/issues/753); [#448](https://github.com/crosspoint-reader/crosspoint-reader/pull/448)
- Maintainer reply on #1269 (2026-03-02): XTC is already supported natively. PDF support is "generally impractical" on the small screen and "not currently planned" — [#1269](https://github.com/crosspoint-reader/crosspoint-reader/issues/1269)

**XtcReaderActivity (upstream `develop`, read 2026-09-25)**
- `applyInitialOrientation()` forces `Orientation::Portrait`. The Reading Orientation setting (Portrait / Landscape CW / Inverted / Landscape CCW) is documented as applying to EPUB — [XtcReaderActivity.cpp](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/XtcReaderActivity.cpp); [USER_GUIDE.md](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/USER_GUIDE.md)
- Navigation: page forward/back, `skipPages()` (long-press skip), and Confirm (or a touch menu gesture) opens chapter selection when the file has chapters. Progress is saved as a 4-byte page index. The status bar shows chapter page x/y — [XtcReaderActivity.cpp](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/XtcReaderActivity.cpp)
- Rendering: a 2-bit page needs a full page buffer, `width*((height+7)/8)*2` (96,000 bytes at 480×800). It is drawn as LSB/MSB grayscale planes with a fast-refresh grayscale base and a half refresh every N pages (the refresh-frequency setting). 1-bit pages go through the normal refresh cycle — [XtcReaderActivity.cpp](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/XtcReaderActivity.cpp)
- No zoom, pan or reading-direction code exists in the XTC reader. Page order is always the file's order — same source.
- The USER_GUIDE says Auto Page Turn is configured "while reading an EPUB". Issue #2545 (2026-07-06, open) requests auto page turn for XTC/XTCH "(like manga or comics)" — [USER_GUIDE](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/USER_GUIDE.md); [#2545](https://github.com/crosspoint-reader/crosspoint-reader/issues/2545)

**Open PRs and issues about XTC or manga UX**
- PR #1872 (open, 2026-05-08): XTC reader menu with Go-to-Page, auto page turn (1/3/6/12 ppm), Go Home and Delete Cache. The motivation is Indic-script XTC books — [#1872](https://github.com/crosspoint-reader/crosspoint-reader/pull/1872)
- #1004 (open, 2026-02-19): percent and goto for XTC "good for manga". Chapter jumping already works — [#1004](https://github.com/crosspoint-reader/crosspoint-reader/issues/1004)
- #753 (closed 2026-02-10): no TOC in CBZ→XTC manga. Maintainers blamed the converter. KOSync is EPUB-only — [#753](https://github.com/crosspoint-reader/crosspoint-reader/issues/753)
- #1623 (open, 2026-04-09): zoom and pan mode for in-book images. The maintainer instead pointed to the web EPUB optimizer's V-split/H-split of images; a request to split full-page images into four parts was noted as a suggestion — [#1623](https://github.com/crosspoint-reader/crosspoint-reader/issues/1623)
- Discussion #3485 (2026-09-10) "possible zoom in function?" asks for zoom and pan when reading manga (compared with 3DS). Discussion #3367 (2026-09-03) asks for zoom-out; the maintainer said it can't be done for reflowable EPUB — [#3485](https://github.com/crosspoint-reader/crosspoint-reader/discussions/3485); [#3367](https://github.com/crosspoint-reader/crosspoint-reader/discussions/3367)
- #2529 (open, 2026-07-03) per-book orientation. #1426 default image orientation. #2488 180° rotation — [#2529](https://github.com/crosspoint-reader/crosspoint-reader/issues/2529); [#1426](https://github.com/crosspoint-reader/crosspoint-reader/issues/1426); [#2488](https://github.com/crosspoint-reader/crosspoint-reader/issues/2488)
- "Right to left" in CrossPoint refers to RTL text scripts (SCOPE call-to-action) and vertical tategaki (#3475, open, 2026-09-09), not manga page order — [SCOPE.md](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/SCOPE.md); [#3475](https://github.com/crosspoint-reader/crosspoint-reader/issues/3475)
- OPDS download of XTC/XTCH: several open PRs (#1518, #2561, #2627) and issue #1603 — [#2627](https://github.com/crosspoint-reader/crosspoint-reader/pull/2627)

**EPUB images and fixed-layout**
- EPUB inline JPG/PNG images are shown when Images=ON (default) — [USER_GUIDE](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/USER_GUIDE.md)
- The image decoder caps source images at 8 MP (`MAX_SOURCE_PIXELS = 8388608`, "e.g. 2048 * 4096"). Progressive JPEGs are decoded DC-only (1/8 resolution upscaled, "lower quality"). Source: local checkout of the CrossPoint fork (lib/Epub/Epub/converters/), branched from upstream develop — [ImageToFramebufferDecoder.h](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/lib/Epub/Epub/converters/ImageToFramebufferDecoder.h)
- No `rendition:layout` / pre-paginated (fixed-layout EPUB) handling exists in the source (grep of local checkout), and no issues were found for "fixed layout" or "pre-paginated". Open PR #3380 (2026-09-05) skips raw `image/*` items in the EPUB spine, which otherwise broke rendering of malformed books — [#3380](https://github.com/crosspoint-reader/crosspoint-reader/pull/3380)
- The built-in EPUB optimizer (merged PR #1224, 2026-02-27) can split large images (V-split/H-split) in "Advanced Mode" when uploading — [#1224](https://github.com/crosspoint-reader/crosspoint-reader/pull/1224); [#1623](https://github.com/crosspoint-reader/crosspoint-reader/issues/1623)
- Image quality in EPUB: Discussion #3370 (2026-09-03) shows EPUB images rendered with stateless Bayer dithering. They look darker and more washed out than the sleep-screen path, which uses Atkinson plus an X4-tuned 4-level quantizer — [#3370](https://github.com/crosspoint-reader/crosspoint-reader/discussions/3370)

**Grayscale and refresh**
- Open PR #2461 (2026-06-28): "Fast" grayscale render mode using a single B/W pass with a 64×64 blue-noise dither, giving "much faster perceived page turns, especially on image-heavy pages". It covers EPUB/text readers — [#2461](https://github.com/crosspoint-reader/crosspoint-reader/pull/2461)
- Open PR #1614 (2026-04-08): factory-LUT grayscale (FactoryFast, with a visible black flash, and FactoryQuality) for EPUB images and XTC — [#1614](https://github.com/crosspoint-reader/crosspoint-reader/pull/1614)
- Merged PR #3018 (2026-08-13) uses the factory LUT for cover images. Merged #3478 (2026-09-09) consolidates grayscale capability checks and enables "absolute grayscale" for supported screens — [#3018](https://github.com/crosspoint-reader/crosspoint-reader/pull/3018); [#3478](https://github.com/crosspoint-reader/crosspoint-reader/pull/3478)
- #215 (open since 2026-01-02): 2-bit XTC grays look poor without a full refresh, and CrossPoint's full refresh is slower than stock's newer mode. On 2026-08-26 an X4 Pro user (UC8279 800×480 panel) reported anti-aliased text looking more jagged than on a non-Pro X4 and asked whether the UC8279 grayscale LUT needs tuning — [#215](https://github.com/crosspoint-reader/crosspoint-reader/issues/215)

### Inferences
- In CrossPoint today, a manga workflow means converting on a PC or phone to XTC/XTCH, which handles rotation, splitting, RTL order and dithering, then paging through on the device in portrait. Landscape spreads have to be pre-rotated inside the XTC.
- The project scope (memory and flash consolidation, PDF out of scope) makes on-device CBZ decoding, zoom, pan or panel view unlikely to land upstream soon. A fork is the more likely home for them.

### Gaps
- Whether PR #1872's features reached a release could not be confirmed; it was still open on 2026-09-25.
- No benchmarks found for CrossPoint XTCH page-turn times in milliseconds.

## CrossPoint++ and other forks: comic/manga-specific features

### Takeaway
No fork named "CrossPoint++" was found on GitHub or the web. None of the known forks (CrossInk, papyrix, inx, Witch(hunt), InkPointX, and smaller ones) advertise CBZ reading, panel view, landscape strip reading, scroll mode or on-device comic dithering. Comic-specific work lives in converter tools (xtcjs, cbz2xtc, xteink-comic-slicer).

### Cited Findings
- CrossPoint README lists community forks: CrossInk (UX and reading stats), papyrix-reader (FB2/MD, Arabic, themes), inx (tabbed UI), Witch(hunt) Reader (CSS fidelity, weather, Markdown). None of the descriptions mention comics — [CrossPoint README](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/README.md)
- CrossInk README warns that "Large image-heavy EPUBs, scanned books, comics, and omnibus files … may load slowly or fail under memory pressure" and targets "Text-first EPUBs" — [CrossInk](https://github.com/uxjulia/CrossInk)
- A Medium review says images render natively in CrossInk as of 1.2.10, which makes manga "suddenly viable" (search summary, not verified first-hand) — [Medium, Khairul Selamat](https://medium.com/@khairul_selamat/xteink-x4-it-just-keeps-getting-better-61254b92f183)
- papyrix-reader supports "XTC/XTCH native format", EPUB images (JPEG/PNG/BMP, baseline JPEG only, max 2048×3072), "4 screen orientations", and separate ESP32-S3 images for X4 Pro/Classic — [papyrix-reader README](https://github.com/bigbag/papyrix-reader)
- InkPointX is described as a "Personal fork of Crosspoint and CrossInk"; no manga features were found — [InkPointX](https://github.com/yokki-vans/InkPointX)
- A GitHub repo search (2026-09-25) found no manga forks. It did find a fork for sheet music (tomlarse/crosspoint-music), MTG, Gutenberg, BLE page turners and Japanese UI (smalltomatowater-boop/crosspoint-reader-x3) — [GitHub search results](https://github.com/search?q=crosspoint+reader+fork&type=repositories)
- zgredex fork branch `feat/factory-lut-grayscale` added the stock fast and quality LUTs (fast LUT used for 2-bit XTC and EPUB, with a white preflash) — [#215 comment](https://github.com/crosspoint-reader/crosspoint-reader/issues/215); [branch](https://github.com/zgredex/crosspoint-reader/tree/feat/factory-lut-grayscale)

**Converter ecosystem (where manga features actually live)**
- xtcjs (browser): inputs CBZ/CBR, PDF, JPG/PNG/WEBP, video. Split modes are No split / Overlapping thirds / Halves / 4-way columns, plus "Split wide pages (right to left)", which starts with the right page and keeps the cover whole. Dithering options are Floyd-Steinberg, Atkinson, Sierra-Lite, Ordered and None. Device profiles: X4/X4 Pro 480×800, X3 528×792, reTerminal Sticky (CrossPoint-only). Outputs XTC (1-bit) or XTCH (2-bit) — [xtcjs](https://github.com/varo6/xtcjs)
- www-gem (2026-04-02) describes cbz2xtc/xtcjs. The converter auto-rotates and splits RTL landscape pages, offers 2-bit output with several dithers, and does panel-by-panel detection using OpenCV or YOLO. Stated limitations: it doesn't improve art resolution and may struggle with double-page spreads — [www-gem](https://www-gem.codeberg.page/cli_x4_manga/)
- tazua/cbz2xtc (CLI, 66 stars, last push 2026-01-03): batch CBZ→XTC with optional page splitting and rotation, dithering, and a full-page mode. It depends on epub2xtc's png2xtc — [cbz2xtc](https://github.com/tazua/cbz2xtc)
- An `.xtcz` (LZ4) variant is defined by the srokl/xtcjsapp fork — [#2573](https://github.com/crosspoint-reader/crosspoint-reader/issues/2573)

### Inferences
- The "panel view" and "landscape strip" features the question asks about exist only as converter-side preprocessing (panel detection in cbz2xtc/xtcjs variants, splitting and rotation). No firmware does them on-device, as far as found.

### Gaps
- "CrossPoint++" could not be located. It may be a Reddit nickname, a misnomer, or a private build. Reddit r/xteink threads could not be fetched directly, so community reports there are under-covered.
- Features in inx and Witch(hunt) that touch comics were not verified beyond README summaries.

## Performance: page turns, 2-bit grayscale, ghosting, memory (ESP32-C3 vs S3)

### Takeaway
XTCH (2-bit) pages need a contiguous 96 KB buffer. On the ESP32-C3 (X4/X3), heap fragmentation has repeatedly caused "Memory error" on XTC/XTCH pages. The X4 Pro (ESP32-S3 with 8 MB PSRAM) should remove this limit, but a Sept 2026 RC bug left PSRAM uninitialized, breaking XTCH there too. On speed, 2-bit grays in CrossPoint are slower or lower quality than stock's fast comic LUT, and XTCH shows more ghosting than 1-bit XTC.

### Cited Findings
- #814 (1.0.0, 2026-02): "Failed to allocate page buffer (96000 bytes)" with 178 KB free but the largest free block about 94 KB, caused by heap fragmentation. Fixed by PR #815, closed 2026-07-22 — [#814](https://github.com/crosspoint-reader/crosspoint-reader/issues/814)
- PR #2287 (open, 2026-06-08): after Home → FileBrowser → XtcReader on the ESP32-C3 ("~380 KB usable RAM"), `malloc(48,000)` for 1-bit and `malloc(96,000)` for 2-bit fail. The fix streams 1-bit rows (peak 48 KB → ~60 bytes). 2-bit is column-major, so it falls back to streaming B/W when the 96 KB buffer isn't available. PR #2361 (open) instead streams XTC pages through a fixed 12 KiB buffer — [#2287](https://github.com/crosspoint-reader/crosspoint-reader/pull/2287); [#2361](https://github.com/crosspoint-reader/crosspoint-reader/pull/2361)
- #3330 (open, 2026-09-01, X4 Pro, 1.6.0 beta RC03): .xtch (572-page, 480×800) shows memory error while the .xtc version works, and EPUB chapter images are missing. Heap is only about 308 KB and PSRAM reads 0 even though esptool reports "Embedded PSRAM 8MB". Linked to #3325 (PSRAM not initialized on S3-R8) — [#3330](https://github.com/crosspoint-reader/crosspoint-reader/issues/3330)
- #3429 (open, 2026-09-07): X4 Pro PSRAM boot init works on platform-espressif32 55.03.37 but is broken on 55.03.311 — [#3429](https://github.com/crosspoint-reader/crosspoint-reader/issues/3429)
- Merged PR #3646 (2026-09-21): "Add TrueType fonts on PSRAM boards", showing CrossPoint now gates features on PSRAM (S3) boards — [#3646](https://github.com/crosspoint-reader/crosspoint-reader/pull/3646)
- XTC page buffer sizes from source: 1-bit is `((w+7)/8)*h` = 48,000 B at 480×800. 2-bit is two bit-planes = 96,000 B — [XtcReaderActivity.cpp](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/src/activities/reader/XtcReaderActivity.cpp)
- Refresh: the stock fast LUT matched stock page-listing speed when ported to CrossPoint. The stock quality LUT is "slower, gentler". CrossPoint's own full refresh is slower than stock's newer 2-bit mode — [#215](https://github.com/crosspoint-reader/crosspoint-reader/issues/215)
- Ghosting: r/XTEINK users report more ghosting with .XTCH (2-bit) than .XTC (1-bit), and manga text is sometimes unreadable (search summary of a Reddit thread; not fetched first-hand) — [search result referencing r/XTEINK](https://github.com/crosspoint-reader/crosspoint-reader/discussions/2932). Related: non-dithered grayscale BMP sleep images ghost on CrossPoint 1.5.0 while dithered ones don't — [Discussion #2932](https://github.com/crosspoint-reader/crosspoint-reader/discussions/2932). Merged PR #2471 (2026-06-29) fixes X4 sleep/boot ghosting — [#2471](https://github.com/crosspoint-reader/crosspoint-reader/pull/2471)
- X3 grayscale: open PR #2412 (stabilize X3 EPUB grayscale page turns) and #3469 (vertical banding on X3 grayscale images) — [#2412](https://github.com/crosspoint-reader/crosspoint-reader/pull/2412); [#3469](https://github.com/crosspoint-reader/crosspoint-reader/pull/3469)
- Hardware split: CrossPoint supports ESP32-C3 X4/X3 (one image) and ESP32-S3 X4 Pro/X4 Classic (separate images) — [PocketInk firmware guide (search summary)](https://pocketink.io/firmware/); [papyrix README](https://github.com/bigbag/papyrix-reader)

### Inferences
- For manga, 1-bit XTC is the robust choice on C3 devices: it can stream, it's faster, and it ghosts less. XTCH looks better but costs a 96 KB contiguous allocation plus a slower grayscale waveform.
- On the X4 Pro, PSRAM should make XTCH buffers trivial once PSRAM init is fixed (#3330/#3429). Until then, XTCH on Pro RC builds can fail.
- On-device CBZ (JPEG/PNG decode, scale and dither per page) would be CPU-heavy on the single-core C3. It is more realistic on the S3/PSRAM X4 Pro, consistent with the scope doc's C3 ceiling.

### Gaps
- No measured page-turn times (ms) found for image or XTCH pages on stock or CrossPoint, and no X4 Pro-specific refresh benchmarks.
- The X4 Pro's exact SoC variant and RAM were not confirmed from an official Xteink source; the official FAQ does not disclose them. The 8 MB PSRAM figure comes from a user's esptool log in #3330.
