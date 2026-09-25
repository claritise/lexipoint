# Manga/Comic Conversion Tools and Pipelines for Xteink X4 / X4 Pro (and X3)

Research date: 2026-09-25. GitHub metadata (created / last push / stars) pulled via the GitHub API on that date.

## Which tools convert CBZ/CBR/PDF/images to XTC (1-bit) / XTCH (2-bit)?

### Takeaway
No official Xteink manga converter turned up. The ecosystem is community-built and descends from one Python CLI, **tazua/cbz2xtc** (Dec 2025). Its TypeScript browser port **XTC.js** (xtcjs.app, 213 stars, 10k+ users, active Sept 2026) is the dominant manga tool. The **srokl/cbz2xtc** fork has the most processing options, including 2-bit XTCH, 9 dither algorithms, manhwa strip mode, and a nonstandard LZ4 `.xtcz`. Niche tools include XteinkImageRefiner (JP, CLAHE pipeline), CUMA (Windows GUI), imgs-to-xtc (web scraper), and xtctool (PDF/Typst). The EPUB tools (cr2xt, epub-to-xtc-converter, XTC Converter Pro and others) target text rather than comics.

### Cited Findings

**Manga/comic-focused converters**
- **XTC.js** (varo6 and sodafmr), https://github.com/varo6/xtcjs, live at https://xtcjs.app. Repo created 2026-01-12, last push 2026-09-19. 213 stars, 20 forks, MIT, TypeScript + Bun. It started as "a TypeScript port of cbz2xtc" and runs entirely in the browser, offline once loaded. — [GitHub README](https://github.com/varo6/xtcjs); metadata via GitHub API
  - Inputs are CBZ/CBR, PDF, JPG/PNG/WEBP, and video frames. Device presets are X4/X4 Pro 480×800, X3 528×792, and Seeed reTerminal Sticky 480×800 (Sticky needs CrossPoint firmware). — [README](https://github.com/varo6/xtcjs)
  - Dithering choices are Floyd-Steinberg, Atkinson, Sierra-Lite, Ordered and None. It also offers a contrast slider, live preview, spread splitting ("overlapping thirds" or halves), orientation, and image scaling (e.g. "Cover"). It can merge and split CBZ/PDF/XTC files, edit metadata, and build a chapter TOC. — [README](https://github.com/varo6/xtcjs)
  - The README says XTC is 1-bit and that "XTCH is the 2-bit variant with 4 grayscale levels... Some content may look better in XTCH". It does not clearly say whether the main app exports XTCH. The readme.club directory lists a fork of varo6/xtcjs that "Added 2-bit Greyscale and Manhwa Portrait Strip". — [README](https://github.com/varo6/xtcjs); [readme.club resources](https://www.readme.club/resources)
  - Spread handling: with "Split wide pages (right to left)" on, wide CBZ/CBR images are treated as double-page spreads. Each half gets the selected thirds/halves split, starting with the right page, and the first image stays whole as the cover. The README calls this "geometric splitting, not panel detection." — [README](https://github.com/varo6/xtcjs)
- **tazua/cbz2xtc**, https://github.com/tazua/cbz2xtc. Created 2025-12-17, last push 2026-01-03 (dormant). 66 stars, Python, also on PyPI as `cbz2xtc` 0.1.0. It is the original ancestor. — [GitHub](https://github.com/tazua/cbz2xtc); [libraries.io](https://libraries.io/pypi/cbz2xtc); metadata via GitHub API
  - Default pipeline: split each page in half horizontally, rotate 90°, resize to 480×800 with white padding, then convert to grayscale with dithering (Floyd-Steinberg by default). Other options are `--no-split` (full page), `--no-dither`, `--dither-algo {ordered, none, rasterize}`, and multithreading ("up to 4x faster"). Output is 1-bit only. The companion `image2bw` makes 1-bit BMPs of 15–40 KB. — [README](https://github.com/tazua/cbz2xtc/blob/main/README.md)
- **srokl/cbz2xtc** (the "enhanced" fork), https://github.com/srokl/cbz2xtc. Created 2026-02-11, last push 2026-04-03. 28 stars, MIT, Python + NumPy/Numba/PyMuPDF/Playwright/lz4. Its tools are `cbz2xtc.py`, `cbz2xtcpoppler.py` (PDF via Poppler), `web2xtc.py` (website screenshots, cookie login, chapter crawling), `video2xtc.py`, `image2xth.py`, `image2bw.py`, and `xtc2xtcz.py`. — [README](https://github.com/srokl/cbz2xtc)
  - Options: `--2bit` (XTCH); `--dither` with stucki (default), atkinson, ostromoukhov, contrast-aware, zhoufang ("recommended for e-ink"), stochastic (Velho SFC), floyd, ordered, none; `--gamma`; `--contrast-boost N`; `--margin %` crop; `--downscale bicubic|bilinear|box`; `--landscape-page-split rtl|ltr` (3 pieces); `--hsplit-count/--hsplit-overlap` (default 70%); `--vsplit-count/--vsplit-overlap` (default 5%); `--manhwa` long strip (default 40% overlap); `--include-overviews` / `--sideways-overviews`; page ranges `--start/--stop/--skip`; `--compress` for LZ4 `.xtcz`. — [README](https://github.com/srokl/cbz2xtc)
  - Its suggested Japanese manga command is `--2bit --landscape-page-split rtl --sideways-overviews --downscale bicubic`. The README calls 2-bit the "Highest overall quality" and "no dithering... best for text-only pages". — [README](https://github.com/srokl/cbz2xtc)
- **pablohc/srokl_cbz2xtc**: fork snapshot of srokl (Feb 2026, 1 star). Mentions an Atkinson variant, Floyd-Steinberg "corrected for 2-bit color space", `--invert`, and presets for standard manga, dark lineart, western comics and high sharpness. — [GitHub](https://github.com/pablohc/srokl_cbz2xtc)
- **XteinkImageRefiner** by ふぇん (feeeeeeen), https://github.com/feeeeeeen/XteinkImageRefiner. Created 2026-03-08, v1.3.0 on 2026-05-09, 3 stars, MIT. It is a GUI with the pipeline "Margin trimming → Auto-rotation → Blur → Resize → Sharpen → Grayscale → CLAHE → Contrast → Dithering → Cleanup". Dithering is Floyd-Steinberg, Atkinson or Sauvola at 1-, 2- or 8-bit. It exports JPEG/PNG/EPUB3/XTC/XTCH/CBZ, has auto margin detection, auto spread splitting (v1.3.0 added split-direction and page-order settings), and zoom/pan preview. — [note.com article, 2026-03-08, updated 2026-05-09](https://note.com/feeeen/n/n9b036c73da6f?hl=en); metadata via GitHub API
- **CUMA (Ultimate Manga Converter)**: a Windows desktop app for "cleaning and converting PDF, image, EPUB, and XTCH files". It removes borders, ads and headers from web-downloaded PDFs and targets X4/X3, Kindle, Kobo and custom resolutions. The author built it with Copilot/ChatGPT. I found no repo URL. — [readme.club resources](https://www.readme.club/resources)
- **jakebirkes/imgs-to-xtc**: a Node.js + Puppeteer tool that scrapes images from web pages and outputs XTC (1-bit XTG or 2-bit XTH pages) or PDF. It auto-rotates and segments portrait images for landscape reading, offers Sierra-Lite, Floyd-Steinberg, Atkinson and ordered dithering, sets contrast and resolution through environment variables, and targets X4 and X3. Created 2026-04-14, last push 2026-05-14, 0 stars. — [GitHub](https://github.com/jakebirkes/imgs-to-xtc)
- **Manga Converter for Matcha Reader**: converts CBZ/EPUB/PDF/image folders into the Matcha Reader firmware's own panel-by-panel format, not XTC. matcha-reader has 40 stars and was updated Sept 2026. — [readme.club](https://www.readme.club/resources); [GitHub topic xteink](https://github.com/topics/xteink)

**General / document converters (can take PDF or images)**
- **chazeon/xtctool**, https://github.com/chazeon/xtctool. Python, GPLv3, 55 stars, created 2025-12-09, last push 2025-12-13. Inputs are PDF, Markdown/Typst and PNG/JPG; outputs are XTG, XTH, XTC, or PNG/PDF for debugging. It supersamples (e.g. renders at 4× and downsamples), uses Floyd-Steinberg with a strength setting (0.0–1.0, Numba-accelerated), has three adjustable thresholds for 4-level quantization, BOX or LANCZOS resampling (BOX "recommended for text"), and supports LTR/RTL/TTB reading direction. — [GitHub](https://github.com/chazeon/xtctool)
- **CrazyCoder/cr2xt**: a crengine (Cool Reader) desktop converter for Windows, macOS and Linux (139 stars, last push 2026-08-11). It turns EPUB, FB2, MOBI, DOC, RTF and similar into XTC/XTCH, with Floyd-Steinberg and ordered dithering in 1-bit or 2-bit. It has no comic-specific features. — [GitHub](https://github.com/CrazyCoder/cr2xt)
- EPUB → XTC/XTCH tools with no comic features: bigbag/epub-to-xtc-converter (CREngine WASM, web + Node CLI, 136 stars, pushed 2026-09-09); zgredex/crosspoint-ko-wasm; Wren6991/EPUB-to-XTCH (0 stars); XTC Converter Pro (xtc-converter.web.app, Floyd-Steinberg); EPUB2XTC (epub2xtc.streamlit.app); x4converter.rho.sh; xtctool.com; XTC 変換 (xtc.hr20k.com, web pages → XTC for X3/X4); XTLibre (self-hosted, 86 stars). — [GitHub search results](https://github.com/bigbag/epub-to-xtc-converter); [readme.club](https://www.readme.club/resources); [GitHub topic xteink](https://github.com/topics/xteink)
- zgredex/crosspoint-pxc-converter converts images to CrossPoint's PXC wallpaper format. It offers Floyd-Steinberg, Atkinson, Jarvis, Stucki, Burkes, Bayer and Zhou-Fang dithering, which is the widest dither set in the ecosystem, but it is for wallpapers, not comics. 17 stars. — metadata via GitHub API ([repo](https://github.com/zgredex/crosspoint-pxc-converter))
- **Official Xteink**: searches found no Xteink-branded manga or XTC converter; all results were community tools. — [WebSearch results](https://xtctool.com/)
- There are video tutorials (TikTok @readingadgets "XTEINK 101: how to convert your CBZ or PDF manga", and a YouTube "Manga on XTEInk X4 Tutorial"). I did not review their content. — [TikTok](https://www.tiktok.com/@readingadgets/video/7601637307518700818); [YouTube](https://www.youtube.com/watch?v=M9slkU-Y1Fo)

### Inferences
- Tool lineage: tazua/cbz2xtc (Python, Dec 2025) led to both XTC.js (TS port, now the mainstream web tool) and srokl/cbz2xtc (the feature-rich Python fork, which pablohc then forked). Most manga users likely use XTC.js because it needs no install.
- Maintenance as of Sept 2026: XTC.js is active, srokl's last push was Apr 2026, and tazua has been idle since Jan 2026.

### Gaps
- Whether the current xtcjs.app exports XTCH natively. The fetched site page returned no usable content, and the README is ambiguous.
- I found no URL for the CUMA download, the XTC.js 2-bit fork, or the "CBZ to XTC (2-bit greyscale fork)".
- I did not find or search the X4 Pro's stock/official firmware for any built-in comic import path.

## Does KCC (or similar) support Xteink, or get used as a preprocessing step?

### Takeaway
KCC has no Xteink profile and cannot write XTC/XTCH. Its "Other" custom-resolution profile could in principle produce 480×800 CBZ for another tool to encode, but I found no documented pipeline or recommendation doing so. The community uses Xteink-specific converters that reimplement KCC-style cropping and splitting.

### Cited Findings
- KCC's device list covers Kindle, Kobo and reMarkable, with no Xteink/XTC/X4 profile. It has an `'OTHER': ("Other", (0, 0), Palette16, 1.0)` custom profile. Outputs are MOBI, EPUB, KEPUB, CBZ, PDF, KFX and image folders. Processing covers margin cropping ("margins only" / "margins + page numbers"), splitting or rotating spreads, gamma correction, quantization to 16 colors, and optional dithering. — [KCC GitHub](https://github.com/ciromattia/kcc)
- A search summary also said KCC lacks an Xteink profile and pointed to CUMA/xtctool instead. — [WebSearch results incl. KCC repo](https://github.com/ciromattia/kcc)
- CrossPoint firmware natively reads .epub, .xtc/.xtch, .txt and .bmp, so KCC's EPUB output would go through CrossPoint's EPUB image path, not XTC. — [crosspoint-reader GitHub](https://github.com/crosspoint-reader/crosspoint-reader)

### Inferences
- A possible two-stage pipeline would be KCC "Other" at 480×800 (crop, split, gamma, 16-level) → CBZ → XTC.js or srokl/cbz2xtc with no split → XTC/XTCH. KCC's 16-level quantization would be requantized to 2 or 4 levels, so its gamma and crop are the useful parts. This pipeline is not documented anywhere I found.
- Mangaka and comic2ebook: no search hits connected them to Xteink.

### Gaps
- I found no KCC GitHub issue or discussion requesting Xteink support. The search returned nothing specific, but I did not browse KCC's issue list exhaustively.

## Image processing: resize/fit, crop, spread split, rotation, dithering, gamma/contrast, 1-bit vs 2-bit

### Takeaway
Every manga tool follows the same pattern: optional margin crop, then geometric splitting (halves, thirds, or an N×M grid with overlap) rotated 90° to use the long axis, then resize to 480×800 (padding or cover), then optional gamma/contrast, then error-diffusion dithering to 1 or 2 bits. None does panel detection. The tools differ mainly in how many dither algorithms they offer and whether they output 2-bit.

### Cited Findings
- **Resize/fit**: tazua resizes to 480×800 with white padding. XTC.js has "Image Scaling: Cover" for wallpapers. srokl image2xth supports Cover, Letterbox and Fill (letterbox/fill/crop). srokl offers bicubic (default), bilinear or box downscale filters. xtctool offers BOX or LANCZOS plus supersampling. — [tazua](https://github.com/tazua/cbz2xtc/blob/main/README.md); [xtcjs](https://github.com/varo6/xtcjs); [srokl](https://github.com/srokl/cbz2xtc); [xtctool](https://github.com/chazeon/xtctool)
- **Margin crop**: srokl `--margin 5` crops a fixed 5% on every side. XteinkImageRefiner auto-detects margins with manual adjustment. CUMA removes borders, ads and headers. KCC crops margins and page numbers. — [srokl](https://github.com/srokl/cbz2xtc); [note.com](https://note.com/feeeen/n/n9b036c73da6f?hl=en); [readme.club](https://www.readme.club/resources); [KCC](https://github.com/ciromattia/kcc)
- **Splitting and rotation**:
  - tazua splits in halves and rotates 90°.
  - XTC.js offers overlapping thirds, halves or none, with RTL spread detection and a whole cover page.
  - srokl splits spreads into 3 pieces (RTL/LTR), has hsplit/vsplit grids with overlap (70% horizontal and 5% vertical by default), rotates pieces "automatically based on display ratio", and can add overview pages (upright or rotated -90°).
  - imgs-to-xtc rotates and segments portrait images for landscape.
  - Sources: [tazua](https://github.com/tazua/cbz2xtc/blob/main/README.md); [xtcjs](https://github.com/varo6/xtcjs); [srokl](https://github.com/srokl/cbz2xtc); [imgs-to-xtc](https://github.com/jakebirkes/imgs-to-xtc)
- **Webtoon/manhwa**: srokl `--manhwa` treats the input as one continuous vertical strip with 40% default overlap between screens. A fork of XTC.js adds "Manhwa Portrait Strip". — [srokl](https://github.com/srokl/cbz2xtc); [readme.club](https://www.readme.club/resources)
- **Dithering algorithms**:
  - tazua: Floyd-Steinberg, Ordered, Rasterize (halftone), None.
  - XTC.js: Floyd-Steinberg, Atkinson, Sierra-Lite, Ordered, None.
  - srokl: Stucki, Atkinson, Ostromoukhov, contrast-aware, Zhou-Fang, stochastic (Velho SFC), Floyd, ordered, none.
  - XteinkImageRefiner: Floyd-Steinberg, Atkinson, Sauvola.
  - xtctool: Floyd-Steinberg with a strength setting.
  - cr2xt: Floyd-Steinberg, ordered.
  - Sources: [tazua](https://github.com/tazua/cbz2xtc/blob/main/README.md); [xtcjs](https://github.com/varo6/xtcjs); [srokl](https://github.com/srokl/cbz2xtc); [note.com](https://note.com/feeeen/n/n9b036c73da6f?hl=en); [xtctool](https://github.com/chazeon/xtctool); [cr2xt](https://github.com/CrazyCoder/cr2xt)
- **Gamma/contrast**: XTC.js has contrast presets (Medium for manga, Strong/Maximum for PDFs). srokl has `--gamma` (<1 brightens, >1 darkens) and `--contrast-boost N`. XteinkImageRefiner has CLAHE, contrast, blur and sharpen. xtctool has three quantization thresholds. — [xtcjs](https://github.com/varo6/xtcjs); [srokl](https://github.com/srokl/cbz2xtc); [note.com](https://note.com/feeeen/n/n9b036c73da6f?hl=en); [xtctool](https://github.com/chazeon/xtctool)
- **Compression**: the spec says "Compression is currently not implemented (compression field = 0)". srokl invented a nonstandard LZ4 `.xtcz` container, and its xtc2xtcz.py also converts existing files. — [CrazyCoder gist](https://gist.github.com/CrazyCoder/b125f26d6987c0620058249f59f1327d); [srokl](https://github.com/srokl/cbz2xtc)
- The local CrossPoint fork (~/Projects/crosspoint-reader, lib/Xtc) accepts only the XTC and XTCH magic values and contains no `xtcz`/LZ4 references. — local source inspection of `lib/Xtc/Xtc/XtcParser.cpp:132-134`, `XtcTypes.h`

### Inferences
- **Page sizes** follow from the spec at 480×800. An XTG page is 480×800/8 = 48,000 bytes + a 22-byte header ≈ 48 KB. An XTH page is 2 × 48,000 = 96,000 bytes + 22 ≈ 96 KB. File size therefore depends only on page count, not content, because there is no compression. A 200-page volume split into thirds becomes about 600 pages: roughly 29 MB as XTC or 58 MB as XTCH. These figures are calculated, not measured.
- `.xtcz` files will likely not open on stock or CrossPoint firmware unless some fork adds LZ4 support. I checked only the local CrossPoint fork.
- Splitting into thirds with 70% overlap trades page count and file size for magnified text. That magnification is the community's main workaround for small text on a 4.3" 480×800 panel.

### Gaps
- I found no measured file-size comparisons published by the tool authors (only tazua's 15–40 KB for 1-bit BMP backgrounds).
- I did not inspect the exact gamma or contrast curves in the source code (e.g. XTC.js's "Medium" contrast value).

## Quality comparisons and recommended manga settings (screentone/moiré, text, furigana)

### Takeaway
Recommendations come from the tool authors, not from independent tests. For 1-bit, the authors recommend Floyd-Steinberg (or Stucki/Zhou-Fang in srokl) plus medium contrast and overlapping-thirds landscape splits. For text-heavy pages they recommend ordered dithering or none, and for best overall quality 2-bit XTCH. The one comparative write-up (a Japanese note.com post) found contrast adjustment plus XTCH gave the best legibility. I found nothing specific on screentone moiré or furigana.

### Cited Findings
- **XTC.js suggested settings**: for manga and comics, Floyd-Steinberg, Medium contrast, Overlapping thirds split, Landscape orientation. For PDFs, Atkinson, Strong/Maximum contrast, Landscape. The FAQ calls Floyd-Steinberg the "Best all-rounder for manga with detailed art", Atkinson "great for text-heavy content", and Sierra-Lite "good for high-contrast art". — [xtcjs README](https://github.com/varo6/xtcjs)
- **tazua recommendations**: for text-heavy manga use ordered dithering or `--dither-algo none` ("Ordered dithering often clearer for dense text"). For clean line art use `--no-dither`. Use `--no-split` for full-page context, at the cost of the "text magnification from splitting". — [tazua README](https://github.com/tazua/cbz2xtc/blob/main/README.md)
- **srokl recommendations**: Stucki is the default ("Balanced for most content"). Atkinson is "Excellent for high-contrast line art". No dithering is "Best for text-only pages to keep edges crisp". 2-bit is the "Highest overall quality". The README advises users to "experiment with `--dither zhoufang` and `--2bit`". — [srokl README](https://github.com/srokl/cbz2xtc)
- **XteinkImageRefiner author**: found their pipeline cleaner than existing converters, found "contrast adjustment with XTCH" most effective for legibility, and noted that manga text can get slightly squashed in conversion. — [note.com, Mar–May 2026](https://note.com/feeeen/n/n9b036c73da6f?hl=en)
- The panel is 4.3", 480×800 (portrait), about 220 PPI per the tazua README. — [tazua README](https://github.com/tazua/cbz2xtc/blob/main/README.md)

### Inferences
- Error diffusion (FS, Stucki, Atkinson) generally re-dithers existing screentone dot patterns. When source tone frequency comes near the 480×800 grid after downscaling, moiré is the expected risk. That would explain why 2-bit output (fewer dither artifacts) and box/area downscaling are favored. This is reasoning, not a documented test.
- Splitting into thirds makes furigana and small text larger by roughly 1.6×. Full-page 1-bit output at 480×800 would likely leave furigana illegible, but I found no source testing this.

### Gaps
- I found no Reddit r/xteink or r/ereader threads with concrete comparisons. Searches surfaced tool READMEs, not community threads.
- I found no discussion of screentone moiré, furigana legibility, or descreening (e.g. a blur before downscale; only XteinkImageRefiner has a blur step).

## XTC/XTCH format technical details (public documentation)

### Takeaway
XTC/XTCH are uncompressed little-endian containers: a 56-byte header, optional 256-byte metadata, a 16-byte-per-page index, optional thumbnails and chapters, and 22-byte-header page bitmaps. Pages are XTG (1-bit, row-major, MSB = leftmost pixel) or XTH (2-bit as two bit planes, column-major, columns scanned right-to-left, 8 vertical pixels per byte). Page dimensions are per-page uint16, so the format allows up to 65535×65535. Page count is uint16, so at most 65,535 pages.

### Cited Findings (all from [CrazyCoder gist](https://gist.github.com/CrazyCoder/b125f26d6987c0620058249f59f1327d), spec dated "2025-01-XX"; mirror at [rberenguel gist](https://gist.github.com/rberenguel/091701430ac3f37367c74dbe0590da4c))
- **Magic values** (little-endian): XTG `0x00475458` "XTG\0"; XTH `0x00485458` "XTH\0"; XTC `0x00435458` "XTC\0"; XTCH `0x48435458` "XTCH". All multi-byte values are little-endian.
- **XTG/XTH page header (22 bytes)**:

  | Offset | Size | Field |
  |---|---|---|
  | 0x00 | 4 | mark |
  | 0x04 | 2 | width (u16) |
  | 0x06 | 2 | height (u16) |
  | 0x08 | 1 | colorMode (0 = mono) |
  | 0x09 | 1 | compression (0 = none) |
  | 0x0A | 4 | dataSize |
  | 0x0E | 8 | md5 (first 8 bytes, optional) |

- **XTG data**: 1 bpp, rows top-to-bottom, 8 pixels per byte with the MSB as the leftmost pixel.
- **XTH data**: two sequential bit planes in vertical scan order (column-major). Columns are scanned right-to-left (x = width−1 down to 0), with 8 vertical pixels per byte. The pixel value is formed from (plane1 bit, plane2 bit): 0 = White, 1 (01) = Dark Grey, 2 (10) = Light Grey, 3 = Black. The mapping is non-monotonic, so 1 is dark and 2 is light.
- **XTC/XTCH container header (56 bytes)**:

  | Offset | Size | Field |
  |---|---|---|
  | 0x00 | 4 | mark |
  | 0x04 | 2 | version (0x0100) |
  | 0x06 | 2 | pageCount (u16) |
  | 0x08 | 1 | readDirection (0 = L→R, 1 = R→L, 2 = top→bottom) |
  | 0x09 | 1 | hasMetadata |
  | 0x0A | 1 | hasThumbnails |
  | 0x0B | 1 | hasChapters |
  | 0x0C | 4 | currentPage (1-based) |
  | 0x10 | 8 | metadataOffset (u64) |
  | 0x18 | 8 | indexOffset (u64) |
  | 0x20 | 8 | dataOffset (u64) |
  | 0x28 | 8 | thumbOffset (u64) |
  | 0x30 | 8 | chapterOffset (u64) |

- **Page index entry (16 bytes)**: u64 absolute offset, u32 size (including the 22-byte page header), u16 width, u16 height.
- **Metadata (256 bytes)**: title (128 bytes), author (64), publisher (32), language (16), createTime (u32 Unix), coverPage (u16, 0xFFFF = none), chapterCount (u16), reserved (8).
- **Chapter entry (96 bytes)**: name (80 bytes, UTF-8), startPage and endPage (u16, 0-based, inclusive), reserved (12).
- The spec says chapters are "not implemented yet in the Xteink firmware (as of 3.1.0)". Compression is not implemented.
- Tool-side confirmations: xtctool documents the same magic values, "Vertical bitplane encoding (XTH), row-major bitmap (XTG)", and LTR/RTL/TTB directions ([xtctool](https://github.com/chazeon/xtctool)). srokl describes its output as "1-bit XTC, 2-bit XTCH (Vertical scan order)" ([pablohc fork](https://github.com/pablohc/srokl_cbz2xtc)). CrossPoint's `lib/Xtc` reads both magic values and reports bit depth 1 or 2 (local source `lib/Xtc/Xtc.h:75`, `XtcTypes.h:24`, on the local ~/Projects/crosspoint-reader fork).

### Inferences
- The `readDirection` byte (1 = R→L) is the format-level hook for manga. Converters such as xtctool expose it, but splitting tools already reorder segments themselves (e.g. srokl `rtl`).
- XTH's column-major, right-to-left plane layout likely mirrors the panel's native 800×480 landscape scan orientation, so the firmware can stream it without transposing. This is unverified; no source states it.

### Gaps
- The gist carries only a placeholder date ("2025-01-XX"). Whether firmware releases after 3.1.0 implement chapters is not stated in the gist.
- The public sources do not document any firmware limit on page dimensions or file size beyond the u16/u64 field widths.
