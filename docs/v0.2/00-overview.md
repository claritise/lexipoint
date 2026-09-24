# Lexipoint v0.2+: Candidates & Triage

> **2026-09-24, revised after reading the API reference** (`../reference/lexirise-api-notes.md`): C4 became cheaper (dynamic tag decks), C7 went from "only if cheap" to yes, C8's reopen condition was ruled out, and C9 was added.
>
> **Not a plan yet. This is a triaged backlog.** It was written 2026-09-24, before v0.1 was built.
> Each idea is scored against the X4 Pro's real constraints (e-ink refresh, WiFi battery cost, the
> 1200 req/h rate limit, one-character CJK tokens) and against what v0.1 already builds. Nothing here
> supersedes a v0.1 decision. When an idea is promoted, it gets its own spec in this folder, and
> a row in a v0.2 build order.

## Triage

| # | Idea | Verdict | When | Cost | Reuses from v0.1 |
|---|---|---|---|---|---|
| C1 | Session counter ("12 words saved this session") | **Yes** | v0.1.x | Tiny | Save success count |
| C2 | Auto-tags: book / chapter / session | **Yes, via the save payload** | v0.1.x | Tiny | D9 payload |
| C3 | Sentence save | **Yes** | v0.1.x | Small | D5 sentence already built |
| C4 | Deck per book | **Yes, as a dynamic tag deck** | v0.1.x | Tiny: 1 request per book, 0 per save | C2 tags |
| C5 | Difficulty preview ("you know 73%") | **Yes** | v0.2 | Medium | Client, parser, match logic |
| C6 | Proficiency highlighting in the text | **Yes: now specced as `page-annotations.md`** (A1–A11) | v0.2 | Large | Page analysis, vocab mirror |
| C7 | Total vocab counter | **Yes** (`languageCount` is on the list response) | v0.1.x | Tiny: 1 request | Client |
| C8 | Upload the book to Lexirise | **Park** | — | Medium | — |
| C9 | Set a saved word's level from the card | **Yes** | v0.1.x | Small | `saved_expression_id`, the card |
| C12 | Page analysis + prefetch (one `analyze/text` per page) | **Yes** | v0.1.x | Medium | Client, SentenceBuilder |
| C13 | Vocab mirror on SD (+ incremental sync) | **Yes** | v0.1.x | Medium | Client |
| C14 | Card: "met before" (your notes + tags on saved words) | **Yes** | v0.1.x | Tiny | `stateByEntryId.notes` |
| C15 | Card: other readings (`also tsuitachi`) | **Yes** | v0.1.x | Tiny | `multipleReadings` |
| C16 | Card: explain the conjugation (te-form, causative-passive…) | **Yes** | v0.1.x | Small | Surface + lemma, on-device rules |
| C17 | Card: Undo save, Ignore word, Save sentence (actions) | **Yes** | v0.1.x | Small | `DELETE`, `PATCH suspended`, C3 |
| C11 | SRS review app on the device | **Blocked on Lexirise** (no review endpoint) | v0.3 | Medium–large | Client, vocab mirror, card UI |
| C10 | Sense and reading chosen from the sentence | **Yes, but it needs a source** | v0.2 | Depends on the source | The sentence (D5), `multipleReadings` |

---

## C1. Session counter

**Why yes:** free. Count successful saves in RAM (and optionally `seen` lookups). Show
`3 saved · 11 looked up` on the card footer, and a one-line summary when the book is closed or at a
chapter end. It needs no request and no API.

**Watch:** "session" means *since the book was opened*. It resets on close or sleep. It isn't
persisted in v0.1.x.

## C2. Auto-tags

**Why yes, but not through `PUT /v1/vocabulary/{id}/tags`:** `POST /v1/vocabulary` already takes
`tags`. Add them at save time, with **zero extra requests**. The PUT would only be needed to re-tag
a word that is *already saved* when you meet it again in a new book. That's a v0.2 question: do we
want words to collect every book they appeared in?

**Tag shape:** namespace them, or they collide across books. `chapter-3` in two books is
meaningless.
- `xteink` (always)
- `book:<slug>` (from the EPUB title; slug is ASCII-folded, so a Japanese title needs a
  deterministic fallback such as a short hash of the OPF identifier)
- `book:<slug>:ch<N>` (optional, off by default: chapters make tag lists noisy fast)
- `session:<yyyy-mm-dd>` (optional). **The device's clock** needs NTP when WiFi is up. Check that
  CrossPoint syncs time.

Config keys: `tag_book=1`, `tag_chapter=0`, `tag_session=0`.

**Tested 2026-09-24:** tags with `:` survive. There's **no API to delete a tag name**, so `book:<slug>` leaves one permanent tag per book, which is acceptable. Chapter and session tags would pile up, which is another reason they're off by default. Re-tagging an already-saved word must be done with `PUT …/tags` holding the full list (a re-POST replaces everything).

## C3. Sentence save

**Why yes:** v0.1 already builds the sentence (D5) and sends it as `notes`. A sentence card is
`POST /v1/vocabulary` with `mode: "sentence"` and the sentence as `text`.

**Don't build drag-to-select.** Drag selection on e-ink is slow (every move is a refresh) and
fiddly with a finger at 300 ppi. Instead:
- The card gets a **second action: `⏎ word · ⏎⏎ sentence`** (long-press Confirm = save
  sentence), or a `Sentence` target on touch.
- If the auto-built sentence is wrong (truncated at the page edge, D5 rule 4), use
  **Up/Down on the card to grow or shrink it by one clause** (split at `、` / `，`) before saving.
  That's buttons only, with a partial refresh of one line.

**Tested 2026-09-24: yes.** Lexirise translates a sentence the moment it's saved, so C3 is small. Send `proficiency` explicitly (sentences default to 2).

## C4. Deck per book

**Revised after the API reference: no per-save deck calls are needed.** `POST /v1/vocabulary`
has no deck field, but `POST /v1/decks` supports **dynamic decks** with `rule_type:
"user_tag_filter"`. So a book's deck is a deck **rule**, not a list we keep adding to:

- On the first save in a book: `POST /v1/decks` with `title: "Lexipoint: <title>"`,
  `language`, `unit_type: "word"`, `deck_type: "dynamic"`, `rule_type: "user_tag_filter"`,
  `user_tags: ["book:<slug>"]`. Store `bookId → deckId` in `/.lexirise/decks.ini` so it's created
  once.
- Every save already carries `book:<slug>` (C2), so the word shows up in the deck with **no extra
  request**.
- Optional: one parent deck, `Lexipoint`, with each book as a subdeck (`parent_deck_id`).
- Sentence saves (C3) get a second dynamic deck with `unit_type: "sentence"`, or they share one.
  Check whether a dynamic deck can mix unit types.

**This makes C4 depend on C2** (book tags), and brings it forward to v0.1.x.

**Watch:** if a deck is deleted on the server, `decks.ini` is stale. A 404 on the next check means
it gets recreated. Saves never touch the deck, so they can't fail because of it. Also, **`GET /v1/decks` first**, in case a
deck with that title exists from another device or a reinstall. Reuse it instead of duplicating.

## C5. Difficulty preview

**Why yes:** it helps you pick a book at your level, it's a small UI (one number on the book info
screen), and it builds the pipeline C6 needs (bulk analyze + coverage math) without any renderer
work.

**How:**
- **Sample, don't read the front.** The first pages are the cover, table of contents and
  preface. Take ~6 samples of ~300 characters from chapters spread across the book, straight from
  the chapter XHTML (the EPUB parser, **no layout**, so no pages are rendered). That's 6 requests.
- **Metric: running-token coverage**, the % of word-like occurrences with proficiency ≥ 3 (fresh
  or known). Show the count of distinct unknown words too. Research puts comfortable extensive
  reading at ~98% token coverage. Show a band, not just the number: `comfortable ≥ 98 · stretch
  95–98 · hard < 95`.
- Cache the result per book on SD, with the date. Offer a manual refresh (your vocabulary grows).
- Run it **only when asked** (a button on the book screen), never automatically in the library
  view. That's 6 requests × N books.

**Check:** the maximum `text` length for `analyze/text`. If it takes 2–3k characters, one request
per sample gets cheaper and more accurate.

## C6. Proficiency highlighting in the text

> **2026-09-24: superseded by `page-annotations.md`**, which specs this plus 10 related page features. The notes below are kept for the record.

**Why yes:** it's the most distinctive feature here: seeing how much of a page is new at a glance.
**Why last:** it touches the part of CrossPoint that changes most upstream (rendering), and every
cost below is paid on **every page turn**.

| Problem | Direction |
|---|---|
| **Latency.** The page is drawn before the response arrives | Draw the page, then **overlay the marks with a partial refresh** when they arrive. **Prefetch the next page's analysis** while the current one is read, so on a normal forward read the marks arrive with the page |
| **Rate limit.** 1200 req/h | One analyze per page (the full page text, not per paragraph). About 60–150 pages/h is well within it. Fast flipping: debounce (only analyze a page after ~1.5 s on it) |
| **Battery.** WiFi on all the time | Only while highlighting is enabled for the book. It's opt-in, and the battery cost gets measured and shown |
| **Mapping spans to glyphs.** CJK tokens are characters | Map `[charStart, charEnd)` back to the page's `(line, token)` list: the reverse of `SentenceBuilder`, same unit logic (H5) |
| **Styling on mono e-ink** | Unknown (proficiency 0 / not saved): **underline**. Learning (1–2): dotted underline. Known (4): **no mark** (dimming needs grayscale, which costs a slower refresh). Test first whether the X4 Pro's gray levels are worth it |
| **Cache** | Page analyses on SD, keyed by (book, section, page offset, font settings), invalidated when your vocabulary changes (a timestamp from the last save) |
| **Upstream churn** | The overlay is drawn **after** the page renders, from our code, with no changes inside the text renderer. That keeps the rebase cost like v0.1 |

It needs its own spec and a bench phase (static page plus a recorded response) before any network
work, like the card.

## C7. Total vocab counter

**Revised: yes.** `GET /v1/vocabulary?language=ja&limit=1` returns `totalCount` and
`languageCount`. That's one request, with no paging. Add `proficiencyLabel=known` for "you know N
words". Show it on the book-open screen, or next to the C1 session summary (`12 saved today · 1,204
words in Japanese`). Cache it for the session and refresh it after a save.

**Check:** what `languageCount` counts exactly (all saved items in the language, or
something else) versus `totalCount` (the items matching the filter).

## C8. Upload the book to Lexirise

**Park, and the reason to reopen it is gone.** The public API has no endpoint that gives back
per-chapter analysis for an uploaded book. Upload status only reports ingest progress. So uploading
doesn't make C5 or C6 cheaper for the device. It's still easier from the computer the EPUB came
from (`content_type: document` accepts EPUB, and `external_id` makes it idempotent, so a desktop
script is the natural home for it).

---

## C9. Set a saved word's level from the card

**Why yes:** `stateByEntryId` gives `saved_expression_id`, and `PATCH /v1/vocabulary/{id}` takes
`proficiency` 0–4. So on a saved word, **Up/Down changes the level** (reserved and unused in
v0.1's `popup-ui.md` §3), and Confirm commits it. It's one request, and it handles "I already know
this one" (→ 4) without leaving the book. Don't commit on every press: pressing four times would
be four requests and four refreshes.

## C10. Sense and reading chosen from the sentence

**The gap:** the card shows the same senses in the same order for every sentence. Tested 2026-09-24:
`analyze/text` doesn't disambiguate. 四月一日 comes back as `ichinichi`, 一枚上手 as `jouzu`, 长得 as
`cháng`, and the part of speech is per entry (`../reference/lexirise-api-notes.md`).

**Options, cheapest first:**
1. **Show the alternatives honestly (v0.1.x, free).** `dictionary/lookup` returns `multipleReadings`
   (`{primary, alternatives[]}`). When a word has alternatives, the reading line shows `ichinichi ·
   also tsuitachi`, so the reader can spot the right one. No guessing.
2. **Ask Lexirise for a contextual endpoint.** The app's popup has a *Context* tab, which may already
   do exactly this server-side. If the API exposed it (sentence + word → sense, reading, short
   explanation), it's one more request, and the device stays a thin client. **Ask this first.**
3. **Our own small service** (sentence + word → contextual sense and reading, via an LLM such as
   Claude). The device calls it next to Lexirise. It works now, but it adds a second backend, a
   second key and a cost per lookup. It also sends book text to another service, which should be
   opt-in. Only worth it if (2) is a no.

## C12–C13. Page analysis and the vocab mirror

These are the foundations for `page-annotations.md` §1, and they also help v0.1:
- **C12:** one `analyze/text` per page, prefetching the next page. Lookups skip request ① on an
  analyzed page, so the card opens at phase A straight away.
- **C13:** your vocabulary (proficiency, saved ID, FSRS fields) cached on SD and synced incrementally.
  Saved state works offline, and it's the base for C11 and the annotations.

## C14–C17. Maxing out the card

- **C14 "Met before":** for a saved word, `stateByEntryId` already carries your `notes` (the sentence
  it was saved with) and `user_tags` (so `book:<slug>` tells us which book). The card shows it as
  "Met in *Norwegian Wood*: 「…」". Meeting a word again in a new context is how it sticks.
- **C15 Other readings:** `multipleReadings.alternatives` → `ichinichi · also tsuitachi`. It
  doesn't disambiguate (C10 would), but it doesn't hide the right answer either.
- **C16 Conjugation** (the card also gets a **Form** tab with the steps from dictionary form to page form): compare the surface form and the lemma with a small on-device rule table
  (the KOReader Japanese plugin's deinflection rules are the reference): 煩わしくて → te-form,
  食べさせられた → causative-passive past. Shown as one line under the word. Japanese only. Chinese
  doesn't inflect, but 了/过/着 could get an aspect note later.
- **C17 Actions** (the detail view's last tab; ▲▼ picks, ⏎ runs. Also on the card, "more" moves to **hold ⏎**, since ▲▼ now changes the level): **Undo save** (`DELETE /v1/vocabulary/{id}`, which
  resets dictionary-backed items to unknown), **Ignore** (`PATCH suspended: true`, for names and noise,
  and it removes A1 marks), and **Save sentence** (C3).

## C11. SRS review app on the device

**The appeal:** e-ink suits flashcards well. They're static, button-driven, need no touch, and use
no battery between presses. Reviewing on the device you read on closes the loop:
mine a word from the book, then review it from the same book.

**What the API allows (tested 2026-09-24):**
- **Reading the schedule: yes.** Items expose FSRS state (`next_review_at`, stability, difficulty,
  reps, lapses). Page through all items (1 request per 200), filter `next_review_at <= now` on the
  device, and cache it to SD for offline review.
- **Recording a review: no.** There's no grade endpoint. `PATCH` only sets `proficiency` / `suspended`.

**Options:**
1. **Ask Lexirise for a review endpoint** (e.g. `POST /v1/vocabulary/{id}/review {grade, reviewed_at}`).
   With it, this is a clean feature: Lexirise stays the only scheduler, the device queues grades
   offline with timestamps, and they flush on the next WiFi-up. **Recommended, and the only option
   that doesn't create a second source of truth.**
2. **Our own FSRS on the device.** This is technically easy (FSRS is small), but the device's
   schedule would drift from Lexirise's, and the app would show different due counts than the
   phone. **Don't do it.**
3. **A "proficiency drill" without FSRS.** Show due or learning cards, and let the reader bump the
   level up or down via `PATCH`. It's honest about what it is, but it isn't SRS, and it's unknown
   whether a `PATCH` to `proficiency` disturbs the FSRS state. Test before offering it.

**Firmware cost:** a new top-level activity (a Home menu entry: one more upstream hook), a card
activity, and a local queue. That's close to the "vocab mirror" work (see the API-usage review), which
it would share.

**Scope note:** upstream `SCOPE.md` rules out "interactive apps". That doesn't bind the fork, but it
does mean this code will never go upstream, and it adds to the rebase cost.

## Questions to put to Lexirise

> **Sent by claritise on 2026-09-24** in the Lexirise community (her posts at 3:58 pm and 5:07 pm): the review and due endpoints (Q8), contextual meaning (Q7), empty grammar (Q7), and the bad-data reports. **Awaiting answers.** Record replies here, with dates. (all from the checks above)

~~1. Deck ID on save / smart decks?~~ No deck ID, **but dynamic tag decks exist** (C4).
~~2. Sentence auto-translation?~~ **Yes, immediately** (tested 2026-09-24). Always send `proficiency`, because sentences default to 2. **C3 is unblocked.**
~~3. Maximum `text` length and `fast`?~~ **No limit hit up to 20k chars. `fast` drops lemmas, so don't use it** (tested 2026-09-24).
~~4. A count or stats endpoint?~~ **Yes**: `totalCount` / `languageCount` (C7).
~~5. Server-side analysis of uploaded chapters?~~ **Not exposed** (C8 stays parked).
~~6. Tags: `:` and merge vs replace?~~ **`:` is fine. A re-POST replaces all tags. Tags can't be deleted through the API**, so keep the set small (C2).
7. Is the app's *Context* tab (the contextual sense and reading) available through the API? Why are `grammar[]` / `grammarStates` always empty? (C10)
8. Could the API get a **review endpoint** (grade + timestamp → FSRS update), and a `due` filter on `GET /v1/vocabulary`? (C11)

## Suggested order after v0.1

**v0.1.x:** C1 → C2 → C4 → C7 → C9 → C14 → C15 → C16 → C17 → C10 option 1 → C3 → C12 → C13 (if Q2 comes back "yes"). **v0.2:** `page-annotations.md` build order (§5) → C10 (once Lexirise answers Q7) → C5. **v0.3:** C11 (once Lexirise answers Q8).
