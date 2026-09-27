# Page annotations: marking the page with what you know

**Status:** proposed 2026-09-24. Supersedes backlog item C6 (`00-overview.md`) and folds in the
other page ideas. ~~Nothing here is built.~~ The vocab mirror (§1.2) is being built (V7a, 2026-09-28). It depends on
**page analysis** and the **vocab mirror** (§1), which it shares with C5 (difficulty preview) and C11 (the SRS app).

Related: `../v0.1/sentence-extraction.md` (the page → text walk, reused in reverse),
`../v0.1/lookup-flow.md` (word select), `../reference/lexirise-api-notes.md`, `../v0.1/languages.md`.

---

## 0. Why

The card only helps once you've stopped on a word. Annotations help **while you read**. You see at a
glance what's new, what you're learning, and how hard the page is, and the reading support fades as
your level rises. This is what `analyze/text` was built for (it powers Lexirise's own reader).

## 1. Foundations (build these first, they pay off everywhere)

### 1.1 Page analysis

- **One `analyze/text` per page**, with the whole page's base text (ruby excluded, D5 rules), not
  per lookup. **Use the default mode, not `fast`**: `fast` drops lemmas, so inflected words wouldn't match saved lemmas (tested 2026-09-24).
- **Prefetch:** while page *n* is on screen, analyze page *n+1*. On a normal forward read, the
  annotations are ready when the page turns, so there's **never a second refresh**. On a jump (a TOC
  link, go-to-percent, turning backwards), the page is drawn plain and the marks come in with one
  partial refresh, and only if they arrive within 1.5 s. Otherwise the page stays plain until
  the next turn.
- **Debounce fast flipping:** don't prefetch while pages turn faster than 1 per 1.5 s.
- **Cache** results on SD: `/.lexirise/cache/<bookId>/<section>-<pageStart>.bin` (compact
  occurrences, no strings the vocab mirror already has). The key includes font and layout settings,
  since a page boundary moves with them. Invalidate on a vocab-mirror change for any entry on that page.
- **Lookups reuse it:** a tap on an analyzed page skips request ① entirely. The card goes straight to
  phase A, and only `dictionary/lookup` goes out.
- **Budget:** 60–150 pages/h is 5–13% of the 1200 req/h limit. Watch `rateLimitMax` from `/me`, and
  stop prefetching (keep on-demand lookups) above 70% of the window.

### 1.2 Vocab mirror

- Page through `GET /v1/vocabulary?language=<lang>`~~`&limit=200` once (1 request per 200 items)~~ into
  `/.lexirise/vocab-<lang>.bin`: `entryId → {proficiency, savedId,`~~`seen,`~~`suspended, next_review_at}`.
  (Superseded 2026-09-28 by V7a: pages of `config::kVocabPageItems`, not 200, since a page blocks the card; `seen`
  isn't kept, nothing reads it. "As built (V7a)" below.)
- **Incremental sync** ~~on each WiFi-up~~ (superseded 2026-09-28: on an idle card, "As built (V7a)"):
  `sortId=updated_at&sortDesc=true`, stopping at the first item ~~older than the last sync~~ older than the cursor,
  or at the first one as old as it that the mirror already has ("As built (V7a)").
- **Local writes** (save, level change, ~~ignore~~) update the mirror straight away, so annotations
  reflect a save on the same page. (Superseded 2026-09-27 for ignore: it isn't a Lexirise write and never touches
  the mirror; it's the reader's own list, `/.lexirise/ignored.ini`, `00-overview.md` C17 "As built (V5, local)".)
- Annotation state comes **from the mirror**, not from `stateByEntryId`. The page analysis only
  supplies the word → entry mapping, so marks still work offline for cached pages, and for uncached
  pages once the chapter is analyzed.

**As built (V7a, 2026-09-28, on `lexi/V7`)** (claritise approved V7 on 2026-09-28, "yes keep going", told the reader
keeps a copy of their vocabulary on the SD card, read-only from their account):

- **What's kept**, per saved word, one 16-byte record (`vocab::Entry`, `config::kVocabRecordBytes`): the entry a save
  targets (the item's `dictionary_id`, which is `stateByEntryId`'s key), the saved expression's id (`id`, the state's
  `saved_expression_id`), the level, `suspended` and `next_review_at` (measured: `../reference/lexirise-api-notes.md`
  "V7's foundations"). **Sentence cards aren't kept** (`unit_type` other than `word`): they mark no word on a page. At
  most `config::kVocabMirrorMax` words per language (past it a new word isn't kept, logged). The file, one per
  language, binary (sorted records behind a header with the sync's progress and a CRC): `../v0.1/settings.md` §3.
  Binary rather than lines: a record is a fixed 16 bytes, read straight into a sorted array for a binary search, and a
  torn or hand-edited file is caught by its CRC. A file that doesn't check out is set aside (`.bad`) and synced again.
- **Memory.** A page is never held: the HTTP body goes to a sink as it's decoded (`net::BodySink`), a push JSON
  reader (`net/JsonStream`, the same events as `JsonReader`) keeps only the path and one value of at most
  `config::kJsonStreamMaxStringBytes`, and the page's visitor (`api/VocabPage`) keeps an item's own top-level fields
  (the embedded `dictionary_entry`, translations, notes and media are walked and dropped) until the page ends. The
  mirror is loaded once per boot per language, on the first idle card in it (not as the card opens: the file can be
  `config::kVocabMaxBytes`, its CRC checked; what cards learn before that waits in memory, the newest
  `config::kVocabPendingMax`, and is applied on load): `kVocabRecordBytes` per word in one sorted
  array, in PSRAM on the device (malloc prefers PSRAM past 4 KB, `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`); a write
  builds the file's bytes in one buffer (PSRAM too). The TLS session and the page's parse state are the only internal
  RAM a page adds; the dev log gives the heap per page (`../v0.1/device-checks.md` "v0.2 V7a").
- **When it syncs: only on an idle card, over WiFi already up.** Like V3's deck steps (`CardSession::shouldFetchVocab`
  after the deck's turn): nothing to fetch, send, draw or handle, no finger down, nothing due, no write queued, idle
  `config::kVocabIdleMs` (longer than a deck step's), and the card reached Lexirise (its words came and its last call
  didn't fail). One page per idle window, at most `config::kVocabPagesPerCard` per card and `kVocabPagesPerHour` in
  all; never as the card closes, never under RenderLock, and never a WiFi join (`LexiriseApi::vocabularyPage` sends
  only over a station already connected, else NoWifi: no radio time the reader didn't cause; nor does a page put off
  the radio's idle teardown, since it doesn't count as WiFi use; nor the TLS session's idle close, which counts from
  the reader's own last call, so the sync never holds the session's internal RAM longer: a lookup right after a page
  may need a new handshake). **A button pressed while a page streams gives it up** (`VocabPageReader`'s cancel,
  `HalGPIO::rawInputActive` read straight from the hardware as each piece of the body arrives: the debounced input
  isn't updated during a call; it sees the side buttons and the power button, but not the capacitive Home key, which
  the touch controller reports), so the press is handled, not lost (the page's TLS connection is closed with it, so
  the next word's phase B pays a new handshake); nothing is learned, no failure wait, and it doesn't count toward the
  card's pages; a button already held as a page is due gives it up before the request (no request spent). **A tap or a
  Home press made and released during a page is likely lost** (unmeasured): the touch controller is only read by the
  loop's input update, which the page blocks, as the side buttons were before the cancel (`../v0.1/device-checks.md`);
  one still held when the page ends is seen after it. Not while reading without a card: a page blocks the loop (a few
  seconds: the device check times it), which a page turn mustn't wait on. No new setting.
- **The file is read and written only on an idle card** (`CardSession::shouldFlushMirror`: the deck's idle rule,
  `config::kDeckIdleMs`, before a deck step or a page), after a page, and as the card closes (a write only); never
  between an answer and its redraw. A write that fails isn't tried again on that card's idle windows (the memory keeps
  the change; the close tries once more, then the next card).
- **An answer that agrees with the mirror writes nothing** (`vocab::applyLive`, `LiveApplied`): the same saved id and
  level on an entry this pass generation already holds changes nothing, so an analysis that matches the mirror neither
  marks it dirty (no file rewrite at the next idle window or the close) nor turns a page's entry into a live one (a
  tie at the cursor still reads as unchanged). An agreeing answer on an entry an older generation marked (one a
  running full pass hasn't reached yet, or one a pass that couldn't sweep left behind) takes this generation's mark,
  live, in memory only (MarkOnly): the next page's write carries it, and a restart that loses it is harmless (the pass
  reaches the item, or the incremental pass after it), so cards during a weekly resync don't each cost a whole-file
  write. Only an answer that changes an entry marks the mirror dirty.
- **How.** Pages of `config::kVocabPageItems`, newest change first; each next page starts `kVocabPageOverlap` items
  before the server's next offset (its slack: an item deleted meanwhile moves the rest up). Each page's list count
  (`languageCount` only: it counts sentence cards, measured, as the list paged holds them; `totalCount` counts words
  only, so it can't bound a shift, and a page without `languageCount` has no count) is kept: when it dropped since the
  last page by more than the page's slack, the pass reads again from drop − slack before that page's start
  (`rereadFrom` in `vocab/VocabMirror.cpp`); the next page's slack is kept with the pass (`PassProgress::slack`, one
  `PassProgress` each for the full pass and the incremental one in `SyncState`): the whole overlap after a full page,
  less after a page the server cut short (the next page never starts at or before the one just read), none for a
  re-read (it doesn't start just before a page it follows), so any drop past it is read again in full. A drop bounds
  the shift up: an addition or a change moves an item to the top, a shift down (a duplicate, never a skip). A pass
  with a later page that gave no count (or followed one that gave none; a first page needs none: nothing was read
  before it) can't bound what it skipped: a full pass sweeps nothing, and either kind brings the next full pass
  forward to one incremental interval later (by the wall clock), once: if that pass has no count either, the weekly
  rule stands until a counted pass (`SyncState::resyncSoon`), not a full pass every interval. A last pass stamped
  after the wall clock (the clock moved back) makes a full pass due. Pinned by randomized tests (fixed seeds):
  `VocabSync` (deletions, sentence cards among them, additions and changes between every page of a full pass) and
  `VocabSyncSim` (`test/lexirise_vocab/VocabSyncSimTest.cpp`: an account changed during full and incremental passes,
  many ties, small pages and pages cut short, pages without a count, live answers and card writes, pages given up or
  failed, file writes missed and reboots from the last file; after a quiet full and incremental pass the mirror equals
  the account's words, field by field). **A full pass** first (and again `config::kVocabResyncS` after the last ended,
  by the wall clock): resumable across cards and boots (its next offset is in the file), and when it ends the cursor
  is the newest `updated_at` it began with. **An incremental pass** starts with a probe (`config::kVocabProbeItems`:
  the usual answer is "nothing new", and a few items say so as well as a whole page of ~6.8 KB items; `limit=2`
  answered as asked, measured), then whole pages from its next offset less the overlap (a re-read sent back to the top
  is a probe again); kept in the file too (every sleep is a deep sleep, a restart: a pass needing more pages than one
  wake gets carries on at its next page after it, never back at the top; a full pass starting supersedes one left
  under way), at most every `config::kVocabSyncIntervalMs` (the first on each boot's first idle card), from the top
  until the first item older than the cursor, or the first one as old as the cursor that the mirror already has as it
  is (so the first of a group tied at the cursor changed in the cursor's own millisecond isn't missed, and a large
  group tied at the cursor, a bulk import, isn't read again every pass; a following page's first items, which this
  pass read a page before, don't count for that stop: ties keep their order across pages, measured,
  `../reference/lexirise-api-notes.md` "V7's foundations"). A full pass that ended before the wall clock was set gets
  its time from the first page with one. A failed page waits (a 429 its retry time, anything else
  `kVocabFailureWaitMs`) and changes nothing.
- **Deletions.** A dictionary word removed in Lexirise stays in the list at level 0 with a new `updated_at` (measured,
  "Suspended (Ignore)" in the reference notes), so an incremental pass takes it. An item really gone (a sentence card,
  or anything removed outright) never shows in that order: the next full pass drops every word it didn't see.
  Meanwhile the live answer wins for every word the card analyzes (below).
- **The card's answers and writes.** Each analysis's states (`stateByEntryId`) for every word's lemma and surface
  entries go into the mirror (`stateByEntryId` lists level-0 and suspended items too, measured:
  `../v0.1/lookup-flow.md` §5, the level-0 item a removal leaves, and the reference notes' "Suspended (Ignore)", so an
  answer never erases them), and an entry the answer doesn't list isn't saved there either (unless the answer was cut
  at `config::kMaxEntries`), so **the live answer wins** where they disagree; except an entry this card wrote (its
  write is newer than a later analysis). Each save, level change and removal goes in once Lexirise took it
  (`LiveSource::recordMirror`, memory only, after every answer), kept under the entry whose state the card had (the
  surface word's when only it was saved; a new save's under the lemma); a removal stays at level 0, as Lexirise keeps
  a dictionary word's item. An entry a live answer put is marked so (`Entry::live`): a full pass under way still takes
  the page's item when it reaches it (the answer has no review time or suspension). Written with the file (above); a
  card closed by sleep loses what it hadn't written (sleep is a deep sleep: the reader restarts), which is harmless:
  Lexirise has it, and the next incremental pass brings it back.
- **Use.** The card's saved state still comes from `analyze/text`; nothing it draws changes. ~~The mirror is the
  offline source (`VocabStore::savedState(language, entryId)`) and V9's~~ (corrected 2026-09-28: nothing calls it yet)
  `VocabStore::savedState(language, entryId)` is there for V9's marks and the offline saved state they'll show; the
  StarDict answer offline isn't given it (it has no entry ids, and its popup would change).
- **Known limits.** The sync moves only while cards sit idle (a reader who closes cards at once syncs slowly: the
  device check measures a first full sync); a page given up for a button costs a request (the hourly budget counts it;
  a button already held when a page is due gives it up before its request, which costs nothing); the Home key and a
  touch don't give a page up (only the side and power buttons do), and one made and released during a page is likely
  lost (the device check says; the candidate fix, once it's confirmed: `HalGPIO::rawTouchActive()` reading the touch
  controller's interrupt line, `BoardConfig::ACTIVE.touch.irq` held low, OR'd into the cancel
  `LexiriseCardActivity::idleStep` passes); the button is only looked at as body bytes arrive, so a server that stalls
  holds a press until the read times out (`config::kHttpTimeoutMs`); a word changed during a full pass reaches the
  mirror with the incremental pass right after it; the re-read after a drop relies on `languageCount` counting exactly
  the items listed (measured for sentence cards; a page without it brings a full pass forward, and were Lexirise to
  stop sending it, full passes would repeat every interval, within the hourly page budget, but for the once-only rule
  above); ~~how items sharing an `updated_at` are ordered across offset pages is unmeasured~~ (measured 2026-09-28:
  stably, the reference notes); a saved id that isn't a whole number isn't kept (all measured ids are); the cursor
  assumes `updated_at` is in commit order (a change stamped earlier than one already read is caught only by the weekly
  full pass), and even in order, a later word of a group tied at the cursor changed in the cursor's own millisecond
  waits for it (pinned by a test); each page that changes the mirror or leaves a pass under way rewrites the whole
  file (up to `config::kVocabMaxBytes`: the device check times it); a quiet incremental pass (one page, nothing new)
  writes nothing.

## 2. The annotations

Each one is a separate on/off setting (`/.lexirise/config.ini`), set per book from the reader menu.

| # | Annotation | What it shows | Default |
|---|---|---|---|
| A1 | **Proficiency marks** | New (not saved or level 0): **solid underline**. Tracked or learning (1–2): **dotted underline**. Fresh or known (3–4): no mark. Suspended/ignored: no mark (ignored: the reader's own list, `/.lexirise/ignored.ini`, keyed by the entry key (`lookup::entryKeyOf`), or its form, `00-overview.md` C17 "As built (V5, local)"; suspended: in Lexirise, from the mirror) | On |
| A2 | **Page stats** | Status bar: `6 new · 2 learning · 89% known` (running-token coverage, as in C5) | On |
| A3 | **Skip to unknown** | With the card open, the side buttons step between A1-marked (unknown or learning) words only, instead of every word (D16); an ignored or suspended word has no mark, so it's skipped. A setting switches back to every word | On |
| A4 | **Seen-again marker** | A small dot after a word you're *learning* when it reappears. Tells you "you saved this, here it is again" | On |
| A5 | **Above-level only** | Restrict A1 to words above a target (`target_level=N2` / `HSK-4`), using `system_tags` from the mirror's embedded `dictionary_entry` | Off |
| A6 | **Adaptive furigana / pinyin** | Readings as ruby **only above words that aren't known** (level < 3). As you learn, the furigana fades on its own | Off |
| A7 | **Hide publisher ruby over known words** | The opposite of A6, for books that print ruby everywhere: suppress the EPUB's own ruby over known words | Off |
| A8 | **Page glossary** | New words on the page get superscript numbers, and a strip at the bottom lists `n word reading · meaning`, capped at 5 lines | Off |

Chapter-level extras (same data, no inline drawing):

| # | Feature | Notes |
|---|---|---|
| A9 | **Chapter primer** | On opening a chapter, the 10 most frequent unknown words in it, with meanings. `⏎` saves all of them. Requires analyzing the chapter (sampled, or all of it if the text limit allows) |
| A10 | **End-of-chapter recap** | Words looked up in this chapter, with `Save all` for the unsaved ones |
| A11 | **Look up later** | Offline: long-press flags the word (stored with its sentence). On the next WiFi-up, the flags are looked up, and a "Flagged words" list appears in the reader menu |

## 3. Drawing (the renderer constraints)

- **Overlay, don't reflow.** A1, A4 and A8's superscripts are drawn **after** the page renders, from
  our code, using word boxes (the same `WordBox` geometry as word select). Nothing inside
  `ParsedText` or the layout changes. That keeps the edits to base files small, as in v0.1.
- **Map spans to glyphs:** each occurrence's `[charStart, charEnd)` (UTF-16) is mapped back to page
  `(line, token)` ranges. It's the inverse of `SentenceBuilder`, shares its unit conversion, and gets
  host tests with the same fixtures.
- **A6 needs line space.** Ruby is laid out at parse time (`TextBlock::getRubyShift`). Rather than
  re-layout per annotation, **furigana mode reserves ruby space on every line** (the same line height
  as a ruby-bearing line), and the readings are overlaid into that space. The trade-off is fewer lines
  per page in exchange for zero reflow. Changing A6 re-lays out the book once, like a font change.
- **A7** is the one annotation that must act at layout: a known-word test while `ChapterHtmlSlimParser`
  collects ruby. It needs the mirror to be available when the chapter is parsed, and a fixed rule when
  it isn't (keep all ruby). It's the most invasive hook, so it's built last.
- **A8 takes space at the bottom** (up to 5 lines). The page reserves it only when A8 is on, which
  again means a one-time re-layout.
- **Mono only:** solid, dotted and double underlines, a small filled dot, superscript digits. No grey
  (grey needs grayscale refresh, which is slow and ghosts). Underline sits **below the descender
  line**, so it doesn't touch ruby from the line below.
- **Refresh:** annotations arrive with the page (prefetch) and cost nothing extra. The late-arrival
  case is one partial refresh of the text area. Count it in the reader's ghost-cleanup cadence.

## 4. Dependencies and blockers

| Needs | For | Status |
|---|---|---|
| Page analysis (§1.1) | Everything | Not built. v0.1 analyzes per sentence |
| Vocab mirror (§1.2) | Offline marks, A4, A5, immediate updates after a save | ~~Not built~~ V7a (in progress, 2026-09-28) |
| **Kana readings** | A6 for Japanese, A8 readings | **Solved:** converted on the device (`../v0.1/languages.md` §3a). Chinese pinyin works as is |
| `analyze/text` maximum text length | Page analysis in one request, and A9 | **No limit hit up to 20k chars** (tested). ~70 bytes of response per character, so ~20 KB per page |
| Whether analyze bumps `seen_count` | Whether prefetching inflates your stats | **Tested: it doesn't.** Prefetching is safe |
| Tokenizer quality | A1 accuracy (一日中雨 came back as one token) | Report issues to Lexirise |

## 5. Build order

1. **Page analysis + cache + prefetch.** Lookups get faster straight away. That alone is worth shipping.
2. **Vocab mirror.**
3. **A2 page stats** (no drawing, proves the data path).
4. **A1 marks + A4 seen-again** (the overlay drawing path, span → glyph mapping).
5. **A3 skip to unknown.**
6. **A5 above-level.**
7. **A10 recap, A11 look up later** (lists, no inline drawing).
8. **A6 furigana**, then **A8 glossary**.
9. **A9 primer**, then **A7 publisher-ruby hiding** (the most invasive).

Each step gets a bench phase (a static page plus recorded analysis) before it touches the network,
the same pattern as the v0.1 card.
