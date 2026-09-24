# v0.1 Build Order: the executable plan

> **This document is a program for a builder, human or agent.** It breaks v0.1 into linearly
> ordered, independently verifiable phases. Each phase states its goal, what to read, what to
> build, and a **gate**: the checks that must pass before the next phase starts. The design lives in
> the per-workstream specs. This doc only sequences and gates. Where it disagrees with a spec on
> *what* to build, the spec wins. Where they disagree on *order*, this doc wins.
> `00-overview.md` is the decision log. (Written 2026-09-24.)

## How to run this document

For each phase, in order:

1. **Read the ledger** (bottom). Find the first phase not `done`. If the one before it isn't
   `done`, stop and fix or report it.
2. **Read the listed specs** in full, then the CrossPoint code the phase touches.
3. **Build** in `~/Projects/crosspoint-reader` on a branch: `git checkout -B lexi/<phase-id> lexipoint`.
4. **Run the gate** (the uniform gate plus the phase's own list). If it still fails after real fix
   attempts, mark the phase `blocked` in the ledger with a precise diagnosis, commit that, and
   **stop**.
5. **Land it:** merge to the fork's `lexipoint` branch, and commit the ledger row in this repo (`lexipoint`).

One phase = one branch = one gate = one ledger row.

## Global rules

- **Every upstream-file change** is wrapped in `#if LEXIRISE` and marked `// LEXIPOINT:`
  (`firmware-base.md` §3). New logic goes in `src/lexirise/`.
- **The key never appears** in logs, screens, test fixtures or commits. Fixtures use `lx_TEST`.
- **Pure logic gets host tests** (`test/lexirise_*`). Hardware-only behavior gets a written manual
  check in the ledger note.
- **Don't modify `util/Dictionary*`.** StarDict is the fallback, and it must keep merging cleanly
  from upstream.
- **The card design is binding and pixel-perfect** (`popup-ui.md` banner, §1.1,
  `reference/card-reference.html`). No phase may change its look or behaviour without claritise's
  sign-off recorded in the ledger. "It looked better this way" is not a reason.
- **Needs-human items (H1–H10 in `00-overview.md`) are never guessed.** If a phase depends on one
  that's still open, it stops.

## The uniform gate

1. `pio run -e x4pro` builds with no new warnings in `src/lexirise/`.
2. `pio run -e x4pro` **with `LEXIRISE` undefined** builds too (the upstream-parity check).
3. The host suite passes: `cmake --build build/test && ctest --test-dir build/test`.
4. Flashed to the device, it boots and opens a book, and the phase's manual check passes.
5. `git grep -n "lx_[A-Za-z0-9]\{8,\}"` returns nothing.

## Phase map

```
P0 pre-flight ─► P1 config+client ─► P2 sentence ─► P3 provider+analyze ─► P5 save ─► P6 errors ─► P7 long-press ─► P8 closeout
                                          P4 card bench (after P0, parallel with P1–P3) ──┘
```

## P0: Pre-flight

**Goal:** a building, flashing fork, and every API unknown answered with evidence.
**Read:** `../reference/lexirise-api-notes.md` (and re-read the live reference first), `firmware-base.md`, `lexirise-client.md` §2, the `00-overview.md` Needs-human list.
**Build:**
- ✅ *Done 2026-09-24:* fork `claritise/crosspoint-reader` created and cloned at
  `~/Projects/crosspoint-reader`, branch `lexipoint` at tag `1.6.5rc`, `freeink-sdk` initialised,
  `upstream` remote added (`firmware-base.md` §1a). Add the `upstream` remote. Build and flash `env:x4pro` unchanged. Capture the
  boot log: free internal heap and PSRAM with a book open.
- Curl every endpoint with a real key. Save the responses as fixtures in
  `test/lexirise_json/fixtures/` **with the key and account IDs removed**. Answer, in the ledger
  note:
  - *(Also answered live on 2026-09-24: upsert = replace, sentence saves are auto-translated, lookups don't bump `seen_count`, `:` in tags is fine, `fast` drops lemmas, no text limit up to 20k chars, Traditional parked. All in the API notes.)*
  - *(Answered live on 2026-09-24, see `../reference/lexirise-api-notes.md`: H4 → the account's target, `en`. H5 → UTF-16. H7 → `zh` + tone-marked pinyin. H9 → `system_tags`. Japanese readings are romaji, which is H10.)*
  - Record fixtures for a `zh` sentence as well as `ja`.
  - The key order in the `analyze/text` body. Response sizes for a 20-, 60- and 120-character
    sentence.
  - Whether `Retry-After` is sent on 429.
- Enable the fork's GitHub Actions, and extend `ci.yml` as in `firmware-base.md` §6 (host tests, the
  `LEXIRISE`-off build, the key-leak grep). Enable secret scanning push protection on both repos.
- **Back up the device's current firmware** before the first flash (a full flash dump with
  `esptool.py --chip esp32s3 … read_flash 0 0x1000000`: upstream's recovery guide covers the C3 only),
  and store it outside the repo.
**Gate:** uniform gate items 1 and 4, plus all the questions above answered in the ledger note, plus CI
green on the fork, plus the backup file verified (size 16 MB).

## P1: Config and client

**Goal:** `LexiriseClient` can POST to any endpoint over verified TLS and stream the body to a
handler.
**Read:** `lexirise-client.md` (all of it).
**Build:** the `LexiriseSettings` store and the **`/lexirise` web page** (`settings.md` §1a–3), so the key can be pasted from a browser from the start, then `LexiriseClient` (§1: the session lifecycle, request body
building, the pre-flight, limits), and a debug-only serial command or test activity that runs one
`analyze/text` and logs the parsed struct.
**Gate:** the uniform gate, plus host suites `lexirise_settings` and `lexirise_request`. Paste the key on the `/lexirise` page. `GET /api/lexirise` returns it masked only (check the network response). `/.lexirise/` isn't listed in the file browser or over WebDAV. The `Lexirise` link shows in all four existing pages' menus. On device, an
analyze call succeeds, and a wrong certificate fails (point `base_url` at a self-signed host and
confirm the call is refused). Log free internal heap before and after 20 consecutive calls: no
monotonic loss.

## P2: Sentence extraction

**Goal:** `SentenceBuilder` turns any tapped token on a page into a sentence plus an offset in
server units.
**Read:** `sentence-extraction.md`, `lookup-flow.md` §2.
**Build:** `SentenceBuilder` with per-language punctuation, and the book-language resolver (`languages.md` §1). Extend `WordBox` with `(lineIdx, tokenIdx)`.
**Gate:** the uniform gate, plus the `lexirise_sentence` suite (every case in §4), plus `lexirise_language`. On device, a
debug log of the sentence, offset and language for 10 taps across two Japanese books, one
Simplified Chinese book and one English book, with
the results written into the ledger note.

## P3: Provider chain and analyze

**Goal:** a tap in word select goes to the Lexirise provider (analyze → match → dictionary/lookup)
and shows the result in a **placeholder** view (the StarDict definition screen with plain text is
fine). StarDict works as the fallback.
**Read:** `lookup-flow.md` §4–6, `offline-and-errors.md` §5.
**Build:** `LookupProvider`, `LexiriseLookupProvider`, `StarDictLookupProvider` (with longest-prefix
CJK), `WifiSession`, the hook in `DictionaryWordSelectActivity::performLookup()`, and the **touch
long-press entry** from the reader (`lookup-flow.md` §1). The X4 Pro has no Confirm button, so
without it there's no quick way into lookups (D15).
**Gate:** the uniform gate, plus the `lexirise_json` suite (fixtures from P0), plus a match-step unit
test (tap offset → occurrence, including a tap on particles and punctuation). On device: 食べさせられた
with a tap on さ resolves to 食べる. 我们在学习中文 with a tap on 习 resolves to 学习, with language `zh`. WiFi off → StarDict answers. Idle for 5 min → WiFi stops
(serial log).

## P4: Card bench (can run alongside P1–P3 once P0 is done)

**Goal:** the final card, drawn from a recorded response, tuned on hardware.
**Read:** `popup-ui.md` (the binding banner and §1.1 first), and open `reference/card-reference.html`.
**Build:** `LexiriseCardActivity`, **exactly as the reference**: the card and the expanded view
(context strip, all tabs including `⋯`), T L F K with the toast, the ▼/▲ rank row with ✕, side
buttons stepping words (the strip follows), Home to collapse and close, loading phases 0/A/B/B′
driven by a timer over fixtures, highlight growth, and the refresh policy. The card is always
bottom-anchored. There's no button legend.
**Gate:** the uniform gate, plus the **Design conformance gate** (below), plus photos of each loading phase in the ledger note (the device, in daylight).
The tapped word stays visible in any of 6 tap positions (`popup-ui.md` §0). Ghosting after 20 cards is
acceptable (claritise signs off).

### Design conformance gate (P4 and P5; also P8 after the final rebase)

1. For **every state** of the reference — card and expanded view · each tab (Meaning, Examples,
   Context, Kanji/Chars, Form, `⋯`) · Japanese and Chinese · a new word and a saved word · Japanese
   readings in kana and in romaji · the sentence **high and low on the page** (D17: the card-view
   strip appears only for covered words) · the save toast and the reading-switch toast — take an **on-device screenshot** (Power + Side Down saves it to `/screenshots/`).
2. Render the same state from `reference/card-reference.html` and scale it to 480×800.
3. Compare them side by side (overlay at 50% if needed). **Every box position and size within
   ±1 px of §1.1, and pure black and white only.** List any sanctioned deviation (§1.1) used.
4. Commit the screenshot pairs to `docs/v0.1/reference/conformance/<phase>/` and link them from
   the ledger note. **claritise signs off on the pairs.** Without that sign-off, the phase isn't `done`.

## P5: Card live, and save

**Goal:** P3's provider feeds P4's card, and Save works.
**Read:** `lookup-flow.md` §6–7, `popup-ui.md` §2–3, D9 (H2 resolved).
**Build:** replace the P3 placeholder with the card. Add Left/Right word stepping, Save (fresh
session), and the double-press guard.
**Gate:** the uniform gate, plus the **Design conformance gate** with live data. Switch the reading to romaji, reboot, and confirm the next lookup opens in romaji. On device, save 3 new words from a real book. They appear in Lexirise
with tag `xteink`, the lemma, the translation, and the sentence in notes. A saved
word re-tapped shows `Saved · tracked`. An inflected form of a saved lemma shows as saved. Then save 3
words from a Simplified Chinese book: they're saved with language `zh`, and the card shows pinyin
with tone marks.

## P6: Errors and fallback

**Goal:** every row in `offline-and-errors.md` §1 and §3 behaves as written.
**Read:** `offline-and-errors.md`.
**Build:** the status handling, 401 self-disable, 429 back-off, the offline mark, and the I18n
strings.
**Gate:** the uniform gate. On device, go through the table: a bad key, WiFi off, an unreachable
`base_url` (timeout), a forced 429 (a proxy or a low-limit test key, if Lexirise offers one), and a
save with WiFi dropped mid-request. Every row gets a check or a note in the ledger.

## P7: Long-press polish and the settings screen

**Goal:** the long-press entry (built in P3) is hardened: gesture conflicts, a long-press on another word while the card is open, and Home to close. **Plus the device settings screen** (Settings → System → Lexirise, `settings.md`), checked with the on-device tests in `settings.md` §5.
**Read:** `lookup-flow.md` §1, `popup-ui.md` §3 (Back behavior by entry point).
**Build:** the `EpubReaderActivity` hook, the `DictionaryWordSelectActivity` `initialTouch`
overload, and a long-press on another word while the card is open.
**Gate:** the uniform gate. On device, a long-press never also turns the page. A tap still turns the
page. Back from a touch-opened card returns to the reader, and from a menu-opened card to word
select.

## P8: Closeout

**Goal:** it's something you and others can use every day.
**Build:** the **user setup guide** (`docs/user-guide.md`: install, WiFi, getting a Lexirise Pro key,
pasting it on the `/lexirise` page, the SD font and fallback dictionaries, privacy, trusted networks),
the version string `<upstream>-lexi.<n>`, **the OTA hook pointing at the fork's releases**
(`firmware-base.md` §6), and a GitHub release built by CI.
**Gate:** the uniform gate, plus the **Design conformance gate** re-run on the release build, plus an
**OTA round trip**: a device on `-lexi.1` sees `-lexi.2` published on the fork and updates to it, and
never offers upstream's release, plus **one hour of real reading** with 30 or more lookups: no crash, no
reboot, and a stable free internal heap at the end (logged). Then a rebase onto the newest upstream
tag and a rerun of the full gate.

## Status ledger

| Phase | Status | Commit | Host tests | Note |
|---|---|---|---|---|
| P0 | in progress | fork `lexipoint` @ `a1ceb63` (tag 1.6.5rc) | upstream baseline **330/330** | 2026-09-24: fork created and cloned. Device confirmed as an **ESP32-S3 (QFN56) rev v0.2, 8 MB embedded PSRAM, 16 MB flash**, USB 303A:1001, MAC 44:bd:8d:7c:48:88. It was running an earlier CrossPoint x4pro build. **Full 16 MB backup** at `~/Projects/x4pro-firmware-backup/` (SHA-256 `c100d8be…eb2376`, outside both repos). Stock `x4pro` build of 1.6.5rc: OK (4 min 48 s), **flashed and verified**, boots as `1.6.5-x4pro`. Boot log on the Home screen: internal heap **223,168 / 293,260 B free, max alloc 172,020**. PSRAM **8,276,232 / 8,388,608 free**. **Panel: UltraChip UC8279** (not SSD1677): full refresh ~1.34 s, partial ~0.49 s. No SD fonts yet. Most API questions answered live (see the API notes). **Still to do:** heap with a book open, CI on the fork, secret scanning |
| P1 | todo | | | |
| P2 | todo | | | |
| P3 | todo | | | |
| P4 | todo | | | |
| P5 | todo | | | |
| P6 | todo | | | |
| P7 | todo | | | |
| P8 | todo | | | |
