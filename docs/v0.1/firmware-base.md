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
- The build flag **`-DLEXIRISE=1`** gates every hook. With it off, the fork builds byte-for-byte the
  same behavior as upstream. That makes upstream-regression checks easy.

## 3. Where our code lives

Our code goes in **new files wherever possible**. Upstream files only get small, marked hooks.

```
src/lexirise/
  LexiriseSettings.{h,cpp}      /.lexirise/config.ini store: every setting, atomic save (settings.md)
  LexiriseSettingsActivity.{h,cpp}  Settings → System → Lexirise (UiListActivity)
  Kana.{h,cpp}                  romaji → kana (languages.md §3a)
  LexiriseWeb.{h,cpp}           the /lexirise web page handlers (settings.md §1a)
  html/LexirisePage.html        the web page, embedded like upstream's pages
  LexiriseClient.{h,cpp}        esp_http_client session, 3 endpoints (lexirise-client.md)
  LexiriseResponses.{h,cpp}     StreamingJsonParser handlers → small structs
  SentenceBuilder.{h,cpp}       page → sentence + tap offset (sentence-extraction.md)
  LookupProvider.h              the seam (lookup-flow.md §4)
  LexiriseLookupProvider.{h,cpp}
  StarDictLookupProvider.{h,cpp}  thin wrapper over util/Dictionary
  LexiriseCardActivity.{h,cpp}  the card (popup-ui.md)
  WifiSession.{h,cpp}           on-demand connect + idle teardown (D10)
test/lexirise_*/                host gtest suites
```

**Upstream files we expect to touch** (each hook is wrapped in `#if LEXIRISE` and a
`// LEXIPOINT:` comment, so `git grep LEXIPOINT` lists every one):

| File | Hook |
|---|---|
| `src/activities/reader/DictionaryWordSelectActivity.{h,cpp}` | `performLookup()` goes to the provider chain instead of calling `Dictionary` directly. Adds a touch long-press → immediate lookup |
| `src/activities/reader/EpubReaderActivity.cpp` | Touch long-press on the page → open word select pre-positioned at the touch point (`lookup-flow.md` §1) |
| `src/main.cpp` | Load `LexiriseSettings` at boot |
| `src/SettingsList.h` | The `Lexirise` ACTION row (device only, `settings.md` §0) |
| `src/network/CrossPointWebServer.cpp` | Register `/lexirise`, `/api/lexirise`, `/api/lexirise/test`, delegating to `src/lexirise/LexiriseWeb.cpp` |
| `src/network/html/{Home,Files,Fonts,Settings}Page.html` | One `Lexirise` link in each page's menu |
| `src/activities/settings/SettingsActivity.{h,cpp}` | `SettingAction::Lexirise` and its dispatch to `LexiriseSettingsActivity` |
| `src/network/OtaUpdater.cpp` | OTA checks **our** fork's releases, not upstream's (§6) |
| `src/network/WebDAVHandler.cpp` (only if needed) | Hide `/.lexirise/` from WebDAV. The file browser already hides dot folders; P1 checks WebDAV |
| `lib/I18n/translations/english.yaml` (+ `japanese.yaml` if it exists) | New `STR_LEXI_*` strings |
| `platformio.ini` | `-DLEXIRISE=1` in `[env:x4pro]` (or a `platformio.local.ini` extra config) |

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
