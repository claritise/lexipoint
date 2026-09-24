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
5. **Land it:** merge `lexi/<phase-id>` **directly** into the fork's `lexipoint` branch (**no pull
   requests**, claritise 2026-09-24), push, and commit the ledger row in this repo (`lexipoint`). Keep
   hook edits to upstream files in **their own commits**, separate from new files, so rebases stay
   readable. Report the result to claritise. Sign-offs she gives in chat (e.g. the design conformance
   screenshots) are quoted with their date in the ledger note.

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
**Gate:** the uniform gate, plus host suites `lexirise_settings` and `lexirise_net` (request building is in the latter), plus `websmoke.py` against the device. Paste the key on the `/lexirise` page. `GET /api/lexirise` returns it masked only (check the network response). `/.lexirise/` isn't listed in the file browser or over WebDAV. The `Lexirise` link shows in all four existing pages' menus. On device, an
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
**Gate:** the uniform gate, plus the `lexirise_json` suite (fixtures from P0; as built, synthetic ja and zh
fixtures in `lexirise_net/ResponsesTest` and `lexirise_lookup/LookupParseTest`, since the public repo holds
no real responses), plus a match-step unit test (tap offset → occurrence, including a tap on particles and punctuation). On device: 食べさせられた
with a tap on さ resolves to 食べる. 我们在学习中文 with a tap on 习 resolves to 学习, with language `zh`. WiFi off → StarDict answers. Idle for 5 min → WiFi stops
(serial log).
**Also (from P1):** Lexipoint keeps its WiFi while a reader activity is on screen or under it, which is
only safe if nothing that uses WiFi is ever *pushed* over the reader (KOSync replaces it). Re-check the
reader's `startActivityForResult` targets, including the new card, and after every upstream sync.

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
   the ledger note. **claritise signs off on the pairs in chat**, and the sign-off is quoted in the ledger. Without that sign-off, the phase isn't `done`.

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
| P0 | **done** | fork `lexipoint` @ `a1ceb63` (tag 1.6.5rc) | upstream baseline **330/330** | 2026-09-24: fork created and cloned. Device confirmed as an **ESP32-S3 (QFN56) rev v0.2, 8 MB embedded PSRAM, 16 MB flash**, USB 303A:1001, MAC 44:bd:8d:7c:48:88. It was running an earlier CrossPoint x4pro build. **Full 16 MB backup** at `~/Projects/x4pro-firmware-backup/` (SHA-256 `c100d8be…eb2376`, outside both repos). Stock `x4pro` build of 1.6.5rc: OK (4 min 48 s), **flashed and verified**, boots as `1.6.5-x4pro`. Boot log on the Home screen: internal heap **223,168 / 293,260 B free, max alloc 172,020**. PSRAM **8,276,232 / 8,388,608 free**. **Panel: UltraChip UC8279** (not SSD1677): full refresh ~1.34 s, partial ~0.49 s. CJK font built and installed (`languages.md` §5.1). Japanese and Chinese both render (claritise, 2026-09-24). **With a book open: internal heap ~193 KB free (min 165,732, max alloc 139,252), PSRAM ~8.2 MB free.** Plenty for TLS (~40 KB). `[SCT] Deserialization failed` after a font change is the layout cache rebuilding, which is benign. Most API questions answered live (see the API notes). **Deferred, not blocking:** GitHub Actions on the fork (claritise to decide), secret scanning (claritise's repo setting) |
| D (dev harness, pre-P1) | **done (host); on-device checks pending** | fork `lexipoint` @ `0184a3f5` (2 commits: new files, then hooks) | **376/376** host (46 new), **11/11** Python | USB remote control for unattended builds (`dev-harness.md`), asked for by claritise 2026-09-24. **Review loop: 4 rounds** → R1: 1 blocker + 17 (keep-awake lease, torn screenshots, SDK-faithful gestures, pure modules + tests, reply matching, commit order) → R2: 5 should + 6 nice (long-press taps on lift, swipes not tap candidates, lease needs a USB host, extends-aware release guard, gesture smoke) → R3: **NO CHANGES NEEDED** (3 optional polish items taken) → R4: **NO CHANGES NEEDED**. Flag-off diff token-identical to upstream; the release binary contains no harness (identical size across rounds). **Owed on device** (the reader was asleep): `lxctl selftest` in 4 orientations, `lxctl smoke`, reader TAP/LONG, BTN timings, 20× SYNC+SHOT, keep-awake lease, legacy CMD:SCREENSHOT |
| P1 | **done (host); on-device checks pending** | fork `lexipoint` @ `b8f39c61` (new files, hooks, 5 review-fix commits, polish) | **492/492** host (116 new: `lexirise_settings`, `lexirise_net`, harness), **21/21** Python (`test_lxctl`, `test_websmoke`) | Settings store (crash-safe, `.bad` quarantine), `/lexirise` page + `/api/lexirise` (masked key, Origin + Host guards, queued key check), verified wolfSSL client (ISRG X1/X2 pinned, hostname, SNI, NTP clock; `[lexirise]` adds SHA-384/P-384, +~30 KB flash → 84.2%), keep-alive client with request deadline and idempotent-only retry, WifiSession (radio-off only, explicit lease, given back on leaving reading via an ActivityManager hook), `LX:LEXI ME/ANALYZE/SOAK [COLD]`, `websmoke.py`. **Review loop: 7 rounds** → R1: 1 blocker (a key check killed the web server's hotspot) + 6 should (event-inferred WiFi ownership, idle TLS heap, no request deadline, stack inside handleClient, unreadable config overwritten, test gaps) → R2: 1 blocker (FAT short name `/LEXIRI~1` served the key) + 3 should (hand-back after onEnter, name allowlist, no recheck) → R3: 2 should (`~` names over-refused, PROPFIND listed hidden folders) → R4: 1 blocker (SdFat skips leading spaces: `/%20.lexirise` served the key; also a planted-config chain via ` .lexirise` names) → R5: 1 should (web listing vs "Show hidden files") → R6: **NO CHANGES NEEDED** (2 polish items taken) → R7: **NO CHANGES NEEDED**. The file-manager hardening also closes the same holes for upstream's `/.crosspoint` (WiFi passwords). **Owed on device:** `lxctl lexi me/analyze ja/zh`, wrong-cert refusal (self-signed `base_url`), `lxctl lexi soak 20` and `soak 20 cold` (heap trend, `stack_free`), `websmoke.py` in station and hotspot modes, key paste → Checking → Connected, hotspot untouched by a key check, WiFi hand-back before KOSync/file transfer, idle teardown (0 and 1 min), corrupt/oversized config → `.bad` + page notice, KOSync/OTA/font download after Lexirise calls (wolfSSL flags), plus the dev-harness checks owed from D |
| P2 | **done (host); on-device checks pending** | fork `lexipoint` @ `1ee2037b` (new files, hooks, 5 review-fix commits, polish) | **552/552** host (60 new: `lexirise_sentence`, `lexirise_language`, `lexirise_pagemodel`, `lexirise_layout`) | `text/SentenceBuilder` (sentence + UTF-16 tap offset; per-language punctuation; in-token splits for CrossPoint's glued tokens like `“好。”“走`; quotative と/って; 」「 dialogue breaks; full-width space as spacing; dots in numbers/abbreviations; 120-unit cap with zh `；` fallback), `text/BookLanguage` + `TapContext` (override > dc:language > sentence; `detected` vs sent), `text/PageModelAdapter` + `ParagraphBreaks` (paragraph starts from geometry: short line, gap vs smallest step, first-line indent, `　` not after `？！`, style change; furigana-aware), `lookup/PageTap` (measured once in `extractWords`, tap is pure; `LXTAP` debug log). End-to-end suite runs XHTML through the real `ChapterHtmlSlimParser`. **Review loop: 7 rounds** → R1: 4 should (furigana gaps, unlocked renderer on tap, ・ as kana, test layout ≠ real layout) → R2: 1 blocker (round-1 in-token split broke `！？` / `3.50`) → R3: 3 should (bracket/title breaks, `　` inside sentences, `３．５`) → R4: 1 should (`？　` wrapping to a line start read as an indent) → R5: 1 should (round-4 pause rule merged `……` paragraphs) → R6: **NO CHANGES NEEDED** (polish taken) → R7: **NO CHANGES NEEDED**. **Owed on device (the P2 gate):** `LXTAP` log for 10 taps across two Japanese books, one Simplified Chinese book and one English book (dialogue, furigana, `！？」と`, `３．５`, page top/bottom, tables, images; line spacing 1.0/1.1; extra paragraph spacing on/off), word-select open time and heap on dense CJK pages. **Carried to P3:** the page model is built in release builds (used from P3 on); tokens are copied into the model (~10-25 KB while word select is open) |
| P3 | **done (host); on-device checks pending** | fork `lexipoint` @ `2a64d98a` (provider + hooks, squashed from 5 review rounds) | **588/588** host (36 new: `lexirise_lookup`, plus `lexirise_net` / `lexirise_language` additions); `x4pro`, `x4pro-gh_release` and `x4c` (Lexirise off) build | `lookup/`: `lookupWithLexirise` (analyze → `matchOccurrence` → `dictionary/lookup` of the headword, behind `api::LexiriseApi`), `chainStep` (a Lexirise answer is final; else StarDict, else "No dictionary set"), `starDictCandidates` + `probeStarDict` (longest ja/zh prefix, ≤ 8), `takeLongPress` / `lookupOwnsLongPress` (judged at the touch-down point via `peekScreenLongPress`; outer zones stay CrossPoint's when its hold action is on in a tap mode; never taken when nothing can answer), `BookLanguage::mayUseLexirise`; placeholder card in `DictionaryDefinitionActivity`. As built: `lookup-flow.md` §5a. **Review loop: 5 rounds** → R1: 5 should (long-press swallowed with nothing to answer, "Dictionary error" without StarDict, one busy sentence failing analyze, untested activity decisions, duplicated zone width) → R2: 2 should (lemma reading without a lemma entry, chain decision untested) → R3: 1 should (zone judged at the finger's current point, not touch-down; settings read every touch frame) → R4: **NO CHANGES NEEDED** → R5: **NO CHANGES NEEDED**. **Owed on device (the P3 gate):** 食べさせられた tap さ → 食べる (lemma reading, surface in brackets); 我们在学习中文 tap 习 → 学习 as `zh`; WiFi off → StarDict (コーヒー, 人々 by longest prefix; one busy repaint); WiFi off and no StarDict → "No dictionary set"; idle 5 min → WiFi stops (serial). **Plus:** `lxctl long <x> <y>` in the centre vs the outer thirds with the hold action on/off and in tap, inverted, swipe and off modes (a press drifting across the one-third line counts where it started); Lexirise off + no StarDict: a slow tap turns the page or opens the menu as before; zh-TW book with a key and no StarDict: long-press not taken; finger lift never taps word select; rotated orientations; Home-hold / menu Look Up unchanged; placeholder CJK glyphs render; a saved word shows "saved" on its conjugated form; a second lookup reuses the session (no new handshake in `LXS`); worst case (out-of-range saved network ≈ 6 s, slow network): no watchdog reset. **Known, deliberate:** the first lookup of a boot queues a `/v1/me` key check (≈ 1 s once); the StarDict prefix stops at the line end; an untagged or non-CJK-tagged book counts as Lexirise-usable (mis-stamped Japanese novels); the lookup blocks the loop until the card phases (P4/P5); the activity glue is covered by the device checks, not unit tests (a harness is a P5 candidate). `pio run -e default` fails only at link with a local PlatformIO SCons error (`FortranCommon`), not our code |
| P4 | **in progress (host done); device and sign-off pending** | branch `lexi/P4` | host: `lexirise_card` (layout vs §1.1, controller), `lexirise_kana`; `x4pro` / `x4pro-gh_release` build | **Built:** `src/lexirise/card/`: `CardMetrics.h` (every §1.1 number), pure `CardLayout` (CSS box model, line boxes from the device fonts' own line heights where §1.1 sets none), `CardController` (phases 0/A/B/B′ on a timer, merged when B follows A within 300 ms; side buttons step words and keep the view and tab; T L F K + toast; ▼/▲; ✕; Home; reading kana ⇄ romaji, saved to `reading`; ⋯ actions), `CardPainter` (device fonts, prewarm, drawing), `LexiriseCardActivity` (the bench; `lxctl lexi card ja\|zh [low]`), `text/Kana` (romaji → kana, languages.md §3a). **Host preview:** `scripts/lexipoint/cardshots.py` renders all 21 reference states three ways (the device layout with the real metrics / the reference in headless Chrome at 480×800 / a 50 % overlay); they line up box for box. **Fonts used (nearest real sizes, §1.1 "Typefaces"):** UI 14-18 px → SMALL (16.7 px), UI 20-23 → UI_10 (20.8), the word 42 → the reader family at 18 pt (37.5), readings/surface 16-17 → 8 pt (16.7), sentences/forms 21-23 → 10 pt (20.8), a character 40 → 18 pt (37.5); CJK inside UI text (a book title) is set in the reader family. ✕ ▼ ▲ ⋯ › are drawn as shapes (no device font has them; deviation 2). **Questions for claritise (sign-off needed):** (1) frequency bars: §1.1 says 6 px wide incl. frame, heights 8-25; the reference *renders* 6 CSS px incl. its border (≈ 8.5 device) and 8-20 CSS tall (≈ 11-28): built to §1.1; (2) the reference joins both senses on 3 lines where popup-ui §1 says the second only "if it fits on 2 lines": built to the text; (3) the expanded card is the reference's 500 px scaled (706), bottom-anchored, so its top is 80 (§1.1's 71 plus the ~10 px extra height it gives the strip); (4) the bench's page uses the reference's line pitch (52 px) in the reader font. (5) **landscape:** the card is measured for the 480×800 portrait panel, so a landscape reader is switched to portrait while the card is open (upside-down portrait stays as it is) and restores the reader's orientation on close (built that way; the alternative is a landscape layout, which §1.1 doesn't define). **Also as built:** the Form tab's last form isn't bold (the SD fonts are regular only); the toast's Undo is a touch target over whatever it covers. **Tools:** `scripts/lexipoint/gen_bench_fixtures.py` regenerates `BenchFixtures.cpp` from the reference (`--check`, and `test_gen_bench_fixtures.py`, catch drift). **Review loop:** R1 → 8 should (toast Undo not tappable, header overflow, body overflow, the half-refresh promotion racing a redraw, loop() locking every pass, kana `nō`, the missing fixture generator, untested frame composition) → fixed → R2: 1 should (a marked word drawn over a cut line's …) + 4 nits → fixed. → R3 (its report was lost at an account switch; its fixes were finished and tested: a cut paragraph now advances the flow, so sense 2 no longer draws over it; long senses, form labels and toasts fit; capital macrons and Hepburn *m* before b/m/p in romaji → kana) → R4: 4 should (a tap during a refresh matched the new frame's targets → `ShownTargets`; the expanded tabs' 5th-7th fonts skipped the SD prewarm → the scan takes 8 fonts with LEXIRISE; the card clipped in landscape → forced portrait while open; missing card/golden/device smoke coverage → 21 golden display lists in ctest and `lxctl card-smoke`) + 4 nits (a long Latin word or form crossing the frame, `#if LEXIRISE` guards, sized label arrays and shape numbers in `CardMetrics.h`, one UTF-8 prefix helper) → fixed. → R5: 2 must (`card-smoke` sent `LEXI CARD JA`, which the device refuses, and back-to-back `BTN`s, refused as busy) + 3 should (`card-smoke` changed the saved reading → `LEXI CARD … KANA` opens in kana and never saves; a tap while `loop()` waited on the render lock was stamped late → input is read every pass and handled when no render runs; upside-down portrait was turned upright → only landscape is turned) + 1 nit (deadlines across the `millis()` wrap) → fixed, with a grammar parity test between `lxctl` and `DevProtocol.cpp`. **Deferred:** swipes (card up/down, tab left/right) to P7 with the other gesture work (`wasSwipe()` has no origin, and §3.2's edge-safety needs one); the per-language rank thresholds to P5. **Owed on device (the P4 gate):** flash, `lxctl lexi card ja` / `zh` / `ja low`, drive every state (tap T L F K, ▼, the tabs, the reading line; buttons; Home), `lxctl shot` each, compare with the reference (±1 px), photos of each loading phase in daylight, thumb reach of T L F K, ghosting after 20 cards, memory with the 8/10/18 pt sizes loaded; `lxctl card-smoke` (every reference state, a screenshot each); taps during each phase refresh land on what was shown; the Examples/Context/Form tabs render without "loads on demand" in the log; a landscape book: the card opens in portrait and the book returns in landscape; sleep with the card open still gets its full refresh; then claritise's sign-off on the pairs |
| P5 | todo | | | |
| P6 | todo | | | |
| P7 | todo | | | |
| P8 | todo | | | |
