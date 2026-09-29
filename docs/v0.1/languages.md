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
   stamped on a Japanese novel): scan the **current sentence**. **Any kana (hiragana/katakana letters; not `・` or `ー`) → `ja`.**
   Otherwise, if it's all Han with no kana → the config's `default_language`.
3. A **per-book override** ~~stored in `/.lexirise/books.ini` (`<bookId>=zh`), set from the card
   with a long-press on the language badge~~ (as built in P9: set from the reader menu, keyed by the book's
   path; see "As built (P9)" below). It's for the rare book where both of the above guess wrong.

**As built (P9, claritise 2026-09-25: "we need to be able to set which language dictionary we use per book,
right now it's using Japanese even for Chinese books"):** a Chinese book whose `<dc:language>` is missing or
wrong has Han-only sentences, which go to "Language when a book doesn't say" (Japanese by default). The
override is now set from the **reader menu**, not the card: a **Lookup language** row (Auto → Japanese →
Chinese (Simplified), cycled in place, in the list menu and the toolbar's More panel), since the reader menu
is where per-book settings live and the card's design is binding. It's kept in `/.lexirise/books.ini` as
`<ja|zh>=<book path>` lines, newest last (Auto removes the line; `config::kBookLanguagesMax` books, the
oldest forgotten, sooner for long paths: `kBookLanguagesMaxBytes`; saved crash-safely like `config.ini`:
`settings/SafeFile.h`). The cycle is Auto, then `kLanguages` in settings order, so a new language joins it
by being added there. If the card can't be written, the row just keeps its value (the serial log says why;
no notice, as with CrossPoint's own menu toggles). The path is the key, so a
moved or renamed book starts at Auto again. The override decides the language sent to Lexirise and the
offline dictionary's language (`LanguageDecision::dictionaryLanguage`). Code: `settings/BookLanguages.h`
(`BookLanguageStore`, `BookLanguageRow`), `lookup::bookLanguageFor`. Tests: `BookLanguagesTest.cpp`.
The book-language row isn't scripted in `lxctl` (where it sits depends on the menu style and on which rows
the page shows), so it's a manual check: set a Chinese book to Chinese, look a word up (sent as `zh`), reboot (still Chinese),
Auto again (back to the default); the row in both menu styles.

The decision is made once, when the book is opened, and cached for the reading session. (As built: the
override is read at each lookup, so a change in the reader menu applies to the next one.) The
sentence heuristic in step 2 runs per lookup only when there's no metadata.

**As built (P2, `text/BookLanguage`):** precedence is override → metadata → sentence. Metadata that is
missing, `und`, or any non-CJK language falls through to the sentence (a converted Japanese novel
stamped `en` still gets `ja`); a sentence with no CJK at all (a real English book) gets no language,
so StarDict answers. `zh-TW`/`zh-HK`/`zh-MO`/`zh-Hant` go to StarDict while H8 is parked. A language
switched off in settings (or Lexirise off) isn't sent, even with an override, but stays `detected`, so
the sentence is still cut with its punctuation. "Kana" means real hiragana/katakana: `・` and `ー` also
appear in Chinese transliterated names (哈利・波特) and don't count. Traditional (`zh-TW`, `zh-Hant`) is
not sent while H8 is parked, but is `detected` as Chinese for its punctuation.

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
| Terminators | `。！？` `!?` `．` | `。！？` `!?` plus `；` **only** as a fallback cut when the sentence would pass the 120-unit (UTF-16) cap |
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

## 3a. Japanese readings: ~~romaji → kana~~ kana and romaji on the device (H10)

~~The API gives Japanese readings **only in romaji**.~~ **Superseded 2026-09-29** (fix-dzu): the dev key's
answers were romaji (every Japanese transliteration kept in `research/`, the last from V9b's probe on 2026-09-29), but on claritise's device the card's
reading line drew the same kana in both modes on every word (教室 きょうしつ, は), and the romaji mode's layout (its
other font) showed the tap was taken: the transliteration reached the card **in kana** (it comes in
romaji or in kana: `../reference/lexirise-api-notes.md`, "Japanese reading"). The
card now takes either form: kana is kept and read back as romaji for the tap (converter step 0). Tested 2026-09-24 on
16 tricky words, ~~its romanization~~ Lexirise's romaji (the dev key's) is consistent:

| Case | Lexirise gives | Kana |
|---|---|---|
| Long vowels, native words: spelled out | `toukyou`, `ookii`, `toori`, `otousan`, `kyou`, `gakkou`, `eiga` | とうきょう, おおきい, とおり, おとうさん, きょう, がっこう, えいが |
| ん before a vowel or y: apostrophe | `kin'youbi`, `fun'iki`, `man'indensha` | きんようび, ふんいき, まんいんでんしゃ |
| Doubled consonant → っ | `kakkoii`, `chotto`, `kekkon` | かっこいい, ちょっと, けっこん |
| Katakana words: macrons | `kōhī`, `bīru` | the word's own surface form (コーヒー, ビール) |

**Converter** (`src/lexirise/text/Kana.{h,cpp}`, pure function, host-tested; `japaneseReading` gives both):
0. (2026-09-29) If the transliteration is **all kana**, it is the kana reading (a katakana word's own surface form
   first, as in step 1), and `kanaToRomaji` gives the romaji in Lexirise's style: Hepburn, long vowels spelled out
   (`toukyou`), ー as a macron (`kōhī`), `n'` before a vowel or y, っ doubling the next consonant (`tch`), づ →
   `dzu`, ぢ → `ji` (unmeasured, see below). Katakana combinations too (デュ `dyu`, クァ `kwa`, ツェ `tse`, フュ
   `fyu`, ウィ `wi`), and the old ゐ ゑ (`wi`, `we`); **a final っ isn't written** (あっ → `a`: Hepburn has no letter for the cut-off,
   and a `'` would read as `n'`); a small vowel after a syllable is drawn out (ねぇ → `nee`); ゝ ゞ repeat the kana
   before (こゝろ `kokoro`, いすゞ `isuzu`). Over the kana words in `research/` (read-only, 2026-09-29) all read but
   fragments cut mid-word (a leading small kana, ー or っ, a doubled っ or ー). Anything it can't read shows as
   given in both modes (a katakana word still shows its own form as the kana). With **no** transliteration, a katakana word's romaji is read from the word itself
   (コーヒー → `kōhī`), or it shows as written.
1. If the word's surface form is **all katakana** (plus ー), the reading is the surface form. Done.
2. Otherwise, convert **romaji → hiragana** with a longest-match table (Hepburn plus wāpuro
   spellings: `shi`, `chi`, `tsu`, `fu`, `ji`, `kya`…, and Lexirise's `dzu` → づ, `n'` → ん, a doubled consonant → っ, a final or
   pre-consonant `n` → ん, and macron vowels → the vowel doubled with う/い per Hepburn usage, for
   the rare mixed words).
3. If any input is left unconverted, **fall back to showing romaji** for that word. Never show
   half-converted text.

**Known limit:** the converter can only be as right as Lexirise's reading. Seen live: 一緒 →
`ichiitoguchi` (should be いっしょ), and 一日 in 四月一日 → `ichinichi` (context, see v0.2 C10).
Report these to Lexirise. Don't patch them on the device. ~~Hepburn itself can't tell ず from づ or じ
from ぢ (`tsuzuku` → つずく, not つづく): the converter writes ず / じ, so a word with づ / ぢ reads one kana off.~~
**Superseded 2026-09-29** (fix-dzu): Lexirise doesn't spell づ as Hepburn's `zu`: it writes **`dzu`** (気づく
`kidzuku`, 続ける `tsudzukeru`, 小遣い `kodzukai`, 日付 `hidzuke`), and the converter turns `dzu` into づ exactly.
Before the fix `dzu` was missing from the table, so when the transliteration came in romaji, such a word's kana
reading failed and the card showed the romaji in both modes (found in `research/`'s transliterations, 2026-09-29). Measured over the
(word, transliteration) pairs kept in `research/` (every Japanese transliteration with a word, and the spike's
readings): every all-romaji transliteration converts; `dz` only ever comes as `dzu`, and never as `dj…`, `dy…`, `di`
or `du`; every `zu` is ず and every `ji` is じ where the word's kana shows it; no word with ぢ came up, so Lexirise's
spelling of ぢ is unmeasured: `di` → ぢ is the table's wāpuro entry, and kana → romaji writes ぢ as `ji` (step 0, a
guess): if Lexirise spells ぢ `ji`, such a word's kana reads じ, one kana off. ~~Nor can it tell the particle は (read *wa*) from わ: こんにちは comes out こんにちわ.~~ **Superseded
2026-09-29, in part:** the particle は comes as its own word, and Lexirise writes it `ha`
(`../reference/lexirise-api-notes.md`, "Tokenizer notes"), so it converts to は. Inside a word the limit stands: `wa`
is わ, so a word Lexirise spells with `wa` for は comes out with わ (こんにちは, if it answers `konnichiwa`: unmeasured).

**Tests** (`test/lexirise_kana/`): every row above, `dzu` → づ and synthetic (word, transliteration) pairs
shaped like the measured ones (`Kana.DzuIsDu`, `Kana.LexirisesZuDzuAndJiPairs`), kana read back as romaji, and romaji
read to kana and back unchanged for the listed words (`Kana.KanaReadsBackAsLexirisesRomaji`, `Kana.KanaAndRomajiRoundTrip`); the card's line in
both modes, with answers in romaji and in kana, on a live lookup, a lemma cache hit, an analyzed page and a step
(`LiveReadingToggle.*` in `test/lexirise_card/`), the six mock words (まいあさ, まんいんでんしゃ,
わずらわしい, かれ, かいしゃ, やめる), `n` edge cases (`kin'en` きんえん vs `kinen` きねん,
`shinbun` しんぶん), and unconvertible input → romaji fallback.

## 4. StarDict fallback per language

CrossPoint's Dictionary setting is **one global dictionary**. A reader with both languages needs
JMdict for Japanese and CC-CEDICT for Chinese. `StarDictLookupProvider` opens the folder named by
`stardict_<lang>` (`Dictionary::open(folderName)` already takes a folder, so there's **no change to
the base code**). If that isn't set, it uses the global setting. Longest-prefix matching
(`lookup-flow.md` §4) works the same for Chinese.

CC-CEDICT is available as StarDict (see the source list in `docs/dictionary.md`, CrossPoint's doc in `firmware/`). Note it in
the user docs at P8.

## 5. Fonts

A book's text only renders if the user already has an SD font with Chinese coverage, so the card
(which reuses the reader font) inherits that coverage. The one trap is **Japanese fonts**: many
JIS-based fonts lack common Simplified characters (这, 们, 说, 过 …). A user reading Chinese with
their Japanese font sees replacement boxes in the book itself, which is a font setup issue,
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
   cd firmware
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

Frequency distributions differ by language, so the thresholds are **per language**
(`config::kRankBandLimitsJa` / `Zh`, tuned in P5 on sampled ranks, `../reference/lexirise-api-notes.md`):

| | very common | common | uncommon | rare |
|---|---|---|---|---|
| Japanese | < 1,000 | < 5,000 | < 20,000 | 20,000 + |
| Chinese | < 1,000 | < 10,000 | < 30,000 | 30,000 + |

Chinese ranks run higher for words as common (景色: #2,643 in Japanese, #8,123 in Chinese). Its first
threshold stays at 1,000 because the approved reference shows 选择 (#1,113) as *common*.

## 7. Tests

- `lexirise_sentence`: Chinese cases. A `“…”` dialogue paragraph, `《书名》` inside a sentence, a
  `……` ending, a `；` fallback cut at the cap, and mixed 我用iPhone拍照。
- `lexirise_language` (new): the `dc:language` mapping table, the kana heuristic, override
  precedence, and the `language=` alias.
