# Lexirise client: HTTPS, endpoints, parsing, config

**Status:** proposed 2026-09-24. Decisions D6, D7, D8, D9, D11 in `00-overview.md`.

Related: `lookup-flow.md` §5 (call sequence), `offline-and-errors.md` (status handling),
<https://lexirise.app/api-reference> (upstream contract; `context-brief.md` has the examples).

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

- **`esp_http_client`**, configured like `HttpDownloader::fetchUrl`:
  `crt_bundle_attach = esp_crt_bundle_attach`, `keep_alive_enable = true`, `timeout_ms = 6000`.
  **Certificate verification is on. Never `setInsecure()`.**
- **Manual request path:** `open(content_length)` → `write(body)` → `fetch_headers` → `read`
  loop, not `perform()`. This mirrors `HttpDownloader`'s open/fetch/read, so the body streams
  straight into the parser.
- **One session per lookup.** `analyze/text` and `dictionary/lookup` reuse the handle (same host,
  keep-alive), then `cleanup()`. Save opens its own session.
- Headers: `Authorization: Bearer <key>`, `Content-Type: application/json`,
  `Accept: application/json`, `User-Agent: Lexipoint/<ver> CrossPoint/<ver>`.
- **Request bodies are built into a fixed PSRAM buffer** with manual JSON string escaping (a
  sentence can contain `"` and `\`). There is no JSON library for writing.

## 2. Endpoints

### `POST /v1/analyze/text`

Request: `{"text": <sentence>, "language": <lang>}`. The reference also documents **`fast: true`** ("fast batch analysis"). v0.1 sends the default (full) analysis, because it needs correct lemmas. **Tested 2026-09-24: `fast` drops lemmas and saves only ~0.3 s, so v0.1 always uses the default.**

Fields we keep (all others are skipped while streaming):

| Path | Keep |
|---|---|
| `occurrences[].{entryId, lemmaEntryId, word, lemma, transliteration, charStart, charEnd, isWordLike}` | All occurrences, compact (§4). **`lemma` is only present when it differs from `word`**, so fall back to `word` |
| `entryMetaById[id].{transliteration, partOfSpeech[0..1], status, rank, frequencyScore}` | All IDs in `occurrences` (lookup-flow §6) |
| `stateByEntryId[id].{saved_expression_id, proficiency, seen_count}` | Same |
| `morphoPending` | Flag. If true, the word boundary may be rough. Show it anyway |
| `grammar`, `grammarStates`, `user_tags`, `images`, `notes`, `updated_at` | Skip |

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

## 3. What the server does for us

- **Tokenization and lemmatization** of Japanese (D4). This is why no MeCab or deinflection runs
  on-device.
- **Normalization** (full-width, kana variants). We send text as it appears on the page.
- **Proficiency state** per entry. The device holds no vocabulary data.

## 4. Parsing

`lib/JsonParser/StreamingJsonParser` is already in the tree and has host tests
(`test/streaming_json_parser`). Write one handler per endpoint that builds these:

```cpp
struct Occ {                 // ~24 B + string refs
  uint32_t entryId, lemmaEntryId;
  uint16_t charStart, charEnd;
  uint16_t word, lemma, reading;   // offsets into a per-response string arena
  bool wordLike;
};
struct EntryInfo { uint32_t id; uint16_t reading, pos; int32_t savedId; uint8_t prof; uint16_t seen; };
```

Strings go into **one PSRAM arena per response** (bump allocator, freed as one block). This avoids
heap churn from many small `std::string`s, which is the same fragmentation the StarDict code works
hard to avoid (`Dictionary.h` `LookupSession` comment).

**Key order.** If `occurrences` streams before `entryMetaById` / `stateByEntryId` (P0 checks
this), a single pass is enough. If not, keep all entries (the §6 v0.1 choice does anyway), so key
order doesn't matter. **Default: keep all. Key order is never a correctness dependency.**

Hard limits (so a hostile or broken response can't exhaust memory): 128 occurrences, a 16KB arena,
and a 64KB body. Anything over a limit aborts the parse and returns `Unavailable`.

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
# base_url=https://api.lexirise.app   (override, for a local proxy)
```

- The dot folder keeps it out of the file browser, the same way `/.dictionaries/` works.
- The `base_url` override lets a **local plain-HTTP proxy** (the brief's fallback idea) be used
  without code changes. It isn't needed on the S3, but costs nothing to support.
- **Key hygiene:** the key is never logged (not even a prefix), never drawn on screen, never
  served by the on-device web server's file listing (check `CrossPointWebServer`: dot folders
  **are** listed or downloadable over WebDAV. If so, exclude `/.lexirise/`, and note it in
  `firmware-base.md` §3 as a hook), and never written anywhere but the config.
- **Key check:** on the first WiFi-up of each boot, call `GET /v1/me` once. A 401 disables the provider before the user's first lookup ever waits on it. The response's rate-limit metadata is logged (counts only).
- **Key entry** is from the settings panel (`settings.md` §2): pasted on the phone web page, or typed on the device.
- A missing or empty key means the Lexirise provider is disabled, and lookups go straight to
  StarDict. The first lookup shows `STR_LEXI_NO_KEY` once per boot.

## 6. Tests (host)

- `test/lexirise_json/`: recorded responses (the brief's examples plus captured real ones from P0)
  → structs. Cover truncated bodies, oversize arrays, unknown keys, a null `saved_expression_id`,
  `status: "pending"`, and keys in reverse order.
- `test/lexirise_request/`: body building. Escape `"`, `\`, control characters and a non-BMP
  character, and never emit invalid UTF-8.
- `test/lexirise_config/`: ini parsing, including the legacy `language=` alias. Comments, CRLF, a BOM, a missing key, trailing spaces on
  the key.
