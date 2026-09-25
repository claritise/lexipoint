# Lexipoint v0.2+: Candidates & Triage

> **2026-09-24, revised after reading the API reference** (`../reference/lexirise-api-notes.md`): C4 became cheaper (dynamic tag decks), C7 went from "only if cheap" to yes, C8's reopen condition was ruled out, and C9 was added.
>
> **Not a plan yet. This is a triaged backlog.** It was written 2026-09-24, before v0.1 was built.
> Each idea is scored against the X4 Pro's real constraints (e-ink refresh, WiFi battery cost, the
> 1200 req/h rate limit, one-character CJK tokens) and against what v0.1 already builds. Nothing here
> supersedes a v0.1 decision. When an idea is promoted, it gets its own spec in this folder, and
> a row in a v0.2 build order.

> **Guiding principle (claritise, 2026-09-25): the device is a dedicated Lexirise client for 95% of
> use.** A computer only for setup, getting content on, and genuinely complex UI (bulk edits, heavy
> typing). When ranking, ask: *does this keep claritise from opening the Lexirise app or web app?*
> If yes, it moves up (C11 reviews, C9 / C17 managing saved words, C10 / C19 trust in the card, C18
> manga). Anything e-ink is bad at stays on the computer, and that's fine.
>
> **Second rule (claritise, 2026-09-25):** build what we can without Lexirise first, and ask
> Lexirise only for what's critical (like `analyze/context`). Bigger asks (C20) wait until after v0.3.
>
> **Open items to confirm** are collected in "Open, to confirm" near the end.

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
| C11 | SRS review app on the device | **Yes, online-only: the study-session API is live** (2026-09-25) | v0.3 | Medium–large | Client, card UI |
| C10 | Sense and reading chosen from the sentence | **Yes: the source is `POST /v1/analyze/context`**, live 2026-09-25, returns a reading | v0.2 | Medium: a third call and a card design pass | The sentence (D5), `multipleReadings` |
| C20 | Deeper Lexirise library integration (library sync, server-side analysis and manga OCR downloaded to the device) | **Later: pitch only after v0.3** | Way down the line | Large, and needs new Lexirise endpoints | C8 uploads, C12–C13, C18 |
| C19 | Card: the grammar pattern the word is part of (～ことにした) | **Yes: the grammar pass is live** (2026-09-25); the second call also refines the split | v0.2 | Medium: a second `analyze/text` and a card design pass | Client, the card |
| C21 | Faster lookups: on-device caches (entries by lemma, chapter analysis, text-keyed cache, warm TLS) | **Yes, no Lexirise changes needed** | v0.1.x–v0.2, with C12–C13 | Small–medium each | Client, C12–C13, SD |
| C23 | Slim down to CJK-learning firmware (remove CrossPoint features we don't need) | **Yes, after M: specced as `slimming.md`** (list approved by claritise 2026-09-25, "yes to all") | After M | Medium; saves ≈ 2.5–2.7 MB of 5.57 MB | M's "Taken from CrossPoint" record |
| C24 | Release and beta (tagged release, install guide, 5–10 testers) | **Yes** | After P10 / M | Small–medium | `user-guide.md` |
| C25 | A backend interface (`VocabProvider`) for a second service | **Park** | — | Small | `LexiriseClient` |
| C22 | One font for everything: a single CJK + Latin family | **Yes, as part of the slimming after M** (claritise, 2026-09-25) | After M | Small–medium; the font choice needs claritise | `languages.md` §5.1, the card's type sizes |
| C18 | Manga: tap a word in a speech bubble (sideways strips, OCR'd on the Mac) | **Yes: specced as `manga.md`; Mac pipeline built as a spike** | v0.2 | Medium: the device side; the card's orientation needs claritise | The card, lookup, saving, `analyze/text` (at conversion) |

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

**Update 2026-09-25: option 2 is live** (announced as `words/context`, shipped as `analyze/context`,
below). Option 3 is dropped. With it, the card can put
the sense that fits the sentence first, and (if the endpoint returns it) the reading that fits:
四月一日 → tsuitachi, 一枚上手 → uwate, 长得 → zhǎng. Right now the card is wrong on exactly those tricky
words.
- **Live 2026-09-25, as `POST /v1/analyze/context`** (not `words/context`). Body: `language`, `text`
  (≤ 1600 characters), and the word's **`charStart` / `charEnd` from `analyze/text`**, plus an optional
  `question`. Returns `meaning` (full), `conciseMeaning` (short) and **`reading`**, which is returned
  "when the offsets match one token". So the reading question is answered: **yes, usually**. Option 1
  (C15) stays the fallback when no reading comes back. Only the answer uses a model.
- **It takes offsets, not the word**, so it explains whatever span we send. If the first
  `analyze/text` answer split a word wrongly (とびら), send the span from the refined second call
  (C19), or the explanation is of the wrong word.
- **Cost:** a third call per lookup (~400 lookups/h at 1200 req/h, fine). It lands after the card is
  up, like phase B, so the card doesn't wait for it. When to call it (on open, or on the Context tab)
  is part of the design.
- **Design:** where the contextual sense and reading go on the card needs claritise's sign-off. The
  approved card is binding.
- **Rule for saves (proposed 2026-09-25):** when the card shows a contextual reading or sense, **the save
  uses it too**, not the dictionary default. A wrong reading saved with a real sentence gets drilled in
  the SRS on a card that looks authoritative, and in CJK the reading is much of what's learned.
  **To confirm:** whether `POST /v1/vocabulary` can carry a reading or sense override. Today it takes
  a custom `translation` and `notes`, and nothing for the reading.

## C19. Grammar on the card

**Added 2026-09-25.** `grammar[]` / `grammarStates` were always empty because the slower second pass
behind `morphoPending: true` never started through the API. **Fixed and documented 2026-09-25:** "When
true, the response has fast tokens only. Call again later for grammar and refined segmentation."
(`../reference/lexirise-api-notes.md`, "Reported to Lexirise"). Detecting ～ことにした is filed
separately with Lexirise. The card could then say that the word is part of ～ことにした, and whether that pattern
is in your SRS. StarDict can't do that.
- **Firmware:** parse `grammar` / `grammarStates` (skipped today, `../v0.1/lexirise-client.md` §2),
  and call `analyze/text` once more after the first answer said `morphoPending: true`. That's a
  second call per lookup at most, arriving after the card is up. How long to wait before the second
  call is to be measured.
- **Design:** where the pattern goes on the card, with claritise's sign-off (the approved card is
  binding).
- Grammar's response shape: the reference doesn't detail `grammar[]` yet. Read a live second-call
  response before specifying.
- **The second pass does refine word boundaries (confirmed by the reference, 2026-09-25).** The first
  answer is "fast tokens only", so **v0.1's card may be built on the rough split today**: the word it
  shows, its entry and C10's offsets can all change on the second call. That makes the second call
  matter for every lookup, not only grammar, and C19 bigger than first written:
  - after a `morphoPending: true` answer, call again; if the tapped word's span or entry changed,
    replace the card's word (a refresh), then fetch the meaning and context for the new word;
  - how long "later" is must be measured (one retry after a delay, or a few with backoff);
  - page analysis (C12) should cache only refined results, or mark fast ones to be redone.
  **Test:** call `analyze/text` twice on the とびら sentence (`../reference/lexirise-api-notes.md`,
  tokenizer notes) and compare. Not reported to Lexirise yet.

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

## C18. Manga

**Added 2026-09-25 (claritise).** Spec: `manga.md`; research: `../reference/manga-on-x4-research.md`.
Hold the reader sideways; each page is 2–4 landscape strips in an XTCH. The Mac OCRs the volume
(mokuro), splits the text into words with `analyze/text` (one call per page), cuts strips around the
text boxes and writes a sidecar of word boxes, so a long-press on a word in a bubble opens the normal
card. The pipeline (`tools/manga/`) ran on one 184-page volume: 613 strips, 10,364 words, all but 9
shown whole on some strip. Owed: a panel check of the strips, a look at `matcha-reader`, and
claritise's call on the card's orientation on a sideways strip (`manga.md` §6).

**The format as an open spec (2026-09-25, for later).** Once the device side works and the format has
stopped moving, write it up as its own doc (e.g. `manga-format.md`): the strip layout, the XTCH use,
the word-box sidecar, sample output from one volume, and a validator. It's needed for our own
pipeline anyway, and it turns a future ask to Lexirise into "export this documented format"
(**confirmed 2026-09-25 from lexirise.app:** Lexirise already OCRs comics, with tappable speech
bubbles on Webtoon, Line Manga, Kakao and others). Open and documented, not
proprietary: easier for Lexirise to adopt, reusable by other e-ink projects. Pitch it with C20, after
v0.3, not while the format is still a spike.

## C20. Deeper Lexirise library integration (way down the line)

**Added 2026-09-25 (claritise). An idea, not a plan.** Lexirise already takes uploads
(`POST /v1/uploads/chapters` accepts EPUB/TXT, and the reference mentions `uploads/series`), but
nothing comes back to the device (C8). With a way back, the device could:
- **Sync the library:** books and manga you've added in Lexirise show up on the device, with no SD
  copying.
- **Download server-side analysis per chapter** (segmentation, lemmas, later contextual readings),
  cached on SD. Lookups become instant and work offline (the gap with Kindle), and C5 / C6 cost
  nothing. Only saves, level changes and reviews stay online.
- **Take manga OCR from Lexirise:** strips and word boxes made server-side, replacing the Mac step in
  C18. The biggest barrier removed for users without a Mac.
- **Sync reading progress** with the app.

**The pitch (notes, 2026-09-25):**
- **Getting books onto the device is the everyday pain of e-readers**, and Kindle's real edge is that a
  book is just *there*. Lexirise's web app as the place you sort your library, with the device syncing it,
  is that experience for learners.
- **Fandom readers are an audience:** people learning Chinese to read danmei in the original, or reading
  Japanese web novels and fan works (AO3 and similar), mostly on phones today. claritise mentions that
  Lexirise already has links to ebook sites (not confirmed: the site lists streaming, comic and podcast
  integrations, not ebook sites).
- **Rights:** only the reader's own works and downloads (AO3 allows personal downloads). No bulk
  scraping, and nothing that looks like redistributing authors' work.
- **Before then, on our side:** make the device's web upload and library screens as painless as
  possible, so the version without syncing still feels decent.
- **Manga from Lexirise's own ingestion:** export to our documented format (C18, "The format as an
  open spec") instead of a new pipeline.
- **A device sign-in flow is a prerequisite** for anything sold preinstalled: a code shown on the
  device, confirmed on the phone, instead of pasting an API key into a web page. Lexirise would build
  it. Not asked yet.
- **The end state discussed (2026-09-25, not a plan):** an X4 Pro sold with Lexipoint preinstalled and
  Lexirise Pro included. That depends on the sign-in flow, library sync, and Lexirise and Xteink
  agreeing a bundle, none of which we control. Beta retention numbers (C24) are what they'd want to see.
  **A 3-month trial is the easier ask than a year:** Pro is $9.99 / month or $99.99 / year (lexirise.app,
  2026-09-25), so a year is worth about the device's price and 3 months about $30. SRS makes renewal
  natural once a few months of mined cards exist. Retention risks: the habit has to form in those months,
  and review backlogs make people quit (C11 helps).
- **Lexirise's current shape** (lexirise.app, 2026-09-25): browser extensions (Chrome, Safari, Firefox)
  over YouTube, Netflix, Crunchyroll, Bilibili and others; iOS and Android apps; comic OCR; podcast
  transcripts; FSRS; Chinese, Japanese and Korean first ("we chose depth", from the founder's comparison
  with LingQ). Pro includes **exports**, so a user's data isn't locked in. There's also a lifetime plan,
  a 7-day Pro trial and a 14-day money-back guarantee. No ebook reader or e-ink device, which is the gap
  Lexipoint fills.

**Gate (claritise, 2026-09-25):** pitch only after the manga pipeline and SRS reviews (C18, C11) work on
the device, phase M (the standalone repo and rebrand) has landed, and Lexipoint has been slimmed down to a
CJK-learning firmware (C23).

**Why later:** it's a big ask of Lexirise (compute and storage per uploaded book, a compact export the
device can hold in memory, their policy on uploaded books). **Policy (claritise, 2026-09-25): build
what we can without Lexirise first, and ask only for what's critical** (like `analyze/context`). Pitch
C20 after v0.3, once our own manga pipeline and the rest work, as the next step, and alongside a
possible Xteink + Lexirise bundle.

## C21. Faster lookups: on-device caches

**Added 2026-09-25 (claritise).** A lookup today is WiFi (up to ~6 s if down), NTP on the first call
after boot (up to ~5 s), a TLS handshake, then ① `analyze/text` and ③ `dictionary/lookup` at
~1.0–1.3 s each. C12–C13 (`page-annotations.md` §1) come first: page analysis skips ①, and the vocab
mirror gives saved state offline. On top of them, cheapest and most likely to pay first:

1. **Cache dictionary entries by lemma on SD.** Books repeat their vocabulary (names, recurring
   words), so a second tap on a word needs no network. Don't cache an entry whose
   `translation_status` isn't ready; expire after ~30 days. Probably the biggest win after C12.
2. **Analyze a chapter at once**, not a page. No limit was hit up to 20k characters (~5.8 s, ~70 bytes of
   response per character, streamed to SD). One call on opening a chapter readies every page and
   uses far fewer requests. It may replace per-page prefetch for forward reading.
3. **Key the analysis cache by a hash of the paragraph text**, not by `<section>-<pageStart>` plus font
   settings (`page-annotations.md` §1.1). A font or layout change then keeps the cache.
4. **Prefetch a few likely unknown words per page** (high rank, not in the mirror), capped and inside
   the 70% rate-limit guard, so the card is often complete on the tap.
5. **Keep the connection warm:** TLS session resumption (skips most of the ECC handshake; check
   wolfSSL's heap cost), and optionally a "fast lookups" setting that keeps WiFi up in a reading
   session (battery cost).
6. **Seed the clock from the RTC** to skip the NTP wait (the HalClock date accessor, P8).
7. **A kanji / hanzi pack on SD** for the character breakdown, instead of one lookup per character.

With 1–4, most taps on a forward read would need no network: the network is left for saves, level
changes and reviews. **Nothing here is measured yet.** Measure 1–3 first (hit rate of the lemma
cache over a chapter, chapter-call time and memory, cache survival across a font change).

## C22. One font for everything

**Added 2026-09-25 (claritise): support one font going forward**, a single family covering CJK and
English. Part of slimming Lexipoint into CJK-learning firmware after phase M (not a decision row
yet). Today the reader font is `NotoSerifCJK` built from `NotoSerifCJKjp` (`languages.md` §5.1), and
the card mixes it with CrossPoint's built-in UI fonts (SMALL, UI_10).

Why: one family to build, test and ship; the card and the book always match; no "wrong font" setup
trap (`languages.md` §5); the font picker and the rest of CrossPoint's font machinery can go.

**Needs claritise:**
- **Which font.** Noto Serif CJK is what works now; Noto Sans CJK is the other obvious candidate
  (it may suit the UI better).
- **Chinese letterforms.** The JP file carries every ideograph but draws Japanese forms. One font means
  Chinese books show those forms, unless the one family ships both the JP and SC builds.
- **The UI fonts too?** Replacing the built-in UI fonts means the card's type changes, and the approved
  card is binding (`popup-ui.md` §1.1), so it needs a check against the reference. The built-ins also
  live in flash and render without an SD card.

Also worth doing when it's rebuilt: widen the glyph ranges to CJK Extension A and the non-BMP
characters fiction uses (𠮟 renders as a box today, §5.1).

## C23. Slim down to CJK-learning firmware

**Added 2026-09-25 (claritise): after phase M, strip CrossPoint features Lexipoint doesn't need**, and
make the firmware purpose-built for Japanese and Chinese learning with Lexirise. Phase M
(`../v0.1/standalone-repo.md`) only restructures and rebrands; it removes nothing. This is its own
decision and phase after M, **not yet a decision row or a phase**.

Why: every removed feature frees heap and flash for TLS, the card, page analysis and grammar (memory is
the real limit on this chip); a smaller settings and menu surface; nothing to merge since M ends upstream
tracking.

**Update 2026-09-25: the list is drawn up, measured and approved** (claritise, "yes to all"): `slimming.md`. KOReader sync, OPDS, Calibre, WebDAV, 33 UI translations, most hyphenation, the built-in reader fonts (with C22), extra themes and code for devices without touch go. EPUB, TXT, XTC (manga), StarDict, OTA and the web upload page stay. The starting points below are kept as history:
- **Likely remove:** code for devices without touch (M already drops their envs), button-only flows,
  network features unrelated to Lexirise, and the font machinery C22 makes redundant.
- **Likely keep:** EPUB and TXT, ruby / furigana, sleep and battery, OTA, the StarDict fallback, the web
  upload page, the dev harness.
- **Unsure:** KOReader sync, OPDS, other reader formats, the reader settings a CJK reader still wants
  (vertical text if it's ever added).

Each removal gets a line in the "Taken from CrossPoint" record M sets up, so what was dropped stays findable.

## C24. Release and beta

**Added 2026-09-25.** People in the Lexirise community are already asking about buying an X4 Pro for this.
Before anyone outside can use it:
- **A tagged release** with a prebuilt firmware file (no release exists yet; M changes the release URL).
- **Flashing steps and a "what you need" section** in `../user-guide.md`: the X4 Pro specifically (not the
  X4 / X3), a Lexirise Pro key, WiFi, and for manga a Mac (C18).
- **5–10 beta testers from the Discord.** Watch where setup fails and whether they still read with it
  after two weeks. That answers whether it's a "no brainer" and is the evidence any Lexirise or Xteink
  pitch needs.

Open: known rough edges to fix or flag first (e.g. USB silent after a flash, seen in P9).

## C25. A backend interface (parked)

**Noted 2026-09-25, low priority.** Lexirise is the only service with every piece Lexipoint needs
(tokenizing and state in one call, save with sentence, key auth). jpdb.io comes closest for Japanese, and
LingQ possibly for more languages (neither checked recently). If a second backend is ever wanted, pull a
`VocabProvider` interface out of `LexiriseClient` (analyze, lookup, save, key check). Also cheap insurance
against depending on one vendor. Not planned.

## C11. SRS review app on the device

**The appeal:** e-ink suits flashcards well. They're static, button-driven, need no touch, and use
no battery between presses. Reviewing on the device you read on closes the loop:
mine a word from the book, then review it from the same book.

**What the API allowed (tested 2026-09-24):** reading the schedule, yes (items expose FSRS state:
`next_review_at`, stability, difficulty, reps, lapses; the due list had to be filtered on the device after
paging everything); recording a review, no (no grade endpoint; `PATCH` only sets `proficiency` /
`suspended`). Both are being fixed by the announced endpoints below.

**Options (2026-09-24):**
1. **Ask Lexirise for a review endpoint.** Recommended then as the only option that doesn't create a
   second source of truth: Lexirise stays the only scheduler. **Asked, and being built** (below). The
   offline grade queue with timestamps first planned here is **superseded** by the online-only decision.
2. **Our own FSRS on the device.** This is technically easy (FSRS is small), but the device's
   schedule would drift from Lexirise's, and the app would show different due counts than the
   phone. **Don't do it.**
3. **A "proficiency drill" without FSRS.** Show due or learning cards, and let the reader bump the
   level up or down via `PATCH`. It's honest about what it is, but it isn't SRS, and it's unknown
   whether a `PATCH` to `proficiency` disturbs the FSRS state. Test before offering it.

**Update 2026-09-25: option 1 is live, as a study-session API** (announced as `vocabulary/due` +
`vocabulary/{id}/review`; shipped differently). Details in `../reference/lexirise-api-notes.md`,
"Study API":
- `GET /v1/study/summary`: due / new counts, overall, per language and per deck (the app's Study tab).
- `POST /v1/study/sessions`: `mode` `study` or `cram`; `source` all / deck / tags / vocabulary ids /
  recent / today; `directions` forward / reverse / listen; `limit` ≤ 180. Returns `sessionId` and the
  cards, **with their content**: `text`, `reading`, `translation`, `sourceSentence {text, translation}`,
  `audioUrl`, `direction`, `isNew`, `dueAt`, `proficiency`. 422 when there's nothing to study.
- `GET /v1/study/sessions/{sessionId}`: resume the session.
- `POST /v1/study/sessions/{sessionId}/reviews`: up to 200 answers per batch, each with its own UUID
  `id`, `rating` again / hard / good / easy, **`reviewedAt` (required)** and optional `durationMs`.
  Applied oldest first; **a resent `id` comes back `duplicate`**, so retries are safe. Each result
  carries the updated card (`dueAt`, `proficiency`, `state`).

**Decision (claritise, 2026-09-25): review is online-only.** It stands until claritise reopens it, but
the API now supports offline review directly (timestamps, client ids, deduped batches), so the cost of
reopening it is only on our side. What changes even online:
- **Reviews are idempotent.** Retry a batch after a dropped connection; nothing counts twice. (The
  earlier "don't resend" rule is superseded.)
- **`reviewedAt` is required, so the device needs the real time** even online. After WiFi is up, NTP
  (already in `TlsConnection`) gives it. Use it, never the 1970 boot clock.
- **The session carries everything**, so no per-card calls: start a session, show cards locally, send
  answers in batches (e.g. every 10, and on leaving).
- **Keep `limit` small (e.g. 30–50).** 180 cards with sentences and translations could exceed our
  64 KB response cap (`../v0.1/lexirise-client.md` §1). Size it once a real response is measured.
- **Directions:** decide which the device offers. `listen` needs audio, which the X4 Pro can't play.

**If offline review is reopened:** queue answers on SD with their UUIDs and `reviewedAt`, and flush on
WiFi-up. The timestamp needs the RTC date accessor (P8, `../v0.1/lexirise-client.md` §1, "Clock
source"). Not checked: whether the RTC survives a fully drained battery.

**Firmware cost:** a new top-level activity (a Home menu entry) and a card activity. `study/summary`
gives the due count for the menu, so the vocab mirror isn't needed for it.

**Scope note:** upstream `SCOPE.md` rules out "interactive apps". That never bound the fork, and after
phase M there's no upstream to rebase onto, so it no longer costs anything.

## Open, to confirm

Everything raised 2026-09-25 without a clear answer yet. Strike each one through with its answer and date
when it's settled.

**When Lexirise's new endpoints are live** (check the reference, `../reference/lexirise-api-notes.md`):
~~1. Does the context endpoint return the **reading**?~~ **Yes, when the offsets match one token** (`POST /v1/analyze/context`, live 2026-09-25) (C10).
~~2. Does each due card carry its content? A limit?~~ **Yes**: sessions return text, reading, translation, source sentence and audio URL per card; `limit` ≤ 180. Still to measure: a response's size against the 64 KB cap (C11).
~~3. The grade scale; the next due date?~~ **`rating` again / hard / good / easy**; each result carries the updated card with `dueAt` (C11). 2026-09-25.
4. The grammar pass: `grammar[]`'s shape, and how long after the first call the second one returns grammar and the refined split. **To measure** (C19).
5. ~~Does the second `analyze/text` call change word boundaries?~~ **Yes, documented** ("refined segmentation", 2026-09-25). **Still to test** on the とびら sentence (C19).
6. The reported bad data (长得, 四月一日, 𠮟る, 一緒) and ～ことにした not detected: **filed and being worked on** by Lexirise (2026-09-25). Re-test when they say it's fixed. 一日中雨 as one token wasn't in the list they named.

**Lexirise, not asked yet:**
7. Can a save carry a **reading or sense override**, so saves match the contextual card? (C10)
8. The **とびら → と + びら** split: **don't report yet** (claritise, 2026-09-25). Test it once C19's second call is built; **report it only if it's still split after v0.2** (sentence and plan in `../reference/lexirise-api-notes.md`, tokenizer notes).
9. A **device sign-in flow** (code on the device, confirmed on the phone), only before any bundle (C20).
~~10. Does Lexirise already ingest manga and link to ebook sites, as claritise says?~~ **Manga: yes**, comic OCR with tappable speech bubbles on Webtoon, Line Manga, Kakao and others (lexirise.app, 2026-09-25). **Ebook sites: not confirmed**, the site lists streaming, comic and podcast integrations only. **Data export: yes**, a Pro feature (C18, C20).

**On the device / measured by us:**
11. Does the X4 Pro's RTC survive a fully drained battery? Matters only if offline review is reopened, which the API now supports (C11).
17. **Reopen offline review?** The study API now takes `reviewedAt`, client ids and deduped batches, so offline review costs only our side (an SD queue and the RTC date accessor). claritise's call (C11).
12. Lemma-cache hit rate over a chapter; time and memory of a whole-chapter `analyze/text`; TLS session resumption's heap cost on wolfSSL (C21).

**Needs claritise:**
13. The one font: which family (Noto Serif CJK or Sans), Chinese letterforms (JP forms, or ship JP + SC), and whether it replaces the UI fonts on the approved card (C22).
14. ~~The keep / remove / unsure list for slimming (C23).~~ **Approved 2026-09-25: `slimming.md`.** Still open there: S1 (the font, which is item 13), S2 (a built-in fallback font), S3 (keeping TXT).
15. The card's orientation on a sideways manga strip (`manga.md` §6, already owed).
16. The save-uses-context rule in C10 (proposed, not yet signed off).

## Questions to put to Lexirise

> **Sent by claritise on 2026-09-24** in the Lexirise community (their posts at 3:58 pm and 5:07 pm): the review and due endpoints (Q8), contextual meaning (Q7), empty grammar (Q7), and the bad-data reports. **Answered 2026-09-24 / 25** (Q7, Q8 below; details in `../reference/lexirise-api-notes.md`, "Reported to Lexirise"). The bad data wasn't addressed yet. Record further replies here, with dates.

~~1. Deck ID on save / smart decks?~~ No deck ID, **but dynamic tag decks exist** (C4).
~~2. Sentence auto-translation?~~ **Yes, immediately** (tested 2026-09-24). Always send `proficiency`, because sentences default to 2. **C3 is unblocked.**
~~3. Maximum `text` length and `fast`?~~ **No limit hit up to 20k chars. `fast` drops lemmas, so don't use it** (tested 2026-09-24).
~~4. A count or stats endpoint?~~ **Yes**: `totalCount` / `languageCount` (C7).
~~5. Server-side analysis of uploaded chapters?~~ **Not exposed** (C8 stays parked).
~~6. Tags: `:` and merge vs replace?~~ **`:` is fine. A re-POST replaces all tags. Tags can't be deleted through the API**, so keep the set small (C2).
~~7. Is the app's *Context* tab (the contextual sense and reading) available through the API? Why are `grammar[]` / `grammarStates` always empty?~~ **Live: `POST /v1/analyze/context`**, with a reading (C10). **Grammar:** the second pass behind `morphoPending` now runs; calling again returns grammar and a refined split (C19). 2026-09-25.
~~8. Could the API get a **review endpoint** (grade + timestamp → FSRS update), and a `due` filter on `GET /v1/vocabulary`?~~ **Live as the study API:** `GET /v1/study/summary`, `POST /v1/study/sessions`, `GET …/{sessionId}`, `POST …/{sessionId}/reviews` (again/hard/good/easy, `reviewedAt`, client ids, deduped batches) (C11). 2026-09-25.

## Suggested order after v0.1

**v0.1.x:** C1 → C2 → C4 → C7 → C9 → C14 → C15 → C16 → C17 → C10 option 1 → C3 → C12 → C13 (if Q2 comes back "yes") → C21 (with C12–C13). **After P10 / M:** C24 (release and beta). **After M:** C23 slimming, with C22. **v0.2:** `page-annotations.md` build order (§5) → C10 and C19 (both unblocked 2026-09-25: `analyze/context` and the grammar pass are live) → C5. **v0.3:** C11 (unblocked 2026-09-25: the study API is live). **C18 (manga):** the panel check any time (no firmware change); the device side after v0.1 and phase M, once the card orientation is decided (`manga.md` §7). **After v0.3:** pitch C20 to Lexirise. Until then, build what doesn't need Lexirise, and ask only for what's critical.
