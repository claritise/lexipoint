# Slimming: Lexipoint as CJK-learning firmware

**Status:** approved by claritise 2026-09-25 ("yes to all"), for v0.2. ~~Not started.~~ Building as phase V8 on
`lexi/V8` (2026-09-29): what each step removed and saved is in "As built (V8)" (§8). It runs **after phase M**
(`../v0.1/standalone-repo.md`), because deleting base code is only cheap once there are no rebases to keep
working. This doc also absorbs M's planned follow-up cleanup, "M2" (`standalone-repo.md` §6), and C22 "One font
for everything" (`00-overview.md`).

Written for the agent that builds it. Firmware paths (`lib/…`, `src/…`) are relative to `firmware/`. Defaults
here apply unless claritise says otherwise. The items under "Needs claritise" (§7) are never guessed.

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
| **Hyphenation** tries: de 201, ru 33, en 26, sv 23, uk 21, pl 15, es 13, fr 7, it, fi | ≈ 342 KB | **Cut all but `en`** (≈ 316 KB). CJK text doesn't hyphenate. English keeps its trie ~~for the English in bilingual books~~ for books tagged English (the hyphenator is picked by the book's language: corrected 2026-09-29, V8 R1) |
| **UI translations** (`lib/I18n`, 33 languages + English) | ≈ 368 KB | **Cut to English.** Lexipoint's own strings are English only |
| **KOReader sync** (`lib/KOReaderSync`, its activities and settings) | ≈ 35 KB + screens | **Cut** |
| **OPDS** (browser activity, `lib/OpdsParser`, server list and settings) | ≈ 23 KB + parser + screens | **Cut** |
| **Calibre connect** | small | **Cut.** Its instruction string goes with it |
| **WebDAV** (`WebDAVHandler`) | small | **Cut.** The web upload page covers getting books on |
| **Font download** (`FontDownloadActivity`), and the font picker once C22 lands | ≈ 19 KB + picker | **Cut** |
| **Button remap, keyboard layouts** | small | **Cut** (touch-first; keep one on-screen keyboard layout, for WiFi passwords) |
| **Themes** Lyra and RoundedRaff | small | **Cut.** ~~Keep the base theme, the one the card matches~~ **Superseded 2026-09-29 (§7 S4, claritise: "Keep Lyra"):** keep Lyra; cut Classic, Lyra 3 Covers and RoundedRaff. The card draws itself and matches neither |
| **Right-to-left text** (`lib/MiniBidi`) | small | **Cut** (only Arabic, Hebrew and Persian needed it) |
| **Code for devices without touch**: the X3/X4 key maps in `MappedInputManager`, the button legend, C3-only paths, `FirmwareBoardTag` entries for other boards | small | **Cut** (was M2; D20) |

**Rough total: 2.5–2.7 MB, close to half the firmware**, most of it the built-in reader fonts. The per-row sizes
are exact. The total is an estimate, because removing a feature also removes code only it used, which the map
can't attribute ahead of time.

**Measured again at V8's start** (step 1, 2026-09-29, `x4pro-gh_release` at `main` `05328117`; per object file
from `firmware.map`, fonts and tries per symbol from `nm -S`). The release image is **5,709,742 B of the 6,553,600 B
app slot (87.1%)**, static RAM (`.data` + `.bss`) 102,104 B.

| What | Flash | Notes |
|---|---|---|
| Built-in reader fonts (Noto Serif and Noto Sans, 12–18 pt, four styles) | 1,875,551 B | Step 6 (waits for S1) |
| Built-in UI fonts: Ubuntu 10/12, Noto Sans 8 | 275,196 B | Kept |
| Hyphenation (`LanguageRegistry.cpp.o`, the tries) | 351,208 B | `en`'s trie is 26,943 B of it; the Liang code is ~7 KB more |
| UI translations (`I18nStrings.cpp.o`) | 376,499 B | |
| KOReader sync (library and screens) | 51,466 B | |
| OPDS (browser, parser, server list and settings) | 38,688 B | |
| WebDAV | 10,053 B | |
| Calibre connect | 3,054 B | |
| Font download and installer (with the web Fonts page) | 21,052 B | |
| Themes Lyra, Lyra 3 Covers and RoundedRaff | 8,313 B | |
| MiniBidi | 7,839 B | |
| Button remap, keyboard layouts | 2,457 B, 2,189 B | |
| `jszip` | 28,379 B | **Kept:** the Files page's EPUB image picker uses it, not a cut feature |
| `mbedcrypto` | 69,132 B | **Kept:** the WiFi supplicant (`libwpa_supplicant`) links it, not KOReader or OPDS |

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

- **Delete, don't `#if` out.** Since M, Lexipoint doesn't merge from CrossPoint (D21), so dead code goes, along with
  its settings keys, strings, tests and web routes. A cut feature must not leave a menu entry, a settings row,
  or a web page behind.
- **Settings files on existing SD cards:** when a removed setting's key is in `settings.bin` or `settings.json`,
  it must be ignored, not an error, and must not shift other settings. Check how `CrossPointSettings`
  serializes before deleting fields. If it's positional, keep a placeholder.
- **The `LEXIRISE` gate and `// LEXIPOINT:` markers (from M2):** once the base code is being deleted, the
  Lexirise-off build no longer means anything. Default: remove the gate (Lexirise is always on) and the
  Lexirise-off build (the `x4pro-lexirise-off` env in `firmware/platformio.ini`, and its line in the uniform gate), in the
  **last** step of this phase, so the gate still helps while features are being cut.
  The markers stay where they say *why* a base file was edited, and go where they only said "our code".
- **The global rule "don't modify `util/Dictionary*`"** existed for merging from CrossPoint. M kept it
  (`../v0.1/standalone-repo.md` §6), and this phase ends it. StarDict is still kept.
- **Record every removal** in the "Taken from CrossPoint" record M set up (`../v0.1/firmware-base.md`), so
  what was dropped, and at which commit, stays findable (C23).
- **cppcheck's style hints in Lexipoint's code (from M):** M moved cppcheck to `x4pro`, which checked
  `src/lexirise/` for the first time, and suppressed three style-only checks there (`useStlAlgorithm`,
  `shadowFunction`, `variableScope`; `firmware/platformio.ini` `check_flags`). Rewrite those places (27 raw
  loops, a few names and one scope) and drop the suppressions, so Lexipoint's code meets the base's bar.
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

Run from `firmware/` (`cd firmware` first).

1. `pio run -e x4pro`, `x4pro-gh_release` and `x4pro-gh_release_rc` build with no new warnings, and so does
   `x4pro-lexirise-off` until step 7 removes it. The host suite and the Python script tests pass.
   Tests of removed features are removed, not skipped, and the ledger records the count before and after.
2. **Size:** the release build's size before and after, per step (§3), in the ledger. The target is at least
   2 MB saved if C22 lands in this phase.
3. **Heap:** free internal heap and PSRAM on the Home screen and with a book open, before and after (the P0
   boot-log method). Nothing may get worse.
4. **On the device:** boot, open a Japanese and a Chinese EPUB and a TXT, look up and save a word, and open the
   settings and walk every screen. The web UI: upload a book, open `/lexirise`. An SD card whose settings
   were written by the previous build still boots with every surviving setting kept.
5. **The card** matches the design conformance gate (`../v0.1/01-build-order.md`), unchanged.
6. `git grep -n -i -e koreader -e opds -e calibre -e webdav -- :/` finds only history (ledger, dated notes
   in `docs/`) and the removal notes.
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
| S4 | **Which theme stays** (added 2026-09-29 by V8's step 5c): §1 says "keep the base theme, the one the card matches", which in the code is Classic (`BaseTheme`); but Lyra is the default every device shows today, and V7b's Sync Vocabulary row and popup were signed off (2026-09-28) as drawn in Lyra (`reference/v7b-home-sync.html`). Cutting Lyra changes every list and the home screen for everyone. Keep Classic (cut Lyra, Lyra 3 Covers, RoundedRaff), or keep Lyra (cut the others)? | ~~Step 5c waits~~ **Answered 2026-09-29, claritise: "Keep Lyra"**: Lyra stays; Classic (as a choice: `BaseTheme` stays, Lyra is built on it), Lyra 3 Covers and RoundedRaff go (step 5c) |

## 8. As built (V8)

Built on `lexi/V8` from `main` `05328117` (2026-09-29), one commit per step (§3). Sizes are the `x4pro-gh_release`
image (`pio run`'s "Flash" line, the app slot is 6,553,600 B) and its static RAM (`.data` + `.bss`, "RAM"); the
free heap on the device is owed (`../v0.1/device-checks.md`, "v0.2 V8").

| Step | Flash (B) | Saved (B) | Static RAM (B) | Saved (B) |
|---|---|---|---|---|
| 1. Measured at the start | 5,709,742 | — | 102,104 | — |
| 2. Code for devices without touch | 5,692,910 | 16,832 | 102,064 | 40 |
| 3. Ecosystems: KOReader sync, OPDS, Calibre, WebDAV | 5,535,622 | 157,288 | 101,552 | 512 |
| 4a. UI translations down to English (and the keyboard layouts) | 5,245,190 | 290,432 | 101,552 | 0 |
| 4b. Hyphenation down to `en` | 4,921,006 | 324,184 | 101,376 | 176 |
| 4c. Right-to-left text (MiniBidi) | 4,909,590 | 11,416 | 98,296 | 3,080 |
| 5a. Button remap | 4,906,050 | 3,540 | 98,288 | 8 |
| 5b. Font download | 4,886,258 | 19,792 | 98,288 | 0 |
| V7c's carried nits (not a slimming step) | 4,886,646 | −388 | 98,288 | 0 |
| 7. The `LEXIRISE` gate and the cppcheck suppressions | 4,890,254 | −3,608 | 98,288 | 0 |
| 5c. The themes but Lyra (after step 7: it waited for S4) | 4,882,238 | 8,016 | 98,280 | 8 |
| R1 (the review's fixes: the extra-text parameter, the hyphenation language map) | 4,881,974 | 264 | 98,280 | 0 |
| R2 (the review's fixes: the fast-refresh parameter, the tab bar's other look, the keyboard's hit table) | 4,881,866 | 108 | 98,272 | 8 |
| R3+R4 (the review's fixes: the lemma cache's account per answer, the leftover pins and language API, tests) | 4,882,294 | −428 | 98,272 | 0 |
| R5 (the review's fixes: an rtl paragraph's layout, the C3 build leftovers) | 4,882,066 | 228 | 98,272 | 0 |
| R6+R7 (the review's fixes: the X3 profile and OTA suffix, the hint band, tests) | 4,882,214 | −148 | 98,272 | 0 |
| R8 (the review's fixes: the themes' hint band, the Files page's target, one string table) | 4,881,990 | 224 | 97,992 | 280 |
| R9 (the review's fixes: tests, the network-mode rows, the updater's refusal) | 4,882,010 | −20 | 97,992 | 0 |
| R10 (the review's fixes: dead code the gate's removal left, the board's constant conditions) | 4,882,006 | 4 | 97,992 | 0 |

**Step 2** removed the branches for other boards (build-time flags and run-time board checks), the button legend, the tilt page turn
and three settings only button boards showed; what was removed and what was kept is in `../v0.1/firmware-base.md` §4
"Removed from the base (v0.2 V8)". A settings file from before V8 still loads: `test/settings_upgrade` loads one
holding every key the file carried (fixture `settings-before-v8.json`, no value the default) through the real
`CrossPointSettings::fromJson` and checks every kept setting keeps its value, every key is either read or listed as
removed, and the next save drops only the removed keys (`kRemovedKeys`, one entry per removed setting).

**Step 3** removed KOReader sync, OPDS, Calibre connect (with the web server's UDP discovery reply, which only the
Calibre plugin asked for) and WebDAV, and every string that only they (or step 2's legend) used, in all 34 languages
(about half of the step's size). What stayed and why: `../v0.1/firmware-base.md` §4. A stored Long-press Menu of
KOReader Sync loads as Disabled and the file is rewritten (`lexipoint::long_press_menu`, tested); the two OPDS keys are
in `test/settings_upgrade`'s removed list. `websmoke.py` now checks that nothing lists a folder over PROPFIND or
serves a file by its path, instead of WebDAV's own refusals. `git grep -i -e koreader -e opds -e calibre -e webdav`
(§5 gate 6) finds, besides history and these notes: removal notes in the code and the web docs, the retired
long-press value (`LP_MENU_KOSYNC`, `kKoSync`), the removed keys `test/settings_upgrade` feeds an old file, Calibre as
a program that makes EPUBs (`lib/Epub`, `lib/LibraryIndex`, the Files page's `calibre:cover` check,
`scripts/generate_br_section_break_epub.py`), and the dictionary list's credit (`firmware/docs/dictionary.md`). (The
keyboard's `/opds` snippet key went with its URL mode in 4a.)

**Step 4** is three commits, one per cut (§2), each measured apart. **4a** cut the UI to English. The keyboard layouts
(listed in step 5) went with it: their table is keyed by the UI languages, so it couldn't outlive them, and the
keyboard's URL mode went too (nothing opens it since step 3). A settings file's `language` and `keyboardLayouts` are
ignored (`test/settings_upgrade`): a reader whose UI was in another language sees English after the upgrade.
**4b** kept only English's hyphenation: with Hyphenation on, a book in another European language no longer
hyphenates (its lines wrap at spaces).
**4c** removed MiniBidi: Hebrew, Arabic and Persian text now draws in logical order, unshaped. ~~LTR and CJK layout
is untouched (the parser, layout and page-model host suites pass unchanged).~~ (corrected 2026-09-29, R5 below)
LTR and CJK layout is untouched except in a paragraph whose direction is rtl (`dir="rtl"`, CSS `direction: rtl`):
4c laid its words out right to left until R5 put them back left to right, at the right margin. The built-in UI fonts still carry the
Arabic presentation forms and the Cyrillic the translations needed: trimming them is a font change, left for C22.

**Step 5** is three commits too (the keyboard layouts went in 4a). **5a** removed the front-button remap, which
only button boards offered: a settings file's `frontButton*` keys are ignored.
**5b** removed the device's font download (Manage Fonts): a font family is added over the web Fonts page or by
copying it to the SD card (`firmware/docs/sd-card-fonts.md`). The font picker stays until C22 (S1).

~~**Step 5c, the themes, is not done** (2026-09-29): it waits for claritise (§7 S4).~~ **Step 5c** (done 2026-09-29
after S4's answer, "Keep Lyra"): Lyra is the one theme, set once by `UITheme` (no setting, no reload); Classic as a
choice, Lyra 3 Covers and RoundedRaff went with the UI Theme row (the device and web settings) and its strings. A
settings file's `uiTheme`, whatever it held, is ignored and dropped on the next save (`test/settings_upgrade`
`SettingsUpgradeThemes`): a reader who had picked another theme sees Lyra. The card draws itself, so its goldens are
unchanged. **Step 7** removed the `LEXIRISE`
gate last, as §2 says (after the nits, since 5c waits): every `#if LEXIRISE` resolved to its Lexirise half, the
Lexirise-off env and its gate line gone, and the bare markers with them. Its size is the cppcheck rewrites': the
gate itself changes nothing in the release build (it was always on there), and `src/lexirise/` now passes the base's
cppcheck bar ~~with no suppressions~~ (corrected 2026-09-29, R3+R4 below) without its path-wide suppressions
(`useStlAlgorithm`, `shadowFunction`, `variableScope`; its inline ones for false positives predate V8, and R3+R4
added one, below), at the cost of the algorithm templates the rewrites instantiate (the table's
growth). `test_lxctl` `LexiriseIsAlwaysBuilt` checks every env carries `[lexirise]` and no source is gated.

**R1 (2026-09-29):** the section cache's format went to 47 (`firmware/docs/file-formats.md`): 4c changed how
right-to-left text is measured and placed, and 4b which books hyphenate, so a book laid out before V8 is laid out
again on its first open (CrossPoint bumped it for the same reason when shaping came in, v30). Leftover files of the
removed features stay on the card (`/.crosspoint/koreader.json`, `/.crosspoint/opds.json`: nothing deletes them;
the user guide says what they are). The review also removed CrossPoint's bricked-device guide
(`firmware/docs/fix-bricked-xteink.md` and `firmware/docs/images/spiflash/`, its photos and a 7.4 MB flash backup):
it recovers an ESP32-C3 Xteink with an SPI flash programmer, and its backup image is a C3's. The X4 Pro is flashed and
recovered over USB (`../user-guide.md` §1), by the SD-card firmware update, or by the recovery boot (Down + Power).

**R2 (2026-09-29):** the TXT reader's page index went to version 4 (its line breaks were measured on shaped
widths); the reader's unused "fast first refresh" parameter (only an X3 wake used it) went from the reader
activities; the USB handoff before a restart is no longer behind `FREEINK_CAP_USB_MSC`; the keyboard's hit table is
sized for the English layout (`kKeyboardInteractions`); the tab bar keeps only Lyra's look. The lemma cache's format
2 is described where the cache is (`../v0.1/settings.md`).

**R3+R4 (2026-09-29):** the table's sizes were measured again from clean builds in `~/Projects/lexipoint` and R2's
row stands (the reviewers' builds, in their worktrees, came out under 1 KB larger). A lemma-cache answer keeps
the API key's tag it was fetched under: one fetched before the key changed is dropped, not written as the new
account's (`../v0.1/settings.md`, tested on a card). The keyboard's layers and hit table moved to
`KeyboardLayers.h`, and `test/keyboard_layers` fails if an SDK layout outgrows the table. `languageSlot` is the one
index into per-language arrays (the lemma cache's folders, the vocabulary mirror's slots). Removed: `I18n`'s language
API and the tables `gen_i18n.py` made for it, the C3's display pin macros, and the empty branches and comments the
legend left (`../v0.1/firmware-base.md` §4); USB Drive's build check is a `static_assert` again. Sync Vocabulary's
once-per-entry count is a plain loop with an inline `useStlAlgorithm` suppression (the `copy_if` it replaces read
the list it was filling). New tests pin the rewrites the review's mutants passed (the key's characters, the book
languages file's byte cap, a saved word twice in the next sentence, the card's latest ignore). Two can't be pinned:
`decks.ini`'s and `book-tags.ini`'s byte caps are never reached before their count caps (a `static_assert` each
says so), and `conjugationOf`'s check of the dictionary form itself never decides alone (probed over the tests'
verbs and adjectives: kept as a backstop, commented).

**R5 (2026-09-29):** 4c had left a paragraph whose direction is rtl (`dir="rtl"`, CSS `direction: rtl`, which the
parser still reads) on CrossPoint's right-to-left placement, which MiniBidi only used for a line it didn't reorder:
every line of English, Japanese or Chinese in such a paragraph came out with its words reversed. Its words now run
left to right, placed as MiniBidi placed a line with no Hebrew or Arabic (at the right margin by default and for
Justify, centred, or at the left for an explicit `text-align: left`); tests in `test/chapter_html_slim_parser`.
The section format stays 47: 47 was never released, so no device has this layout cached. With a touch controller
that failed to start, the side buttons again don't follow the screen's orientation (as before V8). Removed: the C3
board line and link hook in `platformio.ini`, and `update_hyphenation.sh`'s other languages
(`../v0.1/firmware-base.md` §4). The user guide has a recovery note (§1); `test/settings_upgrade` starts each case
from the defaults and loads an out-of-range choice.

**R6+R7 (2026-09-29):** the lemma cache takes the API key's tag before the lookup's call, and a card's own pending
answer counts only for that key. An rtl paragraph keeps a first-line indent at the right end, as MiniBidi did
(tests for an explicit left, an indent and ruby). The Files page lost the X3 profile and its Target Device row (the
X4 Pro's 480×800 is the one), and the updater names its asset in `lexirise/ota/ReleaseAsset.h`, pinned against
`publish_release.py`. `websmoke.py` checks the settings API lists and applies no removed setting and OPDS's API is
gone. The band kept for a button legend is 0 whatever the touch controller does. The keyboard's cursor hit is a
pure function with host tests. Not taken: caching the account's tag in the lemma cache (a settings copy per read,
beside an SD read, and the store's revision isn't bumped by its first load).

**R8 (2026-09-29):** the themes' tables say `buttonHintsHeight` 0, so `UITheme` hands them out as they are (its
run-time copy, read by both tasks, is gone: the RAM saved) and the safe area is the whole screen. The Files page
keeps no device target at all (a saved one is ignored). `I18n` reads English's table directly (`firmware/docs/i18n.md`).
A card's pending answer from before a key change is tested not to be reused.

**R9 (2026-09-29):** a card's answer under the current key is tested to be written and read back as a hit (the
cache fills on the device). `gen_i18n.py` refuses any language file but `english.yaml` and builds only English's
table. A release tag too long for the updater's asset name now ends as no update. The File Transfer choices are one
table of rows (the mode, its label, text and icon), not three arrays read by index. `test/settings_upgrade` checks
every kept key of the old file by name.

**R10 (2026-09-29):** the word-select lookup lost an unreachable StarDict call the gate's removal left after its
`return`. A scan of every file step 7 touched found no other code after an unconditional return and no constant
condition, but four always-true board conditions from before V8 went: the RISC-V panic frame (`__riscv`, now a
build error), the low-power clock and two memory logs without PSRAM (`BOARD_HAS_PSRAM`, now required). The parser's
asset-name buffer is tied to the updater's by a `static_assert`, and File Transfer's order is one host-tested list
(`NetworkModes.h`).

**Later cleanups (not V8's):**
- `buttonHintsHeight` is 0 in the one theme but still appears in layout arithmetic across the screens: folding it
  out is a cleanup of its own, with no size or behaviour to gain.
- The upgrade's one-time costs stay as recorded: the first open of a book cached before V8 lays it out again
  (section format 47, R1), a TXT is indexed again (version 4, R2), and a lemma bucket from before V8 is removed and
  asked again (format 2); `../v0.1/device-checks.md` "v0.2 V8" times them.
