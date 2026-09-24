# CrossPoint × Lexirise Plugin: Context Brief (original)

> **Kept for the record, 2026-09-24.** This is the brief the project started from, unedited
> below the line. Several of its hardware and firmware claims are out of date against CrossPoint
> `main` as of 2026-09-24. **`00-overview.md` § "What the brief got wrong" wins wherever the two
> disagree.** The main corrections:
>
> - The X4 Pro is an **ESP32-S3 with 8MB PSRAM**, not an ESP32-C3 with ~400KB SRAM (`platformio.ini`
>   `[env:x4pro]`). The TLS OOM risk the brief calls "the biggest risk" belongs to the C3 boards.
> - CrossPoint has **no sentence extraction**. It has page-level word boxes only.
> - CrossPoint splits **Japanese into one token per character**, so a tap selects a character, not
>   a word.
> - Upstream `SCOPE.md` **closes new external network connectors**. This has to be a fork.
> - Upstream is at **v1.6.5** now, not v1.6.0rc.
> - The frequency `rank` lives in `dictionary/lookup`, not in `analyze/text`'s `entryMetaById`.

---

## Goal
Build a plugin/fork of CrossPoint Reader firmware that replaces the built-in StarDict dictionary with Lexirise API integration on the XTEINK X4 Pro. When a user long-presses a word while reading a Japanese EPUB, it should look up the word via Lexirise, show whether it's already in their SRS, and let them save it with one tap.

## Hardware

* Device: XTEINK X4 Pro
* SoC: ESP32-C3 (single-core RISC-V, ~400KB SRAM)
* Screen: 4.3" E Ink, 300ppi
* Input: Physical page-turn buttons + capacitive touchscreen + capacitive Home key
* Connectivity: WiFi (2.4GHz), no Bluetooth in stable builds (RAM constraint)
* Storage: 32GB microSD
* Weight: 72g

## Firmware Base

* CrossPoint Reader — open-source firmware for XTEINK devices
* Repo: https://github.com/crosspoint-reader/crosspoint-reader
* Current version: v1.6.0rc (first version with X4 Pro support), stable is v1.5.0 (X4/X3 only)
* Language: C/C++ on Arduino framework (ESP32)
* Existing relevant features:
   * StarDict dictionary lookup (long-press word → popup with definition)
   * Sentence context extraction from EPUB renderer
   * EPUB 2/3 rendering with CJK ruby annotation support
   * WiFi HTTP client (used for file transfers, KOReader sync)
   * TLS/HTTPS support (the ++ fork fixed OOM issues during TLS handshake)
   * Dictionary popup UI rendering on e-ink
* Note on the ++ fork: CrossPoint Reader ++ (by developer jpirnay) has critical TLS stability fixes. The HTTPS OOM fix for KOReader sync is essential for reliable API calls. Evaluate whether to base this on the main repo or the ++ fork: https://github.com/crosspoint-reader (check for the ++ fork links in community)

## Lexirise API

* Docs: https://lexirise.app/api-reference
* Base URL: https://api.lexirise.app
* Auth: Bearer token or x-api-key header, key format `lx_...`
* Rate limit: 1200 requests/hour (Pro account required)
* Key endpoints for this plugin:

### 1. POST /v1/analyze/text (primary lookup)

Send the full sentence containing the selected word. Returns tokenisation with readings, proficiency state per word, grammar patterns.

```json
// Request
POST https://api.lexirise.app/v1/analyze/text
Content-Type: application/json
Authorization: Bearer lx_...

{
  "text": "猫が好きです。",
  "language": "ja"
}
```

Response includes:

* `occurrences[]` — each token with `word`, `lemma`, `transliteration` (reading), `charStart`, `charEnd`
* `entryMetaById` — per entry: `transliteration`, `partOfSpeech[]`, frequency data
* `stateByEntryId` — per entry: `saved_expression_id` (null if not saved), `proficiency` (0-4), `seen_count`, `user_tags[]`
* `grammar[]` and `grammarStates` — grammar pattern matches

```json
// Example response
{
  "occurrences": [
    {
      "entryId": 1234,
      "word": "猫",
      "normalized": "猫",
      "lemma": "猫",
      "lemmaEntryId": 1234,
      "lang": "ja",
      "isWordLike": true,
      "transliteration": "neko",
      "charStart": 0,
      "charEnd": 1
    }
  ],
  "grammar": [],
  "grammarStates": {},
  "morphoPending": false,
  "entryMetaById": {
    "1234": {
      "entryId": 1234,
      "lang": "ja",
      "status": "ready",
      "transliteration": "neko",
      "partOfSpeech": ["noun"],
      "subTokenCount": 1
    }
  },
  "stateByEntryId": {
    "1234": {
      "saved_expression_id": 987,
      "entry_id": 1234,
      "proficiency": 2,
      "expression_text": "猫",
      "source_lang": "ja",
      "seen_count": 8,
      "updated_at": "2026-06-07T10:15:00.000Z",
      "notes": null,
      "user_tags": [
        { "id": 12, "name": "daily" }
      ],
      "images": null
    }
  }
}
```

### 2. POST /v1/vocabulary (save word to SRS)

Called when user taps "Save" in the popup.

```json
POST https://api.lexirise.app/v1/vocabulary
Content-Type: application/json
Authorization: Bearer lx_...

{
  "language": "ja",
  "text": "猫",
  "mode": "word",
  "translation": "cat",
  "proficiency": 1,
  "tags": ["xteink"]
}
```

### 3. POST /v1/dictionary/lookup (fallback/simpler lookup)

Alternative to analyze/text for single-word lookups if memory is tight.

```json
POST https://api.lexirise.app/v1/dictionary/lookup
Content-Type: application/json
Authorization: Bearer lx_...

{
  "text": "猫",
  "language": "ja"
}
```

Returns: `translations[]` with target language translation, examples, part of speech, `transliteration`, `rank` (frequency), `frequency_score`.

```json
// Example response
{
  "entry_id": 1234,
  "id": 1234,
  "word": "猫",
  "lang": "ja",
  "status": "ready",
  "translations": [
    {
      "target_lang": "en",
      "translation": "cat",
      "examples": [
        {
          "text": "猫が好きです。",
          "translation": "I like cats."
        }
      ],
      "part_of_speech": ["noun"],
      "labels": []
    }
  ],
  "translation_status": "ready",
  "translation_target": "en",
  "supplementalEntries": [],
  "transliteration": "neko",
  "rank": 1846,
  "frequency_score": 0.79,
  "system_tags": ["noun"],
  "subTokenCount": 1
}
```

## Plugin Architecture

### User Flow

1. User is reading a Japanese EPUB on CrossPoint
2. Long-press a word on the touchscreen
3. CrossPoint extracts the selected word + the full sentence it appears in
4. Plugin sends `POST /v1/analyze/text` with the full sentence to Lexirise
5. Plugin finds the selected word in the `occurrences` array (match by charStart/charEnd or word text)
6. Popup displays:
   * Word in kanji/kana
   * Reading (transliteration field — hiragana/romaji)
   * Translation (from entryMetaById or a follow-up dictionary/lookup call)
   * Part of speech
   * Frequency rank (how common — from entryMetaById)
   * Status indicator: "Already saved ✓" with proficiency level if `stateByEntryId` has a `saved_expression_id`, or a "Save" button if not
7. If user taps Save → `POST /v1/vocabulary` with the word, sentence as context in `notes`, tagged `["xteink"]`

### Note on analyze/text vs dictionary/lookup

* `analyze/text` is the primary call — it tokenises the full sentence and returns proficiency state for each word, so you know instantly if the word is already saved
* `analyze/text` does NOT return translations directly — you get `transliteration`, `partOfSpeech`, and proficiency state but not the English meaning
* For the actual translation, either:
   * (a) Make a follow-up `dictionary/lookup` call for the selected word — adds latency but gives full translation + examples
   * (b) Use only `analyze/text` and show reading + POS + saved state without translation — faster, still useful
   * (c) Make both calls in parallel if the HTTP client supports it
* Recommended approach: call `analyze/text` first (for saved state + reading), then `dictionary/lookup` for the selected word (for translation). Show the popup progressively — reading + saved state first, translation fills in when it arrives.

### Implementation Strategy

* Hook point: Replace or wrap the existing StarDict dictionary lookup handler. CrossPoint already has the UI pattern (long-press → popup). The change is swapping the data source from local StarDict files to Lexirise HTTPS API calls.
* Sentence extraction: CrossPoint's EPUB renderer already knows the sentence boundaries (used for existing dictionary context). Extract the full sentence containing the selected word.
* HTTP client: Reuse CrossPoint's existing WiFi/HTTP infrastructure. The ++ fork's TLS fixes are important here.
* Memory management:
   * Keep one HTTPS connection at a time, close immediately after response
   * The analyze/text response for a typical Japanese sentence is ~3-5KB
   * Parse JSON incrementally if possible, or allocate a response buffer
   * Free all response memory before rendering popup
* Configuration: API key stored on the microSD card in a config file (e.g., `lexirise.conf` with the `lx_...` key). Language setting configurable (default `ja`).
* Offline fallback: Keep StarDict as fallback when WiFi is unavailable. Try Lexirise first, fall back to local StarDict if the request fails or times out.

### Popup UI Design (4.3" e-ink constraints)

* Monochrome, high contrast, minimal layout
* Top: word in large font + reading in smaller font above/beside it
* Middle: translation + part of speech
* Bottom: frequency rank indicator + saved status / save button
* Physical button mapping: one button = save, another = dismiss
* Keep popup compact — this is a 4.3" screen

### Edge Cases

* No WiFi: Fall back to StarDict silently
* API timeout: Show "offline" indicator, fall back to StarDict
* Word not found: Show "not in dictionary" (Lexirise handles normalisation/lemmatisation server-side)
* Already saved word: Show proficiency level and seen count, no save button (or allow re-saving to update)
* Rate limiting: 1200/hr = 20/min, unlikely to hit but handle 429 gracefully
* TLS memory: This is the biggest risk. If analyze/text responses cause OOM, fall back to dictionary/lookup (smaller response) or implement the local proxy approach (HTTP to a Mac/phone on the same network that handles TLS)

### Proficiency Levels (Lexirise)

* 0 = unknown
* 1 = tracked
* 2 = learning
* 3 = fresh
* 4 = known

When saving a new word, default to proficiency 1 (tracked). The user's Lexirise SRS will handle progression.

### Stretch Goals (not v1)

* Colour-code or dim words in the EPUB reader view based on proficiency (known words greyed out, unknown words highlighted) — would require deeper integration with CrossPoint's renderer
* Sentence save mode: save the full sentence to Lexirise as `mode: "sentence"` with translation
* Batch sync: queue saves when offline, flush when WiFi reconnects
* Auto-tag with book title (e.g., `["xteink", "novel-name"]`)

## Reference Links

* CrossPoint Reader repo: https://github.com/crosspoint-reader/crosspoint-reader
* Lexirise API docs: https://lexirise.app/api-reference
* CrossPoint review with plugin architecture notes: https://www.svartling.net/2026/04/xteink-x4-with-crosspoint-firmware-120.html
* CrossPoint ++ fork review: https://www.svartling.net/2026/04/my-review-of-thte-xteink-x4-with.html
* PocketInk CrossPoint guide: https://pocketink.io/firmware/crosspoint/
* KOReader Japanese plugin (reference for deinflection approach): https://koreader.rocks/doc/modules/koplugin.japanese.html
