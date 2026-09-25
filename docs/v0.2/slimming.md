# Slimming: Lexipoint as CJK-learning firmware

**Status:** approved by claritise 2026-09-25 ("yes to all"), for v0.2. Not started. It runs **after phase M**
(`../v0.1/standalone-repo.md`), because deleting base code is only cheap once there are no rebases to keep
working. This doc also absorbs M's planned follow-up cleanup, "M2" (`standalone-repo.md` §6), and C22 "One font
for everything" (`00-overview.md`).

Written for the agent that builds it. Defaults here apply unless claritise says otherwise. The items under
"Needs claritise" (§7) are never guessed.

---

## 0. Why

The v0.2 guiding principle (`00-overview.md`, top) is that the X4 Pro is **a dedicated Lexirise client for 95% of
use**. CrossPoint is a general reader for many devices, languages and ecosystems, and most of that is
dead weight for a Japanese and Chinese learner on one touch device.

The practical reason is space. **The X4 Pro release build is 5.57 MB in a 6.4 MB app slot (87%)**
(`x4pro-gh_release`, P10, measured 2026-09-25). Everything v0.2 and v0.3 add costs flash and RAM: page
annotations, SRS reviews, the manga reader, on-device caches, a kanji pack. Slimming pays for them. It also
means less code to test and fewer settings screens that don't matter, and it gives C20 the focused product it
wants to show Lexirise.

## 1. Measured: where the flash goes

From `firmware.map` and `nm -S` of the `x4pro-gh_release` build (2026-09-25, P10). Sizes are flash (code
plus read-only data). Redo this measurement at the start of the phase, because M and v0.1.x will have moved
it.

| What | Size | Verdict |
|---|---|---|
| Built-in **reader fonts**: Noto Serif at 12/14/16/18 and Noto Sans at 12/14/16/18, each in regular, bold, italic and bold-italic | **≈ 1,830 KB** | **Cut** (C22: the reader font is the SD CJK family) |
| Built-in **UI fonts**: Ubuntu 10/12 (`UI_10`, `UI_12`), Noto Sans 8 (`SMALL`) | ≈ 265 KB | **Keep.** The card draws with `UI_10` and `SMALL`, and the approved card is binding (`../v0.1/popup-ui.md` §1.1). They also render without an SD card |
| **Hyphenation** tries: de 201, ru 33, en 26, sv 23, uk 21, pl 15, es 13, fr 7, it, fi | ≈ 342 KB | **Cut all but `en`** (≈ 316 KB). CJK text doesn't hyphenate. English keeps its trie for the English in bilingual books |
| **UI translations** (`lib/I18n`, 33 languages + English) | ≈ 368 KB | **Cut to English.** Lexipoint's own strings are English only |
| **KOReader sync** (`lib/KOReaderSync`, its activities and settings) | ≈ 35 KB + screens | **Cut** |
| **OPDS** (browser activity, `lib/OpdsParser`, server list and settings) | ≈ 23 KB + parser + screens | **Cut** |
| **Calibre connect** | small | **Cut.** Its instruction string goes with it |
| **WebDAV** (`WebDAVHandler`) | small | **Cut.** The web upload page covers getting books on |
| **Font download** (`FontDownloadActivity`), and the font picker once C22 lands | ≈ 19 KB + picker | **Cut** |
| **Button remap, keyboard layouts** | small | **Cut** (touch-first; keep one on-screen keyboard layout, for WiFi passwords) |
| **Themes** Lyra and RoundedRaff | small | **Cut.** Keep the base theme, the one the card matches |
| **Right-to-left text** (`lib/MiniBidi`) | small | **Cut** (only Arabic, Hebrew and Persian needed it) |
| **Code for devices without touch**: the X3/X4 key maps in `MappedInputManager`, the button legend, C3-only paths, `FirmwareBoardTag` entries for other boards | small | **Cut** (was M2; D20) |

**Rough total: 2.5–2.7 MB, close to half the firmware**, most of it the built-in reader fonts. The per-row sizes
are exact. The total is an estimate, because removing a feature also removes code only it used, which the map
can't attribute ahead of time.

**Keep** (checked, not cut):
- **The XTC reader.** The manga pipeline's output is XTCH (`manga.md`), so it's needed.
- **The TXT reader.** Cheap, and some Chinese novels only come as `.txt`.
- **EPUB**, of course: `lib/Epub` (491 KB), `expat`, the JPEG and PNG decoders.
- **The web server and its Files page**: book upload and the `/lexirise` settings page. Look at whether
  `jszip` (28 KB) is only used by a feature being cut.
- **USB drive mode, SD firmware update and OTA**: getting content on, and recovery.
- **StarDict** (`util/Dictionary*`): the offline fallback (D3).
- **WiFi, TLS, mDNS**: Lexirise needs them. `mbedcrypto` (67 KB) may be unused beside our wolfSSL. Check
  whether anything still links it once KOReader and OPDS are gone.

## 2. How to cut

- **Delete, don't `#if` out.** After M there's no upstream to stay mergeable with, so dead code goes, along with
  its settings keys, strings, tests and web routes. A cut feature must not leave a menu entry, a settings row,
  or a web page behind.
- **Settings files on existing SD cards:** when a removed setting's key is in `settings.bin` or `settings.json`,
  it must be ignored, not an error, and must not shift other settings. Check how `CrossPointSettings`
  serializes before deleting fields. If it's positional, keep a placeholder.
- **The `LEXIRISE` gate and `// LEXIPOINT:` markers (from M2):** once the base code is being deleted, the
  Lexirise-off build no longer means anything. Default: remove the gate (Lexirise is always on) and the
  Lexirise-off CI build, in the **last** step of this phase, so the gate still helps while features are being cut.
  The markers stay where they say *why* a base file was edited, and go where they only said "our code".
- **The global rule "don't modify `util/Dictionary*`"** exists for merging from upstream. It ends with M.
  StarDict is still kept.
- **Record every removal** in M's "Taken from CrossPoint" record (`../v0.1/firmware-base.md`, after M), so
  what was dropped, and at which commit, stays findable (C23).
- **Also keep** (C23): ruby and furigana, sleep and battery, and the dev harness.
- **One cut, one commit**, each building and passing the host suite on its own, so a bad cut is easy to
  revert.

## 3. Order

1. **Measure** (§1 again, on the current tree), and write the numbers into the ledger row.
2. **Code for devices without touch** (lowest risk; it's mostly already unreachable).
3. **Ecosystems:** KOReader sync, OPDS, Calibre, WebDAV.
4. **UI translations** down to English, then **hyphenation** down to `en`, then **MiniBidi**.
5. **Settings screens:** button remap, keyboard layouts, font download, the two extra themes.
6. **Fonts (C22)**, once claritise has chosen the font (§7): the SD CJK family becomes the only reader font,
   the built-in reader fonts go, and the font picker goes. The UI fonts stay.
7. **The `LEXIRISE` gate** (§2), last.
8. **Measure again**, and record the size and free heap before and after.

## 4. What users see

- The settings shrink to what matters: reading, Lexirise, WiFi, the display, the system. Walk every screen on
  the device and check that nothing still points at a removed feature.
- A book opened before the font change reflows (its layout cache is keyed by font). Expected, and a note for
  the release notes.
- `docs/user-guide.md`: remove anything that describes a removed feature. The release notes list what was
  removed, so nobody hunts for OPDS.

## 5. Gate

1. `pio run -e x4pro`, `x4pro-gh_release` and `x4pro-gh_release_rc` build with no new warnings. The host suite
   and the Python script tests pass. Tests of removed features are removed, not skipped, and the ledger
   records the count before and after.
2. **Size:** the release build's size before and after, per step (§3), in the ledger. The target is at least
   2 MB saved if C22 lands in this phase.
3. **Heap:** free internal heap and PSRAM on the Home screen and with a book open, before and after (the P0
   boot-log method). Nothing may get worse.
4. **On the device:** boot, open a Japanese and a Chinese EPUB and a TXT, look up and save a word, and open the
   settings and walk every screen. The web UI: upload a book, open `/lexirise`. An SD card whose settings
   were written by the previous build still boots with every surviving setting kept.
5. **The card** matches the design conformance gate (`../v0.1/01-build-order.md`), unchanged.
6. `git grep -n -i -e koreader -e opds -e calibre -e webdav` finds only history (ledger, dated notes)
   and the removal notes.
7. The key scan is clean.

## 6. After this

The freed space is spent on purpose. Candidates in the order v0.2 already has them: page annotations (A1–A11), the
on-device caches (C21), the manga device side (C18), then C11 reviews in v0.3. If some of the space isn't needed,
consider a smaller app partition with a larger SD-independent data partition (for example for a kanji pack). That
is a later decision, with claritise.

## 7. Needs claritise

| # | Question | Default until answered |
|---|---|---|
| S1 | **The one font (C22):** Noto Serif CJK (works today) or Noto Sans CJK? And for Chinese books, the JP build alone (Japanese letterforms) or both JP and SC? | Step 6 waits. Steps 1–5 and 7 don't need it |
| S2 | **Keep one small built-in Latin reader font** as a fallback when the SD card has no font? | No. The SD font is required. When it's missing, show a clear message instead of a fallback |
| S3 | **Keep the TXT reader?** | Keep |
