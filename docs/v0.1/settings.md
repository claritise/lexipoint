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
and behaves like the rest of Settings, with no new UI components. (2026-09-29: KOReader sync itself was removed in
v0.2 V8, `../v0.2/slimming.md` §8; the pattern Lexirise copied stays.)

| Piece | What it is |
|---|---|
| ~~`LexiriseSettings` store (`src/lexirise/LexiriseSettings.{h,cpp}`)~~ `settings/Settings` + `settings/SettingsStore` (corrected 2026-09-30) | Owns `/.lexirise/config.ini`. It loads at boot and saves **atomically** (~~write `config.ini.tmp`, then rename~~ through `settings/SafeFile`: `config.ini.tmp` and `config.ini.bak`, `LexiriseConfig.h`, corrected 2026-09-30). It replaces the separate `LexiriseConfig` reader and `state.ini`: **one file holds everything**, including the kana/romaji choice |
| Device entry | System tab → **`Lexirise`** row (`SettingType::ACTION`, new `SettingAction::Lexirise`) → `LexiriseSettingsActivity` (`UiListActivity`), same as ~~`KOReaderSync`~~ (superseded 2026-09-29: KOReader Sync went in v0.2 V8) the other System rows |
| **Web page (revised 2026-09-24, claritise)** | **Its own page in the web UI menu: ~~Home · Files · Fonts · Settings · Lexirise~~ Home · File Manager · Settings · Fonts · Lexirise (corrected 2026-09-30).** It's served by the same web server used to upload books (file transfer / network mode), and it's where the API key is pasted. It's modelled on the Fonts page (`FontsPage.html` + `/api/fonts*`): `LexirisePage.html` at `/lexirise`, plus `/api/lexirise` (GET: masked settings and status; POST: save) and `/api/lexirise/test` (POST: runs `/v1/me`). **Lexirise entries are not added to the generic Settings page**, so there's one place for them, and no `SettingsList.h` web entries |
| Hooks in base files | ~~`SettingsList.h` (the one ACTION row)~~ `SettingsList.h` (Long-press Menu without Dictionary; the `Lexirise` ACTION row is appended in `SettingsActivity.cpp`, `firmware-base.md` §3; corrected 2026-09-30), `SettingsActivity.{h,cpp}` (the `SettingAction` value and its dispatch), `CrossPointWebServer.cpp` (register the `/lexirise` routes, delegating to ~~`src/lexirise/LexiriseWeb.cpp`~~ `src/lexirise/web/LexiriseWeb.cpp` (corrected 2026-09-30)), ~~**one `Lexirise` link in the menu of each of the 4 existing pages**~~ **one `<script src="/lexirise/nav.js">` on each existing page, which adds the `Lexirise` link to its menu** (`web/LexiriseNav.js`, corrected 2026-09-30) (`HomePage`, `FilesPage`, `FontsPage`, `SettingsPage`), and `I18n` strings. All marked `// LEXIPOINT:` / `<!-- LEXIPOINT -->` (`firmware-base.md` §3) |

## 1. The settings (v0.1)

| Group | Setting | Values | Default | Device | Web | Notes |
|---|---|---|---|---|---|---|
| **Account** | Lexirise lookups | On / Off | On | ✓ | ✓ | Off means StarDict only, and no network |
| | API key | *masked* | not set | ✓ keyboard | ✓ **paste** | Shown as `lx_••••••••nas` (last 3 only). See §2 |
| | Account | e.g. `claritise · Pro · key "lexipoint"` | — | ✓ read-only | — | From `GET /v1/me`, cached. `Not checked` until the first test |
| | Test connection | action | — | ✓ | — | Brings WiFi up, calls `/v1/me`, then shows `Connected as …`, `Key rejected`, or `No network` |
| **Japanese** | Lookups | On / Off | On | ✓ | ✓ | Off means Japanese books use StarDict only |
| | Readings | Kana / Romaji | Kana | ✓ | ✓ | **The same value** the card's reading-line tap switches (`popup-ui.md` §1). Changing it in either place changes both |
| | Offline dictionary | StarDict folders found, or None | the global dictionary | ✓ | ✓ | Listed like the reader's Dictionary setting (`DictionaryRegistry`) |
| **Chinese (Simplified)** | Lookups | On / Off | On | ✓ | ✓ | Off means Chinese books use StarDict only |
| | Offline dictionary | same | the global dictionary | ✓ | ✓ | |
| **General** | Language when a book doesn't say | Japanese / Chinese | Japanese | ✓ | ✓ | `languages.md` §1 step 2. It spans both languages, so it lives here |
| | Tags | text | `xteink` | ✓ keyboard | ✓ | Comma-separated. Tags can't be deleted through the API (`../reference/lexirise-api-notes.md`), so the help text says so |
| | Tag with book title | On / Off | On | ✓ | ✓ | (V2, `../v0.2/00-overview.md` C2) Each saved word also gets `book:<slug>`, the book title's ASCII slug (a Japanese or Chinese title: a short hash). One tag name per book, kept on the account for good (tags can't be deleted). Shown with Tags, while Lexirise is on |
| | Deck per book | On / Off | On | ✓ | ✓ | (V3, `../v0.2/00-overview.md` C4) Each book gets a Lexirise deck, `Lexipoint: <title>`, filled by its book tag: made (or found) after the book's first save, once a card in the book sits idle a few seconds. Shown while Tag with book title is on |
| | Keep WiFi on after a lookup | Off (connect each time) / 1 / 2 / **5** / 10 min | 5 min | ✓ | ✓ | D10 |
| **On the page** | Mark words on the page | On / Off | On | ✓ | ✓ | (v0.2 V9a, `../v0.2/page-annotations.md` §2 A1) Marks under the words of an analyzed page: solid, not saved; dotted, tracked or learning. A book can turn them off in the reader menu (**Page marks**, below; that row shows only while this is on and Lexirise is usable for the book). Shown while Lexirise is on |
| | Side buttons on a card | **Marked words** / Every word | Marked words | ✓ | ✓ | (V9a, A3) With a card open on a page with marks, the side buttons step between the marked words only. Shown while Mark words on the page is on |
| **Advanced** | Server | URL | `https://api.lexirise.app` | — | ✓ | Web only, so it can't be mistyped on the device. For a local proxy |

**Grouping rule (claritise, 2026-09-24):** anything that belongs to one language goes in **that
language's group**. A new language (e.g. Korean, or Traditional Chinese once H8 is picked up) adds
its own group, with the same rows where they apply. Only settings that span languages go in
**General**. On the web page the groups are sub-sections of **Lexirise** (~~categories
`STR_LEXIRISE`, `STR_LEXIRISE_JA`, `STR_LEXIRISE_ZH`~~ the page's own cards in `web/LexirisePage.html`; the
device's headings are `STR_LEXI_SET_ACCOUNT`, `STR_LEXI_SET_JAPANESE`, `STR_LEXI_SET_CHINESE`, `STR_LEXI_SET_GENERAL`
and `STR_LEXI_SET_ON_THE_PAGE`, corrected 2026-09-30), in the same order.

**A language's rows only show while it's on (claritise, 2026-09-24).** When a language's
**Lookups** is Off, its group collapses to that one toggle. Turning it back on brings its rows back,
with their values kept (turning a language off never clears its settings). The same applies to the
master **Lexirise lookups** toggle: when it's Off, everything below the Account group is hidden.
**Except each language's Offline dictionary (P13, claritise 2026-09-25: "what is functionally correct"):**
it answers that language's taps whenever Lexirise doesn't (Lexirise off, the language's Lookups off, no
WiFi), so it always shows: with Lexirise off the screen is the Account group, the two dictionaries and
"Language when a book doesn't say" (below); a language that's off collapses to its Lookups toggle and its
Offline dictionary. The web page shows the same rows: the device sends which ones (`shows` in `GET
/api/lexirise`, from `settings_screen::visibleRows`), so the page has no rule of its own (pinned by
`scripts/lexipoint/test_lexirise_page.py` and `WebApiState.SaysWhichRowsShowByTheDeviceScreensRule`).
**"Language when a book doesn't say"** shows whenever Han-only text uses it: always, except while Lexirise is
on with just one language on (`Settings::defaultLanguageApplies()`). With Lexirise off only the offline
dictionaries answer and the per-language switches don't apply; with Lexirise on and no language on, the
offline dictionaries answer too; either way this choice picks the dictionary for Han-only text (P13 review).
The web page's language names stay while Lexirise is off (only their toggles hide), so each Offline
dictionary keeps its heading, as on the device. With one
language on, that language is the fallback, and there's nothing to choose (as built, P7:
`Settings::fallbackLanguage()`, which `BookLanguage` uses for Han-only text; code that spans languages loops
over `kLanguages`, so a new language is added there and in `Settings::language()`; the stored choice is kept for
when both are on again). A consequence to know: with only Japanese on, Han-only sentences of an untagged
Chinese book are read as Japanese (sent to Lexirise as Japanese, or to the Japanese offline dictionary);
tag the book, or set its **Lookup language** in the reader menu (P9), to keep them Chinese. **Kept
(claritise, 2026-09-25: "what is the reasonable answer?"):** with one language on, a Han-only sentence is
almost always that language (Japanese headings, short lines, kanji compounds), so sending it elsewhere would
take lookups away from the common case to protect a rare one that a book's Lookup language now fixes.
- **Device:** `LexiriseSettingsActivity::buildScreen()` simply skips those rows, and a toggle triggers
  a rebuild, as `KOReaderSettingsActivity` does. There's no change to base code.
- **Web page:** it's our own page (`LexirisePage.html`). It has no rules of its own: it hides the rows
  the device's `shows` says are hidden (P13; before, it repeated the rules in its JS). No change to base code.

**Deliberately not settings:** anything about the card's look or layout (it's binding,
`popup-ui.md`), the level saved by a tap (you pick it every time with T L F K), and timeouts.

~~**Added later, hidden until built** (each ships with its feature, never as a dead toggle):
`Page marks` and the other annotation toggles
(`../v0.2/page-annotations.md`), and `Skip to unknown words` (A3).~~ **Superseded 2026-09-29, V9a:** built as §1's
*On the page* rows and the reader menu's **Page marks**; the other annotations are parked
(`../v0.2/page-annotations.md` §2 'Maybe later').

## 1a. The web page (`/lexirise`)

Open it from any browser on the same WiFi, while the device is in file transfer / network mode (the
same URL shown for uploading books).

```
 Home · File Manager · Settings · Fonts · [Lexirise]      (corrected 2026-09-30)
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
   Language when a book doesn't say   Japanese ▾   (hidden while Lexirise is on with one language on)
   Tags                      [ xteink ]
   Tag with book title       [on]
   Deck per book             [on]    (hidden while Tag with book title is off)
   Keep WiFi on after a lookup        5 min ▾
 On the page                                            (added 2026-09-30, V9a)
   Mark words on the page    [on]
   Side buttons on a card    Marked words ▾   (hidden while Mark words on the page is off)
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

## 1b. As built (P7): the device screen

Code: `src/lexirise/settings/SettingsScreen.{h,cpp}` (pure: which rows show, their groups, the edit a tap
makes; tests `test/lexirise_settings/SettingsScreenTest.cpp`) and `LexiriseSettingsActivity` (CrossPoint's
`UiListActivity`, as `KOReaderSettingsActivity`).

- **The row** is appended in `SettingsActivity.cpp` with the other device-only ACTION rows (`WiFi Networks`,
  ~~`KOReader Sync`,~~ …), right after ~~`KOReader Sync`~~ `WiFi Networks` (superseded 2026-09-29, v0.2 V8: KOReader
  Sync was removed). CrossPoint builds those there, not in `SettingsList.h`.
- **Groups** are the list's stock section headings (`ListItem::sectionHeading`, as the library list uses
  them): Account, Japanese, Chinese (Simplified), General. The hiding rules are §1's.
- **Every edit is a `SettingsPatch`** through `applyPatch`, the web page's own validation, then
  `SettingsStore::update`. A toggle flips; Readings flips Kana ⇄ Romaji; the language fallback flips
  Japanese ⇄ Chinese; *Keep WiFi on* steps through Off / 1 / 2 / 5 / 10 min; *Offline dictionary* steps
  through **Same as reader** (the global dictionary; *Same as CrossPoint* until M) and then each StarDict folder on the card whose
  name a setting can hold (`settings_screen::offeredDictionaries`: a plain name of up to 64 bytes).
- **API key:** the keyboard's password mode, starting empty (the device never shows the stored key; an
  empty entry keeps it). A value that isn't a key shows `Not a Lexirise key` on the key row until the next
  edit. Any other edit that fails shows `Couldn't save` on its own row the same way. A new key is checked at once, from the next loop pass (the keyboard's result runs mid
  activity switch): the Account row reads `Checking...`, then the result.
- **Account:** `<name> · <plan>` once connected; otherwise `Not set`, `Not checked`, `Checking...`,
  `Key rejected`, `No network` or `Could not connect` (`settings_screen::accountLine`; `Connected` alone if the account has no name or plan). `/v1/me` gives no
  key name, so the `key "lexipoint"` part of §1's example isn't shown.
- **Test connection** runs the check there and then (the Account row shows `Checking...` first; a slow
  network blocks the screen for up to one call), then gives back any WiFi Lexipoint brought up: Settings
  isn't reading (`offline-and-errors.md` §5).
- **A tap means the row the user saw**: `listCount()` and taps read the rows of the last built frame
  (`drawnRows_`, published by `buildScreen` on the render task under a mutex, `settings_screen::rowAt`),
  and taps wait while an edit's frame is on its way (`settings_screen::TapGate`: the list lays out its new
  rows before the panel refresh, so during it a tap would hit a row that moved). A tap then is dropped:
  tap again once the screen has settled.
- **Offline dictionary** (`lookup/StarDictChoice.h`): a tap on a Japanese/Chinese word (kana, Han, ー, 々)
  uses its language's own folder when one is chosen, else CrossPoint's Dictionary setting; any other word (an
  English word in a Japanese book) goes to CrossPoint's, as before. So does every tap in a Traditional
  Chinese book (zh-TW / HK / MO / Hant, parked by H8): the Chinese group is Simplified
  (`LanguageDecision::dictionaryLanguage`). The language is what the tapped text is
  (`LanguageDecision::detected`), so the choice **still counts while that language's Lexirise lookups are
  off** and while Lexirise is off, which is exactly when StarDict answers every tap in it (the row always
  shows, P13). With Lexirise off, Han-only text uses "Language when a book doesn't say" (shown then), not
  the hidden per-language switches (`Settings::fallbackLanguage()`). Word select reopens its dictionary when the language changes, and opens without CrossPoint's
  dictionary when either language has its own. If the chosen folder can't be opened (removed from the
  card), CrossPoint's own dictionary answers instead.

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
- It's stored in `/.lexirise/config.ini` on the SD card, **in plain text**, the same as the base's
  WiFi and KOReader credentials. The user docs say so: anyone with the SD card has the key, and a lost
  card means rotating the key.
- **The web file manager can't reach `/.lexirise/`** (corrected in P1). Its listing hides dot items, but
  `/download`, upload, rename, move and delete only checked the last path segment, so
  `/download?path=/.lexirise/config.ini` served the key and delete + upload could replace the file. A
  hook now refuses any path with a hidden segment at every entry point (`firmware-base.md` §3).
  **FAT short names too** (P1 review): on FAT cards the folder is also reachable as `/LEXIRI~1`, which
  doesn't start with a dot. A dot name's short name always carries a `~N` tail, so any segment with a
  `~` is looked up on the card and refused if its real name is hidden (or exists but can't be read).
  A `~` name that doesn't exist is allowed (it can't be an alias), and ordinary `~` names like
  `Tolkien ~ The Hobbit.epub` keep working. This applies to the file manager ~~and to WebDAV (which only
  checked typed names, and whose `PROPFIND` listed hidden folders)~~ (superseded 2026-09-29, v0.2 V8: WebDAV was
  removed), and it also closes the same hole
  for the base's `/.crosspoint` (saved WiFi passwords). `websmoke.py` probes it.
  **As SdFat opens it** (P1 review round 4): SdFat skips a segment's leading spaces and trims trailing
  dots/spaces, so `/ .lexirise` *is* `/.lexirise`. Paths and newly created names (mkdir, rename, move,
  upload~~, WebDAV~~) are checked after that same trimming, so a hidden folder can neither be reached nor
  planted (a planted `/.lexirise/config.ini` with its own `base_url` would otherwise receive the key the
  user pastes next).
- **The key card says where the key goes** whenever `base_url` isn't Lexirise's own server.
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
tag_book=1
deck_per_book=1
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
- **`tag_book`** (V2): `1` adds each book's `book:<slug>` to its saves. A file from before V2 has no line, so
  it's on (the default) and written on the next save. Each slug's title is kept apart (also for "Met before"'s book
  titles, copied as a card opens: `BookTagStore::list` via `LiveSource::setBookTitles`, v0.2 V4, `../v0.2/00-overview.md`
  C14), in
  `/.lexirise/book-tags.ini` (`<slug>=<title>` lines, newest last, the oldest forgotten past 100 books), written
  the first time a card opens in a book while this is on (`settings/BookTags`).
- **`deck_per_book`** (V3): `1` gives each tagged book a Lexirise deck. The decks' ids are kept apart, in
  `/.lexirise/decks.ini` (`<ja|zh>:<slug>=<deck id>` lines, newest last, 100 at most; `deck/BookDeck`).
- **`[page]`** (v0.2 V9a): `mark_words` and `step_marked`, `1` or `0` (both `1` by default: a file from before V9a has
  no section and gets them on its next save). Each book's **Page marks** row in the reader menu (and its toolbar's More
  panel) is kept apart: `/.lexirise/marks-off.ini`, the paths of the books turned off, one per line, newest last
  (`config::kBookMarksOffMax` books at most, the oldest forgotten, its marks back on; `settings/BookMarks`), written
  crash-safely (`SafeFile`) on each tap.
- **Ignored words** (v0.2 V5, C17; no setting): the words the reader ignored from the card's ⋯ tab ("stop marking this
  word on the page"; never sent to Lexirise), in `/.lexirise/ignored.ini`, one key per line, the ids first and then the forms, each part newest last:
  `<ja|zh>:<entry key>` (`lookup::entryKeyOf`: the lemma's entry id, else the word's own), or
  `<ja|zh>:~<dictionary form>` for a word Lexirise gave no entry id (one line, at most
  `config::kIgnoredTextMaxBytes`, never cut). At most
  `config::kIgnoredIdsMax` ids and `kIgnoredTextsMax` forms, the oldest forgotten past them; the file is capped at
  `kIgnoredMaxBytes` (a larger one is unreadable and set aside as `.bad`); a line that isn't a key (another language,
  a hand-written note) is skipped, and **not kept**: the next write drops it (unlike `config.ini`'s unknown keys).
  Written crash-safely like the others (`SafeFile`), once per change, outside the card's render lock; read once, as a
  card opens (`settings/IgnoredWords`). Why the key is the entry id: `../v0.2/00-overview.md` C17 "As built (V5,
  local)".
- **The vocab mirror** (v0.2 V7a, C13; no setting): a read-only copy of the user's Lexirise vocabulary, one file per
  language, `/.lexirise/vocab-ja.bin` and `/.lexirise/vocab-zh.bin` (what it's for, and when it syncs:
  `../v0.2/page-annotations.md` §1.2 "As built (V7a)"). **Binary**, little-endian (`vocab/VocabMirror`): a
  `config::kVocabHeaderBytes` header, then one `config::kVocabRecordBytes` record per saved word, sorted by entry id.

  | Header offset | Bytes | Field |
  |---|---|---|
  | 0 | 4 | `LXVM` |
  | 4 | 2 | version (2: the incremental pass's progress; 3, V7b R5: 20-byte records with each state's time; an older file is set aside and synced again) |
  | 6 | 2 | the record's size |
  | 8 | 2 | the language code (`ja` / `zh`) |
  | 10 | 1 | flags: 1 synced (a full pass has ended), 2 a full pass under way, 4 its list count (offset 40) is set, 8 a page of it had no count (it sweeps nothing), bits 4–5 how far short of `kVocabPageOverlap` its next page's slack is (a re-read: all of it), 64 the next full pass was brought forward once (no count) |
  | 11 | 1 | the full pass's generation (each record's mark) |
  | 12 | 4 | records |
  | 16 | 8 | the cursor: every change up to this `updated_at` (ms since the epoch) is in |
  | 24 | 8 | the running full pass's newest `updated_at` (its cursor once it ends) |
  | 32 | 4 | the running full pass's next offset |
  | 36 | 4 | when the last full pass ended (seconds since the epoch; 0: unknown) |
  | 40 | 4 | the running full pass's list count at its last page (deletions: `../v0.2/page-annotations.md` §1.2) |
  | 44 | 4 | the incremental pass under way: its next offset |
  | 48 | 8 | its newest `updated_at` (the cursor once it ends) |
  | 56 | 4 | its list count at its last page |
  | 60 | 1 | its flags: 1 under way, 2 its count set, 4 a page of it had no count, 8 it has read again (no tie stop: V7b, 2026-09-28; 0 in older files), bits 4–5 how far short of the overlap its next page's slack is |
  | 61 | 4 | ~~when the last pass ended~~ the time the mirror is complete as of: the start of the last incremental pass that ran from the top to its end (R3; seconds since the epoch; 0: unknown; V7b R1, 0 in older files) |
  | 65 | 1 | V7b R7: 1 the mirror refused an entry at its cap (absence no longer says unsaved), 2 the full pass under way refused an item (0 in older files) |
  | 66 | 2 | spare (0) |
  | 68 | 4 | CRC-32 (IEEE) of bytes 0–67 and every record |

  A record (20 bytes, version 3): the entry id (`dictionary_id`), the saved expression's id, `next_review_at`
  (seconds; 0: none), the time its state was known (seconds; 0: unknown), each 4 bytes (a saved id 0: a removal,
  always flagged 2); the level (0–4), flags (1: suspended; 2: put by a card's live answer, not a page; 4: the reader's
  own write), the full pass that last saw it, and a spare byte. At most `config::kVocabMirrorMax` records (`kVocabMaxBytes`). A file that doesn't check
  out (magic, version, language, size, CRC, order, a slack past the overlap in either pass's bits 4–5) is set aside as
  `.bad` (flag bits it doesn't know are ignored) and the mirror synced again from Lexirise; nothing in it is the
  user's own (it's all in their account), so hand edits aren't kept. Written crash-safely like the others (`SafeFile`:
  `.tmp`, `.bak`), outside the card's render lock (superseded 2026-09-28, V7c R9: but for a card that leaves without
  `end()`, sleep or the stack cleared, which writes under the lock `exitActivity` holds, as the reader's page loads do SD
  I/O; the lemma cache the same): after a page that changes the mirror or the pass's progress (a
  quiet incremental pass writes nothing), on an idle card after its answers changed it, and as the card closes; read
  once per boot per language, on the first idle card in it.
- **The page cache** (v0.2 V7b, C12; no setting): each analyzed page's `analyze/text` answer, compact,
  `/.lexirise/pages/<book>/<section>-<start>.bin` (`<book>`: the FNV-1a 32 of the book's path in 8 hex digits;
  `<section>`: the spine index; `<start>`: the page's first visible character in it), `page/PageAnalysis`. **Binary**,
  little-endian: a 48-byte header (`LXPA`, version 1 (u16), the language code (2), flags (1: refined and merged), three
  spare, the page text's UTF-16 length (u32) and FNV-1a 32 (u32), when it was analyzed (u64 ms since the epoch; 0:
  unknown), the counts of occurrences, entries, saved states and the pool's bytes (u32 each), a CRC-32 of the header's
  first 44 bytes and everything after it), then occurrences (32 B: start, end, entry, lemma entry, u32 each; the word,
  the lemma and the reading as u16 place and length in the pool; flags, 1 word-like; three spare), entries' facts
  (20 B: id, rank, the frequency's float bits; the reading and part of speech), saved states (16 B: id; the saved id
  as a pool string; the level; three spare; the seen count), both sorted by id, then the pool (UTF-8, at most
  `config::kPageMaxPoolBytes`). A file that doesn't check out is removed. `/.lexirise/pages/index.bin` (crash-safe,
  `SafeFile`): a 16-byte header (`LXPI`, version 1, the record's size, the count, an FNV-1a 32 checksum) and 12 bytes
  per kept page (the book, the section, the start: u32 each), oldest first, at most `config::kPageCacheFiles`, saved
  every `config::kPageIndexSaveEvery` pages and as the reader closes; ~~one unreadable is set aside (`.bad`) and the
  cache starts again~~ (V7b R3/R4) one unreadable (it doesn't parse, is too large, or fails to read) takes the whole
  `/.lexirise/pages/` folder with it and the cache starts again. Nothing in them is the user's own (it's all from
  Lexirise, asked again when missing).
- **The lemma cache** (v0.2 V7c, C21; no setting): phase B's `dictionary/lookup` answers as the card keeps them,
  `/.lexirise/lookups/<ja|zh>/<nn>.bin` (`<nn>`: the text's FNV-1a 32 modulo `config::kLookupBuckets`, 2 hex digits),
  `lookup/LookupCache`. **Binary**, little-endian: ~~a 16-byte header (`LXLK`, version 1 (u8), the language (u8), the count
  (u16), the records' bytes (u32), a CRC-32 of the records)~~ (superseded 2026-09-29, v0.2 V8: format 2) a 20-byte
  header (`LXLK`, version 2 (u8), the language (u8), the count (u16), the records' bytes (u32), a CRC-32 of the
  records, and the account's tag (u32: FNV-1a 32 of the API key, `lookup::accountTag`; the key itself is never
  written)), then each record: its length after the field (u16), when
  it was fetched (u32 s since the epoch), the rank (u32), the frequency's float bits (u32), then as u16-length strings
  the text looked up, the word, the reading and the level, the sense count (u8) and each sense's translation and part
  of speech (strings). Oldest first, at most `config::kLookupBucketMax` records and `kLookupBucketMaxBytes`; a record
  over `kLookupRecordMaxBytes` isn't kept. No saved state in it. Written plainly (regenerable); a file that doesn't
  check out is removed (so is a format-1 file from before V8). A bucket written under another API key (another
  account, whose translation target may differ) reads as empty, and its next write drops the other key's answers. An
  answer keeps the key it was fetched under until it's written (V8 R4): one fetched before the key changed (over the
  web page, with a card open) is dropped, not written as the new account's. Nothing in it is the user's own (it's all from Lexirise, asked again when missing or older than
  `config::kLookupMaxAgeS`).
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
  all three outcomes, turn Chinese off and on again ("Language when a book doesn't say" hides and comes back with its value
  kept; since P13 Chinese's Offline dictionary stays), and the reading setting and card tap staying in sync across a reboot.
- On device (P13): turn Japanese off: Readings hides, its Offline dictionary stays; turn Lexirise off: the
  screen is the Account group, both dictionaries and "Language when a book doesn't say" (7 rows,
  `lxctl settings-smoke`'s minimum), and the web page shows the same rows; turn Lexirise on with both
  languages off: "Language when a book doesn't say" shows. Upgrade note: Lexirise off with only Chinese's switch on and the default at Japanese now reads
  Han-only text of an untagged book with the Japanese dictionary (the row shows, so it's visible).
- V2: `tag_book` read, written and defaulted on for a file from before it (`SettingsTest`), patched from the web
  page and toggled by the device row (`SettingsPatchTest`, `WebApiTest`, `SettingsScreenTest`); the slug
  (`test/lexirise_language/BookSlugTest.cpp`) and `book-tags.ini` (`BookTagsTest.cpp`). With Lexirise on, the
  screen has 13 rows (`lxctl settings-smoke`'s maximum).
- V3: `deck_per_book` (`SettingsTest`, `SettingsPatchTest`, `WebApiTest`, `SettingsScreenTest`: the row shows only
  with Tag with book title on); `decks.ini` and the deck flow (`test/lexirise_deck`, `LiveDeck` in
  `test/lexirise_card/LiveSourceTest.cpp`). With Lexirise on, the screen has 14 rows (`lxctl settings-smoke`'s
  maximum).
- v0.2 V7a: the vocab mirror's file, sync and store (`test/lexirise_vocab`), the card's side of it (`LiveMirror` in
  `test/lexirise_card/LiveSourceTest.cpp`), the streamed page (`VocabPageTest`, `JsonStreamTest` in
  `test/lexirise_net`).
