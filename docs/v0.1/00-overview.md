# Lexipoint v0.1: Overview, Decisions & Doc Index

> Master doc for the v0.1 doc set. Each workstream has its own spec in this directory. This doc
> is where they are reconciled: it holds the source of truth for cross-cutting **decisions**, the
> **corrections** to the original brief, and the **doc index**. The build order and its gates live
> in `01-build-order.md`.
>
> Written 2026-09-24 against CrossPoint `main` @ `0f01106` (v1.6.5, 2026-09-24).
>
> **Repo layout (D21, since phase M):** one repo, **`lexipoint`** (`github.com/claritise/lexipoint`).
> `docs/` is at the root, and the firmware is in **`firmware/`**, built on CrossPoint Reader tag **`1.6.5rc`**
> (`NOTICE`). Source paths in these docs (`src/…`, `lib/…`, `platformio.ini`) are **relative to `firmware/`**.

## Thesis

**Reading is where vocabulary is met, and Lexirise is where it is kept. The gap between them should
be one press.**

Today a Japanese (or Chinese) reader on an X4 Pro who meets 煩わしい has two options. They can look it up in an
offline StarDict dictionary that has no idea what they already know. Or they can put the device
down, open Lexirise on a phone, type the word and save it. Lexipoint closes that gap. Long-press
the word, and in one popup see its reading, its meaning, whether it is **already in your SRS** (and
at what level), and save it with one button, along with the sentence you met it in.

Two properties make this more than a dictionary swap:

1. **It knows you.** `analyze/text` returns per-word proficiency. The popup's first job is to say
   *"you're learning this, seen 8×"*, not just to define it.
2. **It keeps the context.** The saved card carries the sentence from the book. Sentence-mined
   cards are the reason people read in a target language in the first place.

## The loop

> **Long-press → see what it is and whether you know it → Save (or Back) → keep reading.**

The reader never leaves the page. The popup is drawn over the text. Save is one press, Back is one
press, and both return to exactly where you were. If there is no network, the loop still works: it
falls back to StarDict, and Save is queued (stretch goal, `offline-and-errors.md` §4).

## What the brief got wrong (verified 2026-09-24)

The brief (`context-brief.md`) predates CrossPoint 1.6.5. Each of the following was checked
against the source. **This section wins over the brief.**

| Brief says | Actually | Consequence |
|---|---|---|
| X4 Pro is an ESP32-C3, ~400KB SRAM | **ESP32-S3, 16MB flash, 8MB PSRAM** (`platformio.ini` `[env:x4pro]`, `board = esp32-s3-devkitc1-n16r8`, `-DBOARD_HAS_PSRAM`) | TLS OOM stops being "the biggest risk". The C3 constraints still bind the X4/X3, which are out of scope for v0.1 (D2) |
| CrossPoint already extracts sentence context | **No sentence extraction exists.** Word select works on page-level `WordBox`es (`DictionaryWordSelectActivity::extractWords`) | Sentence extraction is new work (`sentence-extraction.md`) |
| Long-press selects "a word" | Japanese text is split **one token per CJK character** (`ParsedText.cpp` `cjkCharacterBreakByteOffsets`). A tap selects a **character** | The word boundary has to come from Lexirise's tokenizer (D4). This fits `analyze/text` well |
| Long-press a word on the touchscreen opens the dictionary | Today: **Confirm** long-press (if the setting is "Dictionary") or menu → Look Up. That opens word select, then a **tap** looks up. `MappedInputManager::wasScreenLongPress` exists but the reader doesn't use it | A touch long-press entry point is new (`lookup-flow.md` §1) |
| Dictionary shows a popup | StarDict opens a **full-screen** `DictionaryDefinitionActivity`. Only "not found" and errors are popups | The Lexirise card is a new view (`popup-ui.md`) |
| ~~Frequency rank is in `analyze/text`'s `entryMetaById`~~ | *Retracted 2026-09-24: the live API **does** return `rank` and `frequencyScore` there.* The brief was right | Rank shows in phase A |
| Base on the ++ fork for its TLS fixes | Upstream already has TLS heap pre-flights (`HttpDownloader::MIN_TLS_FREE_HEAP`, `KOReaderSyncClient` `MIN_FREE_FOR_TLS`). They are aimed at C3 boards | D1: base on upstream. The ++ fork is a reference only |
| v1.6.0rc is current | Latest **release is `1.6.0`** (2026-09-05). There's a **`1.6.5rc`** tag (2026-09-14), and `develop` is working towards 1.6.5 (this survey read `develop` @ `0f01106`) | Base on tag `1.6.5rc` (D1) |
| Screen is 300 ppi. Input is touch + page buttons + Home | **800×480** panel (~217 ppi at 4.3"). Inputs: **touchscreen, one page button on each side edge (Left, Right), a capacitive Home pad, Power.** There's **no Confirm, no Back, and no up/down** (`freeink-sdk/docs/xteink-x4pro-support.md` "Input", confirmed on hardware). The SDK maps Left → previous page, Right → next page, and Home → back/exit | **The UI is touch-only** except for paging and Home (D15). Button-first flows (Confirm long-press to enter, a button legend on the card) don't exist on this device |
| A plugin | CrossPoint has **no plugin system**, and upstream `SCOPE.md` §3 **closes "new external network connectors"** | This is a **fork**, with no upstream PR path (D1) |

## Decisions

Status: **confirmed by claritise 2026-09-24** ("ok to all"), including D1, D9, D10 and D16–D17. Rows are
only changed by a new decision from claritise.

| # | Decision | Default |
|---|---|---|
| D1 | **Fork base** (confirmed; the fork was created 2026-09-24) *(the base stays history; the rebasing it describes ended with D21 in phase M: CrossPoint changes are taken one at a time, `firmware-base.md` §4)* | Fork **upstream `crosspoint-reader/crosspoint-reader`** (its default branch is **`develop`**; releases are tags), not the ++ fork. **Base: tag `1.6.5rc`** (2026-09-14, the newest tag, a release candidate), then rebase onto `1.6.5` when it ships. Upstream is where X4 Pro support lands first. The ++ TLS fixes target C3 heap limits, which the S3 + PSRAM doesn't have. Keep our changes in as few files as possible (a `src/lexirise/` directory plus small hooks) so rebasing onto upstream stays cheap. Rebase monthly, or when an upstream release touches `activities/reader/Dictionary*` or `network/` (`firmware-base.md`) |
| D2 | **Target** *(superseded by D20 in phase M: devices without touch, the X4 Classic included, are out for good)* | **`env:x4pro` only.** X4 Classic (`env:x4c`, S3, no touch, more buttons) is a later target. It needs its own button-only input path, since the X4 Pro design is touch-first (D15). X4/X3 (C3) are **out of scope**: their TLS budget is a separate project |
| D3 | **Lexirise sits beside StarDict, not in place of it** | A `LookupProvider` seam inside the existing word-select flow. The **Lexirise** provider runs first, and **StarDict** is the fallback (`lookup-flow.md` §4). StarDict code stays unmodified, so upstream dictionary fixes keep merging cleanly |
| D4 | **The word comes from the server** | The device sends the **sentence plus the tapped character's offset**. The word is the `occurrences[]` entry whose `[charStart, charEnd)` contains that offset. The device never segments Japanese itself (no MeCab or deinflection on-device). After the response, the highlight grows from the character to the whole word |
| D5 | **Sentence = page-bounded, base text only** | Built from the current page's `TextBlock`s. It is cut at `。！？!?` and at a paragraph end, keeps closing brackets (`」』）`), is **capped at 120 UTF-16 units** around the tap, and **excludes ruby text**. It doesn't cross a page boundary in v0.1 (`sentence-extraction.md`) |
| D6 | **Two sequential requests, one connection** | `analyze/text` (state, reading, POS, word boundary), then `dictionary/lookup` on the **lemma** (translation, rank). They go over **one keep-alive TLS session** to `api.lexirise.app`, so there is one handshake per lookup. The card renders after the first response and fills in after the second. No parallel requests |
| D7 | **HTTP stack: our own verified wolfSSL client** (revised in P1) | CrossPoint's X4 Pro build uses wolfSSL, so `esp_http_client` + the CA bundle is compiled out, and the SDK's `SecureClient` never verifies (every caller uses `setInsecure()`). We send a bearer key, so `src/lexirise/net/TlsConnection` pins ISRG Root X1 + X2, verifies the peer and the hostname, and sends SNI. The api.lexirise.app chain is ECDSA P-384/SHA-384 end to end, so the `[lexirise]` build section adds those to wolfSSL (~30KB flash). Bodies are buffered (≤64KB) and read by a strict JSON reader (`lexirise-client.md` §1, §4) |
| D8 | **Config on SD** (the file is now written by the settings panel, D18) | `/.lexirise/config.ini`: `api_key=lx_…`, `default_language=ja`, `languages=ja,zh`, `tags=xteink`, `enabled=1`. It is read once at boot into a small store (the `KOReaderCredentialStore` pattern). **The key is never logged, rendered or included in a crash report.** *Superseded in part by D18: the key is entered from the settings panel (pasted on the phone web page, or typed on the device keyboard).* |
| D9 | **Save payload** (confirmed: the lemma, with the book's form kept in `notes`) | `text` = the occurrence's **lemma** (辞書形), not the inflected surface form. `translation` = the first `translations[].translation` from `dictionary/lookup`. `proficiency: 1`, `tags` from config, `mode: "word"`, `notes` = the sentence. `notes` is a documented field. The endpoint is an **upsert** ("creates or updates"), so there's no duplicate error, and an already-saved word is never re-posted (its tags or notes could be overwritten). Whether to save the lemma or the surface form is claritise's call |
| D10 | **WiFi lifecycle** (confirmed: 5 min idle timeout) | Bring WiFi up **on the first lookup of a reading session**, then keep it up with an **idle timeout (default 5 min)** after the last Lexirise call, then `esp_wifi_stop()`. Cold-connect cost (~2–4 s) is paid once per burst of lookups, not per word. This trades battery for latency, so it is claritise's call |
| D11 | **Timeouts** | Connect + TLS: 6 s. Each request: 6 s. If it is over budget, **fall back to StarDict** (D3) and show a small "offline" mark on the card. We don't retry automatically |
| D12 | **Card style and input: ⛔ BINDING, PIXEL-PERFECT** (approved 2026-09-24, claritise) | **Build exactly `reference/card-reference.html`, to the device-pixel measurements in `popup-ui.md` §1.1. No restyling. Deviations need claritise's sign-off first.** The clean framed card: small T L F K top right, and the rank row as the bottom row, with no button legend (`popup-ui.md` §1). **Tap T/L/F/K saves at that level** (with Undo). The whole **rank row** is the toggle for the detail view, marked only by an arrow (`▼` / `▲`, no text), with **✕** at its end to close. Tapping the page also closes. The detail view is the same card, taller, with tabs just above the rank row. X4 Pro inputs are in D15 |
| D13 | **Proficiency labels** | 0 `unknown` · 1 `tracked` · 2 `learning` · 3 `fresh` · 4 `known`. New saves are 1 |
| D15 | **X4 Pro inputs first** (2026-09-24, claritise) | Design for **touch + the two side page buttons + the Home pad**. Entry is a **touch long-press** on a word (built in P3, not P7). On the card: every action is a tap; **Home closes** the card (as it exits anywhere on Home-key boards); the **side buttons step to the previous/next word** in the sentence. No button legend on screen. Button-rich boards (X4 Classic) are a later target, with their own input table *(that last part superseded by D20: devices without touch are out)* |
| D16 | **Stepping past the end of a sentence** (2026-09-24, claritise) | v0.1: the side buttons **stop at the sentence's first and last word** (only that sentence is analyzed). Once page analysis exists (v0.2 C12), they **carry on into the next and previous sentence on the page**, stopping at the page's first and last word. They never turn the page while the card is open |
| D17 | **Tapped word under the card** (approved 2026-09-24, claritise, **in the binding reference**; the strip text clip, which also applies to the expanded view's strip, was approved the same day) | The page never moves. If the card would cover the active word, the card gains **the context strip as its top row**: the same line-of-the-page with the word inverted that the expanded view uses, clipped before the `line n/m` marker and scrolled so the word is always visible. The translation stays. It's re-evaluated on every step: the strip appears and disappears as the active word moves under or out from under the card (`popup-ui.md` §0, §1.1) |
| D18 | **A Lexirise settings panel** (2026-09-24, claritise) | One `LexiriseSettings` store owns `/.lexirise/config.ini` (atomic writes; it replaces `state.ini`). It's surfaced **the way CrossPoint does KOReader sync**: Settings → System → **Lexirise** on the device (stock list UI), and **its own `Lexirise` page in the web UI** (next to Files, Fonts and Settings, served by the same web server used to upload books), where the **API key is pasted from a browser**. The key is always masked, and never served over the LAN or listed in the file browser. Full list: `settings.md` |
| D19 | **Repo layout** (2026-09-24, claritise: docs at the root, fork in `~/Projects`) *(superseded by D21 in phase M: one repo, the firmware in `firmware/`)* | Two sibling repos. `lexipoint` = project home with `docs/` at the root. `crosspoint-reader` = the firmware fork (`github.com/claritise/crosspoint-reader`, branch `lexipoint`), cloned at `~/Projects/crosspoint-reader`. There's no submodule. Docs refer to firmware paths relative to that repo. Build commands run there |
| D14 | **Simplified Chinese is a v0.1 language** | `zh` next to `ja`. The language is chosen **per book** from EPUB `dc:language`, then a kana check on the sentence, then `default_language`. Pinyin with tone marks as the reading. A StarDict fallback folder per language. Traditional waits on H8 (`languages.md`) |
| D20 | **Device scope: the X4 Pro only, touch always** (2026-09-25, claritise) | The **Xteink X4 Pro** is the only primary target: the only device built, tested and released. Other FreeInk devices with touchscreens (`FREEINK_CAP_TOUCH`) **may** be supported later. Devices without touch never will be. The build fails without touch (`#error`). Built in phase M (`standalone-repo.md` §1) |
| D21 | **One standalone repo, not a fork** (2026-09-25, claritise) | `claritise/lexipoint` holds `firmware/` (the fork's history, kept), `sd-card/` and `docs/`. CrossPoint Reader (MIT) is the code Lexipoint started from (`a1ceb633`, tag `1.6.5rc`; `24516d4c` here), credited in `NOTICE`. There are no upstream syncs or rebases. A later upstream change is a recorded one-off cherry-pick. The old fork gets archived. Built in phase M (`standalone-repo.md`) |
| D22 | **Rebrand: Lexipoint for the X4 Pro** (2026-09-25, claritise) | The docs, the README, release assets (`lexipoint-<tag>-x4pro.bin`) and what users see (boot and sleep screens) say **Lexipoint**. CrossPoint is named only as the base (credits, and where a doc explains base code). Fork, upstream and rebase framing goes, except in recorded history. Built in phase M (`standalone-repo.md` §5) |

## Needs-human (never guess)

| # | Question | Blocks |
|---|---|---|
| ~~H1~~ | ~~Fork base~~ **Resolved: upstream `main` (D1)** | — |
| ~~H2~~ | ~~Lemma or surface form?~~ **Resolved: the lemma, with the surface form in `notes` (D9)** | — |
| ~~H3~~ | ~~Idle timeout or per-lookup WiFi?~~ **Resolved: 5 min idle timeout (D10). Each API call takes ~1 s, so reconnecting per lookup would add 2–4 s** | — |
| ~~H4~~ | ~~Translation target language~~ **Resolved 2026-09-24: `translation_target: "en"`, set on the account. The request has no target field** | — |
| ~~H5~~ | ~~The unit of `charStart`/`charEnd`?~~ **Resolved 2026-09-24 (live API): UTF-16 code units** | — |
| ~~H6~~ | ~~Does `/v1/vocabulary` accept `notes`?~~ **Resolved 2026-09-24 from the API reference: yes** (`../reference/lexirise-api-notes.md`) | — |
| ~~H7~~ | ~~Chinese code and pinyin format~~ **Resolved 2026-09-24: `zh`, and pinyin comes with tone marks (`xué xí`)** | — |
| H8 | **Parked, Simplified first (claritise, 2026-09-24).** Tested: Traditional text sent as `zh` is understood and mapped to Simplified entries, but shown in Simplified. `zh-Hant` books use StarDict only until this is picked up | — |
| ~~H9~~ | ~~Is the JLPT/HSK level exposed?~~ **Resolved 2026-09-24: yes, `dictionary/lookup` `system_tags` (`JLPT-N3`, `HSK-4`, `HSK-7+`), using HSK 3.0** | — |
| ~~H10~~ | ~~Japanese readings come back in romaji~~ **Resolved 2026-09-24 (claritise): convert on the device.** Lexirise romanizes native words losslessly (long vowels spelled out: `toukyou`, `ookii`, and `'` after ん: `kin'youbi`), so a table-driven converter gives exact hiragana. Katakana words (macrons: `kōhī`) show their own surface form. The only errors are Lexirise's own bad readings (一緒 → `ichiitoguchi`), which get reported to them. See `languages.md` §3a. A Lexirise kana field would still be welcome, but it no longer blocks anything | — |
| H11 | **License for Lexipoint's own code** (the repo root has none) | Nothing in M (default: no root `LICENSE`; `standalone-repo.md` §9) |
| H12 | **Version scheme:** keep `<base>-lexi.<n>` (e.g. `1.6.5-lexi.1`, the CrossPoint version it's built on), or switch to Lexipoint's own before the first release? | The first release, if switching (default: keep; `standalone-repo.md` §9) |
| H13 | **Wording that replaces "Same as CrossPoint"** in settings | claritise's sign-off on that string. M built the default: the offline dictionary's default option reads "Same as reader", pending that sign-off |

## Doc index

| Piece | Doc |
|---|---|
| The brief as received, with corrections noted | `context-brief.md` |
| Firmware base: what Lexipoint is built on and where our code lives (layout, build envs, hooks in base files, releases) | `firmware-base.md` |
| **Phase M:** one standalone repo, the X4 Pro only, the rebrand | `standalone-repo.md` |
| The fork's commit SHAs (in ledger rows before M) and the same commits here | `../reference/firmware-commit-map.md` |
| Long-press → tapped character → sentence → word → card, and the provider seam | `lookup-flow.md` |
| **⛔ The approved card design (binding, pixel-perfect)** | **`reference/card-reference.html`** + `popup-ui.md` §1.1 |
| Building the sentence and the tap offset from a rendered page | `sentence-extraction.md` |
| Japanese + Simplified Chinese: per-book language, punctuation, pinyin, fallback dictionaries | `languages.md` |
| HTTPS, the three endpoints, streaming parse, memory, config, the key | `lexirise-client.md` |
| The Lexirise settings panel (device + phone web page), the config file, the API key | `settings.md` |
| What the Lexirise API reference actually says (read 2026-09-24) | `../reference/lexirise-api-notes.md` |
| The card: layout, progressive fill, buttons, touch, e-ink refresh | `popup-ui.md` |
| No WiFi, timeouts, 401/429/5xx, pending translations, StarDict fallback, the save queue | `offline-and-errors.md` |
| **The order to build it in, with gates** | `01-build-order.md` |

## Stretch goals (not v0.1)

> **2026-09-24: the triaged post-v0.1 backlog now lives in `../v0.2/00-overview.md` (C1–C9).** It
> supersedes the list below, which is kept for the record.

Carried from the brief, each with a note on what it would need:

- **Proficiency tinting in the reader** (known words dimmed, unknown words underlined). This needs
  a per-page `analyze/text` pass on page turn and a renderer hook. It's the biggest feature here
  and the first real candidate for v0.2. It has to be designed around the 1200 req/h budget
  (~1 request per page is fine for reading, but not for fast page flipping).
- **Sentence save** (`mode: "sentence"`). The sentence is already built (D5), so this is mostly UI.
- **Offline save queue.** An append-only file on SD, flushed when WiFi next comes up
  (`offline-and-errors.md` §4).
- **Book-title tag.** `EpubReaderActivity` knows the title. Slug it into `tags`.
- **X4 Classic.** Buttons only, same S3. It's nearly free once the button path works (D2). *(Ruled out by D20.)*
