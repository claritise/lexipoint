# Lexirise client: HTTPS, endpoints, parsing, config

**Status:** proposed 2026-09-24. Decisions D6, D7, D8, D9, D11 in `00-overview.md`.

Related: `lookup-flow.md` §5 (call sequence), `offline-and-errors.md` (status handling),
<https://lexirise.app/api-reference> (the server's contract; `context-brief.md` has the examples).

---

## 0. Budget (X4 Pro)

| Resource | What we have | What a lookup needs |
|---|---|---|
| Internal DRAM | Shared with the reader, fragmented over a session | The TLS session (~40KB peak including the ~17KB record buffer). `HttpDownloader::MIN_TLS_FREE_HEAP = 40000` / `MIN_TLS_MAX_ALLOC = 20000` is the existing pre-flight, so reuse it |
| PSRAM | 8MB | Response structs, the occurrence array, the card's strings. **Put our allocations here** (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` or the `lib/Memory` helpers) |
| Rate limit | 1200 req/h per key | 2 per lookup + 1 per save, so ~500 lookups/h. Not a concern for reading. It would become one for per-page tinting (v0.2) |

The pre-flight still matters on the S3: mbedTLS / wolfSSL allocate from internal RAM, and a long
reading session fragments it. If the pre-flight fails, return `Unavailable`, which falls back to
StarDict. Don't crash.

## 1. Transport

**Revised in P1** (D7). The original plan here (`esp_http_client` with the CA bundle, like
`HttpDownloader`) doesn't exist in this build: the X4 Pro envs use wolfSSL (`FREEINK_NET_WOLFSSL`), which
compiles the `esp_http_client` path out, and the SDK's `SecureClient` has no certificate verification at all
(every CrossPoint caller uses `setInsecure()`, and it never checks the hostname). So:

- **`net/TlsConnection`**: a small wolfSSL client of our own, over `WiFiClient`.
  - **Trust:** only ISRG Root X1 and X2 (`net/TrustAnchors.h`, fingerprints in the header).
    `WOLFSSL_VERIFY_PEER`, `wolfSSL_check_domain_name`, SNI. **Never `setInsecure()`.**
  - **Crypto:** api.lexirise.app's chain (LE YE2 → ISRG Root YE → ISRG Root X2, with X2 cross-signed
    by X1) is ECDSA P-384 / SHA-384 end to end, and this wolfSSL build had neither. The `[lexirise]`
    section of `platformio.ini` adds `WOLFSSL_SHA384`, `HAVE_ECC384`, `WOLFSSL_SP_384` (SP math, heap-light)
    for the X4 Pro envs. Cost: ~30KB flash.
  - **Heap:** the SDK's measures are copied: an X25519 key share (P-256 keygen OOMs at reading-session heap)
    and a 2KB max fragment (no ~17KB receive buffer). Pre-flight: `HttpDownloader::MIN_TLS_FREE_HEAP` /
    `MIN_TLS_MAX_ALLOC`; below them the call fails `LowMemory` before any allocation.
  - **Clock:** certificate dates need a real clock, and the X4 Pro boots at 1970. If `time()` is before
    2026 the connection first waits (≤5 s) for SNTP (`pool.ntp.org`, `time.nist.gov`); otherwise the
    call fails `ClockNotSet`. The RTC (HalClock) is not touched.
  - Every wait feeds the task watchdog (`net/Wait.h`): calls run on the main loop task.
- **`api/LexiriseClient`**: one request at a time over a `net::Connection`, **keep-alive reused**
  while the server allows it. A reused session that fails before any response byte (the server
  dropped it while idle) is reopened and the request sent once more, **only if it's idempotent** (GET,
  PUT, DELETE, or a POST marked so: analyze/text and lookup are; a vocabulary save isn't, since the
  server may have acted on it before dropping the connection). Nothing else is retried.
  Timeouts: 6 s for connect, handshake and each read, and **15 s for the whole request** once the
  connection is open (the stale-session retry shares it), so a trickling server can't hold the main
  loop. One call is bounded by WiFi join 6 s + NTP 5 s + TCP/handshake 12 s (V7c: the resumption fallback included,
  §1 "Session resumption") + 15 s (P11: the join is 3 s direct +
  8 s scan within 11 s, plus 2 s radio slack: `config::kMaxCallMs` = 45 s, `offline-and-errors.md` §5).
- **Idle close:** the TLS session is closed 30 s after the last call (and on WiFi teardown, and on
  leaving reading), so it never sits on internal heap (V7c: but for the small resumption session kept while reading,
  below, dropped when WiFi is given back).
- **Session resumption (v0.2 V7c, C21):** `net::TlsConnection` keeps the last session with the host after a connection
  closes (`net::SessionKeeper`, `net/TlsSession.h`: one `WOLFSSL_SESSION` holding the server's 32-byte ticket, in
  internal RAM, never on SD) and offers it to the next handshake (`wolfSSL_set_session`), which then skips the
  certificate chain and its verification (the chain, the host name and the pinned roots were verified when it was made;
  no early data). A server that doesn't take it answers with a full handshake; one wolfSSL refuses to offer (past its ticket's
  lifetime) is dropped, the handshake full; a handshake that fails having offered
  one drops it and is tried once more in full (`net::openResuming`), inside the same open's budget (a TCP connect and a
  handshake, `2 * kHttpTimeoutMs`, which `kMaxCallMs` counts: each step stops by the open's deadline, and the full retry
  runs only with `config::kTlsFallbackMinMs` (4 s, R9: a full handshake with its TCP connect took 2.5 s) of it left, else the call fails as it would have; V7c R8), unless it rejected the server's certificate (a
  resumed handshake verifies none: the server did a full one, and another would be rejected the same way; V7c R7); a
  timeout drops it too, and so does a handshake given up for the reader's input once the session was offered (~~it may
  hold that handshake's new secret already~~ corrected 2026-09-28, V7c R9: conservative, the handshake worked on its own
  copy, `HaveUniqueSessionObj` duplicating a session the keeper holds, so the kept one wasn't modified; V7c R7). The keeper owns the session while it's offered: a handshake tears
  down without keeping anything (`TlsConnection::teardown`), only `close()` of a connected session keeps one. Kept only when the session
  carries a ticket read on that connection (V7c R5, the mechanism corrected in R9: a resumed connection works on its own copy of the
  session, `SetupPskKey` → `HaveUniqueSessionObj`; its client Finished writes a new resumption secret into the copy while
  the ticket changes only with a NewSessionTicket, `SetTicket`, so a copy closed before one holds the old ticket with the
  new secret, which can't resume: nothing is kept, the next open is full); replaced after every connection (a resumed one gets new tickets); dropped for another host or port; ~~wolfSSL
  refuses one past its ticket's lifetime~~ (struck 2026-09-28, V7c R7: said above). A restart loses it, and giving WiFi back drops it (V7c R2: leaving reading, a
  home sync's end; `Connection::forgetSession`). `[LXT] Verified <host> (<version>, <cipher>, resumed|full)
  in <ms> ms` says which. Needs `HAVE_SESSION_TICKET` (the `[lexirise]` section; its cost: `../v0.2/00-overview.md` C21
  "As built (V7c)").
- **The web page never blocks on the network:** a key check is queued (`requestKeyCheck`) and run by
  `LexiriseService::tick()` from the main loop, off `WebServer::handleClient`'s stack; the page polls.
- **wolfSSL scope:** the SHA-384/P-384 flags (and V7c's `HAVE_SESSION_TICKET`) apply to every wolfSSL user in the X4 Pro builds. OTA~~,
  OPDS, KOSync and font downloads~~ now also offer~~s~~ those suites (superseded 2026-09-29, v0.2 V8: KOReader sync, OPDS and the font download were removed; OTA is the only other wolfSSL user, `../v0.2/slimming.md` §8); P1's on-device list re-tests them. **V7c:** with
  `HAVE_SESSION_TICKET` every one of them parses a TLS 1.3 server's NewSessionTickets after its handshake (none resumes:
  `SecureClient` never offers a session). Two ways that could fail a connection, found in wolfSSL 5.7.2's `tls13.c`:
  a ticket nonce longer than 8 bytes (rustls sends 32) failed the read with "Nonce length not supported", so
  `WOLFSSL_TICKET_NONCE_MALLOC` is set too (the nonce then goes on the heap, freed with its session or when the next
  ticket replaces it); and a ticket lifetime over 7 days (604,800 s, TLS 1.3's cap) fails with `SERVER_HINT_ERROR`,
  which no compliant server sends. Also new with the flag (R8): a ticket too large for the heap left fails with
  `MEMORY_E`, and one read while the millisecond clock reads 0 with `GETTIME_ERROR`; and every connection's heap
  `WOLFSSL_SESSION` is 192 bytes larger (the ticket buffer), OTA~~, KOSync, OPDS and fonts~~ included (superseded
  2026-09-29, v0.2 V8: those were removed). Device checks:
  `device-checks.md` "v0.2 V7c".
- **Clock source:** NTP only for now. Seeding the system clock from the RTC (so a network that blocks
  NTP still works, and the first call after boot skips the wait) needs a HalClock date accessor, a
  change to base code; tracked for P8 (it only matters on NTP-blocking networks).
- **`net/Http`**: the request serialiser and a bounded incremental response parser
  (Content-Length, chunked or close-delimited; 8KB of headers, 1KB lines, 64KB body).
- Headers: `Authorization: Bearer <key>`, `Accept: application/json`, `Content-Type: application/json`
  with `Content-Length` on bodies, `User-Agent: Lexipoint/<ver> CrossPoint/<ver>`.
- **Request bodies** are built by `net/JsonWriter`: escaping, and invalid UTF-8 replaced, so the body is
  always valid JSON.
- **The key is never logged.** Log lines carry the method, path, status and error name only.
- **A call can be given up for the reader's input** (v0.2 V7b: a page's analysis, the vocab mirror's pages and the
  card's probe; `LexiriseClient::send(request, sink, abort)`, `net::Connection::setAbort`): the abort is asked before
  the request is written (true: nothing is sent), between reads, in the wait for the clock (SNTP) before the
  handshake (`ensureClock`), and in the TLS connection's handshake, write and read waits (not inside the TCP connect, which blocks in the SDK); once it's true the call ends `Cancelled` (`sent`
  says whether the request went out), the connection is closed (the rest of an answer is never read into the next
  one), a stale session isn't retried, and the abort is forgotten when the call returns.

## 2. Endpoints

### `POST /v1/analyze/text`

Request: `{"text": <sentence>, "language": <lang>}`. The reference also documents **`fast: true`** ("fast batch analysis"). v0.1 sends the default (full) analysis, because it needs correct lemmas. **Tested 2026-09-24: `fast` drops lemmas and saves only ~0.3 s, so v0.1 always uses the default.**

Fields we keep (all others are skipped while streaming):

| Path | Keep |
|---|---|
| `occurrences[].{entryId, lemmaEntryId, word, lemma, transliteration, charStart, charEnd, isWordLike}` | All occurrences, compact (§4). **`lemma` is only present when it differs from `word`**, so fall back to `word` |
| `entryMetaById[id].{transliteration, partOfSpeech[0..1], status, rank, frequencyScore}` | All IDs in `occurrences` (lookup-flow §6) |
| `stateByEntryId[id].{saved_expression_id, proficiency, seen_count}` | Same |
| ~~`stateByEntryId[id].{notes, user_tags[].name}`~~ | ~~(v0.2 V4, C14) The sentence the word was saved with, cut to `config::kMaxSavedNoteBytes` at a character (a long note never fails the answer), and the tags' names (objects `{id, name}` or plain strings; the first `kMaxSavedTags`; one over `kMaxTokenBytes` is skipped, never failing the answer): "Met before" and its book~~ (Superseded 2026-09-27, v0.2 V4b: `analyze/text` never carries them, `../reference/lexirise-api-notes.md` "A saved word's notes and tags in `analyze/text`"; skipped, and read from the saved item: `GET /v1/vocabulary/{id}` below) |
| `morphoPending` | Flag. If true, the word boundary may be rough. Show it anyway. *(2026-09-25: the reference now says a `true` answer has "fast tokens only"; a later call gives grammar and refined segmentation. v0.1 keeps using the first answer; calling again is v0.2 C19.)* |
| `grammar`, `grammarStates`, `images`, `updated_at` | Skip |

### `POST /v1/dictionary/lookup`

Request: `{"text": <lemma>, "language": <lang>}`. Also keep **`system_tags`** (JLPT/HSK level, see `../reference/lexirise-api-notes.md`) and, for the detail view, `breakdown[]` and `characterInfo`. Keep: `translations[0..1].{translation,
part_of_speech[0]}`, `translations[0].examples[0]` (**skip in v0.1**, since the card has no room),
`rank`, `frequency_score`, `translation_status`, `status`. Target language: see H4.

### `POST /v1/vocabulary`

Request (D9):

```json
{
  "language": "ja",
  "text": "<lemma>",
  "mode": "word",
  "translation": "<first translation>",
  "proficiency": 1,
  "tags": ["xteink"],
  "notes": "<sentence>"
}
```

Keep only the HTTP status, plus the created ID if one is returned (so a v0.2 card can link to it).
`notes` is a documented field (H6 resolved). The endpoint is an **upsert** that returns `{ result, item }`: keep `item`'s ID so the card can offer level changes later (v0.2 C9). **Never re-POST a word the card already shows as saved.** Tested 2026-09-24: a re-POST **replaces** `tags`, `notes`, the translation and `proficiency` (`result.status: "updated"`, `reason: "already_exists"`), so it would wipe tags and notes the user added in the app. Level changes go through `PATCH` only.

### `GET /v1/vocabulary/{id}`

(v0.2 V4b, C14) A saved word's item, read-only (`api::savedItemRequest`; the id is the state's `saved_expression_id`,
refused unless `isPlainId`; logged as `/v1/vocabulary/{id}`). Resent once on a stale keep-alive session like any GET.
When the card asks for it: `../v0.2/00-overview.md` C14 "As built (V4b)".

| Path (at the top, or under `item` when there is one) | Keep |
|---|---|
| `notes` | The sentence Lexipoint's save wrote, cut to `config::kMaxSavedNoteBytes` at a character (never fails the answer) |
| `sentence_text` (or `sentenceText`) | The sentence a word saved in the Lexirise app came with, cut the same way: "Met before" when `notes` is empty |
| `user_tags[]` (or `userTags[]`) | The tags' names (objects `{id, name}` or plain strings; the first `kMaxSavedTags`; an empty one or one over `kMaxTokenBytes` skipped): the book's title from its `book:<slug>` |
| `suspended` | ~~(v0.2 V5, C17) Whether the word is ignored: only a JSON `true` counts (null, a string, anything else: not ignored). `analyze/text` never shows it~~ (Superseded 2026-09-27: the card's Ignore is the reader's own list, `../v0.2/00-overview.md` C17 "As built (V5, local)".) Not read (suspended in the Lexirise app; `analyze/text` never shows it either): skipped like any other field |
| everything else | Skip. Null or missing fields are empty; anything but a JSON object is malformed (`api::parseSavedItem`) |

### `GET /v1/vocabulary` (the list)

(v0.2 V7a, C13) A page of the user's saved items in one language, for the vocab mirror
(`../v0.2/page-annotations.md` §1.2 "As built (V7a)"): `api::vocabularyPageRequest`,
`?language=<ja|zh>&limit=<n>&offset=<n>&sortId=updated_at&sortDesc=true`, read-only; `limit` is
`config::kVocabPageItems`, or `kVocabProbeItems` for an incremental pass's first page (`PagePlan::limit`). Its body
(~6.8 KB an item, measured) is **streamed**, never held: `LexiriseApi::vocabularyPage` hands the decoded body to a
`net::BodySink` (up to the sink's `maxBytes()`, `config::kVocabPageMaxBytes` for a page), `api::VocabPageReader` reads
it with `net/JsonStream`, and gives the page up when its cancel says the reader has input. It goes only over WiFi
already up (`LexiriseService::send`'s `mayJoin` false: NoWifi otherwise, never a join, no WiFi touch, and the TLS
idle close still counts from the reader's own last call).

| Path | Keep |
|---|---|
| `items[].id` | The saved expression's id (a number, or a string of digits; else the item isn't kept) |
| `items[].dictionary_id` | The entry a save targets (`stateByEntryId`'s key) |
| `items[].proficiency` | 0–4 (else not kept) |
| `items[].suspended` | Only a JSON `true` |
| `items[].next_review_at` | ISO 8601 → seconds; null or unreadable: none |
| `items[].updated_at` | ISO 8601 → ms: the sync's order and cursor (unreadable: 0, never stops a pass) |
| `items[].unit_type` | `word`, or any value that isn't a string (null: not said), is kept; any other string (`sentence`: a sentence card) isn't |
| `nextOffset` | null or missing: the last page |
| `languageCount` | The list's size (sentence cards too, as the list holds them): a drop between pages is deletions, and the mirror reads again from as far back. `totalCount` (words only) is read but not used for it |
| everything else | Walked, not kept: the item's nested `dictionary_entry` (whose own `id` and look-alike fields are never taken), translations, notes, tags, media. Past `config::kVocabListLimitMax` items, or not an object with an `items` array: malformed |

### `PATCH /v1/vocabulary/{id}`

A saved word's level, `{"proficiency": 0-4}` (`api::setProficiencyRequest`: a level change and its Undo) ~~and
(v0.2 V5, C17) whether it's ignored, `{"suspended": true}` or `false` (`api::setSuspendedRequest`; measured:
`../reference/lexirise-api-notes.md`, "Suspended (Ignore)": a POST can't set it, `DELETE` keeps it)~~ (Superseded
2026-09-27: Ignore never writes to Lexirise, `../v0.2/00-overview.md` C17 "As built (V5, local)"; the measured
`suspended` facts stay in the reference notes). Idempotent, so resent once on a stale session. The removal's clear
(`api::clearRequest`) is one too.

## 3. What the server does for us

- **Tokenization and lemmatization** of Japanese (D4). This is why no MeCab or deinflection runs
  on-device.
- **Normalization** (full-width, kana variants). We send text as it appears on the page.
- **Proficiency state** per entry. ~~The device holds no vocabulary data.~~ (Superseded 2026-09-28, v0.2 V7a: it keeps
  a read-only copy, the vocab mirror, `../v0.2/page-annotations.md` §1.2; Lexirise stays the source.)

## 4. Parsing

**Revised in P1.** Bodies are buffered (the 64KB cap makes that safe, and the buffer lands in PSRAM; the vocabulary
list is the exception, streamed: `GET /v1/vocabulary` above),
then read by **`net/JsonReader`**, a strict path-reporting reader: `occurrences[3].word` arrives as a path
plus a value. `lib/JsonParser/StreamingJsonParser` wasn't a fit: it silently drops tokens over 512 bytes
(a dropped key hands its value to the previous key), doesn't decode `\u` escapes, and accepts malformed or
truncated JSON. Each endpoint has a visitor in `api/Responses` that builds a small struct:

```cpp
struct Occurrence { std::string word, lemma, reading; uint32_t entryId, lemmaEntryId, charStart, charEnd; bool wordLike; };
struct AnalyzeResult { std::vector<Occurrence> occurrences; bool morphoPending; };
struct MeInfo { std::string name, plan; uint32_t rateLimitMax, rateLimitWindowMs; };  // never the email
```

P2/P3 add `entryMetaById` / `stateByEntryId` / lookup to the same visitor pattern. The per-response
arena from the original plan is deferred until measurements show the `std::string`s fragmenting
anything (they're few, small and short-lived).

**Key order.** Visitors keep everything they need whatever the key order (tested). **Key order is
never a correctness dependency.**

Hard limits (so a hostile or broken response can't exhaust memory): 128 occurrences, 256 bytes per
word/lemma/reading, a 64KB body, and 32 levels of nesting. Anything over a limit is `OverLimit`/`Malformed`,
and the provider returns `Unavailable`. The exception is `entryMetaById` / `stateByEntryId`: they hold
more entries than there are occurrences (surface, lemma and breakdown entries), so past
`kMaxEntries` (512) further entries are **dropped** rather than failing the whole analyze.
`dictionary/lookup` keeps the first `kMaxTranslations` (2) senses that have a translation (an empty one
isn't counted), each cut at
`kMaxTranslationBytes` (512) on a character boundary.

## 4a. Undocumented fields degrade quietly

`breakdown`, `characterInfo`, `multipleReadings`, `tones`, `morphemes` and the `labels` values are **not
in the API reference** (`../reference/lexirise-api-notes.md`). The parser treats every one as
optional, and the card never breaks without them. If a field goes missing, the dependent UI just
doesn't draw: no "also" readings, a Kanji/Chars tab with glyphs and meanings but no components, and
no Traditional-sense filtering. It's logged once per boot, and there's no error on screen. A host test
runs every fixture with those fields stripped.

## 5. Config (D8, D18)

> The file is owned by the settings panel's store, and uses **per-language INI sections**. See **`settings.md` §3**, which wins over the flat example below (the flat keys are still read and migrated).

`/.lexirise/config.ini` on SD, read once at boot:

```ini
api_key=lx_YOUR_KEY_HERE
default_language=ja     # when the book doesn't say (languages.md §1)
languages=ja,zh
stardict_ja=jmdict
stardict_zh=cedict
tags=xteink
enabled=1
# base_url=https://api.lexirise.app   (override: https only, for a staging server)
```

- The dot folder keeps it out of the file browser, the same way `/.dictionaries/` works.
- The `base_url` override is **https only**, and the server must chain to an ISRG root. The brief's
  local plain-HTTP proxy idea is dropped: it would send the key in clear, and the S3 doesn't need it.
  Changing the server from the web page requires pasting the key again (`settings.md` §2).
- **Key hygiene:** the key is never logged (not even a prefix), never drawn on screen, never
  served by the on-device web server (checked in P1: WebDAV already refused dot paths, but the file
  manager's `/download` only checked the last path segment and served `/.lexirise/config.ini`. That's
  fixed by the hidden-path hook in `firmware-base.md` §3), and never written anywhere but the config.
- **Key check:** on the first WiFi-up of each boot, call `GET /v1/me` once. A 401 disables the provider before the user's first lookup ever waits on it. The response's rate-limit metadata is logged (counts only).
- **Key entry** is from the settings panel (`settings.md` §2): pasted on the phone web page, or typed on the device.
- A missing or empty key means the Lexirise provider is disabled, and lookups go straight to
  StarDict. The first lookup shows `STR_LEXI_NO_KEY` once per boot.

## 6. Tests (host)

(v0.2 V7a) The streamed list: `test/lexirise_net/JsonStreamTest.cpp` (the push reader gives `JsonReader`'s events in
any chunking and rejects what it rejects), `VocabPageTest.cpp` (a synthetic 200-item page in the measured shape, with
an embedded `dictionary_entry` whose look-alike fields are never taken; malformed, cut and over-long bodies), the
client's sink in `LexiriseClientTest.cpp` and `HttpTest.cpp`, the WiFi rule in `ServiceTest.cpp`.

- `test/lexirise_json/`: recorded responses (the brief's examples plus captured real ones from P0)
  → structs. Cover truncated bodies, oversize arrays, unknown keys, a null `saved_expression_id`,
  `status: "pending"`, and keys in reverse order.
- `test/lexirise_request/`: body building. Escape `"`, `\`, control characters and a non-BMP
  character, and never emit invalid UTF-8.
- `test/lexirise_config/`: ini parsing, including the legacy `language=` alias. Comments, CRLF, a BOM, a missing key, trailing spaces on
  the key.
