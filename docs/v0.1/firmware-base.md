# Firmware base: what Lexipoint is built on and where our code lives

**Status:** rewritten 2026-09-26 by phase M (`standalone-repo.md`). Decisions D20, D21, D22 in `00-overview.md`;
D1 is the history of where the base came from. The fork-era version of this doc is in the repo's history.

Related: `standalone-repo.md` (phase M, the full reasoning), `lookup-flow.md` (the hooks this doc allows),
`lexirise-client.md` (the one new network path), `../reference/firmware-commit-map.md` (fork SHAs → SHAs here).

---

## 0. Why Lexipoint is its own repo

Lexipoint is its own project, in one repo: `github.com/claritise/lexipoint` (D21). CrossPoint Reader is the
code Lexipoint started from. It is credited in `NOTICE` and the README, and it is not an upstream Lexipoint
keeps up with. In short: Lexipoint supports touch devices only, and CrossPoint's own work is mostly for
devices without touch, so merging it costs more than it gives. One repo also keeps a phase's code and its
ledger row in one history, and the X4 Pro is the only target (D20). The full reasoning is in
`standalone-repo.md` §0.

History: Lexipoint started on 2026-09-24 as a fork of CrossPoint (D1, D19). CrossPoint has no plugin system,
and its `SCOPE.md` had closed new "talk to a server" features, so a Lexirise client could only live in a fork.
Phase M (2026-09-26) turned the fork into this repo and kept its history.

## 1. Repo layout

```
lexipoint/                   github.com/claritise/lexipoint (public)
  README.md                  what Lexipoint is, the X4 Pro, how to build, credits
  NOTICE                     built on CrossPoint Reader (MIT, © 2025 Dave Allie); the FreeInk SDK
  .gitmodules                firmware/freeink-sdk → https://github.com/Free-Ink/freeink-sdk.git
  firmware/                  the firmware (PlatformIO), with its history (CrossPoint's and the fork's)
    LICENSE                  CrossPoint's MIT license, unchanged
    AGENTS.md, CLAUDE.md     coding conventions for Lexipoint on the X4 Pro
    platformio.ini, src/, lib/, test/, scripts/, bin/, .githooks/, docs/ (CrossPoint's technical docs)
    freeink-sdk/             the SDK (submodule)
  docs/                      these docs
  tools/                     tools that run on a computer: the manga converter (`../v0.2/manga.md`), Lexirise API
                             probes, design mockups
  research/                  gitignored local workspace: leave it alone, never `git clean -x`
  sd-card/                   gitignored SD-card staging area
```

Lexipoint's own code is apart from the base code: `firmware/src/lexirise/`, `firmware/test/lexirise_*/`,
`firmware/scripts/lexipoint/`, plus marked hooks in base files (§3).

## 1a. Working in the repo

- The local checkout is `~/Projects/lexipoint`. **`cd firmware` first**: every `pio`, `cmake`, `ctest` and
  `scripts/lexipoint/…` command runs there. Firmware paths in these docs are written from the repo root
  (`firmware/src/lexirise/…`), or relative to `firmware/` where a section says so (§3 does).
- Clone with `--recursive` (the SDK submodule). Turn on the formatter hook once per clone:
  `git config core.hooksPath firmware/.githooks`.
- Phase branches are `lexi/<phase-id>`, merged into `main` with the ledger row in the same history
  (`01-build-order.md` "How to run").
- History: until M, the firmware was the fork `claritise/crosspoint-reader` (branch `lexipoint`), cloned at
  `~/Projects/crosspoint-reader` with `origin` = the fork and `upstream` = CrossPoint (D19). That clone is
  kept for its local `lexi/P*-wip-archive` branches, and claritise archives the GitHub fork. M brought the
  fork in with `git filter-repo`, so every fork commit has a new SHA here: ledger rows from before M name the
  fork's SHAs, and `../reference/firmware-commit-map.md` finds them here.

## 2. Build targets

The X4 Pro only (D20). `firmware/platformio.ini` has these envs and no others, and `default_envs = x4pro`:

| Env | What it is |
|---|---|
| `x4pro` | The dev build: `pio run -e x4pro -t upload`. ESP32-S3 (`esp32-s3-devkitc1-n16r8`), `-DBOARD_HAS_PSRAM`, `LOG_LEVEL=2`, the USB dev harness (`dev-harness.md`), and it waits for USB serial (`CROSSPOINT_WAIT_FOR_USB_SERIAL`) |
| `x4pro-gh_release` | What users get: `publish_release.py` builds it for a release. No harness, `LOG_LEVEL=1`. The uniform gate builds it every phase, so a release-only break shows up early |
| `x4pro-gh_release_rc` | Release candidates (`publish_release.py --prerelease`) |
| ~~`x4pro-lexirise-off`~~ | ~~The Lexirise-off build: `x4pro-gh_release` without `[lexirise]`, so every `LEXIRISE` hook in a base file must compile out. The uniform gate builds it; nobody flashes it. It replaced `x4c` in M~~ **Superseded 2026-09-29 (v0.2 V8):** removed with the `LEXIRISE` gate (`../v0.2/slimming.md` §8) |

- **Touch is required at compile time.** `firmware/src/lexirise/RequiresTouch.cpp` stops the build with an
  `#error` on a device without `FREEINK_CAP_TOUCH`. A touch device the SDK supports would pass on its own,
  but none is built or tested now (D20).
- `freeink-sdk` is a **git submodule** at `firmware/freeink-sdk` (`https://github.com/Free-Ink/freeink-sdk.git`).
  It holds the HAL, the display and touch drivers, and `SecureHttpClient`. We **don't fork the SDK**.
- Host tests, from `firmware/`: `cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test`
  (gtest via FetchContent, see `test/README`). Anything we write that is pure logic (sentence
  building, offset mapping, response parsing, config parsing) gets a suite here.
- ~~The build flag **`-DLEXIRISE=1`** gates the Lexirise hooks. With it off, the firmware behaves like
  CrossPoint except for deliberate, ungated changes (§3): the file manager's and WebDAV's hidden-path
  guard, the nav script tag that 404s harmlessly, and the USB device name.~~ **Superseded 2026-09-29 (v0.2 V8):**
  there is no `LEXIRISE` flag: Lexirise is always built (`../v0.2/slimming.md` §2, §8).

## 3. Where our code lives

Our code goes in **new files wherever possible**. Base files only get small, marked hooks. Paths in this
section are relative to `firmware/`.

```
src/lexirise/
  LexiriseConfig.h              every Lexirise constant (paths, limits, timeouts)
  RequiresTouch.cpp             the build needs FREEINK_CAP_TOUCH (§2; M)
  LexiriseService.{h,cpp}       the one session: settings → WiFi → verified TLS → client (P1)
  settings/Settings.{h,cpp}     settings model + INI parse/serialise, field validators (pure)
  settings/SettingsStore.{h,cpp}  crash-safe save of /.lexirise/config.ini, snapshot reads (settings.md §3)
  settings/SettingsFilesHal.cpp  the SD card adapter for the store
  settings/SettingsPatch.{h,cpp}  validated web edits (pure)
  LexiriseServiceHal.cpp        wires the device LexiriseService (store, WifiSession, TlsConnection, millis)
  net/Http.{h,cpp}              base URL, request serialiser, bounded response parser (pure)
  net/JsonWriter.{h,cpp}        request bodies (pure)
  net/JsonReader.{h,cpp}        strict path-reporting reader for response bodies (pure)
  net/Connection.h              the byte pipe the client talks over (fake in host tests)
  net/TlsConnection.{h,cpp}     wolfSSL, ISRG roots pinned, hostname checked (lexirise-client.md §1)
  net/TrustAnchors.h            ISRG Root X1 + X2
  net/WifiLease.h / WifiSession.{h,cpp}  on-demand connect + idle teardown (D10)
  net/WifiHint.h                the last connection's access point and channel, for a direct join (P11), pure
  api/LexiriseClient.{h,cpp}    keep-alive, stale-session retry, error mapping
  api/Requests.{h,cpp} / Responses.{h,cpp} / KeyCheck.{h,cpp}  endpoints (pure)
  web/LexiriseWeb.{h,cpp}       /lexirise page + /api/lexirise (settings.md §1a)
  web/LexirisePage.html, LexiriseNav.js  embedded by scripts/build_html.py like CrossPoint's pages
  web/WebApi.{h,cpp}            /api/lexirise JSON ↔ SettingsPatch / page state (pure)
  web/HiddenPath.h, Origin.h    file-manager (incl. FAT short names), cross-site and DNS-rebinding guards (pure)
  web/HiddenPathHal.cpp         the SD card name lookup for HiddenPath (built in every env)
  dev/                          the USB dev harness (dev-harness.md), x4pro dev env only
  text/SentenceBuilder, Punctuation  the sentence and tap offset (sentence-extraction.md), pure
  text/BookLanguage, TapContext      the language to send (languages.md §1), pure
  text/PageModelAdapter, ParagraphBreaks  CrossPoint's Page → PageModel (host-tested on real Pages)
  lookup/PageTap                      the device glue (renderer measuring, the gate and notices' device side)
  lookup/LexiriseLookup, LookupCard, Match, LongPress, StarDictCandidates  the lookup (P3, split in P5), pure
  lookup/Fallback.h                   whether Lexirise is asked, what's said when it isn't (P6), pure
  api/AccessPolicy.h                  401 off until a new key, 429 back-off (P6), pure
  text/Kana, Utf8Prefix, Utf8Units    romaji ⇄ kana; UTF-8 / UTF-16 helpers, pure
  card/                               the card (P4-P6): CardMetrics, CardModel, DisplayList, CardLayout, TextRuns,
                                      ShapeGeometry, CardController, CardInput, ShownTargets, CardSession, WordSelectFlow,
                                      CardSource (BenchSource, LiveSource), LiveWord, ReaderScene, CardFrame (pure);
                                      CardPainter, ReaderPageFor, CardStringsI18n, LexiriseCardActivity (device)
  page/PageAnalysis, PageStore        a page's analyze/text answer, compact, streamed, kept on SD (V7b), pure
  page/Prefetch, PageSentences        when a page is analyzed; a tap's sentence from it (V7b), pure
  page/ReaderPages                    the reader's side of it (V7b, device)
  api/RequestWindow.h                 this reader's requests in the last hour (V7b's budget), pure
  util/ByteOrder.h, Crc32.h, Epoch.h  little-endian fields, CRC-32 and FNV-1a, the epoch-or-0 rule (V7b), pure
  input/TouchLine, InputAbort         the reader's input read from the hardware during a call (V7b; TouchLine pure)
  vocab/ManualSync, HomeSync          the home screen's Sync Vocabulary (V7b; ManualSync pure, HomeSync device)
  util/Timing.h                       wrap-safe millis() comparisons
  settings/SettingsScreen.{h,cpp}     the device Settings → System → Lexirise rows and edits (P7), pure
  settings/LexiriseSettingsActivity   that screen (P7, device: CrossPoint's UiListActivity)
  ota/ReleaseVersion                  `<base>-lexi.<n>` versions, which release OTA offers (P8), pure
  lookup/StarDictChoice.h             each language's own offline dictionary (P7), pure
  settings/SafeFile                   crash-safe replace and recovery of a small SD file (config.ini, books.ini; P9;
                                      book-tags.ini, V2; decks.ini, V3; ignored.ini, V5; vocab-<lang>.bin, V7a;
                                      pages/index.bin, V7b; marks-off.ini, V9a)
  settings/BookLanguages              each book's lookup language, books.ini, the reader menu's row (P9), pure + store
  settings/LanguageNames.h            a language's name on screen (settings groups, the menu row; P9)
  settings/LongPressMenu.h            Long-press Menu's choices without Dictionary (P10), pure
  text/BookSlug                       a book's tag, `book:<slug>` of its title (V2, v0.2 C2), pure
  settings/BookTags                   the tags a save carries, and each book tag's title, book-tags.ini (V2), pure + store
  deck/BookDeck                       a Lexirise deck per book: DeckFlow, findBookDeck, decks.ini (V3), pure + store
  text/Conjugation                    a Japanese form's name and steps from its dictionary form (V4, C16), pure
  net/Wait.h                          blocking network waits on the main loop feed the task watchdog (P1)
  text/CharClass.h                    character classes for the sentence builder and language detection (P2), pure
  api/LexiriseApi.h                   the calls a lookup and the card's saves make, as an interface (P3)
  card/BenchFixtures, BenchPage       the card bench's data and the page under it (P4), pure
  card/CardOrientation.h              the orientation the card is drawn in (P4), pure
  lookup/WholeWords                   whole words for a sentence Lexirise returned already refined (V1), pure
  settings/IgnoredWords               the words the reader ignored, ignored.ini (V5, C17), pure + store
  vocab/VocabMirror                   the vocab mirror: the user's Lexirise words on SD, vocab-<lang>.bin (V7a, C13)
  api/VocabPage                       one page of GET /v1/vocabulary, read as it streams (V7a), pure
  net/JsonStream                      a push JSON reader for bodies too large to hold (V7a), pure
  api/JsonNumbers.h                   JSON numbers in Lexirise's answers, read strictly (V7b), pure
  lookup/LookupCache                  the lemma cache: phase B's answers on SD (V7c, C21)
  net/TlsSession.h                    TLS session resumption's policy, in RAM only (V7c, C21), pure
  ota/ReleaseAsset.h                  the firmware asset's name in a release (V8), pure
  settings/BookMarks                  each book's Page marks row, marks-off.ini (V9a), pure + store
  page/PageMarks                      the page marks: which word gets which underline, and where (V9a, A1), pure
  page/ReaderMarks                    the reader's side of the marks: the device's sources and the drawing (V9a, device)
  page/MarkKeeper                     everything ReaderMarks does but drawing (V9a), host-compiled
  page/MarkSlots                      the analyses kept for the pages around the one on screen (V9a), pure
  page/MarkGate.h                     which kept page to read next, where a written page is kept (V9a), pure
  page/MarkRule.h                     the one rule for a word's mark at its level, shared with A3 (V9a), pure
  page/MarkVisibility.h               which books show marks, and A3's switch (V9a), pure
  page/CardMarks.h                    the page under a card, marked once per what decides its marks (V9a), pure
  card/SentencePreview                the ⋯ tab's sentence preview: clauses, Shorter and Longer (V6, C3), pure
  session/ReadingSession              the reading session's counts and summary, in RAM (V6, C1, C7), pure
  session/HomeSummary                 the summary on the home screen (V6, device)
src/activities/reader/WordBoxes.h     (CrossPoint's folder, ours) word select's word boxes and hit rule, shared with the
                                      reader's long-press check (P9), pure
test/lexirise_*/                host gtest suites
scripts/lexipoint/lxctl.py      host side of the dev harness (+ test_lxctl.py)
scripts/lexipoint/websmoke.py   read-only smoke test of the web surface against a device (+ test_websmoke.py)
scripts/lexipoint/keyscan.py    the key-leak scan, uniform gate 5 (+ test_keyscan.py)
scripts/lexipoint/publish_release.py  builds and publishes a release, §6 (+ test_publish_release.py)
test/lexirise_fakes/            fakes shared by the suites (card, connection, WiFi, clock)
```

**Base files we touch.** ~~Each hook carries a `// LEXIPOINT` comment, so `git grep LEXIPOINT`
lists every one. Hooks are wrapped in `#if LEXIRISE` unless noted:~~ **Superseded 2026-09-29 (v0.2 V8):** there is no `LEXIRISE` gate, and a `// LEXIPOINT` marker stays only where it says why a base
file was edited (the bare ones went), so this table, not `git grep LEXIPOINT`, is the list. "Not gated" in a row below
is history.

| File | Hook |
|---|---|
| `src/main.cpp` | Load the settings store at boot; `service().tick()` in the loop (queued key check, idle TLS close, WiFi idle teardown). Dev harness hooks (`LEXIPOINT_DEV_HARNESS`) |
| `src/activities/ActivityManager.cpp` | Tell `LexiriseService` whether a reader activity is still on screen or under it, **before** the next activity's `onEnter` (push/replace, the immediate replace) and after a pop out of reading, so WiFi Lexipoint owns is given back first (`offline-and-errors.md` §5). Relies on nothing that uses WiFi being pushed over the reader ~~(KOSync replaces it)~~ (Superseded 2026-09-29: KOSync removed in V8): re-check when taking a CrossPoint change (§4). (V6) `goHome()` tells the reading session the home screen comes next (`session::readingSession().homeNext()`), so a book closing on the way leaves its summary (C1) |
| `src/activities/reader/ReaderActivity.cpp` | (V6) `onExit()` ends the reading session (`session::readingSession().bookClosed()`), for any reader: only an EPUB's has one |
| `src/activities/ActivityManager.h`, `src/activities/HomeMenuItem.h`, `src/activities/home/HomeMenuIndex.h`, `src/activities/home/HomeActivity.{h,cpp}` | (V7b R5) `HomeMenuItem` in its own header and the menu index as pure functions (`test/home_menu`); `preventAutoSleep()` while a sync runs. (V7b) `HomeMenuItem::VOCAB_SYNC` and the Sync Vocabulary row just above Settings (`vocab::homeSyncRowShown`), its popup over the menu and one step per loop pass (`vocab::HomeSync`); the index mapping takes the row. (V6) `sessionSummary`: the reading session's summary, taken as the screen opens (`session::takeHomeSummary`), drawn over the menu on each frame (`session::drawHomeSummary`) until the next button press or touch (down or lifting), which does what it always does and redraws the screen without it (C1, C7). ~~All inside `#if LEXIRISE` (`scripts/lexipoint/test_home_sync.py`)~~ (superseded 2026-09-29, V8: no gate; `scripts/lexipoint/test_home_sync.py` checks the row sits just above Settings with its signed-off strings, and that only Sync Vocabulary joins WiFi for the mirror) |
| ~~`src/network/WebDAVHandler.cpp`~~ | ~~**Not gated:** `isProtectedPath` also refuses FAT short-name aliases of dot folders (`/LEXIRI~1`), via `web/HiddenPath.h`; `PROPFIND` now checks it too (CrossPoint listed hidden folders' contents)~~ **Superseded 2026-09-29:** WebDAV removed in v0.2 V8 (§4) |
| `src/network/CrossPointWebServer.cpp` | Register the `/lexirise` routes; collect the `Origin` header. **Not gated:** the file manager refuses any path with a hidden segment, typed, after SdFat's space/dot trimming, or as a FAT short name (`web/HiddenPath.h`), at all 8 entry points, and `isProtectedItemName` checks new names the same way. The web listing therefore always hides dot entries, ignoring the device's "Show hidden files" (which still applies to the on-device file browser): on purpose, since they couldn't be opened anyway. CrossPoint only checked the last segment as typed, so `/download?path=/.lexirise/config.ini` (and `/LEXIRI~1/config.ini`) served the key |
| `src/network/html/{Home,Files,Fonts,Settings}Page.html` | **Not gated:** one `<script src="/lexirise/nav.js">` line. Lexirise builds serve it and it adds the nav link (and, since M, the product's name: next row); other builds 404 it and nothing changes |
| `src/lexirise/web/LexiriseNav.js` (ours) | (M) Names the product on CrossPoint's web pages: "CrossPoint Reader" becomes "Lexipoint" in the page title (watched, since the file browser retitles) and in the `<h1>` headings. Lexirise builds only, as the script is |
| `lib/hal/HalGPIO.{h,cpp}` | Dev harness input overlay (`LEXIPOINT_DEV_HARNESS`). (V7b) `rawTouchLevel()`: the touch controller's interrupt line's level read straight from the pin (`input::TouchLine` learns which level is idle) |
| `platformio.ini` | (V8: `-DLEXIRISE=1`, `[env:x4pro-lexirise-off]` and the three cppcheck suppressions below are gone; the `[lexirise]` section stays for the TLS flags and the release number, and `test_lxctl` LexiriseIsAlwaysBuilt guards it.) A `[lexirise]` section (~~`-DLEXIRISE=1` plus~~ (V8) wolfSSL SHA-384/P-384, lexirise-client.md §1; V7c: `-DHAVE_SESSION_TICKET`, session resumption, and `-DWOLFSSL_TICKET_NONCE_MALLOC`, long ticket nonces for every wolfSSL user) referenced by the three X4 Pro envs; the harness flag in `[env:x4pro]` only. `test_lxctl.py` guards both. (P8) `[lexirise] release`, and `-lexi.${lexirise.release}` in the three X4 Pro envs' `CROSSPOINT_VERSION` (**not gated**, and easy to lose in an edit: `test_lxctl` ReleaseVersioning and `test_release_tag` guard them). (M) Only the X4 Pro envs are left (§2), `default_envs = x4pro`~~, plus `[env:x4pro-lexirise-off]`~~ (V8: gone). The ~~four~~ (V8) three envs extend one `[x4pro_board]` section (board, SDK profile, USB, SD) and add their version, log level and features; ~~`test_lxctl` checks the Lexirise-off env stays the release env minus `[lexirise]`~~ (V8: `test_lxctl` LexiriseIsAlwaysBuilt checks every env carries `[lexirise]`). **Not gated:** `USB_PRODUCT` `Lexipoint_X4_Pro` and `USB_MANUFACTURER` `Lexipoint` in `[x4pro_board]`. ~~`[base]`'s cppcheck flags suppress three style-only hints (`useStlAlgorithm`, `shadowFunction`, `variableScope`) for `src/lexirise/` only~~ (V8: gone; `src/lexirise/` passes without them, `../v0.2/slimming.md` §8) |
| `test/CMakeLists.txt` | The `lexirise_*` suites |
| `src/activities/reader/DictionaryWordSelectActivity.{h,cpp}` | (P2) `WordBox` gets `(line, token)`, counted exactly as `text::buildPageModel` counts lines; `setBook(dcLanguage)`. (P3) `performLookup()` asks Lexirise first (`lexiriseLookup()`: busy popup, placeholder definition screen or not-found popup) and falls back to StarDict with longest-prefix CJK candidates (`starDictLookup()`); `setInitialTouch(x, y)` selects the long-pressed word and looks it up on the first `loop()` (`lookup-flow.md` §5a). (P9) `setBook(BookLanguage)`; `pressOnWord()`; **not gated:** the word boxes, `isSelectableToken` and the hit rule moved to `WordBoxes.h` (same behaviour), so the long-press check can't disagree with them. (P5) `performLookup()` opens the Lexirise card (`openLexiriseCard()` when `lookup::asksLexirise`: `card::LiveSource` over a `card::ReaderPage` snapshot from `card::readerPageFor`, taken in `extractWords()` only when Lexirise is usable for the book, with the page model's line filter `text::forEachTextLine`; it sets CrossPoint's `snapshotIdx = -1` so word select's differential repaint can't run over the card's last frame: re-check that when taking a CrossPoint change), which hands back through a shared `LiveOutcome` (`card::afterCard` decides): Not found → the popup, no answer → `runStarDict()` on the next `loop()` (the StarDict half of `performLookup()`, split out unchanged). The P3 busy popup and placeholder are gone (`lookup-flow.md` §5b). (P6) `performLookup()` asks `lookup::lexiriseGate` first; `fallBack()` carries out `lookup::planFallback` (a notice, then `runStarDict()` once it's read, or "No dictionary set"), StarDict's title gets `· offline` (`starDictOffline`), a card that closed with unsent saves gets its notice (`AfterCard::UnsentSave`); `noticeString()`, `starDictSet()`. (P7) one settings copy per lookup; the tap is described once (`describeSelected(settings)`, only when Lexirise is asked or a language has its own dictionary) and its language picks the StarDict dictionary (`lookup::chooseStarDict`, Japanese/Chinese words only, with CrossPoint's as the fallback when the folder won't open; `runStarDict()` opens it through `lookup::prepareStarDict`, reopening when the choice changes, `dictOpened`); `touchEntry` (a long-press on a word opened it); a card that closed (`AfterCard::Closed`) or StarDict's definition (`answerClosed()`) goes where `card::closeStep` says (`finishClose(lookUpAt)`): the word a tap or long-press (P10: tap too) on the page under the card landed on, back to the reader for a touch-opened word select, else this page; after an unsent save's notice the close finishes (`afterPopup`, `card::AfterPopup` / `card::afterNotice`), and a touch-opened word select returns to the reader after any lookup notice. (V2) `setBook()` also takes the book's title and path; `openLexiriseCard()` gives the card `bookSaveTags(...)` (the user's tags, then the book's `book:<slug>` while "Tag with book title" is on, recording the slug's title in `book-tags.ini` the first time) instead of the user's tags alone. (V3) `openLexiriseCard()` also gives the card the book's deck (`deck::bookDeckFor`, `LiveSource::setBookDeck`). (V4) `openLexiriseCard()` also gives the card V2's book-tag record (`LiveSource::setBookTitles`) for Met before's book titles. (V5) `openLexiriseCard()` also gives the card the reader's ignored words (`LiveSource::setIgnoredWords` over `ignoredWordStore()`, C17). (V7a) and the vocab mirror (`LiveSource::setVocabMirror` over `vocab::vocabStore()`, C13). (V7b) `setSpine()`; `openLexiriseCard()` gives the card the page's analysis when one is kept for this page's text (`page::describePage`, `page::PageSentences` over `page::pageStore()`, `LiveSource::setSentenceSource`). (V7c) and the lemma cache (`LiveSource::setLookupCache` over `lookup::lookupCache()`, C21) (V9a) the card's `drawPage` also draws the page's marks (`page::readerMarks().draw`), and `openLexiriseCard()` gives the card A3 (`LiveSource::setStepsMarked(page::stepsMarked(...))`) (V9a R3-R4) `openLexiriseCard()` drops the card's kept marks (`dropCardMarks`); `setPageIndex()`, passed to the marks' draw so the card's page can be read as drawn (V6) `openLexiriseCard()` gives the card the reading session (`LiveSource::setSession` over `session::readingSession()`, C1, C7) and the page's text for the sentence preview's joins (`LiveSource::setPageText` over `text::pageTextOf(pageModel)`, C3) |
| `src/MappedInputManager.{h,cpp}` | `peekScreenLongPress(x, y)`: the long-press without consuming it, so the reader can check it's the lookup's before taking it. (P7) `peekSwipe(startX, startY, endX, endY)`: the swipe's two ends, so the card only takes swipes that start on it, clear of the edge gestures |
| `src/activities/reader/EpubReaderMenuActivity.{h,cpp}` | (P10) no Look Up row ~~with LEXIRISE~~ (V8: always) (`lookup-flow.md` §5g). (P9) the `LOOKUP_LANGUAGE` action and its row (after Look Up; P10: in its place), cycled in place like Night mode (`BookLanguageRow`), `setBookPath()`, `bookLanguageLabel()`. **Not gated:** the row slots are sized by the action count (`MAX_MENU_ITEMS`: each row is a different action) and `listCount()` / `buildScreen()` are capped at them, since the new row can make 17 rows (CrossPoint had exactly 16 slots for 16) (V9a) the `PAGE_MARKS` action and its row after Lookup language, toggled in place (`BookMarksRow`); `MAX_MENU_ITEMS` counts it (V9a R5) `buildMenuItems(..., pageMarks)`: the row only while the book can show marks (`readerMarks().settingsShow()`), the More panel's too |
| `src/SettingsList.h`, `src/CrossPointSettings.cpp` | (P10) Long-press Menu without Dictionary: a `DynamicEnum` over `lexipoint::long_press_menu` (a `static_assert` ties its values to `LP_MENU_*`), saved and loaded by hand (a stored Dictionary becomes Reader Menu, resaved). (v0.2 V8) `SettingInfo` (with `SettingType` and `SettingAction`) moved out of `SettingsActivity.h` into its own `src/SettingInfo.h`, so the settings model builds on the host: `test/settings_upgrade` loads a settings file from before V8 through the real `fromJson` / `toJson` |
| `src/activities/reader/ReaderUtils.h` | **Not gated:** `pageTurnZoneWidth(width)` (the outer thirds) pulled out of `detectTouchPageTurn`, so the long-press rule shares it. Same behaviour |
| `src/activities/reader/EpubReaderActivity.{h,cpp}` | (P2) pass the book's `<dc:language>` to word select. (P3) touch long-press → word select at that point, before link taps, owning the centre third only when CrossPoint's hold action is on (`lookup::lookupOwnsLongPress`); word select opens without a StarDict dictionary when Lexirise is set up (P6: `lookup::lexiriseConfigured`, settings only, not a rate limit's back-off), or when a language has its own (P7: `lookup::anyStarDict`). (P9) a long-press is taken only on a word (`pageWithWordAt`, under the render lock; the page it loaded goes to word select), logged `[LXLP]` for `lxctl reader-longpress`; (P10) off the text in the lookup's zone it's consumed and dropped (`lookup::longPressUse` → `Ignore`, `consumes()`, logged `ignored`); the book language comes with the book's menu choice (`lookup::bookLanguageFor`); the More panel's Lookup language row (`moreBookLanguage`). (V2) word select also gets the book's title and path (`setBook`), for its book tag. **Not gated:** `openReaderMenu()` builds the menu before starting it (to give it the book), `wordSelectOrigin()` (CrossPoint's margin sum, shared), and `openDictionaryWordSelect()`'s optional `page`. (V7b) `loop()` calls `lexiPages.step(...)` (`page::ReaderPages`, a member) ~~after the idle prewarm~~ (V7c: after the partial build's start, so a build it starts is busy first), with the debounced touch state: this page's and the next page's analysis over WiFi already up; `renderBook()` records the page it drew (`lexiDrawn`); (R8) `buildTickDue()` is `skipLoopDelay()`'s predicate, shared with the page analysis (not gated); word select gets the section (`setSpine`) (V9a) `renderContents()` draws the page's marks over the black-and-white page (`page::readerMarks().draw`, after `page->render`, before the status bar); the list menu's result and the More panel's `PAGE_MARKS` row set `readerMarks().setBookOn`; `moreBookMarks` (V9a R2-R4) `loadBook()` opens the marks for the book (`readerMarks().open(path, dcLanguage)`, before the first page); the list menu's callback reopens them when the book's Lookup language changed (`lookupLanguageAtMenu`, kept as the menu opens) and sets the Page marks row (`setBookOn`); the More panel's `LOOKUP_LANGUAGE` reopens them and its `PAGE_MARKS` row sets `setBookOn`; `onReaderMenuConfirm`'s `case PAGE_MARKS` (handled in place, like Lookup language); word select is given the page's index (`setPageIndex`) (V6) `loadBook()` starts the reading session (`session::readingSession().bookOpened()`, C1) |
| `src/activities/settings/SettingsActivity.{h,cpp}` | (P7) `SettingAction::Lexirise`, the System tab's `Lexirise` row (the device-only ACTION rows are appended here, not in `SettingsList.h`), and its dispatch to `LexiriseSettingsActivity` |
| `lib/I18n/translations/english.yaml` | **No marker (YAML):** the `STR_LEXI_*` keys, one block after `STR_DICT_LOW_MEMORY` (the notices, then `STR_LEXI_CARD_*` for every card word, P6). (P7) `STR_LEXIRISE` and `STR_LEXI_SET_*` for the settings screen. (P9) `STR_LEXI_CARD_NEXT_SENTENCE_FAILED`, `STR_LEXI_BOOK_LANGUAGE(_AUTO)`. (V2) `STR_LEXI_SET_TAG_BOOK`. (V3) `STR_LEXI_SET_DECK_PER_BOOK`. (M) `STR_LEXIPOINT` ("Lexipoint"); `STR_LEXI_SET_SAME_AS_CROSSPOINT` became `STR_LEXI_SET_SAME_AS_READER`, "Same as reader" (H13's default, until claritise signs it off). Other languages fall back to English. `scripts/lexipoint/test_card_strings.py` checks them against `CardStrings`; re-check when taking a CrossPoint change (gen_i18n.py rejects comments in the file). (V4) `STR_LEXI_CARD_DICTIONARY_FORM`. (V5) none: Ignore uses P6's `STR_LEXI_CARD_ACTIONS_1` and `STR_LEXI_CARD_ACTION_DONE_1`. (V7b) `STR_LEXI_SYNC_VOCABULARY`, `STR_LEXI_SYNCING_VOCABULARY`, `STR_LEXI_VOCABULARY_UP_TO_DATE`, `STR_LEXI_VOCABULARY_SYNCED(_ONE)`, `STR_LEXI_SYNC_NO_WIFI`, `STR_LEXI_SYNC_FAILED`, `STR_LEXI_SYNC_STOPPED`. (V9a) `STR_LEXI_SET_ON_THE_PAGE`, `STR_LEXI_SET_MARK_WORDS`, `STR_LEXI_SET_STEP_MARKED`, `STR_LEXI_SET_MARKED_WORDS`, `STR_LEXI_SET_EVERY_WORD`, `STR_LEXI_PAGE_MARKS`. (V6) `STR_LEXI_CARD_UNDO_IGNORE`, `STR_LEXI_CARD_SHORTER`, `STR_LEXI_CARD_LONGER`, `STR_LEXI_CARD_ALSO_READINGS`, `STR_LEXI_CARD_ALSO_COMMA`, `STR_LEXI_CARD_NO_LONGER_IGNORED`, `STR_LEXI_CARD_SD_CARD_FAILED`, `STR_LEXI_CARD_CANT_IGNORE`; `STR_LEXI_SESSION_COUNTS`, `STR_LEXI_SESSION_WORDS_JA(_ONE)`, `STR_LEXI_SESSION_WORDS_ZH(_ONE)` |
| `src/activities/boot_sleep/BootActivity.cpp`, `SleepActivity.cpp` | (M) The boot screen and the default sleep screen draw `STR_LEXIPOINT` under the logo, where CrossPoint draws `STR_CROSSPOINT` (D22). CrossPoint's logo stays |
| `src/activities/network/CrossPointWebServerActivity.cpp`, ~~`CalibreConnectActivity.cpp`~~ (removed in V8), `WifiSelectionActivity.cpp` | (M) The product's name on the network (D22): File Transfer's hotspot SSID is `config::kHotspotSsid` ("Lexipoint", CrossPoint's is "CrossPoint-Reader"), the mDNS hostname `config::kMdnsHostname` (`lexipoint.local`, CrossPoint's is `crosspoint.local`), and the name routers list, `config::kDhcpHostnamePrefix` + the MAC ("Lexipoint-AABBCCDDEEFF", CrossPoint's is "CrossPoint-Reader-…"). `test_rebrand.py` checks each file uses them. ~~The calibre plugin's name and its discovery reply are unchanged~~ (Superseded 2026-09-29: Calibre and its discovery reply removed in V8) |
| `src/network/OtaUpdater.cpp` | (P8) OTA reads **Lexipoint's** releases (`config::kReleasesLatestUrl`) and `isUpdateNewer()` compares `<base>-lexi.<n>` versions (`ota::isNewerRelease`, §6). (M) The asset it looks for is `lexipoint-<tag>-x4pro.bin` (~~`config::kReleaseAssetPrefix`~~ (V8 R7) named by `lexirise/ota/ReleaseAsset.h`, `kReleaseAssetPrefix` + tag + `kReleaseAssetSuffix` in a `kReleaseAssetNameBytes` buffer; a tag too long for it means no update; CrossPoint's is `crosspoint-<tag>-x4pro.bin`) |
| `.github/workflows/{ci,release,release_candidate}.yml` | (P8) CI also runs on Lexipoint's branches and adds a `lexipoint` job (the key-leak scan, the Lexipoint script tests); releases and candidates build the X4 Pro only, tagged `<base>-lexi.<n>` (§6). (M) Moved from `firmware/.github/` to the repo root, then deleted the same day: no CI, releases by `publish_release.py` (§6) |
| `bin/clang-format-fix`, `.githooks/pre-commit` | **Not gated (shell):** (M) both run from `firmware/` wherever they're called from, since the repo root also holds `docs/` and `tools/`. The hook re-stages files by their path from the repo root |
| `lib/GfxRenderer/GfxRenderer.cpp` | (P4) `applyPromotedRefresh`: a promoted refresh never weakens the one asked for (the stronger of the two), so the card's half refresh on dismiss can't turn the reader's due full refresh into a half one |
| `lib/GfxRenderer/FontCacheManager.{h,cpp}` | (P4) ~~with LEXIRISE~~ (V8: always) the prewarm scan takes 8 fonts (CrossPoint's 4): the card's expanded tabs draw with 7; a font past the cap is logged once per render (it loads glyph by glyph from SD) |
| `src/SdCardFontSystem.{h,cpp}` | (P4) `familyFontIdAt(renderer, pt)`: the loaded SD family at another point size (the card's 8/10/18 pt), via the manager's existing `loadFamilyExtraSize` |
| `src/lexirise/dev/DevHarness.cpp` (ours) | (P4) `LX:LEXI CARD ja\|zh [LOW] [KANA]` pushes the card bench (`KANA`: opens in kana, never saves the reading; `lxctl card-smoke`, and P7's `lxctl card-gestures`). (P7) `LX:LEXI SETTINGS` pushes the Lexirise settings screen, which logs its row count in dev builds (`lxctl settings-smoke`) |
| (P3+) `lib/I18n/translations/english.yaml` | `STR_LEXI_*` strings |

Anything that needs more than a few lines in a base file is a smell. Move the logic into
`src/lexirise/` and call it from there.

## 4. Taken from CrossPoint

**The base.** Lexipoint's own commits start on CrossPoint's tag `1.6.5rc`: commit `a1ceb63` in CrossPoint,
`24516d4c` here (`../reference/firmware-commit-map.md`). CrossPoint's history up to it is under `firmware/`,
so `git log` and `git blame` work on base files. `[crosspoint] version` in `platformio.ini` (`1.6.5`) is the
CrossPoint version the release numbers build on (§6).

There are no scheduled syncs and no rebases (D21). A CrossPoint change is taken only when Lexipoint needs it,
one commit at a time, and recorded below:

1. Add CrossPoint as a remote once: `git remote add crosspoint https://github.com/crosspoint-reader/crosspoint-reader.git`,
   then `git fetch crosspoint`.
2. From the repo root: `git cherry-pick -X subtree=firmware <sha>`. CrossPoint's paths start at its root,
   and ours at `firmware/`.
3. Check it as any change to a base file: the host suite and the `x4pro` and `x4pro-gh_release` builds
   ~~and `x4pro-lexirise-off`~~ (removed in v0.2 V8), from `firmware/`. Re-check the hook rows in §3 that say so.
4. Add a row here, in the same commit or the next.

| Commit here | CrossPoint commit | Why |
|---|---|---|
| — | — | — |

None has been taken yet. The fork-era table (§5 before M) was empty too.

The v0.2 slimming (`../v0.2/slimming.md`) records what it removes from the base here as well, so what was
dropped, and at which commit, stays findable.

**Removed from the base (v0.2 V8, `../v0.2/slimming.md` §8).** Each row is one slimming step, built on `lexi/V8`
as its own `wip(V8): step <n>` commit and landed in V8's squashed commit on `main`. To see a removed file, check
out `main` before V8's landing (`05328117`).

| Step | Removed | Kept, and why |
|---|---|---|
| 2. Code for devices without touch | Every build-time branch for another board (`FREEINK_DEVICE_*` other than the X4 Pro, `FREEINK_MCU_C3`; `FREEINK_CAP_TOUCH`, `_USB_MSC`, `_FRONTLIGHT`, `_WARMLIGHT` now unconditional) in `src/` and `lib/hal`; the X3/X4 detection, `deviceIsX3()`, the X3 fuel-gauge reads and the C3 battery latch (`HalGPIO`, `HalDisplay`, `HalPowerManager`), the Paper Mono PMIC paths; `FirmwareBoardTag`'s other boards (an image must say `x4pro`); the button legend (`drawButtonHints`, `drawSideButtonHints`, `drawHintLabel` in every theme, `MappedInputManager::mapLabels` / `mapDirectionalLabels`, and every call); the tilt page turn (`HalTiltSensor`, the `Imu` library, `tiltPageTurn`; the X4 Pro has no IMU); the settings only button boards had (`frontButtonFollowOrientation`, `backShortToFileBrowser`, `fadingFix` with `GfxRenderer::setFadingFix`); (R1) CrossPoint's guide to recovering a bricked ESP32-C3 Xteink with an SPI flash programmer (`firmware/docs/fix-bricked-xteink.md` and `firmware/docs/images/spiflash/`: its photos and a 7.4 MB C3 flash backup), which is for the C3 boards only (the backup image is a C3's); the X4 Pro is flashed and recovered over USB (`../user-guide.md` §1), by the SD-card firmware update, or by the recovery boot (Down + Power); (R3) the C3 X4's display and SPI pin macros (`EPD_*`, `SPI_MISO` in `HalGPIO.h`: the SDK takes the display pins from `BoardConfig`), `DictionaryWordSelectActivity`'s unused `dictOpenAttempted`; (R7) the Files page's X3 profile and its Target Device row (the X4 Pro's 480×800 is the one profile), `OtaUpdater`'s C3 `-x3-x4` asset suffix (the asset is named by `lexirise/ota/ReleaseAsset.h`), `getScreenSafeArea`'s unused side-hint parameter; (R8) its legend band (it is the whole screen) and `UITheme`'s run-time metrics copy; (R10) the C3's RISC-V panic-frame branch (`HalSystem.cpp`: `__riscv` is now a build error) and the paths for boards without PSRAM (`HalPowerManager.h`'s 10 MHz low-power clock, two memory logs: `BOARD_HAS_PSRAM` is now required); (R5) `[base]`'s C3 board line in `platformio.ini` (every env takes `[x4pro_board]`'s) and the C3-only ROM-libc link hook (`scripts/patch_arduino_rom_libc.py`), `update_hyphenation.sh`'s other languages | `RequiresTouch.cpp`'s `#error` (touch at build time); `BoardConfig` itself (the SDK's); the runtime `hasTouch()` checks (a touch controller that fails to start still leaves the side buttons working); `ThemeMetrics::buttonHintsHeight` (~~0 with touch~~ 0 always: since R7, and since R8 in the themes' tables; layout arithmetic only) and `sideButtonHintsWidth` (the dictionary screen's landscape gutter); (R3) USB Drive's build check, now a `static_assert` in `HalStorage.cpp` (MSC on, an SDMMC card) |
| 3. Ecosystems | KOReader sync (the sync client, its credential store and document ids, the sync and settings screens, the reader menu's Sync Progress row, the long-press choice: a stored one loads as Disabled), OPDS (`lib/OpdsParser`, the browser, the server list and its store, the home menu row, `/api/opds` and the web settings page's card, `opdsDownloadFolder` / `opdsFilenameFormat`), Calibre connect (its screen, File Transfer's row, the web server's UDP discovery reply), WebDAV (`WebDAVHandler`, its request headers); the strings only they used, in every language | The position mapping bookmarks use (`ProgressMapper`, `ChapterXPathResolver`, `CrossPointPosition`), moved from `lib/KOReaderSync` to `lib/ReadingPosition`, and its test (`test/xpath_resolver`); the WebSocket upload; `HttpDownloader` (OTA); the files the removed features left on an SD card (`/.crosspoint/` KOReader and OPDS stores) are no longer read |
| 4a. The UI in English only | The 33 other UI languages (`lib/I18n/translations/*.yaml` but `english.yaml`), the Language screen (`LanguageSelectActivity`) and `language` setting, the keyboard layouts that followed the UI language (`KeyboardLayoutSet`, `KeyboardLayoutsActivity`, `keyboardLayouts`, the keyboard's language key), the keyboard's URL mode (only OPDS and KOReader server addresses used it), `gen_i18n.py`'s V1 language migration table, `firmware/docs/translators.md`; (R3) `I18n`'s language API (`setLanguage`, `getLanguageName`, `languageFromCode`, `getCharacterSet`) and the tables `gen_i18n.py` made for it (`LANGUAGE_CODES`, `LANGUAGE_NAMES`, `CHARACTER_SETS`, `getLanguageCount`, `SORTED_LANGUAGE_INDICES`) | `lib/I18n` and `tr()` (every string keeps one home: `firmware/docs/i18n.md`); the keyboard's English QWERTY layout (WiFi passwords, the Lexirise key and tags, library search) |
| 4b. Hyphenation down to English | The hyphenation tries and hyphenators of de, es, fi, fr, it, pl, ru, sv and uk (`lib/Epub/Epub/hyphenation/generated/`), and their evaluation tests and word lists (`test/hyphenation_eval/resources/`) | English's trie, the Liang code and the Hyphenation setting (books tagged English: the hyphenator follows the book's language); the Cyrillic letter helpers (`isAlphabetic` still uses them) |
| 4c. Right-to-left text | `lib/MiniBidi` (the UAX#9 bidi algorithm, Arabic shaping, `BidiUtils`) and everything that only called it: the reader's per-word and per-paragraph RTL detection and visual word reordering (`ParsedText`), the per-word base direction (`TextBlock`), `GfxRenderer`'s visual-text pass, its RTL-mark widths and the `BidiBaseDir` parameter of `drawText` / `getTextWidth` / `drawCenteredText`, the TXT reader's RTL line alignment, the keyboard field's RTL cursor; `test/minibidi_arabic` | A paragraph whose CSS or HTML says `direction: rtl` ~~is still laid out right to left, word by word~~ (superseded 2026-09-29, V8 R5) keeps its words left to right, at the right margin (centred, or at the left for an explicit `text-align: left`) as MiniBidi placed a line with no Hebrew or Arabic (`BlockStyle::isRtl`, `ParsedText::extractLine`); ~~no cache format change~~ (corrected 2026-09-29, V8 R1: the section cache's version went to 47, since the measurements changed, `firmware/docs/file-formats.md`); combining marks; the SD font's advance table's extra-text parameter (now unused) |
| 5a. The front-button remap | `ButtonRemapActivity`, its Settings row and `SettingAction`, the four `frontButton*` settings and their check, `FRONT_BUTTON_LAYOUT` / `FRONT_BUTTON_HARDWARE`, `MappedInputManager::getPressedFrontButton` | Back / Confirm / Left / Right, now mapped straight to `HalGPIO::BTN_*` (the X4 Pro's touch controller reports Back and Confirm) |
| 5b. Font download | `FontDownloadActivity` (the catalog download from CrossPoint's font releases), Settings → Reader's Manage Fonts row and its `SettingAction`; the strings the remap (5a) and the download only used | `FontInstaller` and the web Fonts page (uploading `.cpfont` families); `HttpDownloader` (OTA); the SD font scripts (`lib/EpdFont/scripts`, `scripts/generate-font-manifest.py`); the font picker (C22 decides it: `../v0.2/slimming.md` §7 S1) |
| 5c. The themes but Lyra (claritise, 2026-09-29: "Keep Lyra") | `Lyra3CoversTheme`, `RoundedRaffTheme`, Classic as a choice, the UI Theme setting (`uiTheme`, `UI_THEME`, its device and web row, its strings), `UITheme::setTheme` / `reload` and the Settings screen's live theme switch | `BaseTheme` (Lyra's base class and its shared drawing) and `LyraTheme`; ~~the metrics fields the removed themes used (layout options Lyra sets)~~ (R2: the two only the removed themes turned on, `tabPillFullSlot` and `listTitleBold`, and their branches went too) |
| 7. The `LEXIRISE` gate | `-DLEXIRISE=1` and every `#if LEXIRISE` (the code in them kept, the `#else` halves gone: CrossPoint's OTA source and version check, the Look Up menu row and its `MenuAction::DICTIONARY`, the boot screen's CrossPoint name, the harness's "built without LEXIRISE" answer), the `x4pro-lexirise-off` env and its line in the uniform gate, the host suites' `LEXIRISE=1`, the bare `// LEXIPOINT` markers (the ones that only said "ours"), `[base]`'s three cppcheck suppressions for `src/lexirise/` (the code rewritten to meet them) | The `[lexirise]` section (the TLS flags, the release number); the markers that say why a base file was edited |

## 5. (Merged into §4)

M merged the fork-era "Upstream sync" (§4) and "Cherry-picks and divergences" (§5) into §4.

## 6. Releases, OTA updates and checks

- **Releases are on `claritise/lexipoint`.** Each one carries one asset, `lexipoint-<tag>-x4pro.bin`,
  built from `x4pro-gh_release`. The name comes from one prefix, `config::kReleaseAssetPrefix`
  (`src/lexirise/LexiriseConfig.h`, used by the OTA hook), which `scripts/lexipoint/release_tag.py`
  `ASSET_PREFIX` and `publish_release.py` share (`test_publish_release.py` pins them). No release exists yet.
- **OTA** (`OtaUpdater.cpp` hook, §3; the asset's name: `lexirise/ota/ReleaseAsset.h`): the device reads `config::kReleasesLatestUrl`
  (`https://api.github.com/repos/claritise/lexipoint/releases/latest`; prereleases aren't "latest"). A
  release is offered only when it's a Lexipoint version newer than the running one (`ota::isNewerRelease`:
  the base version, then `n`; a release candidate updates to its release). CrossPoint's releases never are,
  and CrossPoint's updater would install stock CrossPoint, which is why the hook exists. The SD-card
  firmware update (`SdFirmwareUpdateActivity`) needs no change: it installs whatever file the user copies.
- **Versions are unchanged by M** (H12's default): `CROSSPOINT_VERSION` is `<base>-lexi.<n>` (e.g.
  `1.6.5-lexi.1`), where `<base>` is `[crosspoint] version` and `n` is `[lexirise] release`. If claritise
  picks Lexipoint's own scheme, that's its own phase before the first release (`standalone-repo.md` §9).
- **Releasing, from this Mac** (there is no CI; claritise, 2026-09-26: "we dont need it"): bump
  `[lexirise] release`, merge and push `main`, then from `firmware/`:
  `python3 scripts/lexipoint/publish_release.py --dry-run`, and without `--dry-run` to publish (with
  `--prerelease` for a release candidate, built from `x4pro-gh_release_rc`, also from `main`). Before building
  it refuses: a dirty tree; a HEAD that isn't a commit on GitHub's `main`; an `origin` that isn't the repo devices read;
  a release or a tag of that name already on GitHub (a release would keep that tag's source, not HEAD's); any
  `PLATFORMIO_*` variable or a `platformio.local.ini` (either would reach the build); a key-shaped string
  (`keyscan.py`); and a tag `release_tag.check` rejects (not
  platformio.ini's, over **27** characters, which is the updater's 48-byte asset name (`lexirise/ota/ReleaseAsset.h`, `kReleaseAssetNameBytes`) less `lexipoint-`,
  `-x4pro.bin` and the terminator, or not newer than every published release). Then it builds clean, checks
  nothing moved meanwhile, creates the release **with** the asset and its tag at HEAD in one step, and checks
  that a full release is GitHub's latest with its asset. Tags are unique: to re-cut a number, delete that
  release and its tag first. Never promote a prerelease: publish a full release. Only claritise publishes
  releases, and only from a commit that passed the uniform gate: the build uses this Mac's `pio`, so it's the
  toolchain the gate ran with.
- **Checks run here, not on GitHub:** everything CI used to run is the uniform gate (`01-build-order.md`):
  clang-format, cppcheck on `x4pro`, builds of `x4pro` and `x4pro-gh_release` (~~and `x4pro-lexirise-off`~~, removed in
  v0.2 V8), the host
  tests, the key scan over the whole repo, and every `scripts/lexipoint/test_*.py`. The repo has no
  `.github/` (M moved CrossPoint's workflows to the root; they were deleted the same day).
- **Formatting:** our code follows `firmware/.clang-format`. `firmware/bin/clang-format-fix` fixes it, and
  the pre-commit hook (`firmware/.githooks`, §1a) runs it once enabled.
- **Release notes** say which version each release is built on, and link the user setup guide.

**As built (P8):** history, written when Lexipoint was the fork `claritise/crosspoint-reader`. M moved
releases to `claritise/lexipoint`, renamed the asset to `lexipoint-<tag>-x4pro.bin`, raised the tag limit to
27, moved CI to the root and put the key scan over the whole repo; the same day CI was dropped and releases
became `publish_release.py` (the bullets above are current).
- `platformio.ini` `[lexirise] release = <n>` (bump per release; back to 1 **only when `[crosspoint]
  version` changes**: a rebase that keeps the upstream version keeps counting, or devices on a higher `n`
  would never be offered the new release). `x4pro-gh_release` builds `CROSSPOINT_VERSION = <upstream>-lexi.<n>`, its `_rc` twin
  `<upstream>-lexi.<n>-rc+<hash>`, and the dev env `<upstream>-lexi.<n>-x4pro`. `LEXIPOINT_VERSION` (0.1.0)
  stays the product version in the Lexirise User-Agent.
- **Releasing:** tag `<upstream>-lexi.<n>` (a prerelease: `…-rc`; `release_tag.py expected` prints it) and
  publish a GitHub release on the fork. `release.yml` runs `scripts/lexipoint/release_tag.py check` (tested):
  the tag must be exactly what `platformio.ini` says (no leading `v`: devices look for
  `crosspoint-<tag>-x4pro.bin`), at most 26 characters (the updater's buffers), from an `X.Y.Z` upstream
  version and `1 ≤ n ≤ 1,000,000`, and newer than the highest-versioned release the fork has published (the
  tag list comes from `gh release list`; if that fails, the step fails). After the upload, a full release
  must be GitHub's `/releases/latest` with its asset (not unticked as latest, not on an older commit), or
  the workflow fails. CI also builds `x4pro-gh_release` on every push, so a release-only break shows up
  before a release. Tags are unique, so a release number has one `-rc`: to re-cut it, delete that
  prerelease **and its tag** first. **Never promote a prerelease** (unticking "pre-release" sends GitHub's `released`
  event, which `release.yml` doesn't run on, and leaves an `-rc` tag as latest, which devices never take):
  publish a new full release tagged `<upstream>-lexi.<n>`. Then it builds `x4pro-gh_release` and
  attaches the asset. Only claritise publishes releases. Checklist: run `release_tag.py check <tag>` locally
  **before** publishing (a release that fails the workflow is already GitHub's latest, with no firmware:
  devices then see no update, so delete it); run `keyscan.py` in the docs repo too (it has no CI); until the first release exists, a device's *Check for updates* shows an error (GitHub's
  `/releases/latest` is 404), not "no update".
- **OTA** (`OtaUpdater.cpp` hook): the fork's `/releases/latest` (prereleases aren't "latest"); a release is
  offered only when it's a Lexipoint version newer than the running one (`ota::isNewerRelease`: upstream
  version, then `n`; a release candidate updates to its release). Upstream's releases never are.
- **CI** (`ci.yml`): runs on pushes to `lexipoint` too (the fork's Actions must be enabled). The
  `unit-tests` job already builds every `lexirise_*` suite and the card goldens; ~~`x4c` in the build matrix is
  the LEXIRISE-off parity build~~ (superseded: M left only the X4 Pro envs, and V8 removed the Lexirise-off build); the new `lexipoint` job (with submodules: `test_lxctl` reads the SDK's
  edge bands) runs `scripts/lexipoint/keyscan.py` and the script tests (`test_gen_bench_fixtures` skips
  there: it needs this docs repo). It runs every `scripts/lexipoint/test_*.py` (`unittest discover`). `cppcheck` runs on the `default` env, which doesn't build Lexirise.
- **Rebase (P8 gate):** checked 2026-09-25: the newest upstream tag (`1.6.5rc`) is already in `lexipoint`,
  and upstream `master`'s two newer commits are empty merges, so there was nothing to rebase onto.
