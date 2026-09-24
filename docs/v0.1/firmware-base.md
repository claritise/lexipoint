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
  (P2+) SentenceBuilder, LookupProvider + Lexirise/StarDict providers, the card, Kana,
        the device settings activity
test/lexirise_*/                host gtest suites
scripts/lexipoint/lxctl.py      host side of the dev harness (+ test_lxctl.py)
scripts/lexipoint/websmoke.py   read-only smoke test of the web surface against a device (+ test_websmoke.py)
test/lexirise_fakes/            fakes shared by the suites (card, connection, WiFi, clock)
```

**Upstream files we touch.** Each hook carries a `// LEXIPOINT` comment, so `git grep LEXIPOINT`
lists every one. Hooks are wrapped in `#if LEXIRISE` unless noted:

| File | Hook |
|---|---|
| `src/main.cpp` | Load the settings store at boot; `service().tick()` in the loop (queued key check, idle TLS close, WiFi idle teardown). Dev harness hooks (`LEXIPOINT_DEV_HARNESS`) |
| `src/activities/ActivityManager.cpp` | Tell `LexiriseService` whether a reader activity is still on screen or under it, **before** the next activity's `onEnter` (push/replace, the immediate replace) and after a pop out of reading, so WiFi Lexipoint owns is given back first (`offline-and-errors.md` §5). Relies on nothing that uses WiFi being pushed over the reader (KOSync replaces it): re-check on upstream syncs |
| `src/network/WebDAVHandler.cpp` | **Not gated:** `isProtectedPath` also refuses FAT short-name aliases of dot folders (`/LEXIRI~1`), via `web/HiddenPath.h`; `PROPFIND` now checks it too (upstream listed hidden folders' contents) |
| `src/network/CrossPointWebServer.cpp` | Register the `/lexirise` routes; collect the `Origin` header. **Not gated:** the file manager refuses any path with a hidden segment, typed, after SdFat's space/dot trimming, or as a FAT short name (`web/HiddenPath.h`), at all 8 entry points, and `isProtectedItemName` checks new names the same way. Upstream only checked the last segment as typed, so `/download?path=/.lexirise/config.ini` (and `/LEXIRI~1/config.ini`) served the key |
| `src/network/html/{Home,Files,Fonts,Settings}Page.html` | **Not gated:** one `<script src="/lexirise/nav.js">` line. Lexirise builds serve it and it adds the nav link; other builds 404 it and nothing changes |
| `lib/hal/HalGPIO.{h,cpp}` | Dev harness input overlay (`LEXIPOINT_DEV_HARNESS`) |
| `platformio.ini` | A `[lexirise]` section (`-DLEXIRISE=1` plus wolfSSL SHA-384/P-384, lexirise-client.md §1) referenced by the three X4 Pro envs; the harness flag in `[env:x4pro]` only. `test_lxctl.py` guards both |
| `test/CMakeLists.txt` | The `lexirise_*` suites |
| (P3) `src/activities/reader/DictionaryWordSelectActivity.{h,cpp}`, `EpubReaderActivity.cpp` | Provider chain; touch long-press → lookup (`lookup-flow.md` §1) |
| (P6) `src/SettingsList.h`, `src/activities/settings/SettingsActivity.{h,cpp}` | The device `Lexirise` settings row |
| (P8) `src/network/OtaUpdater.cpp` | OTA checks **our** fork's releases (§6) |
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
