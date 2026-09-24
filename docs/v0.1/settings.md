# Settings: the Lexirise panel

**Status:** proposed 2026-09-24. Decision D18 in `00-overview.md`. It replaces the "config file only,
no on-device key entry" part of D8.

Related: `lexirise-client.md` §5 (the store it writes), `languages.md` (the language options),
`offline-and-errors.md` §5 (WiFi), `popup-ui.md` (the card: **no card design is configurable**).

---

## 0. Where it lives

CrossPoint has **one settings list** (`src/SettingsList.h`, `getSettingsList()`), and it feeds two UIs:

- the **device Settings screen** (`SettingsActivity`, tabs: Display, Reader, Controls, System), and
- the **web settings page** of the on-device web server (`CrossPointWebServer::handleGetSettings` /
  `handlePostSettings`), opened from a phone's browser in network mode.

KOReader sync is the precedent. Its credentials are `DynamicString` entries backed by a store
(`KOREADER_STORE`), and the device reaches them through an `ACTION` row that opens
`KOReaderSettingsActivity` (a `UiListActivity`). **Lexirise copies that pattern exactly**, so it looks
and behaves like the rest of Settings, with no new UI components.

| Piece | What it is |
|---|---|
| `LexiriseSettings` store (`src/lexirise/LexiriseSettings.{h,cpp}`) | Owns `/.lexirise/config.ini`. It loads at boot and saves **atomically** (write `config.ini.tmp`, then rename). It replaces the separate `LexiriseConfig` reader and `state.ini`: **one file holds everything**, including the kana/romaji choice |
| Device entry | System tab → **`Lexirise`** row (`SettingType::ACTION`, new `SettingAction::Lexirise`) → `LexiriseSettingsActivity` (`UiListActivity`), same as `KOReaderSync` |
| **Web page (revised 2026-09-24, claritise)** | **Its own page in the web UI menu: Home · Files · Fonts · Settings · Lexirise.** It's served by the same web server used to upload books (file transfer / network mode), and it's where the API key is pasted. It's modelled on the Fonts page (`FontsPage.html` + `/api/fonts*`): `LexirisePage.html` at `/lexirise`, plus `/api/lexirise` (GET: masked settings and status; POST: save) and `/api/lexirise/test` (POST: runs `/v1/me`). **Lexirise entries are not added to the generic Settings page**, so there's one place for them, and no `SettingsList.h` web entries |
| Upstream hooks | `SettingsList.h` (the one ACTION row), `SettingsActivity.{h,cpp}` (the `SettingAction` value and its dispatch), `CrossPointWebServer.cpp` (register the `/lexirise` routes, delegating to `src/lexirise/LexiriseWeb.cpp`), **one `Lexirise` link in the menu of each of the 4 existing pages** (`HomePage`, `FilesPage`, `FontsPage`, `SettingsPage`), and `I18n` strings. All marked `// LEXIPOINT:` / `<!-- LEXIPOINT -->` (`firmware-base.md` §3) |

## 1. The settings (v0.1)

| Group | Setting | Values | Default | Device | Web | Notes |
|---|---|---|---|---|---|---|
| **Account** | Lexirise lookups | On / Off | On | ✓ | ✓ | Off means StarDict only, and no network |
| | API key | *masked* | not set | ✓ keyboard | ✓ **paste** | Shown as `lx_••••••••nas` (last 3 only). See §2 |
| | Account | e.g. `claritise · Pro · key "lexipoint"` | — | ✓ read-only | — | From `GET /v1/me`, cached. `Not checked` until the first test |
| | Test connection | action | — | ✓ | — | Brings WiFi up, calls `/v1/me`, then shows `Connected as …`, `Key rejected`, or `No network` |
| **Japanese** | Lookups | On / Off | On | ✓ | ✓ | Off means Japanese books use StarDict only |
| | Readings | Kana / Romaji | Kana | ✓ | ✓ | **The same value** the card's reading-line tap switches (`popup-ui.md` §1). Changing it in either place changes both |
| | Offline dictionary | StarDict folders found, or None | the global dictionary | ✓ | ✓ | Listed like the upstream Dictionary setting (`DictionaryRegistry`) |
| **Chinese (Simplified)** | Lookups | On / Off | On | ✓ | ✓ | Off means Chinese books use StarDict only |
| | Offline dictionary | same | the global dictionary | ✓ | ✓ | |
| **General** | Language when a book doesn't say | Japanese / Chinese | Japanese | ✓ | ✓ | `languages.md` §1 step 2. It spans both languages, so it lives here |
| | Tags | text | `xteink` | ✓ keyboard | ✓ | Comma-separated. Tags can't be deleted through the API (`../reference/lexirise-api-notes.md`), so the help text says so |
| | Keep WiFi on after a lookup | Off (connect each time) / 1 / 2 / **5** / 10 min | 5 min | ✓ | ✓ | D10 |
| **Advanced** | Server | URL | `https://api.lexirise.app` | — | ✓ | Web only, so it can't be mistyped on the device. For a local proxy |

**Grouping rule (claritise, 2026-09-24):** anything that belongs to one language goes in **that
language's group**. A new language (e.g. Korean, or Traditional Chinese once H8 is picked up) adds
its own group, with the same rows where they apply. Only settings that span languages go in
**General**. On the web page the groups are sub-sections of **Lexirise** (categories
`STR_LEXIRISE`, `STR_LEXIRISE_JA`, `STR_LEXIRISE_ZH`), in the same order.

**A language's rows only show while it's on (claritise, 2026-09-24).** When a language's
**Lookups** is Off, its group collapses to that one toggle. Turning it back on brings its rows back,
with their values kept (turning a language off never clears its settings). The same applies to the
master **Lexirise lookups** toggle: when it's Off, everything below the Account group is hidden.
**"Language when a book doesn't say"** only shows while **two or more** languages are on. With one
language on, that language is the fallback, and there's nothing to choose.
- **Device:** `LexiriseSettingsActivity::buildScreen()` simply skips those rows, and a toggle triggers
  a rebuild, as `KOReaderSettingsActivity` does. There's no upstream change.
- **Web page:** it's our own page (`LexirisePage.html`), so it applies **the same rules** in its JS.
  Switching a language off collapses its section, with no upstream change needed.

**Deliberately not settings:** anything about the card's look or layout (it's binding,
`popup-ui.md`), the level saved by a tap (you pick it every time with T L F K), and timeouts.

**Added later, hidden until built** (each ships with its feature, never as a dead toggle):
`Tag with book title` (C2), `Deck per book` (C4), `Page marks` and the other annotation toggles
(`../v0.2/page-annotations.md`), and `Skip to unknown words` (A3).

## 1a. The web page (`/lexirise`)

Open it from any browser on the same WiFi, while the device is in file transfer / network mode (the
same URL shown for uploading books).

```
 Home · Files · Fonts · Settings · [Lexirise]
 ───────────────────────────────────────────────
 Lexirise                                  ● Connected as claritise (Pro)
 API key   [ Paste your lx_… key          ] [Save]
           Current: lx_••••••••nas            [Test connection]
 ───────────────────────────────────────────────
 Lexirise lookups            [on]
 Japanese                    [on]
   Readings                  Kana ▾
   Offline dictionary        jmdict ▾
 Chinese (Simplified)        [on]
   Offline dictionary        cedict ▾
 General
   Language when a book doesn't say   Japanese ▾   (only while 2+ languages are on)
   Tags                      [ xteink ]
   Keep WiFi on after a lookup        5 min ▾
 Advanced ▸  Server  [ https://api.lexirise.app ]
```

- **Styling:** it copies the Fonts and Settings pages' own markup and CSS classes, so it looks like
  the rest of CrossPoint's web UI. No new visual language.
- **Status line:** `Connected as <name> (<plan>)` / `Key rejected` / `Not checked` / `No internet` /
  `Could not connect`, from the cached `/v1/me` result (`api/KeyCheck`). `Test connection` re-runs it.
  A check is queued and run from the device's main loop (never inside the HTTP request); meanwhile the
  status reads `Checking…` and the page polls (for the device's `checkTimeoutS`). In hotspot mode the
  device has no internet and never touches the hotspot's radio, so the status reads `No internet`. It is
  checked again when the page is next opened (`GET /api/lexirise?recheck=1`) and whenever a Lexirise
  call next gets WiFi.
- **Settings reset notice:** if `config.ini` couldn't be read at boot it is moved to `config.ini.bad`
  (never overwritten), defaults are used, and the page says so.
  The name and plan are shown on this page only, never logged; the email is never read.
- **Save:** a pasted key is validated client-side (`lx_` prefix, no spaces), saved, and tested straight
  away. The page never receives the full key back (§2).
- **Every control posts its own change** to `/api/lexirise` (a partial JSON update, validated as a whole by
  `SettingsPatch` before anything is saved). Settings apply at once, and the device's own screen shows
  the same values. The response is the page's full state, so the page always shows what was saved.

## 2. The API key

- **The web page (`/lexirise`, §1a) is the main way to enter it**: paste it from a phone or laptop browser. The device keyboard
  (`KeyboardEntryActivity`, as for WiFi passwords) is the fallback. A 67-character key is fine to
  paste but painful to type on e-ink.
- **It's never shown or served in full.** The `DynamicString` getter returns the **masked** form, so
  `handleGetSettings` never sends the key over the LAN. KOReader's password entry needs checking for
  the same problem, and we don't copy it if it doesn't mask. `GET /api/lexirise` returns only the masked form, and
  `POST` accepts a full key: **an empty or unchanged masked value leaves the key as it is**.
- **Anyone on the same WiFi can open the page while network mode is on.** That's true of all of
  CrossPoint's web UI, which has no login. They can replace the key but never read it. The user docs
  say to use network mode on trusted networks.
- **Other websites can't use the page** (P1). CrossPoint allows every origin (`enableCORS`), so without a
  guard any page open in the user's browser could POST new settings to the device. Every `/api/lexirise`
  call is refused (403) when it carries an `Origin` that isn't the device itself, or a `Host` that isn't an
  IPv4 literal or an mDNS `<name>.local` (DNS rebinding). Browsers always send `Origin` cross-site;
  curl and the dev harness send none and are allowed (`web/Origin.h`).
- **A new server needs the key pasted again** (P1). Pointing `base_url` somewhere else is only accepted
  in the same save as a pasted key (or with the key removed), so no edit can redirect the stored key
  to another server (`SettingsPatch`, error field `apiKeyForServer`). The page says so under Advanced.
- A newly entered key is **checked straight away** with `GET /v1/me` if WiFi is up (`Connected as …` /
  `Key rejected`). Otherwise it's checked at the next WiFi-up.
- It's stored in `/.lexirise/config.ini` on the SD card, **in plain text**, the same as upstream's
  WiFi and KOReader credentials. The user docs say so: anyone with the SD card has the key, and a lost
  card means rotating the key.
- **The web file manager can't reach `/.lexirise/`** (corrected in P1). Its listing hides dot items, but
  `/download`, upload, rename, move and delete only checked the last path segment, so
  `/download?path=/.lexirise/config.ini` served the key and delete + upload could replace the file. A
  hook now refuses any path with a hidden segment at every entry point (`firmware-base.md` §3).
  **FAT short names too** (P1 review): on FAT cards the folder is also reachable as `/LEXIRI~1`, which
  doesn't start with a dot. A dot name's short name always carries a `~N` tail, so any segment with a
  `~` is looked up on the card and refused if its real name is hidden (or can't be resolved). This
  applies to the file manager and to WebDAV (which only checked typed names), and it also closes the
  same hole for upstream's `/.crosspoint` (saved WiFi passwords). `websmoke.py` probes it.
- **Never logged**, and not written anywhere else (unchanged from D8).

## 3. The file

```ini
# /.lexirise/config.ini — written by the device. Hand edits (device off) are kept.
# One section per settings group, and one section per language.
[account]
enabled=1
api_key=lx_YOUR_KEY_HERE

[ja]
enabled=1
reading=kana
stardict=jmdict

[zh]
enabled=1
stardict=cedict

[general]
default_language=ja
tags=xteink
wifi_idle_min=5

[advanced]
base_url=https://api.lexirise.app
```

- **Sections mirror the groups in §1.** A new language is a new section (`[ko]`, `[zh-Hant]`)
  with the same keys where they apply, so the code reads `settings.lang(code).enabled`, never a list
  of language names.
- This **replaces** the flat keys shown in `lexirise-client.md` §5 and `languages.md` §1
  (`languages=`, `stardict_ja=`, `language=`). Those docs point here. **The old flat keys are still
  read once and migrated** into sections on the first save, so a hand-written early config keeps working.
- **`reading` lives in `[ja]`** (it moved from `state.ini`, which is dropped).
- Unknown keys are **kept** on rewrite, so a newer firmware's settings survive a downgrade.
- A missing file means all defaults with no key, so Lexirise is effectively off until a key is set.

## 4. Look

The device screen is **CrossPoint's own list UI** (`UiListActivity` + `UITheme`, as in
`KOReaderSettingsActivity`). The web page (`/lexirise`) reuses **the markup and CSS of CrossPoint's own web pages**. **We add no styling of our own.** A mock for orientation is in the
conversation of 2026-09-24. The real look is whatever CrossPoint's list renders, so there's nothing
extra to keep pixel-perfect here beyond "uses the stock components".

## 5. Tests

- `test/lexirise_settings/`: ini round-trip (defaults, unknown keys kept, CRLF, BOM, comments on
  hand-edited files), the atomic save (a crash between tmp and rename leaves the old file), key
  masking (masked getter, empty/masked setter keeps the key, a full key replaces it), migrating the old
  flat keys into sections, and the
  shared `reading` value (a card toggle is visible to the settings getter, and vice versa).
- On device (P7): set the key from a phone, check the masked value on both UIs, `Test connection` in
  all three outcomes, turn Chinese off and on again (its rows hide and come back with values kept), and the reading setting and card tap staying in sync across a reboot.
