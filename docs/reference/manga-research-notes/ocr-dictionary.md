# Manga OCR text layers + dictionary lookup (mokuro-style pipelines, e-ink integrations)

Research date: 2026-09-25. About 17 tool calls. Sources are mostly GitHub, PyPI and Hugging Face pages, so they are primary but thin on dates. Reddit, TheMoeWay and MobileRead did not come up directly in search. See the Gaps sections.

## mokuro: output format, versions, accuracy/speed, readers built on it

### Takeaway
mokuro (kha-white) is still the de facto PC-side pipeline. It runs comic-text-detector, then manga-ocr, and writes one `.mokuro` JSON per volume. Each page records image size and a list of text blocks with `box`, `vertical`, `font_size`, `lines` and `lines_coords`. The coordinates stop at the line level: there are **no per-character or per-word boxes**. Version 0.2.5 was released on 2026-07-20, so the project is still maintained.

### Cited Findings
- mokuro targets Japanese learners who read manga with a pop-up dictionary such as Yomitan. It runs text detection and OCR offline, before reading. It uses comic-text-detector for detection and manga-ocr for OCR, on PyTorch with optional GPU. The repo has about 1.7k stars, 121 forks, 35 open issues and 87 commits. — [kha-white/mokuro](https://github.com/kha-white/mokuro)
- Version 0.2.0 moved from per-volume HTML, which embedded the whole reader GUI, to a `.mokuro` file that holds only the OCR results and the metadata the web reader needs. Legacy HTML is still generated for backward compatibility but is no longer developed. OCR results are cached in `_ocr` directories. — [kha-white/mokuro](https://github.com/kha-white/mokuro); [PyPI mokuro](https://pypi.org/project/mokuro/)
- Release history: 0.2.0 (2024-07-07), 0.2.1 (2024-07-09), 0.2.2 (2025-01-28), 0.2.3 and 0.2.4 (both 2026-02-22), 0.2.5 (2026-07-20). It requires Python 3.10+. `--disable_ocr` generates .mokuro/HTML without OCR results. — [PyPI mokuro](https://pypi.org/project/mokuro/)
- The volume-level JSON is built in `generate_mokuro_file` as `{"version", "title", "title_uuid", "volume", "volume_uuid", "pages": []}`. Each page is the cached per-page OCR JSON plus `img_path`, which is relative and uses forward slashes. — [mokuro_generator.py](https://raw.githubusercontent.com/kha-white/mokuro/master/mokuro/mokuro_generator.py)
- The per-page result is `{"version", "img_width", "img_height", "blocks": [...]}`. Each block is `{"box": list(blk.xyxy), "vertical": blk.vertical, "font_size": blk.font_size, "lines_coords": [...], "lines": [...]}`. `box` is the detector's xyxy rectangle. `lines_coords` comes from `line.tolist()` on each detected text-line polygon. `font_size` is the detector's estimate of character height. — [manga_page_ocr.py](https://raw.githubusercontent.com/kha-white/mokuro/master/mokuro/manga_page_ocr.py)
- OCR detail: each line crop is OCR'd separately. Vertical lines are rotated 90 degrees clockwise before OCR. Long lines are split into chunks when the aspect ratio exceeds `max_ratio_vert=16` or `max_ratio_hor=8`. Split points come from Gaussian-smoothed density analysis, and the chunk texts are concatenated. No explicit character replacement happens in this step. — [manga_page_ocr.py](https://raw.githubusercontent.com/kha-white/mokuro/master/mokuro/manga_page_ocr.py)
- A third-party .mokuro importer (kotoba-studio PR, 2026-09-16) checked field names against mokuro 0.2.5 and the 0.2.0-beta.6 fixture. It enforces a `pages` array, a `blocks` array per page, `len(lines) == len(lines_coords)`, a boolean `vertical`, and numeric coordinates. It also found an OverflowError bug on oversized integer coordinates. This is useful as a list of validation rules for any converter. — [kotoba-studio PR #95](https://github.com/ColinHouse/kotoba-studio/pull/95)
- Readers built on the format:
  - Official Svelte web reader at reader.mokuro.app, by ZXY101, kha-white and Gnathonic. Features: single or dual page detection, preloading, night mode and inversion, automatic OCR text sizing, stat tracking, AnkiConnect with image cropping, offline/PWA install. The legacy repo has 201 commits and needs mokuro 0.2.0-beta.6 or later. — [ZXY101/mokuro-reader](https://github.com/ZXY101/mokuro-reader); [search summary](https://sveltethemes.dev/ZXY101/mokuro-reader); [Gnathonic fork](https://github.com/Gnathonic/mokuro-reader)
  - Yomitan works over the reader. There is a known quirk where the Yomitan and AnkiConnect sentence capture grabs UI text ("Volume"). — [issue #24](https://github.com/ZXY101/mokuro-reader/issues/24)
  - Mobile: jidoujisho-mokuro-reader, and a jidoujisho issue asking for .mokuro support. — [ZXY101/jidoujisho-mokuro-reader](https://github.com/ZXY101/jidoujisho-mokuro-reader); [jidoujisho #416](https://github.com/arianneorpilla/jidoujisho/issues/416)
  - A Kavita self-hosted server PR adds mokuro OCR overlays to its manga reader. — [Kavita PR #4896](https://github.com/Kareadita/Kavita/pull/4896)
  - fsestini/mokuro-reader runs mokuro's OCR as a single-page webapp. — [fsestini/mokuro-reader](https://github.com/fsestini/mokuro-reader)
- Forks:
  - faster-mokuro adds batched OCR inference, Apple Silicon MPS, parallel prefetch, `torch.inference_mode()` with `max_length=96` decoding, and a `--bundle`/`--single_file` flag that embeds the .mokuro inside the .cbz. It publishes no benchmark numbers. — [faster-mokuro](https://github.com/JanLancelot/faster-mokuro)
  - mokuro_chinese is a translation-oriented fork with swappable OCR engines. — [mokuro_chinese](https://github.com/hkrds1996/mokuro_chinese)

### Inferences
- `lines_coords` entries are almost certainly 4-point quadrilaterals, one per line, from the DBNet line detector (`line.tolist()` of a polygon). `box` is an axis-aligned `[x1,y1,x2,y2]` in page-image pixels, relative to `img_width`/`img_height`. A converter should rescale to the device's rendered image size.
- mokuro does not supply per-character or per-word boxes. Browser readers get char-level hit-testing for free: they render the OCR text as an HTML overlay sized with `font_size`, in vertical writing-mode, and the browser lays out each glyph. An ESP32 sidecar would have to **interpolate** character positions itself. For line i with n characters, split its quad evenly along the reading axis. Then group characters into words with MeCab or Sudachi (JP) or jieba/pkuseg (ZH) on the PC.
- For the Lexipoint design, the practical pipeline is: run mokuro (or faster-mokuro) on the PC, parse the .mokuro, segment each line's text, and emit compact per-word rectangles plus dictionary-entry IDs. The .mokuro file itself is too verbose and too JSON-heavy to parse on an ESP32.

### Gaps
- No published accuracy (CER) or throughput (pages/sec) figures for mokuro itself, CPU or GPU. faster-mokuro also gives no numbers.
- The exact current ZXY101 reader repo activity and release dates were not visible. The fetched page looked like the legacy repo.
- I did not confirm the exact point ordering of `lines_coords` from source (it comes from the comic-text-detector internals).

## manga-ocr, comic-text-detector, and alternatives (Chinese, VLMs, Google Lens)

### Takeaway
manga-ocr, a roughly 400 MB ViT encoder-decoder, handles vertical text and furigana well but can hallucinate and gets worse on long text. comic-text-detector (GPL-3.0) combines YOLOv5 block detection, DBNet lines and UNet masks. In 2025-26, VLM OCR fine-tunes such as PaddleOCR-VL-For-Manga reached much higher full-sentence accuracy. Google Lens (via owocr) is widely seen as the best engine for ad-hoc use. For Chinese manhua there is no mokuro equivalent aimed at learners. PaddleOCR-based translator toolchains are the closest building blocks.

### Cited Findings
- manga-ocr:
  - It uses the Hugging Face VisionEncoderDecoder, trained on Manga109-s plus synthetic text from CC-100.
  - It handles vertical and horizontal text, furigana, text over images, varied fonts and low-quality images.
  - It reads multi-line text in a single pass.
  - The model is about 400 MB. It runs on CPU or GPU and is Apache-2.0.
  - Limitations: handwritten text is weak, it may hallucinate realistic text where there is none, and accuracy drops on longer passages.
  
  — [kha-white/manga-ocr](https://github.com/kha-white/manga-ocr)
- comic-text-detector: YOLOv5 text-block detection, DBNet text-line segmentation and a UNet text mask. It was trained on about 13k images (Manga109-s, Digital Comic Museum, and synthetic data). GPL-3.0. 378 stars and 41 commits (moderately active). No speed figures are published. — [dmMaze/comic-text-detector](https://github.com/dmMaze/comic-text-detector)
- PaddleOCR-VL-For-Manga (1.0B params, BF16, Apache-2.0) was fine-tuned from PaddleOCR-VL on about 0.1M Manga109-s crops plus 1.5M synthetic samples. It reaches **70% full-sentence accuracy** on Manga109-s test crops, against 27% for the base PaddleOCR-VL. Its common errors are full-width vs half-width punctuation and digits. It works best on text-region crops, not full pages. Quantized llama.cpp builds exist. — [HF model card](https://huggingface.co/jzhang533/PaddleOCR-VL-For-Manga); [GitHub](https://github.com/jzhang533/PaddleOCR-VL-For-Manga)
- MangaOCR (gnurt2041) is a lighter PaddlePaddle-based model positioned against manga-ocr's 444 MB size and slow speed. It reports a CER of about 14.4%. — [gnurt2041/MangaOCR](https://github.com/gnurt2041/MangaOCR) (via search snippet)
- Research benchmarks on vertical Japanese and manga: an evaluation of multimodal LLMs on vertically written Japanese (arXiv 2511.15059), a manga OCR tutorial covering the "vertical text curse", and Manga109-v2026 re-annotations (arXiv 2605.21182). — [arXiv 2511.15059](https://arxiv.org/pdf/2511.15059); [Medium: fine-tuning PaddleOCR-VL for manga](https://medium.com/@alex_paddleocr/cracking-the-vertical-text-curse-fine-tuning-paddleocr-vl-for-japanese-manga-on-an-rtx-3060-abc129bd0939); [Manga109-v2026](https://arxiv.org/pdf/2605.21182)
- owocr is a multi-engine OCR client derived from manga-ocr. It supports Google Lens (which its docs recommend as arguably the best engine), manga-ocr with comic-text-detector as a segmenter, EasyOCR and RapidOCR. It automatically filters furigana for Japanese. YomiNinja is a similar learner-focused OCR and dictionary app that also uses Google Lens. — [AuroraWright/owocr](https://github.com/AuroraWright/owocr); [YomiNinja](https://github.com/matt-m-o/YomiNinja)
- Chinese: comic translation toolchains based on BallonsTranslator bundle 20+ detectors and 30+ OCR engines, including PaddleOCR, for manga, manhwa and manhua. Lexirise is a commercial app that OCRs manhua panels and shows pinyin and definitions. — [GitHub topic: manga-translator](https://github.com/topics/manga-translator?o=asc&s=stars); [Lexirise blog](https://lexirise.app/blog/article/best-apps-learn-chinese-manhua) (vendor source, promotional)

### Inferences
- For JP on a PC, mokuro's defaults are fine. A higher-accuracy option is to re-OCR the mokuro line crops with PaddleOCR-VL-For-Manga or Google Lens and keep mokuro's geometry.
- For ZH, comic-text-detector is language-agnostic for detection. OCR needs a Chinese model (PaddleOCR or a VLM), since manga-ocr is Japanese-only. Segmentation needs jieba or pkuseg. Manhua is often horizontal and colour or webtoon-format, which is a different layout from JP manga.
- Google Lens runs in the cloud, so it conflicts with an offline and privacy-friendly pipeline. Keep it optional.

### Gaps
- No head-to-head CER comparison of manga-ocr, PaddleOCR-VL-For-Manga and Google Lens on the same test set was found.
- No learner-oriented "mokuro for Chinese" project that outputs a text-layer format was found.

## E-ink integrations (KOReader, Kindle, Kobo, Boox, PocketBook): tap mapping, dictionaries, segmentation

### Takeaway
The only dedicated e-ink integration found is **mokuroreader.koplugin** for KOReader. It reads a .mokuro inside a CBZ, detects taps on text bubbles, and shows the OCR text in a popup. The user then long-presses or drags to select characters and hands the selection to KOReader's dictionary. So segmentation is left to the user and to KOReader's Yomichan-style Japanese plugin. It does not come from precomputed word boxes. On Kindle, the other route is Mokuro2Pdf, which bakes a selectable text layer into a PDF.

### Cited Findings
- mokuroreader-koreader (Magyarapointe):
  - It auto-detects mokuro-processed CBZs. The format is "CBZ (ZIP with images + .mokuro file)", parsed with rapidjson.
  - A tap on a bubble opens the OCR text in a popup at the bottom of the screen. The user holds a finger on the text, drags to select characters, and releases to open the dictionary through KOReader's `DictQuickLookup`.
  - It claims to work on all KOReader platforms (Kindle, Kobo, PocketBook, Android). Requirements are given as KOReader v2024.07+ in one place and v2020.01+ in another (inconsistent docs), plus mokuro 0.2.0+.
  - The project is small: 4 commits, 25 stars, 6 forks. No dates were visible.
  
  — [mokuroreader-koreader](https://github.com/Magyarapointe/mokuroreader-koreader); [search summary of README/QUICKSTART](https://github.com/Magyarapointe/mokuroreader-koreader/blob/main/QUICKSTART.md)
- KOReader's built-in Japanese plugin (by cyphar, PR #8312) is modelled on Yomichan:
  - It has a pure-Lua port of Yomichan's deinflector.
  - Its cleanup steps cover half-width kana, katakana/hiragana conversion and collapsing of emphatic sequences.
  - It expands the selection to produce more candidate words, because Japanese has no spaces.
  - This enables one-tap lookup of inflected verbs and multi-character words.
  
  — [koplugin.japanese docs](https://koreader.rocks/doc/modules/koplugin.japanese.html); [PR #8312](https://github.com/koreader/koreader/pull/8312)
- There is an open KOReader request to improve Japanese word lookup with MeCab, and an issue about Japanese text-selection problems. Longest-match scanning is therefore the current approach, not a morphological analyzer. — [issue #11728](https://github.com/koreader/koreader/issues/11728); [issue #8270](https://github.com/koreader/koreader/issues/8270)
- dictionary-mode.koplugin enables one-tap dictionary lookups with language support. — [E5DR/dictionary-mode.koplugin](https://github.com/E5DR/dictionary-mode.koplugin)
- On Kindle, Mokuro2Pdf builds PDFs with selectable text from mokuro's HTML overlay. It has options for Kindle upscaling (`-u`), gamma (`-g 0.8` suggested for contrast), text transparency and JPG compression, and it can also run online. Mined text can be pushed to Anki with Memo2Anki. The repo has 95 commits. — [Kartoffel0/Mokuro2Pdf](https://github.com/Kartoffel0/Mokuro2Pdf)
- jmdict-kindle provides a JMdict-based J-E dictionary for e-ink Kindles, with inflected-verb lookup. — [jmdict-kindle](https://github.com/lippmann/jmdict-kindle)

### Inferences
- Current e-ink UX takes two steps: tap a bubble, then select characters in a text popup. Selecting characters directly on the image is impractical at e-ink resolution and touch accuracy. This fits the Lexipoint design: tap a region to pick a block or word, then show a card, with side buttons stepping through words.
- Precomputing word boxes on the PC (MeCab/Sudachi or jieba) with lemma and dictionary IDs removes the need for on-device deinflection. No existing e-ink project was found that does this, so it would be novel.

### Gaps
- No Boox-specific app, native Kobo (nickel) hack, or PocketBook-native mokuro integration was found beyond KOReader.
- Kindle's native Panel View / Kindle Comic Converter was not confirmed to support hidden text layers.
- The koplugin's coordinate scaling and tap hit-testing code was not documented in the README. Only the "touch zones" wording was found.
- I could not reach Reddit (r/LearnJapanese), TheMoeWay or MobileRead threads via search, so community reports of real-world e-ink use are missing.

## Formats embedding text layers in images

### Takeaway
There are three existing patterns: (1) a sidecar JSON inside the archive (CBZ + .mokuro, which the KOReader plugin and faster-mokuro's `--bundle` both use), (2) a PDF with an invisible text layer (Mokuro2Pdf), and (3) legacy mokuro HTML, which uses an absolutely positioned text overlay. No format stores word-level boxes.

### Cited Findings
- Using a CBZ with an embedded .mokuro as the transport is supported by the KOReader plugin and produced by faster-mokuro `--bundle`/`--single_file`. — [mokuroreader-koreader](https://github.com/Magyarapointe/mokuroreader-koreader); [faster-mokuro](https://github.com/JanLancelot/faster-mokuro)
- PDF with selectable text from the mokuro overlay: [Mokuro2Pdf](https://github.com/Kartoffel0/Mokuro2Pdf)
- Legacy per-volume HTML with the embedded reader GUI: [kha-white/mokuro](https://github.com/kha-white/mokuro)

### Inferences
- A Lexipoint sidecar could live next to the CBZ, or inside it the way .mokuro does, as a binary file. Keeping mokuro's block/line hierarchy and adding a word layer gives upward compatibility, since the tools could regenerate from .mokuro.

### Gaps
- I did not verify whether ComicInfo.xml or EPUB3 fixed-layout manga with hidden text are used in practice by any learner tool. No sources were found.

## Known pitfalls

### Takeaway
The pitfalls that recur across sources are hallucinated text, long-text degradation, handwriting and SFX, furigana contamination, vertical-text difficulty for general OCR and VLMs, and punctuation width variants. On e-ink, the lack of direct char-level selection on images pushes designs toward popup-based selection.

### Cited Findings
- manga-ocr hallucinates plausible text on non-text regions, is weak on handwriting, and loses accuracy on longer text. — [kha-white/manga-ocr](https://github.com/kha-white/manga-ocr)
- Vertical Japanese text is a known weak point for general OCR and VLMs. The base PaddleOCR-VL scored only 27% full-sentence accuracy on manga crops before fine-tuning. — [PaddleOCR-VL-For-Manga](https://huggingface.co/jzhang533/PaddleOCR-VL-For-Manga); [arXiv 2511.15059](https://arxiv.org/pdf/2511.15059)
- Errors of full-width vs half-width punctuation and numerals should be normalized after OCR. — [PaddleOCR-VL-For-Manga](https://huggingface.co/jzhang533/PaddleOCR-VL-For-Manga)
- Furigana: owocr filters furigana automatically, which implies raw OCR can include it. manga-ocr is trained to handle text with furigana. — [owocr](https://github.com/AuroraWright/owocr); [manga-ocr](https://github.com/kha-white/manga-ocr)
- mokuro splits very long lines into chunks by aspect ratio, which risks a mis-split mid-word. — [manga_page_ocr.py](https://raw.githubusercontent.com/kha-white/mokuro/master/mokuro/manga_page_ocr.py)
- KOReader has open issues on Japanese text selection. — [issue #8270](https://github.com/koreader/koreader/issues/8270)

### Inferences
- Because mokuro gives only line quads, interpolated character boxes will drift where OCR drops or adds characters, such as hallucinations or merged furigana. Word boxes should be padded, and taps should resolve to the nearest word, not require an exact hit.
- On a small screen of about 480x800 showing a scaled-down manga page, a word may be only a few pixels wide. Plan for tap-to-block, then step through words with buttons, instead of precise word taps.

### Gaps
- There is no quantitative data on tap accuracy for small e-ink screens with manga.
- No numbers were found on how often furigana leaks into manga-ocr output.
