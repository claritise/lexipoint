# Languages: Japanese and Simplified Chinese

**Status:** proposed 2026-09-24. Decision D14 in `00-overview.md`. It adds **Simplified Chinese
(`zh`)** next to Japanese as a v0.1 language. Traditional Chinese is out of scope until H8 is
answered.

Related: `lookup-flow.md` (the flow is language-agnostic), `sentence-extraction.md` §2
(punctuation), `popup-ui.md` §1 (the reading line), `lexirise-client.md` §5 (config).

---

## 0. Why this is cheap

The expensive part of Japanese support is that **the device can't find word boundaries**. That was
solved by letting the server do it (D4). Chinese has exactly the same shape:

- **No spaces.** CrossPoint splits Chinese into one token per character (`ParsedText`
  `utf8IsCjkBreakable` covers all CJK ideographs), so a tap selects 学 in 学习.
- **The server segments.** `analyze/text` with `language: "zh"` returns occurrences that span the
  word (学习, charStart..charEnd). The match step (`lookup-flow.md` §5 ②) is unchanged.
- **No inflection.** Lemma = surface form, so H2 (lemma vs surface) doesn't arise for Chinese.

What differs is the **language choice per book**, **punctuation**, **the reading line (pinyin)**,
**fonts**, and **the StarDict fallback dictionary**.

## 1. Which language to send (per book, not global)

Config `language=ja` (D8) can't serve a reader who has both Japanese and Chinese books. Han
characters alone can't tell the two apart (学生 is valid in both). The language is decided **per
book**, in this order:

1. **EPUB metadata.** CrossPoint already parses `<dc:language>` (`ContentOpfParser`, cached in
   `BookMetadataCache::language`). Map it:
   | `dc:language` | Send |
   |---|---|
   | `ja`, `ja-*` | `ja` |
   | `zh`, `zh-CN`, `zh-SG`, `zh-Hans`, `zh-Hans-*`, `cmn`, `cmn-Hans` | `zh` |
   | `zh-TW`, `zh-HK`, `zh-MO`, `zh-Hant`, `zh-Hant-*` | **Traditional**. Treat as `zh` only if H8 says Lexirise handles it. Otherwise it goes straight to StarDict |
   | anything else | the provider is disabled for this book, so StarDict answers |
2. **Missing or bogus metadata** (common in scanned or converted books, where `en` or `und` is
   stamped on a Japanese novel): scan the **current sentence**. **Any kana (U+3040–U+30FF) → `ja`.**
   Otherwise, if it's all Han with no kana → the config's `default_language`.
3. A **per-book override** stored in `/.lexirise/books.ini` (`<bookId>=zh`), set from the card
   with a long-press on the language badge. It's for the rare book where both of the above guess
   wrong.

The decision is made once, when the book is opened, and cached for the reading session. The
sentence heuristic in step 2 runs per lookup only when there's no metadata.

**As built (P2, `text/BookLanguage`):** precedence is override → metadata → sentence. Metadata that is
missing, `und`, or any non-CJK language falls through to the sentence (a converted Japanese novel
stamped `en` still gets `ja`); a sentence with no CJK at all (a real English book) gets no language,
so StarDict answers. `zh-TW`/`zh-HK`/`zh-MO`/`zh-Hant` go to StarDict while H8 is parked. A language
switched off in settings (or Lexirise off) gives none, even with an override.

Config (superseded by the per-language sections in `settings.md` §3; the flat keys below are still read and migrated):

```ini
default_language=ja      # used only when the book doesn't say (was: language=)
languages=ja,zh          # the languages Lexirise is used for; others go to StarDict
stardict_ja=jmdict       # fallback dictionary folder per language (§4)
stardict_zh=cedict
```

`language=` is still read as an alias of `default_language` so existing configs work.

## 2. Punctuation (sentence extraction)

Extend `sentence-extraction.md` §2 rules 2–3:

| | Japanese | Simplified Chinese |
|---|---|---|
| Terminators | `。！？` `!?` `．` | `。！？` `!?` plus `；` **only** as a fallback cut when the sentence would pass the 120-codepoint cap |
| Ellipsis | `…` `……` | `……` (two U+2026, as standard) |
| Closers kept after a terminator | `」』）】` | `”’）】》` |
| Openers (the left walk stops *before* these only if a terminator precedes them) | `「『（【` | `“‘（【《` |
| Clause split (for C3 grow/shrink) | `、` | `，、` |

Chinese quotes are the curly `“ ”`, which are also used in English. Treat them as closers only
when the book language is `zh`, so English text keeps its current behavior.

## 3. The reading line: pinyin

- The language code is **`zh`** (the API reference's own examples use it). `transliteration` for `zh` is expected to be **pinyin**. **Verified 2026-09-24:** it comes with tone marks,
  space-separated per syllable (`xué xí`), plus a `tones` array (`[2, 2]`). Show it as is. No conversion is
  needed, so the `lexirise_pinyin` test suite is dropped.
- **Font check:** tone-marked vowels (ā á ǎ à, ē é ě è, ī í ǐ ì, ō ó ǒ ò, ū ú ǔ ù, ǖ ǘ ǚ ǜ, ü) are
  Latin Extended-A/B. The card uses the reader font. If that font lacks them (P4 checks this on the
  built-in and common SD fonts), fall back to the UI font for the reading line, and to tone
  numbers if neither has them.
- Layout is the same as Japanese: reading small on top, word large. Chinese words are short
  (mostly 1–3 characters), so the large line has room.
- The "surface form ≠ lemma" line never shows for `zh`.

## 3a. Japanese readings: romaji → kana on the device (H10)

The API gives Japanese readings **only in romaji**. Tested 2026-09-24 on 16 tricky words, its
romanization is consistent:

| Case | Lexirise gives | Kana |
|---|---|---|
| Long vowels, native words: spelled out | `toukyou`, `ookii`, `toori`, `otousan`, `kyou`, `gakkou`, `eiga` | とうきょう, おおきい, とおり, おとうさん, きょう, がっこう, えいが |
| ん before a vowel or y: apostrophe | `kin'youbi`, `fun'iki`, `man'indensha` | きんようび, ふんいき, まんいんでんしゃ |
| Doubled consonant → っ | `kakkoii`, `chotto`, `kekkon` | かっこいい, ちょっと, けっこん |
| Katakana words: macrons | `kōhī`, `bīru` | the word's own surface form (コーヒー, ビール) |

**Converter** (`src/lexirise/Kana.{h,cpp}`, pure function, host-tested):
1. If the word's surface form is **all katakana** (plus ー), the reading is the surface form. Done.
2. Otherwise, convert **romaji → hiragana** with a longest-match table (Hepburn plus wāpuro
   spellings: `shi`, `chi`, `tsu`, `fu`, `ji`, `kya`…, `n'` → ん, a doubled consonant → っ, a final or
   pre-consonant `n` → ん, and macron vowels → the vowel doubled with う/い per Hepburn usage, for
   the rare mixed words).
3. If any input is left unconverted, **fall back to showing romaji** for that word. Never show
   half-converted text.

**Known limit:** the converter can only be as right as Lexirise's reading. Seen live: 一緒 →
`ichiitoguchi` (should be いっしょ), and 一日 in 四月一日 → `ichinichi` (context, see v0.2 C10).
Report these to Lexirise. Don't patch them on the device.

**Tests** (`test/lexirise_kana/`): every row above, the six mock words (まいあさ, まんいんでんしゃ,
わずらわしい, かれ, かいしゃ, やめる), `n` edge cases (`kin'en` きんえん vs `kinen` きねん,
`shinbun` しんぶん), and unconvertible input → romaji fallback.

## 4. StarDict fallback per language

The upstream Dictionary setting is **one global dictionary**. A reader with both languages needs
JMdict for Japanese and CC-CEDICT for Chinese. `StarDictLookupProvider` opens the folder named by
`stardict_<lang>` (`Dictionary::open(folderName)` already takes a folder, so there's **no change to
the upstream code**). If that isn't set, it uses the global setting. Longest-prefix matching
(`lookup-flow.md` §4) works the same for Chinese.

CC-CEDICT is available as StarDict (see `docs/dictionary.md`'s source list upstream). Note it in
the user docs at P8.

## 5. Fonts

A book's text only renders if the user already has an SD font with Chinese coverage, so the card
(which reuses the reader font) inherits that coverage. The one trap is **Japanese fonts**: many
JIS-based fonts lack common Simplified characters (这, 们, 说, 过 …). A user reading Chinese with
their Japanese font sees replacement boxes in the book itself, which is an upstream setup issue,
not ours. Note it in the user docs.

### 5.1 Building the CJK font (verified 2026-09-24 on claritise's X4 Pro)

CrossPoint's prebuilt font collection (`crosspoint-reader/crosspoint-fonts`, release
`sd-fonts-m1-b4-r9`) has **no CJK font**. With no SD font selected, Japanese and Chinese render as
boxes. What works:

1. Get **`NotoSerifCJKjp-Regular.otf`** (Serif 2.003, 23.4 MB) from `github.com/notofonts/noto-cjk`
   (`Serif/OTF/Japanese/`). The "jp" region file still carries **every** CJK ideograph; it just uses
   Japanese letterforms by default, which read fine in Chinese books. For mainland letterforms,
   convert `NotoSerifCJKsc` the same way and switch fonts per book.
2. Convert it with the firmware's own script (`pip install freetype-py fonttools`). It takes ~40 s:
   ```
   python3 lib/EpdFont/scripts/fontconvert_sdcard.py NotoSerifCJKjp-Regular.otf \
     --intervals latin-ext,punctuation,cjk --sizes 8,10,12,14,16,18 \
     --style regular --name NotoSerifCJK --output-dir ./NotoSerifCJK/
   ```
   `latin-ext` carries **all pinyin tone marks** (ǎ ǐ ǒ ǔ ǖ ǘ ǚ ǜ), and `punctuation` the curly quotes.
   **Sizes 8, 10 and 12 matter**: CrossPoint uses them to draw CJK book titles in its menus.
   The result is 6 files, 25.6 MB, with 22,219 glyphs each.
3. Copy the folder to `/fonts/NotoSerifCJK/` (or `/.fonts/`) on the SD card. Over **USB Drive** mode, run
   the copy from the Mac's own Terminal or Finder, **not** a sandboxed shell, which macOS blocks from
   removable volumes. **Always eject before unplugging.** An unplug mid-copy truncates files.
4. On the reader: **Settings → Reader → Font Family → NotoSerifCJK**. The boot log confirms it with
   `[SDREG] Found family: NotoSerifCJK (6 files)`.

Not covered: CJK Extension A (U+3400–4DBF) and non-BMP characters (e.g. 𠮟). The `cjk` preset stops at
the main block. That's rare in fiction, and they fall back to boxes.

This goes into the P8 user guide as is.

## 6. Rank labels

Frequency distributions differ by language. The labels in `popup-ui.md` §1 (`very common` < 1k …)
are Japanese-tuned guesses. Keep the thresholds **per language** in one table, and tune them in P5
with both languages. Until then they're the same.

## 7. Tests

- `lexirise_sentence`: Chinese cases. A `“…”` dialogue paragraph, `《书名》` inside a sentence, a
  `……` ending, a `；` fallback cut at the cap, and mixed 我用iPhone拍照。
- `lexirise_language` (new): the `dc:language` mapping table, the kana heuristic, override
  precedence, and the `language=` alias.
