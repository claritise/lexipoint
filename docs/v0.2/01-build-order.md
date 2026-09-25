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
| V1 | The refined word: the second `analyze/text` call | C19 (pulled forward) | — |
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

## V1: The refined word (the second `analyze/text` call)

**Goal:** the card is built on Lexirise's refined split, not the first "fast tokens only" answer.
**Read:** `00-overview.md` C19 (the pulled-forward part) and Open #4, #5, #8; `../v0.1/lexirise-client.md` §2
(`morphoPending` is parsed and ignored, `api/Responses.cpp`); `../v0.1/lookup-flow.md` (the phases A / B / B′);
`../reference/lexirise-api-notes.md` (tokenizer notes, the とびら sentence).
**Measure first:** from the Mac, call `analyze/text` on a few sentences (the とびら sentence among them) and
repeat after increasing delays: how soon the second answer stops saying `morphoPending: true`, and what changes
(the split, entries, `grammar[]`'s shape for V11). Record it in the reference.
**Build:** after an answer with `morphoPending: true`, call again (the measured delay; a bounded retry), then:
if the tapped word's span or entry changed, replace the card's word (one refresh) and look up the new one; if
not, nothing visible changes. Never block the card on it; offline or out of time, the first answer stands. The
retry policy and its limits in `LexiriseConfig.h`.
**Gate:** the uniform gate; host tests of the policy (unchanged / changed span / changed entry / still pending
at the limit / offline); the measurement in the reference; on the device, the とびら sentence and one sentence
whose split doesn't change. Per Open #8, とびら is reported to Lexirise only if it's still split after v0.2.

## V2: Book tags on every save

**Goal:** every word saved from a book carries `book:<slug>`, so it's findable per book (and V3's deck).
**Read:** C2; `../v0.1/settings.md` §1 (the hidden "Tag with book title" row) and §3; the save request
(`api/Requests.cpp`).
**Build:** the slug (ASCII-folded title; a deterministic fallback from the OPF identifier for a title with no
ASCII, e.g. a Japanese one), `book:<slug>` added to the tags of each save, and a book → slug → title record on
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

C19's grammar part, once V1 has read a live `grammar[]`. **Needs:** claritise's placement sign-off.

## V12: Difficulty preview

C5, on V7's page analysis and mirror.

## Status ledger

| Phase | Status | Branch / commit | Tests | Notes |
|---|---|---|---|---|
| V1 | not started | — | — | — |
| V2 | not started | — | — | — |
| V3 | not started | — | — | — |
| V4 | not started | — | — | — |
| V5 | not started | — | — | — |
| V6 | not started (design asked of claritise first) | — | — | — |
| V7 | not started | — | — | — |
| V8 | not started | — | — | — |
| V9 | not started | — | — | — |
| V10 | not started | — | — | — |
| V11 | not started | — | — | — |
| V12 | not started | — | — | — |
