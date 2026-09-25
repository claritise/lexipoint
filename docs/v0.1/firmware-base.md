# Firmware base: fork strategy and where our code lives

**Status:** proposed 2026-09-24. Decisions D1, D2 in `00-overview.md`.

Related: `lookup-flow.md` (the hooks this doc allows), `lexirise-client.md` (the one new network
path).

---

## 0. Why a fork, not a plugin or a PR

- **No plugin system.** CrossPoint is one firmware image. New features are compiled in.
- **Upstream won't take it.** `SCOPE.md` §3 "Temporarily Closed Areas" closes *"New external
  network connectors … any new 'talk to a server' feature"*. A Lexirise client is exactly that.
- So: **a long-lived fork that rebases onto upstream.** How cheap that fork is to keep up comes
  down to one thing: how few upstream files we touch.

## 1. Which upstream

**Upstream `crosspoint-reader/crosspoint-reader`** (D1, confirmed): tags from its `develop` branch, starting at `1.6.5rc`.

| | Upstream `main` | ++ fork (jpirnay) |
|---|---|---|
| X4 Pro support | First-class (`env:x4pro`, lands here first) | Follows upstream |
| TLS | Pre-flight heap checks in `HttpDownloader` and `KOReaderSyncClient`. The CA bundle is used for downloads | Extra OOM fixes, aimed at **C3** heaps |
| Relevance of the TLS fixes on the X4 Pro | — | Low: the S3 has 8MB PSRAM |
| Rebase target stability | Moves fast (v1.6.5 on 2026-09-24) | Moves with upstream plus its own changes, so two sources of conflict |

If a ++ change turns out to matter (for example, a TLS fix that also affects the S3), we
cherry-pick it and note it in §5.

## 1a. Repo layout (D19)

```
~/Projects/
  lexipoint/               this repo (public): the project home, docs/ at the root
  crosspoint-reader/       the firmware: fork github.com/claritise/crosspoint-reader
                           branch `lexipoint`, based on upstream tag 1.6.5rc
                           (upstream tree + src/lexirise/ + test/lexirise_*/ + hooks)
```

- Created 2026-09-24 with `gh repo fork crosspoint-reader/crosspoint-reader`, then cloned with the
  `freeink-sdk` submodule. `origin` = the fork, `upstream` = crosspoint-reader.
- **Upstream's default branch is `develop`**, and releases are tags (`1.6.0`, `1.6.5rc`, …).
- All `pio` and `cmake` commands run in `~/Projects/crosspoint-reader`.

## 2. Build targets

- **`pio run -e x4pro -t upload`**, the only v0.1 target. It's an ESP32-S3 (`esp32-s3-devkitc1-n16r8`)
  with `-DBOARD_HAS_PSRAM` and `LOG_LEVEL=2`, and it waits for USB serial (`CROSSPOINT_WAIT_FOR_USB_SERIAL`).
- `freeink-sdk` is a **git submodule** (`https://github.com/Free-Ink/freeink-sdk.git`). It holds the
  HAL, the display and touch drivers, and `SecureHttpClient`. Clone with `--recursive`. We **don't
  fork the SDK** in v0.1.
- Host tests: `cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test`
  (gtest via FetchContent, see `test/README`). Anything we write that is pure logic (sentence
  building, offset mapping, response parsing, config parsing) gets a suite here.
- The build flag **`-DLEXIRISE=1`** gates the Lexirise hooks. With it off, the fork behaves like
  upstream except for deliberate, ungated changes (§3): the file manager's and WebDAV's hidden-path
  guard, and the nav script tag that 404s harmlessly.

## 3. Where our code lives

Our code goes in **new files wherever possible**. Upstream files only get small, marked hooks.

```
src/lexirise/
  LexiriseConfig.h              every Lexirise constant (paths, limits, timeouts)
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
  api/LexiriseClient.{h,cpp}    keep-alive, stale-session retry, error mapping
  api/Requests.{h,cpp} / Responses.{h,cpp} / KeyCheck.{h,cpp}  endpoints (pure)
  web/LexiriseWeb.{h,cpp}       /lexirise page + /api/lexirise (settings.md §1a)
  web/LexirisePage.html, LexiriseNav.js  embedded by scripts/build_html.py like upstream's pages
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
  ota/ReleaseVersion                  `<upstream>-lexi.<n>` versions, which release OTA offers (P8), pure
  lookup/StarDictChoice.h             each language's own offline dictionary (P7), pure
test/lexirise_*/                host gtest suites
scripts/lexipoint/lxctl.py      host side of the dev harness (+ test_lxctl.py)
scripts/lexipoint/websmoke.py   read-only smoke test of the web surface against a device (+ test_websmoke.py)
scripts/lexipoint/keyscan.py    the key-leak scan, uniform gate 5 and CI (+ test_keyscan.py)
test/lexirise_fakes/            fakes shared by the suites (card, connection, WiFi, clock)
```

**Upstream files we touch.** Each hook carries a `// LEXIPOINT` comment, so `git grep LEXIPOINT`
lists every one. Hooks are wrapped in `#if LEXIRISE` unless noted:

| File | Hook |
|---|---|
| `src/main.cpp` | Load the settings store at boot; `service().tick()` in the loop (queued key check, idle TLS close, WiFi idle teardown). Dev harness hooks (`LEXIPOINT_DEV_HARNESS`) |
| `src/activities/ActivityManager.cpp` | Tell `LexiriseService` whether a reader activity is still on screen or under it, **before** the next activity's `onEnter` (push/replace, the immediate replace) and after a pop out of reading, so WiFi Lexipoint owns is given back first (`offline-and-errors.md` §5). Relies on nothing that uses WiFi being pushed over the reader (KOSync replaces it): re-check on upstream syncs |
| `src/network/WebDAVHandler.cpp` | **Not gated:** `isProtectedPath` also refuses FAT short-name aliases of dot folders (`/LEXIRI~1`), via `web/HiddenPath.h`; `PROPFIND` now checks it too (upstream listed hidden folders' contents) |
| `src/network/CrossPointWebServer.cpp` | Register the `/lexirise` routes; collect the `Origin` header. **Not gated:** the file manager refuses any path with a hidden segment, typed, after SdFat's space/dot trimming, or as a FAT short name (`web/HiddenPath.h`), at all 8 entry points, and `isProtectedItemName` checks new names the same way. The web listing therefore always hides dot entries, ignoring the device's "Show hidden files" (which still applies to the on-device file browser): on purpose, since they couldn't be opened anyway. Upstream only checked the last segment as typed, so `/download?path=/.lexirise/config.ini` (and `/LEXIRI~1/config.ini`) served the key |
| `src/network/html/{Home,Files,Fonts,Settings}Page.html` | **Not gated:** one `<script src="/lexirise/nav.js">` line. Lexirise builds serve it and it adds the nav link; other builds 404 it and nothing changes |
| `lib/hal/HalGPIO.{h,cpp}` | Dev harness input overlay (`LEXIPOINT_DEV_HARNESS`) |
| `platformio.ini` | A `[lexirise]` section (`-DLEXIRISE=1` plus wolfSSL SHA-384/P-384, lexirise-client.md §1) referenced by the three X4 Pro envs; the harness flag in `[env:x4pro]` only. `test_lxctl.py` guards both. (P8) `[lexirise] release`, and `-lexi.${lexirise.release}` in the three X4 Pro envs' `CROSSPOINT_VERSION` (**not gated**, and easy to lose resolving a rebase conflict: `test_lxctl` ReleaseVersioning and `test_release_tag` guard them) |
| `test/CMakeLists.txt` | The `lexirise_*` suites |
| `src/activities/reader/DictionaryWordSelectActivity.{h,cpp}` | (P2) `WordBox` gets `(line, token)`, counted exactly as `text::buildPageModel` counts lines; `setBook(dcLanguage)`. (P3) `performLookup()` asks Lexirise first (`lexiriseLookup()`: busy popup, placeholder definition screen or not-found popup) and falls back to StarDict with longest-prefix CJK candidates (`starDictLookup()`); `setInitialTouch(x, y)` selects the long-pressed word and looks it up on the first `loop()` (`lookup-flow.md` §5a). (P5) `performLookup()` opens the Lexirise card (`openLexiriseCard()` when `lookup::asksLexirise`: `card::LiveSource` over a `card::ReaderPage` snapshot from `card::readerPageFor`, taken in `extractWords()` only when Lexirise is usable for the book, with the page model's line filter `text::forEachTextLine`; it sets upstream's `snapshotIdx = -1` so word select's differential repaint can't run over the card's last frame: re-check that on upstream syncs), which hands back through a shared `LiveOutcome` (`card::afterCard` decides): Not found → the popup, no answer → `runStarDict()` on the next `loop()` (the StarDict half of `performLookup()`, split out unchanged). The P3 busy popup and placeholder are gone (`lookup-flow.md` §5b). (P6) `performLookup()` asks `lookup::lexiriseGate` first; `fallBack()` carries out `lookup::planFallback` (a notice, then `runStarDict()` once it's read, or "No dictionary set"), StarDict's title gets `· offline` (`starDictOffline`), a card that closed with unsent saves gets its notice (`AfterCard::UnsentSave`); `noticeString()`, `starDictSet()`. (P7) one settings copy per lookup; the tap is described once (`describeSelected(settings)`, only when Lexirise is asked or a language has its own dictionary) and its language picks the StarDict dictionary (`lookup::chooseStarDict`, Japanese/Chinese words only, with CrossPoint's as the fallback when the folder won't open; `runStarDict()` opens it through `lookup::prepareStarDict`, reopening when the choice changes, `dictOpened`); `touchEntry` (a long-press on a word opened it); a card that closed (`AfterCard::Closed`) or StarDict's definition (`answerClosed()`) goes where `card::closeStep` says (`finishClose(lookUpAt)`): the word a long-press on the page under the card landed on, back to the reader for a touch-opened word select, else this page; after an unsent save's notice the close finishes (`afterPopup`, `card::AfterPopup` / `card::afterNotice`), and a touch-opened word select returns to the reader after any lookup notice |
| `src/MappedInputManager.{h,cpp}` | `peekScreenLongPress(x, y)`: the long-press without consuming it, so the reader can check it's the lookup's before taking it. (P7) `peekSwipe(startX, startY, endX, endY)`: the swipe's two ends, so the card only takes swipes that start on it, clear of the edge gestures |
| `src/activities/reader/ReaderUtils.h` | **Not gated:** `pageTurnZoneWidth(width)` (the outer thirds) pulled out of `detectTouchPageTurn`, so the long-press rule shares it. Same behaviour |
| `src/activities/reader/EpubReaderActivity.{h,cpp}` | (P2) pass the book's `<dc:language>` to word select. (P3) touch long-press → word select at that point, before link taps, owning the centre third only when CrossPoint's hold action is on (`lookup::lookupOwnsLongPress`); word select opens without a StarDict dictionary when Lexirise is set up (P6: `lookup::lexiriseConfigured`, settings only, not a rate limit's back-off), or when a language has its own (P7: `lookup::anyStarDict`) |
| `src/activities/settings/SettingsActivity.{h,cpp}` | (P7) `SettingAction::Lexirise`, the System tab's `Lexirise` row (the device-only ACTION rows are appended here, not in `SettingsList.h`), and its dispatch to `LexiriseSettingsActivity` |
| `lib/I18n/translations/english.yaml` | **No marker (YAML):** the `STR_LEXI_*` keys, one block after `STR_DICT_LOW_MEMORY` (the notices, then `STR_LEXI_CARD_*` for every card word, P6). (P7) `STR_LEXIRISE` and `STR_LEXI_SET_*` for the settings screen. Other languages fall back to English. `scripts/lexipoint/test_card_strings.py` checks them against `CardStrings`; re-check on upstream syncs (gen_i18n.py rejects comments in the file) |
| `src/network/OtaUpdater.cpp` | (P8) OTA reads **our** fork's releases (`config::kReleasesLatestUrl`) and `isUpdateNewer()` compares `<upstream>-lexi.<n>` versions (`ota::isNewerRelease`, §6); the asset name (`crosspoint-<tag>-x4pro.bin`) is upstream's rule |
| `.github/workflows/{ci,release,release_candidate}.yml` | (P8) CI also runs on `lexipoint` pushes and adds a `lexipoint` job (the key-leak scan, the Lexipoint script tests); releases and candidates build the X4 Pro only, tagged `<upstream>-lexi.<n>` (§6) |
| `lib/GfxRenderer/GfxRenderer.cpp` | (P4) `applyPromotedRefresh`: a promoted refresh never weakens the one asked for (the stronger of the two), so the card's half refresh on dismiss can't turn the reader's due full refresh into a half one |
| `lib/GfxRenderer/FontCacheManager.{h,cpp}` | (P4) with LEXIRISE the prewarm scan takes 8 fonts (upstream 4): the card's expanded tabs draw with 7; a font past the cap is logged once per render (it loads glyph by glyph from SD) |
| `src/SdCardFontSystem.{h,cpp}` | (P4) `familyFontIdAt(renderer, pt)`: the loaded SD family at another point size (the card's 8/10/18 pt), via the manager's existing `loadFamilyExtraSize` |
| `src/lexirise/dev/DevHarness.cpp` (ours) | (P4) `LX:LEXI CARD ja\|zh [LOW] [KANA]` pushes the card bench (`KANA`: opens in kana, never saves the reading; `lxctl card-smoke`, and P7's `lxctl card-gestures`). (P7) `LX:LEXI SETTINGS` pushes the Lexirise settings screen, which logs its row count in dev builds (`lxctl settings-smoke`) |
| (P3+) `lib/I18n/translations/english.yaml` | `STR_LEXI_*` strings |

Anything that needs more than a few lines in an upstream file is a smell. Move the logic into
`src/lexirise/` and call it from there.

## 4. Upstream sync

- Remote layout: `origin` = our fork, `upstream` = crosspoint-reader.
- **Rebase, don't merge**, onto an upstream **release tag** (not a random `main` commit), so every
  Lexipoint build names the upstream version it's built on: `1.6.5-lexi.N` (on `1.6.5rc` until 1.6.5 ships).
- When to sync: monthly, or whenever an upstream release touches `activities/reader/Dictionary*`,
  `network/`, `lib/JsonParser`, `MappedInputManager` or the X4 Pro SDK profile.
- After every rebase: the host test suite, an `x4pro` build, and the manual smoke from
  `01-build-order.md` P8.

## 5. Cherry-picks and divergences

Record any upstream or ++ commit we carry that isn't in the base tag.

| Commit | From | Why | Drop when |
|---|---|---|---|
| — | — | — | — |

## 6. Releases, OTA updates and CI (public fork)

- **⚠ The upstream OTA updater would uninstall Lexipoint.** `src/network/OtaUpdater.cpp` checks
  `api.github.com/repos/crosspoint-reader/crosspoint-reader/releases/latest`. A Lexipoint device that
  taps *Update* would install stock CrossPoint. **Hook:** point `latestReleaseUrl` at
  `claritise/crosspoint-reader` (`#if LEXIRISE`), and pick the release asset for the x4pro build
  only. The SD-card firmware update (`SdFirmwareUpdateActivity`) needs no change: it installs
  whatever file the user copies.
- **Versions:** `CROSSPOINT_VERSION` becomes `<upstream>-lexi.<n>` (e.g. `1.6.5-lexi.1`). Check in P8
  how `OtaUpdater` compares versions, so `1.6.5-lexi.2` counts as newer than `1.6.5-lexi.1`, and an
  upstream `1.6.6` release is never offered.
- **CI comes with the fork.** Upstream's `.github/workflows` (`ci.yml` builds and checks,
  `pr-formatting-check.yml`, `release.yml` with the `x4pro-gh_release` env) run on the fork as they
  are. **Add** to `ci.yml`: the `lexirise_*` host tests, a `LEXIRISE`-off build (the parity check), and the
  key-leak grep (`01-build-order.md` uniform gate 5). **Trim** release jobs to the boards we support
  (x4pro), so we never publish untested builds for other devices.
- **Formatting:** upstream enforces clang-format in CI (`pr-formatting-check.yml`), and our code follows
  the same `.clang-format`, which keeps rebases quiet.
- **Release notes** say which upstream version each release is built on, and link the user setup guide.

**As built (P8):**
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
