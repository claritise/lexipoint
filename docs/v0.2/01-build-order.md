# v0.2 Build Order: the executable plan

> **A program for a builder, human or agent,** like `../v0.1/01-build-order.md`: linearly ordered phases, each
> with a goal, what to read, what to build and a gate. The design lives in `00-overview.md` (the candidates
> C1–C25) and the specs next to it (`page-annotations.md`, `slimming.md`, `manga.md`). Where this doc disagrees
> with a spec on *what* to build, the spec wins; on *order*, this doc wins. Started 2026-09-26 (claritise: "ok
> start v0.2", then "the documented order": `00-overview.md` "Suggested order after v0.1").

## How to run this document

Exactly as v0.1's "How to run this document", "Global rules" and "The uniform gate"
(`../v0.1/01-build-order.md`): one phase = one branch `lexi/<phase-id>` from `main` = one gate = one ledger
row below; the review loop until two clean rounds; squash, merge into `main`, ledger row in the same history;
pushing needs claritise's OK; document as you go. Phase IDs here are `V<n>`.

**Also for v0.2:**

- **The card is binding** (D12): anything that adds to or moves something on the approved card waits for
  claritise's sign-off on a mockup in the reference HTML's style (`../v0.1/reference/card-reference.html`),
  and then passes the design conformance gate (`../v0.1/01-build-order.md`). Filling a slot the approved card
  already draws (Met before, the Form tab's conjugation) needs no new sign-off.
- **The X4 Pro is touch-only** (D15): where a candidate describes ▲▼, Confirm or "hold ⏎" (C3, C9, C17), the
  touch design replaces it. That's part of the sign-off.
- **Measure before specifying** what the reference doesn't say (the second call's timing, `grammar[]`'s
  shape, a deck mixing units): with the dev key from the Mac (`~/.lexirise_key`), responses kept in
  `research/` (gitignored), findings in `../reference/lexirise-api-notes.md`. Writes to claritise's account
  only with their OK, and undone afterwards.
- **One phase branch at a time** in this repo; other sessions' docs-only commits to `main` are fine, but rebase
  nothing: merge `main` into the phase branch if it moved.

## Order, and why it differs from the suggestion

The suggestion (`00-overview.md`, 2026-09-25) is: the second `analyze/text` call first; then C1 → C2 → C4 → C7
→ C9 → C14 → C15 → C16 → C17 → C10 option 1 → C3 → C12 → C13 → C21; C24; the slimming (C23 with C22); then
`page-annotations.md` → C10 → C19 → C5; C11 in v0.3. Kept, with three changes found by checking what v0.1
built (2026-09-26):

1. **C9 is done:** v0.1's card sets a saved word's level by tapping T / L / F / K (`PATCH proficiency`, with
   Undo; P5). Only its on-device check is owed (V2's gate takes it).
2. **The card additions that need a sign-off (C1's footer, C7's count, C15's "also" reading, C3's sentence
   save) are gathered into one design phase (V6)**, asked for once, so the phases that don't touch the card's
   design don't wait on it.
3. **C24 (a release) is claritise's, not a phase:** publishing is `firmware/scripts/lexipoint/publish_release.py`
   with their OK, whenever they want a release (`../v0.1/firmware-base.md` §6). It's listed as a milestone.

## Phase map

| Phase | What | Candidates | Blocked on |
|---|---|---|---|
| V1 | Whole words, not refined fragments (re-planned after measuring) | C19 (pulled forward) | — |
| V2 | Book tags on every save | C2 | — |
| V3 | A deck per book | C4 | V2 |
| V4 | Met before, and the conjugation | C14, C16 | — |
| V5 | Ignore a word | C17 (Ignore) | — |
| V6 | Card additions: design, then build | C1, C7, C15 / C10 option 1, C3, C17 (Save sentence) | claritise's sign-off |
| V7 | Page analysis, the vocab mirror, caches | C12, C13, C21 | — |
| V8 | Slimming | C23, C22 | S1 for its font step only |
| V9 | Page annotations | C6 (`page-annotations.md`) | V7 |
| V10 | The sense and reading from the sentence | C10 (`analyze/context`) | V1; placement sign-off |
| V11 | Grammar on the card | C19 | V1; placement sign-off |
| V12 | Difficulty preview | C5 | V7 |
| — | A release (milestone) | C24 | claritise |
| v0.3 | Reviews on the device | C11 | — |

## V1: Whole words, not refined fragments

**Re-planned 2026-09-26 after measuring** (`../reference/lexirise-api-notes.md`, "The second `analyze/text`
pass, measured"; claritise chose "rejoin over-split words"). Lexirise's refined pass takes 35–80 s, is cached,
and cuts words into grammatical morphemes (深深 → 深 · 深, 一边 → 一 · 边, 小さな → 小さ · な, 长得 → 长 · 得).
Any sentence it has refined before reaches the card that way on the first call, which is why 深深 showed as 深.
Swapping to the refined word would make cards worse, so the card never does; the refined pass is used only for
grammar (V11).
**Goal:** the card shows the whole word the reader tapped, even when Lexirise's answer came back refined.
**Read:** the measurement above; `../v0.1/lookup-flow.md` (①, the phases); `lookup/LexiriseLookup.cpp`,
`api/Responses.*`.
**How:** `analyze/text` with `fast: true` still returns the word-level split for a refined sentence, with each
word's entry, reading, part of speech, rank and saved state (not its lemma). So when ① answers with
`morphoPending: false` (already refined), `analyzeTap` sends one more, `fast` ① for the same text, and
`lookup::wholeWords` merges the two for the **whole sentence** (so stepping goes word by word, and the strip
shows whole words): for each word-level token, the refined token with exactly its span (it keeps the lemma);
else the word-level token if Lexirise ranks it (a dictionary word the refined pass cut up: 深深, 一边, 小さな);
else the refined pieces that tile it (the refined pass split a bad token: 一日中雨 → 一日中 · 雨, which has no
rank); else the word-level token. States and facts come from both, the word-level answer's first (the refined
one can be cached). A whole word has no lemma: the card uses its own entry, as v0.1 does when a lemma is
missing. If the extra call fails (offline, 429, unreadable), ①'s answer stands.
**Cost:** one more call (~1 s before phase A) for each analysis that comes back refined. Lexirise caches
refined answers for everyone, so over time that's most sentences: measure the tap-to-card time on the device,
and watch the rate limit when stepping through many sentences (each is two analyze calls then).
**Gate:** the uniform gate; host tests (a refined answer with a longer fast word → swapped; a refined answer
where the fast word is the same → not; a first-pass answer → no second call; the fast call failing → ①'s token;
a tap on a token the fast answer splits differently at both ends); on the device, 这首歌深深地打动了我。 shows 深深,
and 他一边吃饭一边看电视。 shows 一边.

## V2: Book tags on every save

**Goal:** every word saved from a book carries `book:<slug>`, so it's findable per book (and V3's deck).
**Read:** C2; `../v0.1/settings.md` §1 (the hidden "Tag with book title" row) and §3; the save request
(`api/Requests.cpp`).
**Build:** the slug (ASCII-folded title; a deterministic fallback for a title with no ASCII, e.g. a Japanese
one: as built, a hash of the title, C2's "As built (V2)"), `book:<slug>` added to the tags of each save, and a book → slug → title record on
the SD card (V4 shows the title). Settings: the "Tag with book title" row, on by default (web page and device
screen). Chapter and session tags stay unbuilt (C2: they pile up, and tags can't be deleted).
**Gate:** the uniform gate; host tests of the slug (Latin, Japanese, Chinese, punctuation, two books with one
title); on the device, a save from a Japanese book carries `book:…` (checked with the dev key, then undone),
plus v0.1's owed "a level change and its Undo" (C9).

## V3: A deck per book

**Goal:** each book gets a Lexirise deck that fills itself from V2's tags.
**Read:** C4; the decks API in `../reference/lexirise-api-notes.md`.
**Measure first:** can a dynamic deck mix word and sentence units (C3 later)? `GET /v1/decks` shape.
**Build:** on a book's first save: find a deck with the title (`GET /v1/decks`) or create a dynamic
`user_tag_filter` deck on `book:<slug>`; remember it in `/.lexirise/decks.ini`; a 404 later recreates it. The
save never waits on it or fails because of it. A settings row, "Deck per book", default on.
**Gate:** the uniform gate; host tests (new book, existing deck found, deleted deck recreated, offline); on the
device, one book's deck appears in Lexirise (with claritise's OK; kept or removed as they say).

## V4: Met before, and the conjugation

**Goal:** fill two slots the approved card already draws.
**Read:** C14, C16; `../v0.1/popup-ui.md` (Context and Form tabs); `card/CardLayout.cpp`, `card/LiveWord.cpp`.
**Build:** C14: parse `stateByEntryId`'s `notes` and `user_tags` (skipped today), show "Met before" with the
saved sentence and, from V2's record, the book's title (a book saved on another device has no title here: the
sentence alone). C16: a small Japanese deinflection table (the KOReader plugin's rules as the reference) that
names the form (te-form, causative-passive past…) and the steps from dictionary form to the page's.
**Gate:** the uniform gate; host tests of the deinflection table and the parsing; goldens unchanged except the
filled slots; on the device, a saved word met in another book, and a conjugated verb.

## V5: Ignore a word

**Goal:** the ⋯ tab's "Ignore" row works (it says "Not in this version yet" today).
**Read:** C17; `card/CardController.cpp` (the actions).
**Build:** `PATCH suspended: true` with an Undo toast like the save's; the card reflects it. (A1 marks follow in
V9.)
**Gate:** the uniform gate; host tests (ignore, undo, offline); on the device, ignore then undo.

## V6: Card additions (design, then build)

**Goal:** the additions that change the approved card, signed off together, then built.
**Design (first, for claritise):** mockups in the reference HTML's style for: C1 the session counter
(`3 saved · 11 looked up`) and C7 the vocab count (`1,204 words in Japanese`), with where they show (the card
footer, the book-close summary, or both); C15 / C10 option 1, the "also" reading (`ichinichi · also
tsuitachi`); C3 saving the sentence as a card, and growing or shrinking it by a clause, by touch; C17's "Save
sentence" row. One sign-off in chat, quoted in the ledger.
**Build:** what's signed off. C7 is one `GET /v1/vocabulary?limit=1` per session (check `languageCount` first);
C3 is `mode: "sentence"` with an explicit `proficiency`.
**Gate:** the uniform gate and the design conformance gate for the new states.

## V7: Page analysis, the vocab mirror and caches

**Goal:** the foundations `page-annotations.md` needs, which also make lookups faster.
**Read:** C12, C13, C21; `page-annotations.md` §1.
**Ask first:** C13's order entry says "if Q2 comes back yes"; Q2 is answered, so confirm C13 goes ahead.
**Build:** as `page-annotations.md` §1.1 (one analyze per page, the next page prefetched, refined results only:
V1) and §1.2 (the vocabulary on SD, synced incrementally); C21's lemma cache and warm TLS where measured to pay.
**Gate:** as `page-annotations.md` §5 for these steps.

## V8: Slimming

**Goal and gate:** `slimming.md` (approved list). Steps 1–5 and 7 go ahead; step 6, the font, waits for S1
(the family and letterforms). The phase ends by removing the `LEXIRISE` gate and the Lexirise-off build, as
`slimming.md` §2 says, and the cppcheck style suppressions for `src/lexirise/`.

## V9: Page annotations

`page-annotations.md` §5 steps not done in V7, each with its bench phase before it touches the network.

## V10: The sense and reading from the sentence

C10 with `POST /v1/analyze/context`, on V1's refined offsets. **Needs:** claritise's placement sign-off, their
decision on Open #16 (the save uses the contextual reading), and Lexirise's answer to Open #7.

## V11: Grammar on the card

C19's grammar part, once V1 has read a live `grammar[]` (its shape is in `../reference/lexirise-api-notes.md`). Map grammar to words by its `anchors` (character spans), not `indices`: V1's merge renumbers the occurrences. **Needs:** claritise's placement sign-off.

## V12: Difficulty preview

C5, on V7's page analysis and mirror.

## Status ledger

| Phase | Status | Branch / commit | Tests | Notes |
|---|---|---|---|---|
| V1 | **done**, checked on the device (claritise, 2026-09-26: "Rejoin over-split words") | merged into `main` (wip on `lexi/V1-wip-archive`) | 893 host (+17: `WholeWords` ×9, `WholeWordsLookup` ×4, `LiveWholeWords` ×3, a request), 115 Python | Measured first (`tools/lexirise/probe_morpho.py`, `probe_refine.py`): the refined pass over-splits and is cached, so the phase's premise (take the refined word) was dropped; `fast: true` recovers the whole word. Reported to Lexirise by claritise (draft in chat, 2026-09-26). **Built:** `api::analyzeWordsRequest` (`fast: true`) and `LexiriseApi::analyzeWords`; `lookup::wholeWords` (a pure merge: each word-level token, or the refined token with the same span for its lemma; facts and states from both); `analyzeTap` asks for it only when ① came back with `morphoPending: false`, and keeps ①'s answer if it fails. The whole sentence is merged, so stepping between words goes word by word. **R1** (no must; 6 should): the build order said only the tapped word was swapped (the code merges the sentence) → rewritten; merging the whole sentence brought back 一日中雨, the one bad token the refined pass fixes → a whole word is taken only when Lexirise ranks it (the bad merge has no rank; measured), else the refined pieces; the notes still said "use the default everywhere" and didn't record that `fast` carries saved state → recorded (checked live); the cached refined state overwrote the fresher word-level one → the word-level answer's wins; missing tests (a crossing split, 一日中雨, a kept lemma, the card: a refined tapped sentence, the next sentence's own second call, the same whole word twice) → added; the cost (most sentences, over time) → stated, to measure on the device. Nits: stale comments, a shared request builder. **R2 clean** (nits taken: the merge's branches flattened; the word-level answer alone decides whether its own words are saved, so a word unsaved since the refined answer was cached isn't shown saved; the extra call is logged apart; a note for V11 to map grammar by anchors). **R3 clean** (nits taken: a saved lemma's state pinned through the merge; the ranked-inflected-form risk noted in the reference). **On the device 2026-09-26 (`../v0.1/device-checks.md`):** 深深 whole on the card, stepping 深深 → 地 in one step; the word-level call added 0.7 s. |
| V2 | **done (host); device check owed** (claritise, 2026-09-26: "keep working through the phases autonomously") | merged into `main` (wip on `lexi/V2-wip-archive`) | 919 host (+26: `BookSlug` ×8, `BookTags` / `SaveTags` / `BookSaveTags` / `BookTagStore` ×15, a `LiveSave` book tag, a `SettingsScreen` toggle, a `Settings` upgrade case; row lists and `tag_book` added to the settings, patch and web tests), 115 Python (row counts: `test_lexirise_page`, `test_lxctl`) | **Built:** `text::bookSlug` / `bookTag` (ASCII-folded title, ≤ 35 bytes so `book:<slug>` fits a user tag's 40; fewer than 3 ASCII letters/digits → `h` + FNV-1a-32 of the title; untitled → of the path); `saveTags` (the user's tags, then the book's while on, once) and `bookSaveTags` (records the slug's title first); `BookTagStore` over `/.lexirise/book-tags.ini` (SafeFile, `<slug>=<title>`, newest last, 100 books / 16 KB, written only for a new slug); the setting "Tag with book title" (`tag_book`, on) through `Settings`, `SettingsPatch`, the web API and page (`tagBook`) and the device screen (`Row::TagBook` after Tags, 13 rows at most: `lxctl` `SETTINGS_ROWS_MAX`); the hook: `EpubReaderActivity` gives word select the title and path with `setBook`, and `openLexiriseCard` passes `bookSaveTags(...)` to `LiveSource`. Docs: C2 "As built (V2)", `../v0.1/settings.md` §1, §1a, §3, `../v0.1/firmware-base.md` §3. **R1 clean** (nits taken: one rule for an untitled book, `text::isUntitled`, so a title of only control characters is untitled for the tag and the record alike; the record's slug from `bookSlug` itself; C2 says the 40-byte tag cap is Lexipoint's, the hash is of the trimmed title, and a save carries up to `kMaxTags` + 1 tags; tests: the book tag in a save's POST body, a record retried after a failed write, a control-characters-only title). **R2 clean** (nit taken: a stale comment). **Owed on the device:** a save from a Latin-titled and a CJK-titled book carries `book:…` (checked in Lexirise), `/.lexirise/book-tags.ini` written once per book, the setting off and on from both screens, 13 settings rows. |
| V3 | **done (host); device check owed** (claritise, 2026-09-26: "keep working through the phases autonomously") | merged into `main` (wip on `lexi/V3-wip-archive`) | 975 host (+56: `DeckState` ×10, `FindBookDeck` ×3, `BookDeckFor`, `Decks` ×2, `DeckStore` ×6, `LiveDeck` ×18, deck requests, `ResponsesDecks` ×8, `LexiriseClient` sent, `SettingsWatch`, a `SettingsScreen` toggle; `deck_per_book` in the settings, patch and web tests), 137 Python (+22: `test_lxctl` `DeckSmokeRules` ×18, `DeckSmokeDriver` ×4; `test_lexirise_page`: 14 rows) | **Measured first (read-only):** the reference's deck API (`../reference/lexirise-api-notes.md`, "Decks": a deck is words or sentences, no mixed mode; 404 for a gone deck); live, `GET /v1/decks` gave the empty list (the account has no decks, so a listed deck's shape is unseen) and `GET /v1/decks/{unknown}` a 404. No deck was created. **Built:** "Deck per book" (`deck_per_book`, on; web and device rows after Tag with book title, shown while it's on; `lxctl` `SETTINGS_ROWS_MAX` 14); `api::deckListRequest` / `deckRequest` / `createDeckRequest`, `parseDeckList` / `parseCreatedDeck`; `deck/BookDeck`: per book and per boot, in the store (check a recorded deck once per boot, 404 → forget; else list and reuse the book's own word deck by tag or title, in its language; else, only after a readable and whole list without it, create: at most one sent creation per book per boot, so a lost answer's deck is found by a later list; any other failure waits for the next tagged save), `findBookDeck`, `bookDeckFor`, `sendDeckStep`, `DeckStore` over `/.lexirise/decks.ini`; `CardSession::shouldFetchDeck` (a step only on a card idle `kDeckIdleMs`, with no write queued, no toast or timer due and no finger on the screen; nothing at close); `LexiriseApi::deck` (the service sends it like a write). **Owed on the device (with claritise's OK):** one book's deck appears in Lexirise, filled with its saved words; the list's real field names (open in the reference notes); side-button presses during a deck call (lost, rarely); how many quick-closed cards pass before the deck appears; `lxctl.py --creates-a-deck deck-smoke` (creates a real deck and saves two words: only with claritise's OK; a dev build, `env:x4pro`), which checks the calls from the log (not across sleep). **R1** (1 must, 9 should): answers in another shape (camelCase, `deckId`, a `data` wrapper) or a creation whose answer was lost could POST a new deck on every card → tolerant parsers (both namings, `decks`/`data`/bare list, `deck`/`data`/flat creation), an unreadable list is an error (never "no deck"), a creation of unknown outcome is pending (`=?`, kept across reboots: only the list may find it), a title match counts when the type isn't said but never on someone else's starred deck, a created deck that isn't a tag deck is logged, an unreadable answer's head is logged; a 404 whose Forget couldn't be saved asked again every pass → memory changes even when the SD write fails; deck calls at close, in the card's state and under the render lock, per card → redesigned per book and per boot in the store, a step only on an idle card (`kDeckIdleMs`), never at close, applied outside the lock; the policy tested in `CardSession`, not by re-enacting the activity; tests added (camelCase list and creation, an unreadable creation then a second card, the 404 loop, quick closes then a later card, a write queued first, a starred deck); the deck names in named constants and one `isPlainId`; the docs no longer overclaim. **R2** (1 must, 2 should): a creation whose connect failed (or timed out, or whose write failed) was taken as maybe made and left pending for good → `ApiResponse::sent` (the whole request was written) and only a sent creation can be pending; the pending escape (a pending line from an earlier boot is cleared by a readable, whole list without the deck, then one creation; at most one creation per book per boot; `parseDeckList` says whether the list is whole); a side-button press during a deck call is lost → documented, and owed on the device; a new book needs two idle windows → documented, and owed. Nits: `api::kHttpNotFound`; deck ids must be plain; a card whose saves don't carry the tag, or with nothing wanted, stops early; an undone save still leaves the deck wanted, documented. **R3** (no must, 1 should): a list that dropped an entry (an id it couldn't use, or none) still counted as whole, so a pending deck of ours listed that way was made again once per boot → whole only when every entry was read. Nits: the idle time starts when the card opens (`CardSession::opened`); Deck per book or Lexirise turned off from the web page while a card is open stops its deck steps (`deck::deckAllowed`, re-read when the settings' revision changes); tests pin a retried stale GET keeping `sent`, and no deck step while a redraw or input waits. **R4** (no must, 4 should): the pending state never changed a creation (traced: every step and answer creates the same, this boot and the next; the one difference was that after a lost answer a later save listed once instead of on every save, fewer calls and nothing visible) → removed, with its `=?` line (read as nothing) and its SD writes; the rule stays "at most one creation per book per boot, sent, and only after a readable, whole list"; a listed deck's language wasn't checked (a book's ja and zh decks share title and tag) → read and a deck saying another language is never taken; `starred` marked the user's own deck as someone else's → ownership decides when said, starred only when it isn't; a deck step could start with a finger on the screen → a touch held counts as activity; the settings re-read was repeated untested → `SettingsWatch` (tested), used by the service and the card, and "Deck per book turned off while a card is open" tested through `CardSession::setDeckAllowed`. Nits: a list of scalars or arrays is Malformed; `hasMore` read at the top level only (noted); the title match needs a word deck (C3's sentence decks share title and tag); a refused creation's status is logged; tests for one card with ja and zh decks and a sent 4xx creation. **R5** (no must, 1 should): a deck whose `language` this can't read ("ja_JP", "japanese", "jpn") was left out, so the book's own deck could read as "not there" → only a deck that reads as the other language is left out (the code cut at `-` or `_`). Nits: a whole list without the deck after this boot's creation clears `wanted` (no idle lookups for nothing); the R4 note corrected; an unused include; the created-deck visitor's slots named. **R6** (no must, 2 should): a deck step could start while "Save failed · Retry" was up (the idle time is shorter than the toast), on the same bad network, holding a Retry tap → nothing may be due on the card (`CardController::nextDueMs`, the loop's copy); the ledger's "Built" still described the pending state → rewritten. Nits: a record key's language must be lowercase, as `deckKey` writes it; the store's in-memory book decks are capped (`kDeckWorkMax`, the idle ones go first); decks.ini's size is a `static_assert` from the slug and id limits. **R7** (1 must, from R6's cap): the store's reads (`next`, `state`, the card's per-pass probe of both languages) added an entry per key, so ~16 more tagged books could push out a book whose creation went this boot, and its next save would create a second deck → reads never add (a key not wanted reads as the file says, nothing to do); only `want` and a real step's answer do; past `kDeckWorkMax` only a merely checked entry is dropped, never one with work left or a creation sent. Nits: the language code's length named in the `decks.ini` `static_assert`; a finger down is its own call (`CardSession::touched`), not a side effect of the query; nested paging noted as a device check. **R8** (no must, 1 should): no device check covered the deck's calls → `lxctl.py deck-smoke`, which refuses to run without `--creates-a-deck` (checked before the port opens) and asserts from the log: no deck call before `kDeckIdleMs` of idle after a save; check → list → create, in order, a creation followed by "recorded"; at most one creation and one check per boot; nothing once settled; nothing while a card closes; host tests on synthetic logs, never run against a device here. Nits: a `totalCount` / `total_count` above the entries read means not whole; the service sends a deck creation on a fresh session and reuses it for a list (tested); `fetchDeck` isn't `const` (it does I/O); every language code's length is `kLanguageCodeBytes` (tested). **R9** (no must, 4 should, all deck-smoke): the save tap was a fixed point from the bench's golden, but a live card's level row moves with its content → dev builds log the card's level buttons (`[LXCARD] level <i> <x> <y> <w> <h> <saved>`, when they change) and the smoke taps L's centre from the log, refusing a card that logs none or a saved word; the idle check timed each call's end (the service logs a call once answered) → each step logs its start (`[LXDECK] step <kind> <key>`) and must start `kDeckIdleMs` after the save and after the deck call before it (the second idle window); the driver untested → a fake harness tests the commands, the stops (recorded, a found check), the close's log kept apart, and StarDict, no buttons or a saved word raising; the tests sat after the main guard → moved. Nits: the second-creation and second-check guards are tested; the smoke doesn't cover sleep (said); other commands ignore `--creates-a-deck` (said); the error names the watch time used. **R10 clean** (nits taken): the `[LXDECK] step` line is in every build (one to three per book per boot; only the level-button line is dev-only), said in C4; no slack in the smoke's idle check (the idle time never starts before the logged line); `DECK_SAVE_LEVEL` pinned to `Level::Learning`; the smoke fails when steps name more than one book deck; the title match refuses a `saved_vocab_query` deck (rule said and not a tag rule); "recorded" is logged only when the id was recorded. **R11 clean** (two in a row with R10; merged). |
| V4 | not started | — | — | — |
| V5 | not started | — | — | — |
| V6 | not started (design asked of claritise first) | — | — | — |
| V7 | not started | — | — | — |
| V8 | not started | — | — | — |
| V9 | not started | — | — | — |
| V10 | not started | — | — | — |
| V11 | not started | — | — | — |
| V12 | not started | — | — | — |
