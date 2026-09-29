# Page annotations: marking the page with what you know

**Status:** proposed 2026-09-24. Supersedes backlog item C6 (`00-overview.md`) and folds in the
other page ideas. ~~Nothing here is built.~~ The vocab mirror (§1.2) is built (V7a, 2026-09-28); page analysis (§1.1) is built on the host (V7b, 2026-09-28,
"As built (V7b)"; device checks owed). It depends on
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
  occurrences, no strings the vocab mirror already has). ~~The key includes font and layout settings,
  since a page boundary moves with them. Invalidate on a vocab-mirror change for any entry on that page.~~
  (Superseded 2026-09-28 by "V7b design" (c) below: the file holds the hash of the exact text sent, which any layout
  change that moves the page's end changes; the split doesn't depend on the reader's words, so a mirror change
  re-analyzes nothing: the saved state comes from the mirror when the card opens.)
- **Lookups reuse it:** a tap on an analyzed page skips request ① entirely. The card goes straight to
  phase A, and only `dictionary/lookup` goes out.
- **Budget:** 60–150 pages/h is 5–13% of the 1200 req/h limit. Watch `rateLimitMax` from `/me`, and
  stop prefetching (keep on-demand lookups) above 70% of the window.

**V7b design (2026-09-28, on `lexi/V7`; ~~awaiting claritise's answer on (a) and (d)'s saved state before any
firmware~~ answered the same day, "claritise's decisions" below).** Measured first, read-only (`tools/lexirise/probe_v7b.py`: `../reference/lexirise-api-notes.md` "Page
analysis (V7b), measured"): a page-sized answer is 76–93 KB and ~1.9 s from the Mac, first byte at ~1.3 s; every
sentence sent alone split exactly as it does inside the page, cut sentences at the page's edges included; Lexirise
refines a page ~1.5–2.5 min after it first sees it, and `fast` on the refined page still gives the first split.

- **(a) The radio: prefetch only over WiFi already up; no join, no longer radio time.** Today WiFi comes up only for
  a card and stays up `wifi_idle_min` after it (D10, default 5 min, `../v0.1/settings.md` §1). Options:
  1. **Only while WiFi is already up** (recommended for V7b): the prefetch sends as V7a's pages do
     (`mayJoin=false`: NoWifi when the station isn't connected; it doesn't count as WiFi use, so the idle teardown
     comes when it would have; nor does it hold the TLS session open). Pages read within `wifi_idle_min` of a card
     get analyzed (at ~40 s a page, the default's 5 min is ~7 pages); after that, reading is as today, radio off.
     No new radio time, no setting, no battery question. The cost: each prefetch usually opens its own TLS session
     (a page outlasts `kTlsIdleCloseMs`), ~2.5 s of handshake on the device (`../v0.1/device-checks.md`), inside the
     window WiFi is up anyway (V7c's design resumes the session: `00-overview.md` C21 "V7c design").
  2. **The prefetch keeps WiFi up** (counts as WiFi use): once a card brings WiFi up it stays up for the whole
     reading session.
  3. **Join on page turns:** WiFi up whenever a book is open and Lexirise is on, every page analyzed.
  4. **A setting** ("Analyze pages ahead": Off / While WiFi is on / Always).

  2–4 put WiFi on while reading, which is a battery trade (D10's kind) and **claritise's decision**; nothing measures
  the X4 Pro's current with WiFi up yet (no figure in the docs), so it would come with a device measurement. V7b
  builds 1 only. V9's marks on every page need 2, 3 or 4: the question comes back then, with numbers.
- **The call blocks the reader's loop, so it gives way to input.** Lexipoint's calls are synchronous, on the loop
  task (no worker task: a second task's TLS-sized stack would come out of the internal RAM the handshake's 40 KB
  pre-flight needs, `../v0.1/lexirise-client.md`). The prefetch runs from the reader's loop only when nothing is
  rendering, and gives up for input as V7a's pages do, but checked in every wait of the call (the TCP connect, the
  handshake's poll loop, the ~1.3 s before the first byte), not only as body bytes arrive, and for a touch too:
  the side and power buttons from `HalGPIO::rawInputActive`, a touch from the touch controller's interrupt line (V7a's
  candidate fix, `BoardConfig::ACTIVE.touch.irq` low: **unmeasured**, the device check below decides whether a tap made
  during a prefetch is seen). A page turn during a prefetch then costs the request and the next handshake, not the
  turn. Rare: the prefetch starts `kPagePrefetchDwellMs` (1.5 s, the debounce) after the page is drawn and is done
  within ~3–5 s of a page read in 30 s or more. If the device check finds a tap lost, the fallback is a worker task
  (measured for internal RAM first), not a longer dwell.
- **(b) Memory: streamed, compact, in PSRAM.** A page's answer (up to ~100 KB, over `kHttpMaxBodyBytes`) is never
  held: `net::BodySink` → `json::StreamReader` → a page visitor that writes straight into one `PageAnalysis`:
  sorted fixed-size arrays (occurrences, entries, the saved states) and one string pool, each reserved once
  (`kPageMaxOccurrences`, `kPageMaxEntries`, `kPagePoolMaxBytes`; past a cap the page isn't analyzed and lookups on it
  send ① as today), so above 4 KB they land in PSRAM (malloc's threshold) and nothing makes per-entry nodes in internal
  RAM (no `std::map`, no `std::string` per occurrence: `AnalyzeResult` stays the sentence's type). Occurrences come
  before `entryMetaById` in the answer (measured), so only the entries the page's occurrences name are kept. The
  measured pages hold ~210 occurrences, ~130 entries and up to ~35 saved states: ~10 KB compact. Internal RAM adds
  only the TLS session (as a card's) and the stream's parse state (`kJsonStreamMaxStringBytes`). The V1 merge at page
  scale (below) holds a second `PageAnalysis` (the `fast` answer, ~10 KB PSRAM) until it's merged.
- **(c) The cache file.** ~~`/.lexirise/cache/<book>/<spine>-<visibleTextOffset>.bin`: `<book>` is `h` + the FNV-1a-32
  of the book's path (as `bookSlug`'s fallback), `<visibleTextOffset>` the page's start in the section
  (`Page::visibleTextOffset`, codepoints of visible text, layout-independent). Binary, through `SafeFile`: a header
  (magic, version, language, whether it's a first pass or a merged refined one, the UTF-16 length and FNV-1a-32 of
  the exact text sent, when it was analyzed, the counts, a CRC), then the arrays: an occurrence 20 B (span, entry,
  lemma entry, word-like, the lemma's and reading's places in the pool; the surface is the page text's own slice), an
  entry 20 B (id, rank, frequency, the reading and part of speech in the pool), a saved state 12 B (entry, saved id,
  level, seen count), then the pool. ~10 KB a page, at most `kPageCacheMaxBytes`.~~ (Superseded 2026-09-28 by "As built
  (V7b)" below: `/.lexirise/pages/`, the word kept in the pool too, 13-15 KB a page measured, written plainly.) **Hit** only when the language,
  length and hash match the page's text now: a font, margin, spacing or orientation change that moves the page's end
  changes the text, so the settings needn't be in the key (a layout that keeps the same start and text reuses it,
  rightly). A miss is analyzed again and overwrites the file. **The mirror** (V7a): the split never depends on the
  reader's words, so nothing is re-analyzed for a mirror change; the file's saved states are a snapshot, and the
  card's state comes from the mirror (d). ~~**Eviction:** `/.lexirise/cache/books.ini`, newest book last with its page
  count; past `kPageCacheBooks` books the oldest book's folder goes (a few files per idle prefetch window, never a
  long blocking delete); past `kPageCacheBookPages` pages a book's folder is emptied and starts again (orphans from
  old layouts go with it).~~ (Superseded 2026-09-28: one index of pages, oldest first, "As built (V7b)": the SD layer
  lists no folders, so a page is removed by name.) A file that doesn't check out is removed and the page analyzed
  again.
- **(d) The hooks.**
  - **Page n+1's text before it's drawn:** a section is laid out ahead of the page shown
    (`EpubReaderActivity::loop` keeps `BUILD_WINDOW_AHEAD` pages built), and the reader's idle prewarm already loads
    page n+1 from the section file (`section->loadPage(currentPage + 1)`) and warms its glyphs, 400 ms after a
    render with nothing building and the heap clear. The hook goes there: `buildPageModel` (as word select does) and a
    new ~~`text::pageText`~~ `text::buildPageText`, as built (the `SentenceBuilder` join over the whole page: the D5 rules, ruby excluded as `wordText()`
    never returns it) give the text sent. ~~At a section's last page, n+1 is the next section's first page only when
    its section file is already built (`Section::loadSectionFile`, no build started: a build releases the SD font
    caches); otherwise that page is analyzed once it's shown.~~ (Superseded 2026-09-28: at a section's last page
    nothing more is read ahead; the next section's first page is analyzed once it's shown, "As built (V7b)".)
  - **What's analyzed, in order:** page n if it isn't cached (a jump, a card that just brought WiFi up, the card's
    own page), then n+1; one call per loop pass; never while a card or menu is open (the reader's loop only), under
    RenderLock, while building, or before the page has been up `kPagePrefetchDwellMs`; nothing while pages turn
    faster than that. The analysis is written to the cache, and its saved states go into the vocab mirror as a card's
    analysis does (`LiveSource`'s `liveStatesOf`: ~~the live answer wins~~ (Superseded 2026-09-28, V7c, from V7b's review: the newer of the two says, §1.1 "The saved-state rule (R5)".)).
  - **Budget:** every Lexirise request the service sends counts in an hourly window (60 one-minute buckets); the
    prefetch stops above 70% of `rateLimitMax` (`/v1/me`: 1200 per 3,600,000 ms, measured; answers carry no
    rate-limit headers, so the key's use elsewhere is unseen, and a 429's block, `AccessPolicy`, stops it too).
    Reading at 60–120 pages/h is 5–10% (first passes; a refined page is two calls).
  - **The lookup's reuse:** word select hands the card the current page's `PageAnalysis` when it's cached for the
    page's text now. `LiveSource::analysis(sentence)` then builds the sentence's `AnalyzedSentence` from the page's
    occurrences inside the sentence's span (shifted by its start in the page text: `BuiltSentence` records it) instead
    of sending ① (measured: each sentence alone splits and names entries exactly as the page does); the next sentence
    on the page likewise. The sentence's `AnalyzedSentence` is today's type, so phase A, the form names (C16), Met
    before (C14) and stepping are unchanged; phase B's `dictionary/lookup` goes out as before. A sentence past the
    page's analysis (a page not cached) sends ① as today.
  - **V1's whole words at page scale:** a first-pass answer (`morphoPending: true`, the usual one for a page Lexirise
    hasn't seen) is the word-level split, cached as it is. A refined answer (a page it has seen: the prefetch itself
    makes Lexirise refine it ~1.5–2.5 min later, measured, so a page analyzed again after an eviction or a layout
    change comes back refined) is followed by the page's `fast` call and merged by `wholeWords`' rule (measured on the
    two pages: 5 and 14 ranked whole words put back, 深深, 长得, 小さな, 一気に among them); if the `fast` call fails the
    page isn't cached (the over-split refined answer would stay), and is tried again later. So the cache holds what
    ① would give the card, and "refined results only" (the build order's V7b line) means refined answers only after
    V1's merge. No re-analysis to upgrade a first pass: nothing in V7b uses grammar (V11 will).
  - **The saved state on a cached page (a decision for claritise):** without ①, the card's saved state isn't live.
    Each entry's state comes from the vocab mirror (which the page's own analysis updated, and which holds every save
    and level change made on the reader at once); ~~the file's snapshot only fills an entry the mirror has no record of
    when the page was analyzed after the mirror's last full pass (a prefetch's states lost to a deep sleep before the
    mirror was written), or when the mirror isn't loaded yet this boot.~~ (Corrected 2026-09-28, V7b R1: the mirror
    keeps a removal the reader's answers or writes reported, so it speaks for a removed word too; ~~for an entry it
    doesn't hold at all, the page's snapshot stands until the mirror's last pass, full or incremental, ended after the
    page was analyzed (`SyncState::lastSyncS`, not the cursor: a removal doesn't move the cursor)~~ (R3: an entry the
    reader's own answers or writes put speaks always; any other entry, and the lack of one, only once the mirror is
    complete as of a time after the page was analyzed, `SyncState::lastSyncS`: the start of the last incremental pass
    that ran from the top to its end, not a pass's end, since a pass holds only what was in the account when its first
    page was read; else the page's snapshot, the newer word, stands); and all of it while the mirror isn't loaded, but
    for the reader's pending answers and writes: "As built (V7b)".) (Superseded 2026-09-28, R5, by one model after
    four patched holes: **the newer word wins.** Every mirror entry carries the time its state was known; the page's
    snapshot has its analysis time; the newer of the two says; a word the mirror doesn't hold is unsaved only once the
    mirror is complete as of a time after the page; with a time unknown, only the reader's own writes outrank the
    snapshot. "As built (V7b)", "The saved-state rule (R5)".) **The difference:** a word saved or changed in
    the Lexirise app since the page was analyzed shows its older state until the mirror's next sync (on idle cards,
    `kVocabSyncIntervalMs`). Recommended: accept it (the reader's own changes are immediate; the alternative, sending ①
    anyway, gives the speed back).

**claritise's decisions (2026-09-28, relayed by the coordinator):**

1. **The radio: "Only if already on".** Page analysis and the prefetch never join WiFi: (a)'s option 1, as built.
2. **The saved state on an analyzed page:** the recommendation stands (① skipped, the state from the mirror, then the
   page's snapshot), and claritise asked for a sync button, placed by them: **"put it on the home screen"**. Two
   freshness measures come with it:
   - **(a2) A probe as a card opens.** Once the card's phase A and B are on screen, the incremental pass's probe (the
     `kVocabProbeItems` page) runs after a short idle (`kVocabCardProbeIdleMs`, 1 s: the reader is reading the meaning;
     a tap or step in that second goes first) instead of V7a's `kVocabIdleMs`, at most once per
     `kVocabCardProbeIntervalMs` (5 min: a change made in the app is usually made in one sitting before reading, so
     the first card after it finds it; at most 12 probes an hour, inside `kVocabPagesPerHour`, ~1% of the limit; the
     default `wifi_idle_min` is 5 min, so a burst of cards on one WiFi-up probes once). Over WiFi already up only, the
     card's own: never a join; it gives way to input as every V7a page does (and now a touch, below). A probe that
     changes a word on the open card updates that word's saved state through the card's usual redraw (the same
     path a live answer takes; nothing new drawn). Nothing between the tap and phase B waits on it: it runs only on a
     card with nothing to fetch, send or draw.
   - **(b2) "Sync vocabulary" on the home screen.** A menu entry, shown only when Lexirise is on and a key is set
     (compiled out without Lexirise). Pressing it is the reader's own request, so it **may join WiFi** (the saved
     network, as a card does); it runs the incremental pass to the cursor for each language switched on (and the full
     pass where one is due or under way), with the home screen's own popup and progress bar
     (`GUI.drawPopup`, `GUI.fillPopupProgress`) and a result popup; any button cancels. Its look is a mockup for
     claritise's glance first (`reference/v7b-home-sync.html`), built once they've seen it. **Signed off by claritise
     2026-09-28:** the place "Just above Settings" (after File Transfer), and the short wording set: the row "Sync
     Vocabulary"; the popups "Syncing vocabulary..." · "Vocabulary up to date" · "Synced · N words changed" · "Sync
     failed · No Wi-Fi" · "Sync stopped" (the mockup's own wording superseded in place there). R2 (2026-09-28): a rejected key
     and a rate limit use the card's own "Lexirise key rejected" and "Lexirise: rate limited"; ~~the plain "Sync failed"
     for other failures and a singular "Synced · 1 word changed" are asked of claritise (pending).~~ **Answered by
     claritise 2026-09-28:** other failures show plain "Sync failed" (signed off), and "yes" to the singular "Synced · 1
     word changed" (picked by the count: `vocab::syncedText`). R5 (2026-09-28): a press with the hour's manual pages spent
     (`kVocabManualSyncPagesPerHour`) shows the card's "Lexirise: rate limited" at once, without joining WiFi (the
     coordinator tells claritise about the reuse); R10 (2026-09-28): so does a press once this reader's requests in the
     last hour reach `kVocabManualSyncStopPercent` of the key's limit.
- **(e) ~~Nothing on screen changes.~~** (Superseded 2026-09-28 by claritise's decisions above: the home screen gains
  the Sync Vocabulary row, signed off; the card and the reader draw nothing new.) No new drawing, no setting, no card change. What differs: a card on an analyzed
  page reaches phase A without waiting for ① (on the device a warm ① is ~0.4 s, and ~1.1 s when it came back refined
  and needed the `fast` call: `../v0.1/device-checks.md`), and the dev log gains `[LXPAGE]` lines. Marks are V9's.
- **(f) Tests and checks.** Host: ~~`text::pageText`~~ `text::buildPageText` (every `describeTap` sentence is the page text's slice at its
  start, fixtures from `test/lexirise_sentence`); the page visitor on synthetic answers (key orders, caps, a
  malformed or cut body, a streamed body in pieces equal to one piece); the page-scale merge against `wholeWords` on
  the V1 fixtures; the file (round trip, CRC, version, text-hash and language misses, caps, eviction and the books
  index); the sentence slice (equal to `parseAnalyze` of the same synthetic sentence); the state precedence; the
  prefetch policy (pure: dwell, flipping, WiFi up only, rendering, a card open, the budget at 70% and across the
  millis wrap, a 429's block, give-up for input in each wait); `LiveSource` on a cached page (the fake API sees only
  `dictionary/lookup`). `lxctl.py page-smoke` (read-only, a dev build, one held session): open a card (WiFi up),
  close it, turn pages at reading pace and fast, then open a card on a prefetched page, asserting from the log: one
  `analyze/text` per page, none while flipping, none on the card, and each `[LXPAGE]` line's time and heap. **Owed
  on the device:** a tap and a side button during a prefetch (handled at once, the page given up); a prefetch's heap
  (free, lowest, largest) with a fresh TLS session; its time to the first byte and to the end; the file's write time;
  the reuse's phase A time against ①.
- **Also in V7b** (carried from V7a's review, its landing commit): a persisted re-read bit, so a re-read or a
  restart at the top can't end an incremental pass inside a cursor tie; the page log's "written" only when written;
  `kVocabPendingMax`'s comment (20 B on Xtensa); `parseVocabPage` over the measured raw pages on the host, and in
  Known limits that a page that never parses holds a full pass; `loggablePath`'s `/v1/vocabulary?` prefix pinned in
  `RequestsTest`; `kMsDigits` in `VocabPage.cpp`; a device check for a tap during the first idle card's file read.

**As built (V7b, 2026-09-28, on `lexi/V7`):**

- **What's analyzed, when** (`page/Prefetch.h` `PagePrefetcher`, the reader's side `page/ReaderPages`, called from
  `EpubReaderActivity::loop()` after the idle prewarm): the page on screen, then the next, each once, when the page
  has been drawn and up `config::kPagePrefetchDwellMs` (1.5 s: pages turned faster are never analyzed), the station is
  connected already (`LexiriseService::wifiConnected`; never a join, claritise's "Only if already on"), nothing renders
  or lays out, Lexirise is usable for the book, no 429 or rejected key, and this reader's own requests over the last
  hour (`api::RequestWindow`, minute buckets, counting every request that reached Lexirise) are below
  `config::kPageBudgetPercent` of the key's limit (`/v1/me`'s `rateLimitMax`, else the measured
  `config::kRateLimitDefault`). One page per loop pass; the text comes from the section's layout (`Section::loadPage`
  under RenderLock; the page model without measuring: only the tokens make the text), `page::describePage`
  (`text::buildPageText`, the book's language, or the page's own text for a book that doesn't say). At a section's
  last page the next isn't read. A page over `config::kPageMaxTextUnits` isn't sent.
- **The call** (`LexiriseApi::analyzePage`, `api::analyzePageRequest`: the default mode, the page's whole text) is
  streamed (`page::PageReader`, a `net::BodySink` over `json::StreamReader`) into a `page::PageAnalysis`: sorted
  arrays and one string pool, the entries the occurrences name only (measured on the probe's pages: 206 and 220
  occurrences, 107 and 129 entries, a 4.2-4.6 KB pool; `RawPages` in `test/lexirise_page`, run with
  `LEXIPOINT_RAW_PAGES`). Like a mirror page it's never counted as the reader's WiFi use or TLS activity. **It gives
  way to input:** the client asks the call's abort between reads and the TLS connection in its handshake, write and
  read waits (`net::Connection::setAbort`, `ApiError::Cancelled`, the connection closed), and the reader's abort is a
  side or power button (`HalGPIO::rawInputActive`) or a finger on the screen (~~`HalGPIO::rawTouchActive`~~ `HalGPIO::rawTouchLevel` read through `input::TouchLine`, R1; the touch
  controller's interrupt line: unmeasured, `../v0.1/device-checks.md` "v0.2 V7b"); the TCP connect itself (DNS and
  SYN, inside the SDK) can't be interrupted. A page given up waits a whole dwell again; a failed one
  `config::kPageFailureWaitMs` (a 429 its own retry time).
- **A refined answer** (`morphoPending: false`: Lexirise has refined the text before) gets the page's `fast` call and
  V1's merge (`page::mergeWholeWords`, `lookup::wholeWords`' rule, pinned against it); if that call fails the page
  isn't kept (and waits). On the probe's refined pages the merge gave back the first pass's split (206 and 220
  occurrences).
- **The file** (`page/PageStore`, `../v0.1/settings.md` §3 "The page cache"): `/.lexirise/pages/<book>/<spine>-<start>.bin`
  (`<book>` the FNV-1a 32 of the book's path, 8 hex digits; `<start>` `Page::visibleTextOffset`), used only when its
  language, length and text hash match the page's text now; 13.0 and 14.8 KB for the probe's pages. Written plainly
  (it's regenerable: a torn one fails its CRC and is removed); `/.lexirise/pages/index.bin` (crash-safe) keeps the
  pages oldest first, at most `config::kPageCacheFiles`, one more removes the oldest page's file. A page analyzed
  again moves to the newest end; a read doesn't reorder (no write for a read).
- **The mirror:** the page's states go to the vocab mirror as a live answer (`VocabStore::record`) only when it's
  loaded already (one not loaded keeps only the newest card answers, which a page's hundred entries would push out).
- **The card** (`page/PageSentences`, given to `LiveSource` by word select when it knows the section): the first
  sentence asked reads the page's file once (outside RenderLock, never as the card opens) for the page's text as word
  select laid it out; a sentence in the same language is the page's occurrences over its span (`page::sliceSentence`,
  its start from `text::pageOffsetOf`; an occurrence across the sentence's cut edge is left out) and
  `lookup::analyzeTap` skips ① (`known`); the card's words, form names, Met before and stepping are as before, phase B
  asks `dictionary/lookup`. Its states (`page::applyMirrorStates`; superseded by R5's rule, below, the struck and
  R3/R4 text kept for the record): ~~the mirror's entry when it holds one (a removal:
  unsaved); unsaved when it doesn't and its cursor has passed the page's analysis time (R1: the cursor is the
  account's newest change, which a removal doesn't move) unsaved when it doesn't and its last pass ended after the
  page's analysis time~~ ~~(R3, 2026-09-28) an entry the reader's own answers or writes put (`live`: a removal is always
  one) is the mirror's; once the mirror is complete as of a time after the page's analysis (`SyncState::lastSyncS`:
  the first page's time of the last incremental pass that ran from the top to its end this boot and could bound what
  it read; a full pass never sets it, nor a pass resumed after a restart; in the file when it's written, a quiet pass
  keeping it in memory only, so after a restart it can read older, never newer), any entry it holds is its and one it
  doesn't hold is unsaved; else the page's snapshot (and all of it while the mirror isn't loaded, but for the reader's
  pending answers and writes, `VocabStore::pendingState`). **Removals** (V7b R1, `vocab::Entry` with
  saved id 0, always `live`): a live answer or a write that says a word the mirror holds isn't saved keeps it as a
  removal instead of erasing it (only for an entry it held: the unsaved words an answer lists aren't added), in the
  file like any record; a page's item for the entry replaces it; a full pass's end drops the removals older than it
  (R3: one made during the pass, marked with its generation, is kept: the pass may have read the word before) (R4,
  2026-09-28: a full pass's sweep never drops a removal, since a page analyzed before it may still need it and the
  mirror isn't complete past that page until the incremental pass after it); the incremental pass from the top that
  ends after a full pass has ended (`lastSyncS` then past every page analyzed before that full pass began) drops the
  removals made before the full pass (an older mark); one made since (during the full pass, or during that incremental
  pass, after its start) stays until the same happens after the next full pass. Bounded by the mirror's cap: a
  removal only ever replaces an entry the mirror held (`config::kVocabMirrorMax` in all), so they can't grow past it
  even if no incremental pass ends.~~ (Struck 2026-09-28, R8: R3's precedence and R1/R4's removals, superseded by the saved-state rule (R5), below.)
- **The saved-state rule (R5, 2026-09-28; replaces R1-R4's precedence above).** Every mirror entry carries `asOfS`,
  the time its state was known: a sync page's item, the page's read time (with no clock, the item's `updated_at`, a
  time it was surely true at: the read time is right because a page read after a change reflects it, and the reader's
  writes reach the account before they reach the mirror); a live answer, the answer's time (a card's, stamped as it's
  recorded, `VocabStore::setClock`; a page prefetch's, ~~its analysis time~~ the time before its call when the clock
  is set, after it otherwise: superseded 2026-09-28 by V7c, `00-overview.md` C21 "As built (V7c)"); the reader's own write, the write's time,
  flagged `own`; a removal, its time. The file keeps both (version 3, 20-byte records: `../v0.1/settings.md` §3; an
  older file is set aside and synced again). A page sentence's word: the newer of the entry and the page's snapshot
  says (`vocab::mirrorOutranks`: the same second, the mirror); a word the mirror doesn't hold is unsaved only once
  the mirror is complete as of a time after the page (`SyncState::lastSyncS`), else the snapshot stands; before the
  mirror loads, the reader's pending answers and writes (with their times) take the entry's place. **With a time
  unknown** (the clock not set when the entry or the page was known): the reader's own writes still win, and anything
  else leaves the snapshot (the conservative way). **Removals** now: kept for an entry the mirror held and for any
  entry the reader removed themselves (`LiveState::own`: not for every word an answer lists unsaved, which would fill
  the mirror and then refuse real saves); a page's item replaces one; a sweep never drops one; the incremental pass
  from the top that sets `lastSyncS` drops those known as of it or before (then the word's absence says unsaved), and
  those of unknown time. Bounded by the mirror's cap (`config::kVocabMirrorMax` entries, removals included). A page
  reading an entry again whose state didn't change moves its time on in memory only (no file write; after a restart
  the time reads older, which is safe: the state was the same then). The `live` flag no longer decides precedence (a
  full pass still takes a live entry's page item). Pinned in one place: `SavedStateRule` in
  `test/lexirise_page/SavedStateTest.cpp`, every earlier scenario (R1 M1, R2 S2, R3 S1, R4 S1, R5 M1/M2) and the
  reviewers' probes. Such a sentence isn't given to the mirror again (it isn't a live answer). The dev log says
  `[LXPAGE] card: page <spine>-<start> analyzed: no analyze/text for its sentences` (or `not analyzed`).
- **The log** (every build): `[LXPAGE] <this|next> page (<n> of section <s>): <analyzed|kept already|failed|unusable|
  given up for input|no text> in <ms> ms (<error>), <occurrences> occurrences[ (refined, merged)], <calls> calls in
  <ms> ms, written in <ms> ms[ (failed)]; heap <free> free, <min> min, <largest> largest`. **R1:** a page is analyzed
  only once it's drawn (`page::Drawn`, set where `renderBook` draws it), and its dwell counts from its latest drawing
  (a card closed over it draws it again: nothing starts as the reader gets back to it); a page given up for input
  waits a dwell from the call's end; an answer that can't be read or kept (malformed, over a cap: `page::fitsFile`,
  checked before any write, a merge of two answers can pass one) is `unusable` and not asked again on this showing;
  whether Lexirise is usable for the book is worked out once per page shown and when the settings change. **The touch
  line** (`input/TouchLine`, `input/InputAbort`): which level means a finger isn't assumed (the X4 Pro's config, the
  SDK's pin mode and the controller's mode all bear on it): the idle level is learned from the reader's and the card's
  loop passes with no finger down (the dev log says `[LXIN] touch line idles <high|low>` once), anything else is a
  touch, nothing is before it's learned, and a line that changes `config::kTouchLineFlipsMax` times within
  `kTouchLineFlipWindowMs` with no finger down is never used again that boot (logged), so it can't make every call give
  up. One `input::inputCame` serves the page's call, the mirror's pages and the card's probe. Device checks owed: `../v0.1/device-checks.md` "v0.2 V7b".
- **The probe as a card opens** (claritise's (a2); `VocabStore::takeCardProbe`, `CardSession::shouldProbeVocab`, the
  card's idle step `Probe`): once the card has nothing to fetch, send, draw or handle and has been idle
  `config::kVocabCardProbeIdleMs` (1 s), the incremental pass's probe (`kVocabProbeItems`, from the top) over the card's
  WiFi, for a loaded mirror that has synced with no pass under way and no failure's wait, at most once per
  `config::kVocabCardProbeIntervalMs` (5 min) across cards, inside the hourly page budget, not in the card's share of
  pages. It's the incremental pass's first page as any (a longer change goes on by V7a's idle pages). The first card of
  a boot probes once the mirror has loaded (its first idle window's file read, `kDeckIdleMs`), a second later. A word on
  the card whose entry the probe changed takes the mirror's state (`LiveSource::takeMirrorChanges`, the lemma's entry
  first, notes and tags from its item kept; not an entry this card wrote) and its level (`CardController::
  savedLevelChanged`, no toast), and the card is redrawn through the usual path (R1: an idle mirror page that
  changes a word on the card does the same); the log says `[LXVOCAB] card probe:
  <n> items in <ms> ms (<error>), <n> entries changed[, the card redrawn]`. It gives way to a button or a touch like a
  page (the mirror's pages now take the call's abort too, `LexiriseApi::vocabularyPage`); a probe given up is spent
  (the next in 5 min).
- **Sync Vocabulary on the home screen** (claritise's (b2), signed off 2026-09-28; `vocab/ManualSync` pure,
  `vocab/HomeSync` the device side, hooks in `HomeActivity` and `HomeMenuItem::VOCAB_SYNC`, all inside `#if LEXIRISE` until v0.2 V8 removed the gate):
  a row just above Settings with the Wi-Fi icon, shown when Lexirise is on, a key is set and a language is switched on
  (`vocab::syncRowShown`, read as the home screen opens). Pressed: the home screen's popup "Syncing vocabulary..." with
  its progress bar; the reader's own request, so it joins WiFi once, first (`LexiriseService::joinForUser`, the saved
  network as a card joins: the only join outside a lookup's calls; WiFi is held up while it runs, ~~and its idle
  teardown counts from the end~~ and given back as soon as it ends (R2: nothing on the home screen uses it,
  `../v0.1/offline-and-errors.md` §5)); then one page per loop pass for each language switched on: the full pass while one is under
  way or due, then an incremental pass from the top (a probe first) to the cursor (`VocabStore::manualNext`: no
  interval, no failure's wait, outside the idle pages' hourly budget, which its pages don't count against either (R2:
  a big manual sync never holds the idle pages back for an hour; `PageCall::manual`); at most `config::kVocabManualSyncPagesMax` pages a
  press, and `config::kVocabManualSyncPagesPerHour` across presses in an hour (R4: ~~repeated presses can't run the key
  into a 429 that would block the cards~~ (2026-09-28, R10: not with the page analysis and the idle pages counted; a
  run also stops at `config::kVocabManualSyncStopPercent` of the key's hour, "R10" below); past it a press says "Synced" with what it read), the passes resumable, so a large first sync goes on at the next press or on idle cards). The progress is the
  pass's offset against its list count (a probe: half the language). Any button or touch stops it (between pages, and
  a page under way gives up by `input::inputCame`). The result for `config::kVocabSyncResultMs` over the menu drawn
  again: "Vocabulary up to date" (nothing changed and nothing left: a run the page cap stopped says "Synced"), "Synced ·
  N words changed" (distinct entries whose saved state, level, suspension or review time a page added or changed: R2,
  a weekly full pass's new marks alone count nothing, `visiblyChanges`), "Sync failed · No Wi-Fi", "Lexirise key
  rejected" and "Lexirise: rate limited" (R2: the card's own words for them), "Sync failed" (any other failure; signed off by
  claritise 2026-09-28, as is "Synced · 1 word changed" for one, picked by the count), "Sync stopped"; then the menu. What the popup shows is worked out under RenderLock after each
  step (`HomeSync::refresh`): the render task never reads the sync while a step changes it. The log:
  `[LXVOCAB] home sync: <result> after <n> pages, <n> words changed`.
- **R2:** nothing starts while the toolbar or a panel is over the page or pages turn by themselves (`readerBusy`);
  the drawn page travels from the render task in one atomic word (`page::packDrawn`); the reader's own answers and
  writes waiting for the mirror's load win over a page's snapshot (`VocabStore::pendingState`); an answer's small
  arrays (the entries, ~2 KB, and the states) are reserved as their first element arrives, after the handshake, and
  being under 4 KB they're in internal RAM (the occurrences and the pool, larger, in PSRAM).
- **R3:** a page is known by its section and its first visible character, not its index (a reflow gives the same
  index other text: it's analyzed again); an input already queued on a loop pass holds a page's step back until it's
  handled; the V1 merge carries a cut answer's `statesCut`; the page index is saved every `config::kPageIndexSaveEvery`
  pages and as the reader closes (`PageStore::flush`: the index is rewritten whole, 12 KB when full; a power loss
  between saves leaves up to `kPageIndexSaveEvery - 1` pages unindexed: read by name and replaced when analyzed again,
  never evicted); an unreadable index takes the whole page folder with it (`SettingsFiles::removeTree`: its pages
  can't be evicted one by one any more). The home screen's sync: what a loop pass does is ~~`vocab::homeSyncAction`~~
  `vocab::HomeSyncFlow` (R9; pure: stop on a press, a step once its popup is drawn, the result dismissed ~~when its
  time is up or on input once shown~~ on the release of a press that began while it was shown, or when its time is up
  with nothing held, so the dismissing press never reaches the menu: a Confirm starting a second sync, a Back opening
  the last book, a tap on the row underneath); the sync ends outside the render lock (its release of WiFi blocks),
  logged `[LXVOCAB] home sync: WiFi <given back|left up (not Lexipoint's)|not up> in <ms> ms` (R9: "not up" when it
  never joined); a press while Lexirise refuses calls (a 429's wait, a rejected key) says so without joining (R9);
  `lxctl.py home-sync-smoke` checks it, and taps the result: no second sync (read-only, never run here).
- **R5 also:** a press of Sync Vocabulary with the hour's pages spent says "Lexirise: rate limited" at once, without
  joining WiFi; the home screen keeps the reader awake while a sync runs (`preventAutoSleep`); the home menu's index
  is `home/HomeMenuIndex.h` (pure, every ~~OPDS ×~~ Sync Vocabulary combination tested; OPDS removed in V8, `slimming.md` §8); a page step counts only calls
  answered (one given up isn't, in the step or the log's count).
- **R7:** the binary files' helpers have one home (`util/ByteOrder.h`, `util/Crc32.h` with `bytes::Fnv1a`), the
  files byte for byte as before (the pinned tests unchanged); a tap on an analyzed page whose slice has no word (a
  token crossing the sentence's cut edge) asks ① after all (`LookupReport::fromPage`).
- **R8:** a chapter's first read is analyzed too: a section build paused ahead (it stays open until the chapter's
  end) isn't busy, only a build tick due now (`EpubReaderActivity::buildTickDue`, one predicate with
  `skipLoopDelay`); the reader's conditions are a pure `page::readerConditions` (tested); while the mirror is
  overflowed its removals stay (absence can't speak for them); the home sync applies each page with the wall clock
  read after its call (a cold boot's first page gets the time its call set).
- **R9:** a page step and page-smoke both count only calls answered with an HTTP status; one log tag for the page
  analysis (`page::kLogTag`); §1.2's V7a statements that V7b changed are struck in place.
- **R10:** a call given up for input waits a whole dwell from its end: the same drawing told again on every pass
  doesn't restart the dwell from the drawing (`PagePrefetcher::shown` restarts it only on a new drawing); a button
  still held (release-mode page turns, a long Confirm: no edge queued) or a finger down is busy
  (`ReaderInputs::inputHeld`, from `HalGPIO::rawInputActive`), and a step with input there already reads nothing and
  calls nothing; a loop pass's decision (the cheap gates, the page's start once per drawing, the dwell, the
  conditions) is the pure `page::PagePass` (tested), `ReaderPages` only its device glue. A press that stopped the home
  sync inside a call (its edge seen only on the next pass, maybe with "Sync stopped" drawn already) doesn't dismiss
  the result on its release: `HomeSyncFlow` arms only on a press after a pass with the result shown and nothing held.
  A press of Sync Vocabulary doesn't join, and a run stops between pages, once this reader's requests in the last
  hour reach `config::kVocabManualSyncStopPercent` of the key's limit (`ManualSync::KeyUse`: the page analysis and
  the idle pages count too; `kVocabManualSyncPagesPerHour` alone could pass the default limit).
- **Known limits:** the next section's first page isn't analyzed ahead; a page analyzed while the mirror isn't loaded
  shows its snapshot's states, but for the reader's own answers and writes, until the mirror loads (the first idle
  card), ~~and once it's loaded can lose to an older card's answer for the same word (a live entry wins whatever its
  age) until the next incremental pass from the top ends~~ (superseded 2026-09-28 by "The saved-state rule (R5)": the
  newer of the entry and the page says, whoever put the entry); **a change stamped behind the cursor** (`updated_at`
  not in commit order, or a later word of a group tied at the cursor changed in its own millisecond: V7a's known
  limits) is missed by the incremental pass, which still makes the mirror complete as of its start, so a card on a
  page analyzed before then shows that word unsaved although the page's snapshot said saved (V7a hid this: ① was
  live); it heals with a later answer for the word (a card's, a page's) or the weekly full pass (pinned by
  `SavedStateRule.AChangeStampedBehindTheCursorIsMissedUntilALaterAnswerOrTheWeeklyPass`; no guard: whether
  `updated_at` follows commit order is an open measurement, `../reference/lexirise-api-notes.md`); **at the mirror's
  cap** (`kVocabMirrorMax`) the reader's own removal of a word the mirror doesn't hold isn't kept (`Put::Full`), and
  the page's snapshot stands; and (R7) once the mirror has refused any entry at its cap (`SyncState::overflowed`, set
  by a page's item or a live answer refused, cleared when a full pass ends having refused nothing), a word's absence
  no longer says unsaved: absent words fall back to the page's snapshot (the size alone doesn't decide: a full
  mirror that refused nothing is complete); the reader's own removal of unknown time (no clock) stays until a full
  pass ends; the home screen's sync joins WiFi without a way to interrupt it (up to `kWifiJoinMaxMs`: a device check
  times a press during it); a word removed in the Lexirise app shows
  its page's saved state until a mirror pass ends after the page's analysis (the probe as a card opens, or the idle
  pages); a quick tap between two samples of the touch line (every few ms while a call waits) may be missed; the hourly count is this reader's only
  (the key's use elsewhere is unseen; a 429 stops it); ~~an index lost or unreadable leaves its pages on the card,
  unindexed (they're found again by name when their page is read, and overwritten, but never evicted)~~ (R3/R4: an
  unreadable index, one that doesn't parse, is too large or fails to read, takes the page folder with it; only a power
  loss between the index's saves leaves pages unindexed, at most `kPageIndexSaveEvery - 1` each time: read by name
  and replaced when analyzed again, never evicted); removing the page folder after an unreadable index blocks the
  loop once (up to `kPageCacheFiles` files, within a page step, and input can't interrupt it: a device check times
  the pause); the page index's last save runs as the reader
  activity is destroyed, under the activity manager's lock (up to 12 KB, crash-safe); the TCP connect of a page's call (DNS and SYN, up to
  `kHttpTimeoutMs` on a weak signal) can't be interrupted by input.

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

- **What's kept**, per saved word, one ~~16-byte~~ record (`vocab::Entry`, `config::kVocabRecordBytes`; 20 bytes since V7b R5, with the time
  its state was known: superseded 2026-09-28, V7c, §1.1 "The saved-state rule (R5)"): the entry a save
  targets (the item's `dictionary_id`, which is `stateByEntryId`'s key), the saved expression's id (`id`, the state's
  `saved_expression_id`), the level, `suspended` and `next_review_at` (measured: `../reference/lexirise-api-notes.md`
  "V7's foundations"). **Sentence cards aren't kept** (`unit_type` other than `word`): they mark no word on a page. At
  most `config::kVocabMirrorMax` words per language (past it a new word isn't kept, logged). The file, one per
  language, binary (sorted records behind a header with the sync's progress and a CRC): `../v0.1/settings.md` §3.
  Binary rather than lines: a record is a fixed ~~16~~ `kVocabRecordBytes` bytes (superseded 2026-09-28, V7c), read straight into a sorted array for a binary search, and a
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
- **When it syncs: ~~only on an idle card, over WiFi already up~~** (V7b, 2026-09-28: also the card's probe as it
  settles, and the home screen's Sync Vocabulary, which joins WiFi itself: §1.1 "As built (V7b)"). Like V3's deck steps (`CardSession::shouldFetchVocab`
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
  card's pages; a button already held as a page is due gives it up before the request (no request spent). ~~**A tap or a
  Home press made and released during a page is likely lost** (unmeasured): the touch controller is only read by the
  loop's input update, which the page blocks, as the side buttons were before the cancel (`../v0.1/device-checks.md`);
  one still held when the page ends is seen after it.~~ (Superseded 2026-09-28 by V7b: a finger on the screen gives a
  page up too, `HalGPIO::rawTouchActive` OR'd into the cancel, and the cancel is asked in every wait of the call, not
  only as body bytes arrive: §1.1 "As built (V7b)"; whether a quick tap is always seen is a device check.) Not while reading without a card: a page blocks the loop (a few
  seconds: the device check times it), which a page turn mustn't wait on. No new setting.
- **The file is read and written only on an idle card** (~~`CardSession::shouldFlushMirror`~~ `CardSession::shouldFlushFiles` since V7c, with the lemma cache's answers,
  superseded 2026-09-28: the deck's idle rule,
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
  Meanwhile ~~the live answer wins~~ the newer state wins (superseded 2026-09-28, V7c: §1.1 "The saved-state rule
  (R5)") for every word the card analyzes (below).
- **The card's answers and writes.** Each analysis's states (`stateByEntryId`) for every word's lemma and surface
  entries go into the mirror (`stateByEntryId` lists level-0 and suspended items too, measured:
  `../v0.1/lookup-flow.md` §5, the level-0 item a removal leaves, and the reference notes' "Suspended (Ignore)", so an
  answer never erases them), and an entry the answer doesn't list isn't saved there either (unless the answer was cut
  at `config::kMaxEntries`; V7b: an entry the mirror held is kept as a removal rather than erased, with its time, and
  ~~**the live answer wins** where they disagree~~ the newer of the mirror's and an answer's state wins: §1.1 "The
  saved-state rule (R5)"), so ~~**the live answer wins** where they disagree~~ (struck 2026-09-28, V7c: the clause the R5
  line above already superseded); except an entry this card wrote (its
  write is newer than a later analysis). Each save, level change and removal goes in once Lexirise took it
  (`LiveSource::recordMirror`, memory only, after every answer), kept under the entry whose state the card had (the
  surface word's when only it was saved; a new save's under the lemma); a removal stays at level 0, as Lexirise keeps
  a dictionary word's item. An entry a live answer put is marked so (`Entry::live`): a full pass under way still takes
  the page's item when it reaches it (the answer has no review time or suspension). Written with the file (above); a
  card closed by sleep loses what it hadn't written (sleep is a deep sleep: the reader restarts), which is harmless:
  Lexirise has it, and the next incremental pass brings it back.
- **Use.** ~~The card's saved state still comes from `analyze/text`; nothing it draws changes.~~ (Superseded
  2026-09-28 by V7b: on a page analyzed ahead, a card's saved states come from the mirror by the saved-state rule, and
  a probe or an idle page that changes a word on the open card redraws it: §1.1 "As built (V7b)".) ~~The mirror is the
  offline source (`VocabStore::savedState(language, entryId)`) and V9's~~ (corrected 2026-09-28: nothing calls it yet)
  `VocabStore::savedState(language, entryId)` is there for V9's marks and the offline saved state they'll show; the
  StarDict answer offline isn't given it (it has no entry ids, and its popup would change).
- **Known limits.** The sync moves only while cards sit idle (a reader who closes cards at once syncs slowly: the
  device check measures a first full sync); a page given up for a button costs a request (the hourly budget counts it;
  a button already held when a page is due gives it up before its request, which costs nothing); ~~the Home key and a
  touch don't give a page up (only the side and power buttons do), and one made and released during a page is likely
  lost (the device check says; the candidate fix, once it's confirmed: `HalGPIO::rawTouchActive()` reading the touch
  controller's interrupt line, `BoardConfig::ACTIVE.touch.irq` held low, OR'd into the cancel
  `LexiriseCardActivity::idleStep` passes); the button is only looked at as body bytes arrive, so a server that stalls
  holds a press until the read times out (`config::kHttpTimeoutMs`)~~ (superseded 2026-09-28 by V7b: a touch gives a
  page up too, through the learned touch line, `input::inputCame`, and the abort is asked in every wait of the call,
  the TCP connect aside: §1.1 "As built (V7b)"; the capacitive Home key still doesn't); a word changed during a full pass reaches the
  mirror with the incremental pass right after it; the re-read after a drop relies on `languageCount` counting exactly
  the items listed (measured for sentence cards; a page without it brings a full pass forward, and were Lexirise to
  stop sending it, full passes would repeat every interval, within the hourly page budget, but for the once-only rule
  above); ~~how items sharing an `updated_at` are ordered across offset pages is unmeasured~~ (measured 2026-09-28:
  stably, the reference notes); a saved id that isn't a whole number isn't kept (all measured ids are); the cursor
  assumes `updated_at` is in commit order (a change stamped earlier than one already read is caught only by the weekly
  full pass; V7b: meanwhile a card on a page analyzed before shows it unsaved, §1.1 Known limits), and even in order, a later word of a group tied at the cursor changed in the cursor's own millisecond
  waits for it (pinned by a test); each page that changes the mirror or leaves a pass under way rewrites the whole
  file (up to `config::kVocabMaxBytes`: the device check times it); a quiet incremental pass (one page, nothing new)
  writes nothing. **A page that never parses holds its pass** (V7b, 2026-09-28): an unreadable page waits
  `kVocabFailureWaitMs` and is asked again at the same offset, so a full pass stops there for good (and the
  incremental passes wait behind it) until Lexirise answers it readably; the measured pages (the dev account's lists,
  `research/v7/`) all parse, whole and streamed in 1 KB pieces (`RawPages` in `test/lexirise_vocab`, run with
  `LEXIPOINT_RAW_PAGES` on the raw answers: skipped without it). **An incremental pass that has read again**
  (`PassProgress::reread`, V7b: in the file, bit 3 of its flags) no longer ends at an unchanged item as old as the
  cursor: its pages repeat items anywhere, so it ends at an older item or the list's end (before V7b a re-read, or one
  sent back to the top, could end the pass inside a group tied at the cursor, the rest waiting for the weekly pass).

## 2. The annotations

~~Each one is a separate on/off setting (`/.lexirise/config.ini`), set per book from the reader menu.~~ (Superseded 2026-09-29 for V9a: two global rows in Settings → Lexirise → On the page and one per-book **Page marks** row in the reader menu, "V9a decisions" below.)

| # | Annotation | What it shows | Default |
|---|---|---|---|
| A1 | **Proficiency marks** | New (not saved or level 0): **solid underline**. Tracked or learning (1–2): **dotted underline**. Fresh or known (3–4): no mark. Suspended/ignored: no mark (ignored: the reader's own list, `/.lexirise/ignored.ini`, keyed by the entry key (`lookup::entryKeyOf`), or its form, `00-overview.md` C17 "As built (V5, local)"; suspended: in Lexirise, from the mirror) (2026-09-29, claritise: "Every unsaved word": as written, particles included; V9a) | On |
| A2 | **Page stats** | ~~Status bar: `6 new · 2 learning · 89% known` (running-token coverage, as in C5)~~ (Superseded 2026-09-29, claritise: "Keep the chapter title": nothing in the status bar; the page's numbers are left for C5 / V12) | ~~On~~ — |
| A3 | **Skip to unknown** | With the card open, the side buttons step between A1-marked (unknown or learning) words only, instead of every word (D16); an ignored or suspended word has no mark, so it's skipped. A setting switches back to every word. (2026-09-29, claritise: "Marked words by default"; past the page's last marked word a press stops) | On |
| ~~A4~~ | ~~**Seen-again marker**~~ | ~~A small dot after a word you're *learning* when it reappears. Tells you "you saved this, here it is again"~~ (Dropped 2026-09-29, claritise: "Drop it": the dotted underline already says learning) | ~~On~~ — |
| ~~A5~~ | ~~**Above-level only**~~ | ~~Restrict A1 to words above a target (`target_level=N2` / `HSK-4`), using `system_tags` from the mirror's embedded `dictionary_entry`~~ (Dropped 2026-09-29, claritise: "Drop it", after the measurement: no cheap level for unsaved words, `../reference/lexirise-api-notes.md` "Levels for A5 (V9b)"; "V9b design" below) | ~~Off~~ — |
| A6 | **Adaptive furigana / pinyin** | Readings as ruby **only above words that aren't known** (level < 3). As you learn, the furigana fades on its own (**parked**: claritise, 2026-09-29: "i dont think we need features this deep yet", "i think features are getting to complex", parked in "Maybe later" below) | Off |
| A7 | **Hide publisher ruby over known words** | The opposite of A6, for books that print ruby everywhere: suppress the EPUB's own ruby over known words (**parked**: claritise, 2026-09-29: "i dont think we need features this deep yet", "i think features are getting to complex", parked in "Maybe later" below) | Off |
| A8 | **Page glossary** | New words on the page get superscript numbers, and a strip at the bottom lists `n word reading · meaning`, capped at 5 lines (**parked**: claritise, 2026-09-29: "i dont think we need features this deep yet", "i think features are getting to complex", parked in "Maybe later" below) | Off |

**V9a decisions (2026-09-29).** claritise, on the mockups (`reference/v9a-annotations.html`): new words **"Every
unsaved word"** (A1 as written: every word not saved or at level 0 gets the solid underline, particles included;
tracked and learning dotted; fresh, known, ignored and suspended none); A4 **"Drop it"**; the status bar **"Keep the
chapter title"** (A2 draws nothing); A3 **"Marked words by default"**. The coordinator's, on the rest (the recommended
defaults, 2026-09-29): the radio stays "only if already on" (§1.1, claritise's earlier answer); the solid underline
2 px at baseline + 6 and + 7, dotted 2×2 squares every 4 px, each end pulled in 2 px; a page not analyzed yet stays
plain until the next turn (no extra refresh), the status bar as today; after the page's last marked word a press
stops. **Settings:** ~~each one a separate on/off setting, set per book from the reader menu~~ (superseded the same
day): two global rows in Settings → Lexirise, group "On the page": **Mark words on the page** (On) and **Side
buttons on a card** (Marked words / Every word; Marked words), and one per-book row in the reader menu, **Page marks**
(On / Off; On), which turns A1 and A3 off for that book.

**As built (V9a, 2026-09-29, on `lexi/V9`):**

- **The marks** (`page/PageMarks`, pure): each word-like occurrence of an analyzed page gets `markFor` of its state,
  the lemma's entry first, then the word's own (as the card reads a word's saved state, `lookup::cardFor`); each entry
  as the vocab mirror says by the saved-state rule (`page::MirrorView`, the same rule the card's sentences use:
  `applyMirrorStates` now goes through it), else the page's snapshot; an ignored word (the reader's list, by
  `ignoredKeyFor` of its entry key or lemma) or one suspended in Lexirise gets none. Not saved or level 0: solid; 1-2:
  dotted; 3-4, ignored, suspended, punctuation: none (`config::kMarkNewMaxLevel`, `kMarkLearningMaxLevel`). Placed by
  the card highlight's own span → glyph walk (`card::pieceBoxes`, split out of `readerScene` unchanged: the page text's
  characters from `text::pageTextOf` to their `(line, token)` and codepoints, measured in the token's style), ~~one
  run per piece~~ (R1: each CJK character is a token of its own on the device, so a word's pieces on one line merge
  into one run, from its first character's x to its last character's advance end, the justification's gaps included:
  the signed-off mockup's one segment per word per line; a word across a line end is marked on both lines), drawn as
  `markFills`: 2 px, or 2×2 squares every 4 px, each end pulled in 2 px (`config::kMark*`), ~~at baseline + 6~~ (R1)
  `page::markBelowBaseline` of the page font's CJK em under the baseline (6 px at the default 14 pt, 7 at 18 pt). The
  level rule is one (`page::markForLevel`, `page/MarkRule.h`), shared with A3.
- **When they're drawn** (`page/ReaderMarks`, device glue; its policy pure and host-tested: `page/MarkGate.h`,
  `page/MarkSlots`): on the loop, once per page on screen ~~(not on a redraw of the same page)~~ (struck 2026-09-29, R7: since R5 a
  redraw looks at the page on screen again, below), the page on screen, the
  next and (R1) the previous are looked at in turn (`MarkGate`), each one's text through the section's layout as V7b's
  prefetch reads it, and its analysis file read only when it isn't kept already (`PageStore::read`; offline too: no
  WiFi needed, a page analyzed any time before). ~~into three slots in memory~~ (R2) `MarkSlots` keeps three pages'
  analyses (~~~10 KB each, PSRAM~~ R5: a few hundred bytes each plus its arrays: the occurrences and the pool, past malloc's 4 KB threshold, in PSRAM; the entries and saved states, smaller, in internal RAM, as V7b's R2 says; a device check measures the internal heap with marks on and off) by page and language, and a new one takes the place of the kept page farthest from the
  page on screen that this page hasn't asked for (R6: first, one at the very place of the page being kept with
  another key: an old layout's page, left by a reflow; ~~never that one~~, R2's exclusion, which kept a stale page over
  a useful one), so a turn either way reads one analysis file (the page just left stays
  kept; ~~"no SD read"~~ the section's layout is still read for each page looked at: `Section::loadPage`, under the
  render lock). A page known not analyzed isn't read again until the prefetcher writes it (`reload`); a change of the
  book's Lookup language drops what's kept (~~`languageChanged`~~ R3/R4: `ReaderMarks::open` with the new decision, from the reader menu and its toolbar's More panel; and the reader's own view of the book, `ReaderPages`, is worked out again on the next pass, `page::usableStale` with `BookLanguageStore::revision`). The render
  task draws a page's marks as it draws the page, after `page->render` on the black-and-white pass
  (`EpubReaderActivity::renderContents`), when a page is kept for its very text (key, length and hash): no second
  refresh. (R1) A page drawn before the loop kept it (a book's first page, a jump, and R2: a fast turn; R8: a chapter's first page, never read ahead) is read as it's
  drawn (`PageStore::peek`: one bounded file read, never removing a file, any language), (R2) once per page until a
  kept page is drawn or another page is (so a page left and come back to is read again), so an analyzed page is always
  drawn marked and only a page not analyzed yet stays plain. (R8) A page the loop (or an earlier drawing) already knows
  is not analyzed, for this very text, isn't read as it's drawn (`MarkSlots::knownNotAnalyzed`): offline with nothing
  analyzed, or after a Lookup language change with the pages analyzed in the other language, a turn reads no file on
  the render task (tested; a write of the page, `reload`, drops what's known). The states are resolved as the page is drawn (the mirror
  and the ignore list from memory). (R2) **As a book opens** (`ReaderMarks::open`, from `EpubReaderActivity::loadBook`,
  on the loop before its first page is drawn): whether it shows marks, nothing kept from another book, and the ignore
  list and the vocab mirror for its language (`page::marksLanguages`: its own when it says, else each one switched on)
  loaded, so the first page's marks read them from memory; ~~each page looked at loads its language's mirror first too~~ (R7: loaded as the book shows marks, below).
  A save or level change on a card shows when the card closes and the page is drawn again. Word select's page under the
  card draws them too, (R2) once per card page: the fills are kept and drawn again on the card's later frames (it draws
  the page twice a frame) ~~until the reader draws a page again~~ (R3, `page::CardMarks`) while the page object, the
  vocab mirror's and the ignore list's revisions (`VocabStore::revision`, `IgnoredWordStore::revision`) are the same:
  a save or an ignore on the card shows on its next frame, and each card starts afresh (`dropCardMarks`, as word
  select opens it). (R3) The prefetcher names the page it wrote (`PagePrefetcher::Step::key`), reloaded wherever it's
  kept; a Lookup language change reopens the marks with the new decision (`ReaderMarks::open`), and a page read as drawn
  counts only in one of the book's languages (`page::marksLanguages`); a page read as drawn and found not analyzed is
  known so in any language (`MarkSlots::have`). When pages read as drawn fill every slot, a new one takes the place of
  the kept page farthest from it (`MarkSlots`). (R4) The book's row turned on loads what its marks read at once, on
  the main task (`ReaderMarks::setBookOn`, `loadBookSources`); the card's page under word select can be read as it's
  drawn too (word select is given the page's index). The rules are pure headers: `page/MarkGate.h` (the pages looked
  at, `slotOfKey`), `page/MarkVisibility.h` (`settingsShowMarks`, `bookShowsMarks`, `stepsMarked`, `MarkVisibility`),
  `page/CardMarks.h`, `page/MarkSlots`; a property test drives them through random navigation (fixed seeds) and pins
  that an analyzed page is always drawn marked~~, but for one drawing between the prefetcher rewriting the page on screen
  and the loop's next look at it~~ (R6: that window closed: a reload forgets that the page was read as drawn,
  `MarkSlots::reload`, so its next drawing reads it again). (R5) All of it but the drawing is `page/MarkKeeper` (host-compiled over `PageTexts`
  and a `PageStore`), `ReaderMarks` only the device's sources and the drawing; the property test drives the real keeper
  over a fake book and a store on fake files (forward, back, fast, jump, redraw, a prefetch write, a reflow on the same
  index, a Lookup language change), and a steady turn reads at most one analysis file. (R6) Tests pin that each revision moves where it should
  (`VocabStore` at a load, a page applied and a live answer; `IgnoredWordStore` at the load and each write;
  `BookLanguageStore` at a saved change only) and the page each prefetch step names; turning the book's row off and on
  reads the pages around afresh, and its sources load as a book opens showing marks or its row is turned on, never
  otherwise. (R7) The normal online path is tested end to end: the next page kept as not analyzed, the prefetcher
  writes it and names it, the loop reads it again ahead of the turn (`MarkGate::reload`), and the turn draws it from
  memory, not read as drawn; a steady turn reads nothing as drawn after the book's first page. A page's language
  mirror is no longer loaded on each look (the book's languages are loaded as it opens, its row is turned on, or R7:
  Mark words on the page is turned on with it open, on the loop's next pass, before the render task follows it);
  `MarkSlots::have` no longer moves a kept page's place (the same key and text at another place only changes which
  kept page goes first, once); a new page on screen forgets what the last drawing asked for, so this drawing's pages
  stay when a page left by a jump is as far away (tested). The vocab mirror's and the ignore list's revisions move
  only on a real change (a page that changed the mirror; a list written), so a page given up, failed or with nothing
  new, and an unchanged or failed ignore, don't make the card work its page's marks out again, (R8) and a live answer moves the mirror's revision only when it
  changed the entry (its state or the time it was known) or waits for its language's load: the card records one after
  every sentence. (R8) The keeper tells the kept pages which page is on screen (`MarkSlots::onScreen`): a keeper test
  after jumps pins that the next turn draws from memory. **A reflow on the same index**
  (Rotate screen from the reader menu): the gate looks at the page on screen again on each new drawing of the same
  page (one text read, no file read when kept) and, when its text changed, at the pages around it too
  (`MarkGate::rearm`); a page is read as drawn once per page *and text* (`MarkSlots::peekDue`). The reader menu's
  **Page marks** row (and its toolbar's More panel's) shows only while the book can show marks (Lexirise usable and
  Mark words on the page on), and the web page's On the page card goes when none of its rows shows.
- **Which books:** Lexirise usable for the book, Settings → Lexirise → On the page → **Mark words on the page**
  (`mark_words`), and the book's **Page marks** row (the reader menu's list and its toolbar's More panel,
  `settings/BookMarks`, `/.lexirise/marks-off.ini`); the row's change applies to the page drawn right after the menu
  (R3: whichever task runs first: the render task reads the settings' flag and the row's itself, and a page turned on
  again is read as it's drawn; the loop's own view, `MarkVisibility`, only decides when what's kept goes),
  and a book with it Off reads no analyses. "Side buttons on a card" shows only while "Mark words on the page" is on
  (R1), on the device and the web page (a Marked words / Every word choice on both). `BookMarks` stays apart from
  `BookLanguages` (R1 nit 6: membership against a value per book; a shared store would be a template, not simpler).
- **A3** (`CardController::nextMarked`): with the card open on an analyzed page (`SentenceSource::analyzed`), the book
  showing marks and **Side buttons on a card** on **Marked words** (`step_marked`, `page::stepsMarked`), a side button
  goes to the next (or previous) word marked at its level on the card now (a level set on this card counts:
  `CardController::marked`, `markFor`'s rule), never an ignored or suspended one (`LiveSource::neverMarked`, the
  mirror by the page's saved-state rule, R1: `page::MirrorView`); past the sentence's last, on into the page's next
  sentence's first marked word, over sentences with none; past the page's last marked word a press stops. Back: to the
  previous marked word, never before the tapped sentence. On a page not analyzed, or with "Every word", every word as
  before. **Its cost:** a sentence of the page whose slice has no word (a token across its cut edge, V7b R7) asks ① as
  a card always did, so stepping over many such sentences can cost ① calls, bounded by the page's end.
- **The bench** (`test/lexirise_page/PageMarksTest.cpp`): a static page (the signed-off mockup's first paragraph) and a
  recorded, synthetic analysis (`bench/v9a-ja-analysis.json`, made up), through the same walk into the marks'
  rectangles, pinned in `bench/v9a-ja-marks.golden`; then each rule. The card's goldens are unchanged. (R1) The golden
  is checked against the signed-off mockup's own rule (`tools/mockups/check_v9a_golden.py`: its segments per word per
  line over the bench's geometry, equal fill for fill). `lxctl.py marks-smoke` (read-only, dev builds) turns forward
  and back over analyzed pages and checks each is marked again on the way back, with the draw and read times and the
  heap. (R6) Its turns, `marks-smoke fast`'s and page-smoke's are spaced by the press's hold (`lxctl.press`,
  `BUTTON_HOLD_S`): the harness refuses a press while the last is held, so back to back they couldn't run on a device.
- **Known limits:** on a page with images under the UC8279's absolute grayscale mode, the gray pass redraws the page
  and may not keep the marks (a device check); the marks' measuring builds the page model and its token boxes on the
  render task (small, freed after the page is drawn; the device check logs its time and the heap, `[LXPAGE] marks: <n>
  words, <n> fills in <ms> ms; heap <free> free`, dev builds), and a page drawn before the loop kept it adds one file
  read to its drawing (`read as drawn`, timed by the device check), (R8) always so for a chapter's first page: the
  loop reads ahead only within the section on screen; the mirror's first load (once per boot, per
  language) happens ~~on the loop when a book's first page is kept~~ (R3) as the book opens (`ReaderMarks::open`, before
  its first page is drawn: it delays that page; timed: `the <ja|zh> mirror loaded in <ms> ms`); ~~a book's Page marks
  turned on shows from the next page drawn with its analysis read~~ (R3: the page drawn right after the menu follows
  the row, "Which books"); the underline's place is from
  the font's em, not its line spacing: at Tight spacing and 18 pt a device check looks for it touching the next line;
  (R2) a page reached faster than the loop reads the pages around (auto page turn, a held button, a turn past the next
  page before the loop's pass) is read as it's drawn, on the render task with the render lock held: one analysis file
  (up to `config::kPageFileMaxBytes`, 13-15 KB measured) and its parse added to that page's drawing (`lxctl.py
  marks-smoke fast` measures it); (R5) the book's languages (`marksLanguages`) are worked out as it opens: were Mark
  words on the page turned on while a book is open (not reachable now: Settings can't be opened from the reader), its
  sources would load ~~only as its pages are looked at~~ (R7) on the loop's next pass, as the keeper sees the setting;
  a language switched on while a book is open (not reachable either) counts from the book's next open.

**V9b design (proposed 2026-09-29, on `lexi/V9`; ~~awaiting claritise's sign-off before any firmware~~ parked, not signed off, 2026-09-29: A5 dropped, A10 and A11 parked, "Maybe later" below; kept as the record).** Mockups:
`reference/v9b-annotations.html` (drawn by `../../tools/mockups/v9b_annotations.py`). §2's rows change only once
claritise has signed off.

- **A5, measured first** (`../reference/lexirise-api-notes.md` "Levels for A5 (V9b), measured"): the page analysis
  carries no level, only `dictionary/lookup` does, one word per call, and a lookup per new word would cost several
  hundred calls an hour beside the page analysis, against the key's 1200; the mirror could keep a saved word's level for
  free (`dictionary_entry_system_tags`, a top-level field of each item), but A5 is about unsaved words; many words have
  no level (35% of the Japanese probe page's, particles and compounds; 22% of the Chinese page's); the V7c lemma cache
  holds a level only for words already looked up on a card (by lemma text). The rank the page analysis carries is a
  poor stand-in for JLPT (74% agreement with "above N3") and a fair one for HSK (91-92%). **Recommended: drop A5.** The
  alternative that's free and works offline, a frequency cut ("New words to mark: Every one / Less common only",
  unsaved words among the language's ~3,000 most common unmarked), is a different feature and undoes V9a's "Every
  unsaved word"; it's in the mockups for comparison only.
- **A10 and A11 share one list: "Looked-up words".** A reader-menu row after Page marks (shown while Lexirise is usable
  for the book; its value "N waiting" when words wait to be looked up) opens a list of the book's looked-up words, the
  chapter on screen first, then the others, newest first; a chapter is a spine section, headed by its TOC title. Listed:
  every word the reader **tapped** to open a card (not the words stepped to with the side buttons), once per chapter by
  its lemma entry, and every **waiting** word (below); an ignored word isn't listed. A row: the word (its dictionary
  form), reading, first meaning, and its state now (the vocab mirror by the saved-state rule, as the marks read it:
  `page::MirrorView`); a waiting word says "Not looked up yet (you were offline)". Rows are read-only in V9b (opening a
  card from a row is left for later). Nothing pops up between chapters (A10's "end-of-chapter" is the list, on demand).
- **Save all:** a chapter's heading carries "Save N" (its unsaved words, waiting ones included; none: no button). A
  confirm dialog first ("Save N words as tracked?", the chapter as its caption, Cancel selected); then each word is saved
  as a card save is (D9: the lemma, the first meaning, the sentence as the note, the configured tags and the book's tag,
  `mode: word`) at **tracked** (1), one after another behind the home screen's popup and progress bar ("Saving
  words..."), a waiting word looked up first. The reader's own request, so it **may join WiFi** (as Sync Vocabulary,
  `LexiriseService::joinForUser`); any button stops it between words. Each save goes into the mirror as the card's do and
  marks the book's deck wanted (V3: made or found on the next idle card). No Undo for the batch (two calls a word); a
  word's level can be changed on its card. Results: "Saved N words" / "Saved 1 word", "Saved N of M · Save failed",
  "Save failed · No Wi-Fi", "Save stopped", and the card's "Lexirise key rejected" and "Lexirise: rate limited". A
  cap per press (`config::kSaveAllMax`) keeps one press inside the hour's budget.
- **A11, look up later, with no gesture:** "long-press flags the word" can't coexist with the long-press that opens the
  card (`../v0.1/popup-ui.md` §3.2), and offline the long-press already runs a lookup. So **a lookup Lexirise couldn't
  answer is kept as waiting**: the card left at phase A without its meaning (offline, a timeout, a 429, a 5xx:
  `../v0.1/offline-and-errors.md` §1's "Card stays at phase A"), or word select falling back to StarDict for any of those reasons
  (`lookup::Fallback`, the notice kinds that mean "couldn't reach"; not "No Lexirise key", "Not in dictionary" or Lexirise
  off). Kept with its sentence (≤ `kMaxSentenceUnits`, as a save's note) and the tap's offset. Nothing new is drawn at the
  time. **When they're looked up:** only while WiFi is already on (claritise's V7b decision), from the reader's loop after
  the page prefetch's step and under its conditions (the dwell, nothing rendering, input gives it up, `kPageBudgetPercent`
  of the key's hour, no 429 or rejected key), one word per pass, at most `config::kLaterPerHour`; or at once from the
  list's top row, "Look up N waiting words", the reader's own request, which may join WiFi (popup "Looking up words...";
  "Looked up N words", "Lookup failed · No Wi-Fi", "Lookup stopped"). A waiting word costs its sentence's `analyze/text`
  (none when its page is analyzed: `page::sliceSentence`) and one `dictionary/lookup`; the answer fills the record and the
  lemma cache. A word that no longer resolves (no word at the offset) is dropped.
- **The ⋯ tab's "Look up later" row:** with the above it has nothing left to do (a word whose card shows is already
  listed), so the proposal **removes it** (a change to the approved card, asked: the card's goldens
  `ja-expanded-actions*.json` / `zh-expanded-actions.json` change only for that row).
- **What's kept:** `/.lexirise/looked-up/<book>.bin` (`<book>` the FNV-1a 32 of the book's path, as the page files),
  binary through `SafeFile` (it's the reader's own data, not regenerable): a header, a CRC, and records (spine, flags
  tapped / waiting, lemma entry, time, tap offset, then the lemma, reading and first meaning, each capped, and the
  sentence). At most `config::kLookedUpPerBook` records a book (the oldest looked-up first out, never a waiting one
  before a looked-up one) and `kLookedUpBooks` books (`/.lexirise/looked-up/index.bin`, oldest book first out). Written
  with the card's other files (`CardSession::shouldFlushFiles`, and as it closes), and by word select as it finishes after
  a fallback. Read when the list opens (no resident memory while reading).
- **Hooks:** the reader menu (`EpubReaderMenuActivity::MenuAction::LOOKED_UP_WORDS`, and the toolbar's More panel as
  Page marks has it); a new list activity (`UiListActivity`, the list above); the card's tapped word and phase B's
  outcome (`LiveSource`, `CardController`); word select's `fallBack()`; the reader's loop (`page/ReaderPages`, after the
  prefetch step). Pure parts host-tested: the store (round trip, CRC, caps, eviction order, dedupe per chapter), the
  list model (grouping, order, states from a `MirrorView`, ignored left out, the Save N count), Save all's run and the
  look-up run (as `vocab::HomeSyncFlow`: fake API; saves, a failure mid-run, cancel, a waiting word looked up then saved,
  a 429, a rejected key, the cap), the background policy (WiFi up only, budget, input, the hourly cap). A bench first: a
  made-up book's file and a recorded, synthetic lookup set, the list model's rows pinned.
- **Device checks owed with the build:** the list opening with a full book file (time, heap); Save all's time per word
  (a save closes the session first: a handshake each, resumed since V7c); a waiting word written after an offline
  StarDict lookup and after an offline card, and looked up once WiFi is on; a page turn during a background lookup.

Chapter-level extras (same data, no inline drawing):

| # | Feature | Notes |
|---|---|---|
| A9 | **Chapter primer** | On opening a chapter, the 10 most frequent unknown words in it, with meanings. `⏎` saves all of them. Requires analyzing the chapter (sampled, or all of it if the text limit allows) (**parked**: claritise, 2026-09-29: "i dont think we need features this deep yet", "i think features are getting to complex", parked in "Maybe later" below) |
| A10 | **End-of-chapter recap** | Words looked up in this chapter, with `Save all` for the unsaved ones (**parked**: claritise, 2026-09-29: "i dont think we need features this deep yet", "i think features are getting to complex", parked in "Maybe later" below; its V9b proposal, not signed off: "V9b design" above) |
| A11 | **Look up later** | Offline: long-press flags the word (stored with its sentence). On the next WiFi-up, the flags are looked up, and a "Flagged words" list appears in the reader menu (**parked**: claritise, 2026-09-29: "i dont think we need features this deep yet", "i think features are getting to complex", parked in "Maybe later" below; the card keeps its "Look up later" row unchanged; the V9b proposal, not signed off: "V9b design" above) |

### Maybe later (parked 2026-09-29)

claritise, 2026-09-29, on V9b's mockups: A5 "Drop it"; on Save all, "i dont think we need features this deep yet, lets
add it tot a maybe list of features"; on look up later, "where is this list? i think features are getting to
complex.........."; then all of V9b-V9d parked. Page annotations end with V9a (A1, A3). Not planned; each comes back
only if claritise asks, with a fresh design and sign-off. This is the list's one home (`00-overview.md` C6 and
`01-build-order.md` V9 point here).

- **A10, the chapter recap** (words looked up, per chapter): designed as the "Looked-up words" list, "V9b design" above,
  `reference/v9b-annotations.html` (parked, not signed off).
- **Save all** (A10's): the same design and mockups (parked, not signed off).
- **A11, look up later:** the same design and mockups (parked, not signed off); the card's ⋯ "Look up later" row stays as
  built ("Not in this version yet").
- **A6, adaptive furigana / pinyin:** §2's row and §3 (ruby space on every line); no design.
- **A8, the page glossary:** §2's row and §3 (a strip at the bottom); no design.
- **A9, the chapter primer:** the table above; no design (a chapter's analysis is its own budget question).
- **A7, hiding the publisher's ruby over known words:** §2's row and §3 (the parse-time hook); no design.

## 3. Drawing (the renderer constraints)

- **Overlay, don't reflow.** A1, A4 and A8's superscripts are drawn **after** the page renders, from
  our code, using word boxes (~~the same `WordBox` geometry as word select~~ V9a: `card::pieceBoxes`, the card
  highlight's own span → glyph walk, one run per word per line: "As built (V9a)"). Nothing inside
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
  (grey needs grayscale refresh, which is slow and ghosts). Underline sits ~~**below the descender
  line**~~ (V9a: just under the CJK ink, `page::markBelowBaseline` of the font's em, 6 px at 14 pt: the descender line
  would put it on the next line's top), so it doesn't touch ruby from the line below.
- **Refresh:** annotations arrive with the page (prefetch) and cost nothing extra. ~~The late-arrival
  case is one partial refresh of the text area. Count it in the reader's ghost-cleanup cadence.~~ (Superseded
  2026-09-29, V9a decisions: no extra refresh; a page not analyzed yet stays plain until the next turn.)

## 4. Dependencies and blockers

| Needs | For | Status |
|---|---|---|
| Page analysis (§1.1) | Everything | ~~Not built. v0.1 analyzes per sentence; V7b designed (§1.1 "V7b design", 2026-09-28)~~ V7b built on the host (2026-09-28; device checks owed) |
| Vocab mirror (§1.2) | Offline marks, A4, A5, immediate updates after a save | ~~Not built~~ ~~V7a (in progress, 2026-09-28)~~ V7a done on the host (landed 2026-09-28; device check owed) |
| **Kana readings** | A6 for Japanese, A8 readings | ~~**Solved:** converted on the device~~ **Superseded 2026-09-29:** solved, kana either way: Lexirise's kana is kept, its romaji converted on the device (`../v0.1/languages.md` §3a). Chinese pinyin works as is |
| `analyze/text` maximum text length | Page analysis in one request, and A9 | **No limit hit up to 20k chars** (tested). ~~~70 bytes of response per character, so ~20 KB per page~~ (Superseded 2026-09-28: ~200–310 B per UTF-16 unit, 76–93 KB a page, `../reference/lexirise-api-notes.md` "Page analysis (V7b), measured") |
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

**V9 (2026-09-29):** steps 3–9 are built as V9a–V9d, in this order (`01-build-order.md` V9). ~~V9a's design (A1, A2, A3,
A4), with the changes to §2 it proposes (what counts as new, A4's meaning, where the settings live), awaits claritise's
sign-off: `reference/v9a-annotations.html`. §2 changes only once they've signed off.~~ (Signed off 2026-09-29: §2
"V9a decisions"; V9a is A1 and A3.) ~~V9b's design (A5 measured, A10 and A11 as one list) awaits claritise's sign-off: §2 "V9b
design", `reference/v9b-annotations.html`.~~ (Superseded 2026-09-29: A5 dropped, steps 6-9 (V9b-V9d) parked by
claritise, §2 "Maybe later"; V9 is done with V9a.)
