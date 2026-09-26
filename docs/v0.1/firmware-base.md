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
  tools/                     tools that run on a computer (the manga converter, `../v0.2/manga.md`)
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
| `x4pro-lexirise-off` | The Lexirise-off build: `x4pro-gh_release` without `[lexirise]`, so every `LEXIRISE` hook in a base file must compile out. The uniform gate builds it; nobody flashes it. It replaced `x4c` in M |

- **Touch is required at compile time.** `firmware/src/lexirise/RequiresTouch.cpp` stops the build with an
  `#error` on a device without `FREEINK_CAP_TOUCH`. A touch device the SDK supports would pass on its own,
  but none is built or tested now (D20).
- `freeink-sdk` is a **git submodule** at `firmware/freeink-sdk` (`https://github.com/Free-Ink/freeink-sdk.git`).
  It holds the HAL, the display and touch drivers, and `SecureHttpClient`. We **don't fork the SDK**.
- Host tests, from `firmware/`: `cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test`
  (gtest via FetchContent, see `test/README`). Anything we write that is pure logic (sentence
  building, offset mapping, response parsing, config parsing) gets a suite here.
- The build flag **`-DLEXIRISE=1`** gates the Lexirise hooks. With it off, the firmware behaves like
  CrossPoint except for deliberate, ungated changes (§3): the file manager's and WebDAV's hidden-path
  guard, the nav script tag that 404s harmlessly, and the USB device name.

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
  text/Kana, Utf8Prefix, Utf8Units    romaji → kana; UTF-8 / UTF-16 helpers, pure
  card/                               the card (P4-P6): CardMetrics, CardModel, DisplayList, CardLayout, TextRuns,
                                      ShapeGeometry, CardController, CardInput, ShownTargets, CardSession, WordSelectFlow,
                                      CardSource (BenchSource, LiveSource), LiveWord, ReaderScene, CardFrame (pure);
                                      CardPainter, ReaderPageFor, CardStringsI18n, LexiriseCardActivity (device)
  util/Timing.h                       wrap-safe millis() comparisons
  settings/SettingsScreen.{h,cpp}     the device Settings → System → Lexirise rows and edits (P7), pure
  settings/LexiriseSettingsActivity   that screen (P7, device: CrossPoint's UiListActivity)
  ota/ReleaseVersion                  `<base>-lexi.<n>` versions, which release OTA offers (P8), pure
  lookup/StarDictChoice.h             each language's own offline dictionary (P7), pure
  settings/SafeFile                   crash-safe replace and recovery of a small SD file (config.ini, books.ini; P9;
                                      book-tags.ini, V2)
  settings/BookLanguages              each book's lookup language, books.ini, the reader menu's row (P9), pure + store
  settings/LanguageNames.h            a language's name on screen (settings groups, the menu row; P9)
  settings/LongPressMenu.h            Long-press Menu's choices without Dictionary (P10), pure
  text/BookSlug                       a book's tag, `book:<slug>` of its title (V2, v0.2 C2), pure
  settings/BookTags                   the tags a save carries, and each book tag's title, book-tags.ini (V2), pure + store
src/activities/reader/WordBoxes.h     (CrossPoint's folder, ours) word select's word boxes and hit rule, shared with the
                                      reader's long-press check (P9), pure
test/lexirise_*/                host gtest suites
scripts/lexipoint/lxctl.py      host side of the dev harness (+ test_lxctl.py)
scripts/lexipoint/websmoke.py   read-only smoke test of the web surface against a device (+ test_websmoke.py)
scripts/lexipoint/keyscan.py    the key-leak scan, uniform gate 5 (+ test_keyscan.py)
scripts/lexipoint/publish_release.py  builds and publishes a release, §6 (+ test_publish_release.py)
test/lexirise_fakes/            fakes shared by the suites (card, connection, WiFi, clock)
```

**Base files we touch.** Each hook carries a `// LEXIPOINT` comment, so `git grep LEXIPOINT`
lists every one. Hooks are wrapped in `#if LEXIRISE` unless noted:

| File | Hook |
|---|---|
| `src/main.cpp` | Load the settings store at boot; `service().tick()` in the loop (queued key check, idle TLS close, WiFi idle teardown). Dev harness hooks (`LEXIPOINT_DEV_HARNESS`) |
| `src/activities/ActivityManager.cpp` | Tell `LexiriseService` whether a reader activity is still on screen or under it, **before** the next activity's `onEnter` (push/replace, the immediate replace) and after a pop out of reading, so WiFi Lexipoint owns is given back first (`offline-and-errors.md` §5). Relies on nothing that uses WiFi being pushed over the reader (KOSync replaces it): re-check when taking a CrossPoint change (§4) |
| `src/network/WebDAVHandler.cpp` | **Not gated:** `isProtectedPath` also refuses FAT short-name aliases of dot folders (`/LEXIRI~1`), via `web/HiddenPath.h`; `PROPFIND` now checks it too (CrossPoint listed hidden folders' contents) |
| `src/network/CrossPointWebServer.cpp` | Register the `/lexirise` routes; collect the `Origin` header. **Not gated:** the file manager refuses any path with a hidden segment, typed, after SdFat's space/dot trimming, or as a FAT short name (`web/HiddenPath.h`), at all 8 entry points, and `isProtectedItemName` checks new names the same way. The web listing therefore always hides dot entries, ignoring the device's "Show hidden files" (which still applies to the on-device file browser): on purpose, since they couldn't be opened anyway. CrossPoint only checked the last segment as typed, so `/download?path=/.lexirise/config.ini` (and `/LEXIRI~1/config.ini`) served the key |
| `src/network/html/{Home,Files,Fonts,Settings}Page.html` | **Not gated:** one `<script src="/lexirise/nav.js">` line. Lexirise builds serve it and it adds the nav link (and, since M, the product's name: next row); other builds 404 it and nothing changes |
| `src/lexirise/web/LexiriseNav.js` (ours) | (M) Names the product on CrossPoint's web pages: "CrossPoint Reader" becomes "Lexipoint" in the page title (watched, since the file browser retitles) and in the `<h1>` headings. Lexirise builds only, as the script is |
| `lib/hal/HalGPIO.{h,cpp}` | Dev harness input overlay (`LEXIPOINT_DEV_HARNESS`) |
| `platformio.ini` | A `[lexirise]` section (`-DLEXIRISE=1` plus wolfSSL SHA-384/P-384, lexirise-client.md §1) referenced by the three X4 Pro envs; the harness flag in `[env:x4pro]` only. `test_lxctl.py` guards both. (P8) `[lexirise] release`, and `-lexi.${lexirise.release}` in the three X4 Pro envs' `CROSSPOINT_VERSION` (**not gated**, and easy to lose in an edit: `test_lxctl` ReleaseVersioning and `test_release_tag` guard them). (M) Only the X4 Pro envs are left (§2), `default_envs = x4pro`, plus `[env:x4pro-lexirise-off]`. The four envs extend one `[x4pro_board]` section (board, SDK profile, USB, SD) and add their version, log level and features; `test_lxctl` checks the Lexirise-off env stays the release env minus `[lexirise]`. **Not gated:** `USB_PRODUCT` `Lexipoint_X4_Pro` and `USB_MANUFACTURER` `Lexipoint` in `[x4pro_board]`. `[base]`'s cppcheck flags suppress three style-only hints (`useStlAlgorithm`, `shadowFunction`, `variableScope`) for `src/lexirise/` only |
| `test/CMakeLists.txt` | The `lexirise_*` suites |
| `src/activities/reader/DictionaryWordSelectActivity.{h,cpp}` | (P2) `WordBox` gets `(line, token)`, counted exactly as `text::buildPageModel` counts lines; `setBook(dcLanguage)`. (P3) `performLookup()` asks Lexirise first (`lexiriseLookup()`: busy popup, placeholder definition screen or not-found popup) and falls back to StarDict with longest-prefix CJK candidates (`starDictLookup()`); `setInitialTouch(x, y)` selects the long-pressed word and looks it up on the first `loop()` (`lookup-flow.md` §5a). (P9) `setBook(BookLanguage)`; `pressOnWord()`; **not gated:** the word boxes, `isSelectableToken` and the hit rule moved to `WordBoxes.h` (same behaviour), so the long-press check can't disagree with them. (P5) `performLookup()` opens the Lexirise card (`openLexiriseCard()` when `lookup::asksLexirise`: `card::LiveSource` over a `card::ReaderPage` snapshot from `card::readerPageFor`, taken in `extractWords()` only when Lexirise is usable for the book, with the page model's line filter `text::forEachTextLine`; it sets CrossPoint's `snapshotIdx = -1` so word select's differential repaint can't run over the card's last frame: re-check that when taking a CrossPoint change), which hands back through a shared `LiveOutcome` (`card::afterCard` decides): Not found → the popup, no answer → `runStarDict()` on the next `loop()` (the StarDict half of `performLookup()`, split out unchanged). The P3 busy popup and placeholder are gone (`lookup-flow.md` §5b). (P6) `performLookup()` asks `lookup::lexiriseGate` first; `fallBack()` carries out `lookup::planFallback` (a notice, then `runStarDict()` once it's read, or "No dictionary set"), StarDict's title gets `· offline` (`starDictOffline`), a card that closed with unsent saves gets its notice (`AfterCard::UnsentSave`); `noticeString()`, `starDictSet()`. (P7) one settings copy per lookup; the tap is described once (`describeSelected(settings)`, only when Lexirise is asked or a language has its own dictionary) and its language picks the StarDict dictionary (`lookup::chooseStarDict`, Japanese/Chinese words only, with CrossPoint's as the fallback when the folder won't open; `runStarDict()` opens it through `lookup::prepareStarDict`, reopening when the choice changes, `dictOpened`); `touchEntry` (a long-press on a word opened it); a card that closed (`AfterCard::Closed`) or StarDict's definition (`answerClosed()`) goes where `card::closeStep` says (`finishClose(lookUpAt)`): the word a tap or long-press (P10: tap too) on the page under the card landed on, back to the reader for a touch-opened word select, else this page; after an unsent save's notice the close finishes (`afterPopup`, `card::AfterPopup` / `card::afterNotice`), and a touch-opened word select returns to the reader after any lookup notice. (V2) `setBook()` also takes the book's title and path; `openLexiriseCard()` gives the card `bookSaveTags(...)` (the user's tags, then the book's `book:<slug>` while "Tag with book title" is on, recording the slug's title in `book-tags.ini` the first time) instead of the user's tags alone |
| `src/MappedInputManager.{h,cpp}` | `peekScreenLongPress(x, y)`: the long-press without consuming it, so the reader can check it's the lookup's before taking it. (P7) `peekSwipe(startX, startY, endX, endY)`: the swipe's two ends, so the card only takes swipes that start on it, clear of the edge gestures |
| `src/activities/reader/EpubReaderMenuActivity.{h,cpp}` | (P10) no Look Up row with LEXIRISE (`lookup-flow.md` §5g). (P9) the `LOOKUP_LANGUAGE` action and its row (after Look Up; P10: in its place), cycled in place like Night mode (`BookLanguageRow`), `setBookPath()`, `bookLanguageLabel()`. **Not gated:** the row slots are sized by the action count (`MAX_MENU_ITEMS`: each row is a different action) and `listCount()` / `buildScreen()` are capped at them, since the new row can make 17 rows (CrossPoint had exactly 16 slots for 16) |
| `src/SettingsList.h`, `src/CrossPointSettings.cpp` | (P10) Long-press Menu without Dictionary: a `DynamicEnum` over `lexipoint::long_press_menu` (a `static_assert` ties its values to `LP_MENU_*`), saved and loaded by hand (a stored Dictionary becomes Reader Menu, resaved) |
| `src/activities/reader/ReaderUtils.h` | **Not gated:** `pageTurnZoneWidth(width)` (the outer thirds) pulled out of `detectTouchPageTurn`, so the long-press rule shares it. Same behaviour |
| `src/activities/reader/EpubReaderActivity.{h,cpp}` | (P2) pass the book's `<dc:language>` to word select. (P3) touch long-press → word select at that point, before link taps, owning the centre third only when CrossPoint's hold action is on (`lookup::lookupOwnsLongPress`); word select opens without a StarDict dictionary when Lexirise is set up (P6: `lookup::lexiriseConfigured`, settings only, not a rate limit's back-off), or when a language has its own (P7: `lookup::anyStarDict`). (P9) a long-press is taken only on a word (`pageWithWordAt`, under the render lock; the page it loaded goes to word select), logged `[LXLP]` for `lxctl reader-longpress`; (P10) off the text in the lookup's zone it's consumed and dropped (`lookup::longPressUse` → `Ignore`, `consumes()`, logged `ignored`); the book language comes with the book's menu choice (`lookup::bookLanguageFor`); the More panel's Lookup language row (`moreBookLanguage`). (V2) word select also gets the book's title and path (`setBook`), for its book tag. **Not gated:** `openReaderMenu()` builds the menu before starting it (to give it the book), `wordSelectOrigin()` (CrossPoint's margin sum, shared), and `openDictionaryWordSelect()`'s optional `page` |
| `src/activities/settings/SettingsActivity.{h,cpp}` | (P7) `SettingAction::Lexirise`, the System tab's `Lexirise` row (the device-only ACTION rows are appended here, not in `SettingsList.h`), and its dispatch to `LexiriseSettingsActivity` |
| `lib/I18n/translations/english.yaml` | **No marker (YAML):** the `STR_LEXI_*` keys, one block after `STR_DICT_LOW_MEMORY` (the notices, then `STR_LEXI_CARD_*` for every card word, P6). (P7) `STR_LEXIRISE` and `STR_LEXI_SET_*` for the settings screen. (P9) `STR_LEXI_CARD_NEXT_SENTENCE_FAILED`, `STR_LEXI_BOOK_LANGUAGE(_AUTO)`. (V2) `STR_LEXI_SET_TAG_BOOK`. (M) `STR_LEXIPOINT` ("Lexipoint"); `STR_LEXI_SET_SAME_AS_CROSSPOINT` became `STR_LEXI_SET_SAME_AS_READER`, "Same as reader" (H13's default, until claritise signs it off). Other languages fall back to English. `scripts/lexipoint/test_card_strings.py` checks them against `CardStrings`; re-check when taking a CrossPoint change (gen_i18n.py rejects comments in the file) |
| `src/activities/boot_sleep/BootActivity.cpp`, `SleepActivity.cpp` | (M) The boot screen and the default sleep screen draw `STR_LEXIPOINT` under the logo, where CrossPoint draws `STR_CROSSPOINT` (D22). CrossPoint's logo stays |
| `src/activities/network/CrossPointWebServerActivity.cpp`, `CalibreConnectActivity.cpp`, `WifiSelectionActivity.cpp` | (M) The product's name on the network (D22): File Transfer's hotspot SSID is `config::kHotspotSsid` ("Lexipoint", CrossPoint's is "CrossPoint-Reader"), the mDNS hostname `config::kMdnsHostname` (`lexipoint.local`, CrossPoint's is `crosspoint.local`), and the name routers list, `config::kDhcpHostnamePrefix` + the MAC ("Lexipoint-AABBCCDDEEFF", CrossPoint's is "CrossPoint-Reader-…"). `test_rebrand.py` checks each file uses them. The calibre plugin's name and its discovery reply are unchanged |
| `src/network/OtaUpdater.cpp` | (P8) OTA reads **Lexipoint's** releases (`config::kReleasesLatestUrl`) and `isUpdateNewer()` compares `<base>-lexi.<n>` versions (`ota::isNewerRelease`, §6). (M) The asset it looks for is `lexipoint-<tag>-x4pro.bin` (`config::kReleaseAssetPrefix`; CrossPoint's is `crosspoint-<tag>-x4pro.bin`) |
| `.github/workflows/{ci,release,release_candidate}.yml` | (P8) CI also runs on Lexipoint's branches and adds a `lexipoint` job (the key-leak scan, the Lexipoint script tests); releases and candidates build the X4 Pro only, tagged `<base>-lexi.<n>` (§6). (M) Moved from `firmware/.github/` to the repo root, then deleted the same day: no CI, releases by `publish_release.py` (§6) |
| `bin/clang-format-fix`, `.githooks/pre-commit` | **Not gated (shell):** (M) both run from `firmware/` wherever they're called from, since the repo root also holds `docs/` and `tools/`. The hook re-stages files by their path from the repo root |
| `lib/GfxRenderer/GfxRenderer.cpp` | (P4) `applyPromotedRefresh`: a promoted refresh never weakens the one asked for (the stronger of the two), so the card's half refresh on dismiss can't turn the reader's due full refresh into a half one |
| `lib/GfxRenderer/FontCacheManager.{h,cpp}` | (P4) with LEXIRISE the prewarm scan takes 8 fonts (CrossPoint's 4): the card's expanded tabs draw with 7; a font past the cap is logged once per render (it loads glyph by glyph from SD) |
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
3. Check it as any change to a base file: the host suite and the `x4pro`, `x4pro-gh_release` and
   `x4pro-lexirise-off` builds, from `firmware/`. Re-check the hook rows in §3 that say so.
4. Add a row here, in the same commit or the next.

| Commit here | CrossPoint commit | Why |
|---|---|---|
| — | — | — |

None has been taken yet. The fork-era table (§5 before M) was empty too.

The v0.2 slimming (`../v0.2/slimming.md`) records what it removes from the base here as well, so what was
dropped, and at which commit, stays findable.

## 5. (Merged into §4)

M merged the fork-era "Upstream sync" (§4) and "Cherry-picks and divergences" (§5) into §4.

## 6. Releases, OTA updates and checks

- **Releases are on `claritise/lexipoint`.** Each one carries one asset, `lexipoint-<tag>-x4pro.bin`,
  built from `x4pro-gh_release`. The name comes from one prefix, `config::kReleaseAssetPrefix`
  (`src/lexirise/LexiriseConfig.h`, used by the OTA hook), which `scripts/lexipoint/release_tag.py`
  `ASSET_PREFIX` and `publish_release.py` share (`test_publish_release.py` pins them). No release exists yet.
- **OTA** (`OtaUpdater.cpp` hook, §3): the device reads `config::kReleasesLatestUrl`
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
  platformio.ini's, over **27** characters, which is the updater's 48-byte asset name less `lexipoint-`,
  `-x4pro.bin` and the terminator, or not newer than every published release). Then it builds clean, checks
  nothing moved meanwhile, creates the release **with** the asset and its tag at HEAD in one step, and checks
  that a full release is GitHub's latest with its asset. Tags are unique: to re-cut a number, delete that
  release and its tag first. Never promote a prerelease: publish a full release. Only claritise publishes
  releases, and only from a commit that passed the uniform gate: the build uses this Mac's `pio`, so it's the
  toolchain the gate ran with.
- **Checks run here, not on GitHub:** everything CI used to run is the uniform gate (`01-build-order.md`):
  clang-format, cppcheck on `x4pro`, builds of `x4pro`, `x4pro-gh_release` and `x4pro-lexirise-off`, the host
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
  `unit-tests` job already builds every `lexirise_*` suite and the card goldens; `x4c` in the build matrix is
  the LEXIRISE-off parity build; the new `lexipoint` job (with submodules: `test_lxctl` reads the SDK's
  edge bands) runs `scripts/lexipoint/keyscan.py` and the script tests (`test_gen_bench_fixtures` skips
  there: it needs this docs repo). It runs every `scripts/lexipoint/test_*.py` (`unittest discover`). `cppcheck` runs on the `default` env, which doesn't build Lexirise.
- **Rebase (P8 gate):** checked 2026-09-25: the newest upstream tag (`1.6.5rc`) is already in `lexipoint`,
  and upstream `master`'s two newer commits are empty merges, so there was nothing to rebase onto.
