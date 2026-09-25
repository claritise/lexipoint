# Lexirise API: notes for Lexipoint

> Read from <https://lexirise.app/api-reference> on **2026-09-24**. This summarizes what matters to
> Lexipoint. It is not a copy of the reference, **and the reference wins where they differ**.
> Re-read it before each build phase that touches the API, and date any change here.

**Base** `https://api.lexirise.app` · **Auth** `Authorization: Bearer lx_…` or `x-api-key: lx_…`
(Pro-only keys, scoped to the owning account) · **Limit** 1200 req/h · 31 endpoints under `/v1`.

**Proficiency:** 0 `unknown` · 1 `tracked` · 2 `learning` · 3 `fresh` · 4 `known`. Writes take the
number. Filters take either the number or the label.

## Endpoints we use or plan to

| Endpoint | Use | Documented facts that matter |
|---|---|---|
| `GET /v1/me` | Key check (v0.1) | Returns `{ user, apiKey }`, **including the key's rate-limit metadata**. A cheap way to validate the key and read the remaining budget |
| `POST /v1/analyze/text` | Lookup ① (v0.1). C5 and C6 | Body: `text`, `language`, **`fast`** (optional bool, "fast batch analysis"). Returns `{ occurrences, grammar, grammarStates, morphoPending, entryMetaById, stateByEntryId }` |
| `POST /v1/dictionary/lookup` | Lookup ③ (v0.1) | "Normalizes and **inserts the lookup word if needed, queues translation work**". So `translation_status` can be non-ready on a first lookup of a rare word. **No target-language field** in the request |
| `POST /v1/vocabulary` | Save (v0.1). C3 | **"Creates or updates"** (an upsert: no 409 to handle). Body: `language`, `text`, `mode` (`word`/`sentence`), `translation` ("optional **custom** translation"), **`notes`** (optional private notes), `proficiency` (**1–4**), `tags`, `audioUrls`. Returns `{ result, item }` |
| `GET /v1/vocabulary` | C7 | Returns `{ items, totalCount, languageCount, nextOffset, availableTags }`. Filters: `language` (required), `mode` = **`words`/`sentences`** (plural, unlike POST), `proficiency`, `proficiencyLabel`, `userTags`, … `limit` ≤ 200 |
| `PATCH /v1/vocabulary/{id}` | C9 (set the level from the card) | `proficiency` 0–4, `notes`, `customTranslation`, `tags`, `suspended`, … The `{id}` is the `saved_expression_id` from `stateByEntryId` |
| `PUT /v1/vocabulary/{id}/tags` | C2 re-tagging only | **Replaces every tag.** Appending needs a read-modify-write |
| `POST /v1/decks` | C4 | `deck_type` `snapshot` or **`dynamic`**. A dynamic deck with `rule_type: "user_tag_filter"` + `user_tags` fills itself from tagged vocabulary. Also `unit_type`, `parent_deck_id` (subdecks) |
| `POST /v1/uploads/chapters` | C8 | Multipart. `content_type: document` accepts **EPUB/TXT**. `external_id` / `Idempotency-Key` for idempotency. Processing is async (`/status`) |

## What the reference answers (open questions from the docs)

| Question | Answer |
|---|---|
| H6: does `/v1/vocabulary` take `notes`? | **Yes.** Resolved |
| "Already exists" on save | **Upsert** ("creates or updates"). There's no 409 path. ⚠ Whether re-saving *replaces* or *merges* `tags` / `notes` / `proficiency` isn't stated. Never re-POST a word that's already saved (v0.1 doesn't) |
| H7: Chinese code | **`zh`**, used in the reference's own examples (`vocabulary` sentence, `uploads/series`). The site also lists Chinese, Japanese and Korean. Pinyin format is still unverified |
| Proficiency on save | 1–4 (0 isn't accepted on create). Our default of 1 is valid |
| Deck ID on save | **No.** `POST /v1/vocabulary` has no deck field. **Dynamic tag decks exist**, which is better for C4 (see v0.2) |
| Vocab count | **Yes**: `totalCount` / `languageCount` on the list response. One request with `limit=1` |
| Server-side analysis of uploaded books, fetchable by the device | **No such endpoint.** Upload status only reports ingest progress |
| Sentence translation if `translation` is omitted | **Not stated.** "Custom" implies Lexirise has its own. Verify in P0 |

## Verified against the live API (2026-09-24)

16 calls (`dictionary/lookup` + `analyze/text` × 勉強 兄貴 猫 煩わしい / 学习 引擎 猫 选择), plus one
`analyze/text` on `𠮟る猫がコーヒーを飲んだ。`. All returned HTTP 200.

| Question | Answer |
|---|---|
| **JLPT / HSK level (H9)** | **Yes, in `dictionary/lookup` → `system_tags`.** Values seen: `JLPT-N5` (猫), `JLPT-N3` (勉強), `JLPT-N1` (煩わしい), `HSK-1` (学习), `HSK-2` (猫), `HSK-4` (选择), `HSK-7+` (引擎). No tag when the word isn't on a list (兄貴 → `[]`). Other tags sit beside it (`kanji`, `char`), so match the pattern, not index 0. **`HSK-7+` means HSK 3.0** (levels 1–6, then 7–9 banded as one) |
| Is the level in `analyze/text`? | **No.** `entryMetaById` has no `system_tags`, so the badge arrives with phase B |
| **H5: the `charStart` unit** | **UTF-16 code units.** 𠮟 (U+20B9F) spans `0–2`, and the next token starts at 2 |
| **Japanese reading** | **Romaji, not kana.** Native words use **spelled-out long vowels** (`toukyou`, `ookii`, `kyou`) and `'` after ん (`kin'youbi`), which converts losslessly to hiragana. Katakana words use **macrons** (`kōhī`, `bīru`). Bad readings seen: 一緒 → `ichiitoguchi`. Other examples: `aniki`, `neko`, `nonda`. The device converts these to kana (`../v0.1/languages.md` §3a). `multipleReadings` gives `{primary, alternatives[]}` in romaji too (猫: `neko`, alternatives `nekoma`, `byou`) |
| **H7: Chinese reading** | **Pinyin with tone marks, space-separated**: `xué xí`. There's also a `tones` array (`[2, 2]`) |
| **Rank in `analyze/text`** | **Yes.** `entryMetaById[id]` has `rank` and `frequencyScore` (camelCase there, snake_case in `dictionary/lookup`). The brief was right, and `00-overview.md`'s "correction" on this was wrong |
| `lemma` on occurrences | **Only present when it differs** from the surface form (飲んだ → `lemma: 飲む`). Otherwise it's missing. Fall back to `word` |
| Meta `status` | `completed` (not `ready` as in the example) |
| `examples` | Can be `[]` **or `null`** |
| H4: translation target | `translation_target: "en"`. The request has no target field, so it's tied to the account |

## Undocumented fields (seen live, may change without notice)

- **`breakdown`** on `dictionary/lookup` **and** on each `analyze/text` occurrence: `[{text, entryId,
  isWordLike}]` per character (兄貴 → 兄, 貴). This is the kanji/hanzi breakdown's skeleton. Each
  character's meaning and level needs its own `dictionary/lookup` (by `text`), which gives `system_tags`
  like `JLPT-N5` + `kanji`.
- **`morphemes`** on occurrences: `[{segment, role: "kanji"}]`.
- **`characterInfo`** (Chinese single characters, 猫): `pinyin[]`, `gloss`, `hint` (a mnemonic, e.g.
  "犭 = meaning and 苗 = sound"), `components[{character, type: meaning|sound}]`, `strokeCount`,
  **`statistics.hskLevel`**, and `topWords[{word, share, trad, gloss}]`. That's enough for a rich
  Chinese character tab.
- **`labels`** on translations: `spoken`, `system_deck`, `system_deck_traditionalized`.

## Data quirks seen while building the card mock (2026-09-24)

- **A character's HSK level isn't always in `system_tags`.** 择 has `system_tags: ["char"]`, but
  `characterInfo.statistics.hskLevel: 4`. For characters, read `characterInfo` first, then
  `system_tags`. (选 has both: `HSK-2`.)
- **Kanji lookups carry a JLPT tag when the kanji is on a list:** 一 → `JLPT-N3` + `kanji`, 日 →
  `JLPT-N4` + `kanji`. 煩 → only `kanji` (not on a list). Readings are romaji (`ichi`, `hi`, `han`),
  and there's no `characterInfo` for Japanese. *(Corrected 2026-09-24: an earlier note said kanji
  have no level. They can.)*
- **The two endpoints can disagree on the reading.** 一日: `analyze/text` → `ichinichi`, while
  `dictionary/lookup` → `transliteration: tsuitachi` with `multipleReadings.alternatives`
  `[ichinichi, ichijitsu, tsukitachi, hitohi, ippi]`. The card shows analyze's reading (it is at least
  per sentence) plus the alternatives (C15).
- **Examples can be in the wrong script.** 选择's only example is Traditional (我不知道該怎麼選擇),
  from a sense labelled `system_deck_traditionalized`. Filter out senses with that label in
  Simplified books, or ask Lexirise.
- **Examples are often missing.** 煩わしい's senses all have `examples: []`.

## SRS fields on vocabulary items (read 2026-09-24, `GET /v1/vocabulary?language=zh&limit=1`)

Lexirise schedules with **FSRS**, and each item exposes the schedule **read-only**: `next_review_at`,
`last_review_at`, `fsrs_stability`, `fsrs_difficulty`, `fsrs_reps`, `fsrs_lapses`, `fsrs_state`,
`suspended`, `buried_until`, `introduced_at`, plus `seen_count`, `first_seen_at`, `last_seen_at`,
`proficiency_source` (`manual`), and the full `dictionary_entry` (with `system_tags`, rank and
translations) embedded.

- **There's no endpoint to record a review** *(one is being built: see "Reported to Lexirise" below)*. `PATCH /v1/vocabulary/{id}` accepts `proficiency`,
  `suspended` and so on, but no grade, and no FSRS fields.
- **There's no "due" filter** *(`GET /v1/vocabulary/due` is being built)*. `sortId` has `last_review_at` but not `next_review_at`. Due cards
  have to be filtered on the device (`next_review_at <= now`) after paging everything, which is 1
  request per 200 items.
- `GET /v1/me` → `apiKey` has `rateLimitMax: 1200` and `rateLimitTimeWindow: 3600000`, plus
  `lastRequest` and `name`. There's no "remaining" count.

## Is `analyze/text` context-aware? (tested 2026-09-24): mostly no

It segments and lemmatizes in context. But **the reading, part of speech and meaning are per
dictionary entry, not per occurrence**:

| Sentence | Returned | Correct in context |
|---|---|---|
| 四月**一日**に入学した。 | `ichinichi` | ついたち (1st of the month) |
| 彼は私より一枚**上手**だ。 | `jouzuda` (same entry as 料理が上手だ) | うわて (a cut above) |
| 他**长得**很高 | `chángdé` | zhǎngde |
| 他的头发**长**了。 | `cháng` | zhǎng (to grow) |
| 做出自己的**选择** vs 我**选择**了 | `['noun','verb']` both times | noun vs verb |

- **`grammar[]` and `grammarStates` were empty for every sentence**, including ～ことにした. Grammar
  patterns don't seem to be populated through the API (or need an account or setting we don't have).
- The tokenizer merged `一日中雨` into one token with no part of speech.
- Inflected forms (煩わしくて) have **no part of speech on their own entry**. Use the lemma's entry.

**Consequence:** the card can't pick "the right sense" or "the right reading" from the sentence using
this API. `dictionary/lookup` gives every sense in a fixed order. See v0.2 C10 for options.

## Tokenizer notes

- `𠮟る` was split into `𠮟` (`shiっ`) + `る` (lemma `り`). The rare kanji broke the tokenizer.
  That's a server-side quality issue, not something to fix on the device, but it's worth reporting to
  Lexirise.
- `コーヒー` came through as one token, with the reading `kōhī`.
- **Kana spelling of a common word split as particle + word** (seen on the device, 2026-09-25):
  in ときどきどこかの教室の**とびら**のあけしめされる音が… (とびら = 扉, door) the word came back
  as と + **びら** (handbill), so the card showed びら. The full sentence was sent (~45 characters,
  bounded by 。 on both sides, ruby excluded), so more context wouldn't help: the clue (a noun after
  の) is next to the word. Most likely a lexicon cost for the kana spelling of a word usually written
  in kanji. Kana-heavy books (children's, YA) are the weak spot. Chinese segmentation seems better
  in use (claritise's impression, not measured). Not reported yet.
- **Seen segmenting a whole manga volume** (2026-09-25, 178 `analyze/text` calls, one per page, the
  page's OCR'd text blocks joined with `\n`; `../v0.2/manga.md` §4):
  - `\n` comes back as its **own occurrence** with `isWordLike: false`, like punctuation. Blocks joined
    with newlines keep their offsets clean; filter on `isWordLike`.
  - The topic particle **は reads `ha`**, not `wa`: all 374 times in the volume (e.g. それは → それ + は `ha`). A reading
    shown for は should be corrected on the device or hidden.
  - **Kanji numerals split per character:** 一九九九年 → 一 / 九 / 九 / 九年.
  - Full-width Latin (ＨＯＴＥＬ) is one word-like token with no reading; OCR's full-width dots
    (`．．．`) are three non-word tokens.
  - Occurrences carry **`lemmaEntryId`** beside `entryId` (the lemma's entry for an inflected form;
    use it first for the lemma), plus `normalized`, `subTokenCount` and `lang`.
  - Lemmas were right on inflected forms (めがけて → めがける, 落ちて来た → 落ちる, 早く → 早い).
  - Latency ~1–5 s per call for a manga page's text (a few hundred characters).

## Tested live, 2026-09-24 (second round)

| Question | Answer |
|---|---|
| **Traditional Chinese (H8)** | Sent as `zh`, 到了最後…選擇 **is understood**: it maps to the Simplified entries (最後 → `最后` id 636, 選擇 → `选择` id 1504), and occurrences come back **in Simplified**. `zh-TW` / `zh-Hant` return 200 with near-empty bodies, and `zh_TW` returns 500. **Parked: Simplified first (claritise, 2026-09-24).** |
| **`fast: true`** | ~1.0 s vs ~1.3 s for the default, same segmentation, but **no lemmas** (煩わしくて stays 煩わしくて, not 煩わしい). **Use the default everywhere.** Lookups need the lemma, and page marks need it to match inflected words to saved lemmas. Oddly, the default reports `morphoPending: true` even though its lemmas are present |
| **Maximum `text` length** | **No limit hit up to 20,000 characters** (HTTP 200, 5.8 s). The response is **~70 bytes per character** (20k chars → 1.4 MB), so a ~300-character page is ~20 KB. That's fine to stream into PSRAM |
| **Latency** | Every call took **~1.0–1.3 s** from here, even tiny ones. A lookup is two calls, so expect ~2–3 s plus WiFi and TLS. Phase 0 of the card (instant feedback) matters |

## Account write tests (2026-09-24, with claritise's OK, one throwaway word: 蓋然性)

| Question | Answer |
|---|---|
| **Re-saving an existing word: merge or replace?** | **Replace.** `POST /v1/vocabulary` again returns `result.status: "updated"`, `reason: "already_exists"`, the same ID, and the item's `tags`, `notes`, custom translation and `proficiency` are **all overwritten** by the new request. Never re-POST a saved word. Use `PATCH` |
| **Sentence saved without `translation`** | **Lexirise translates it straight away**: `custom_translation: "This conclusion is highly probable."` was in the POST response itself. ⚠ A sentence saved without `proficiency` **defaults to 2 (learning)**, so always send `proficiency` explicitly. `dictionary_id` is `null` for sentences |
| **Do `analyze/text` / `dictionary/lookup` bump `seen_count`?** | **No.** Two of each left `seen_count: 0` and `last_seen_at: null`. Page analysis won't inflate your stats, but reading on the device won't count as exposure either |
| **Tags with `:`** | **Kept as is** (`book:test-slug`). Tags are scoped per `source_lang` |
| `POST` response shape | `result: {text, type, status: added\|updated, reason?, savedExpressionId, dictionaryId}` plus the full `item` |
| **`DELETE` on a dictionary word** | `{success: true, deleted: false}`: the item **stays, reset to proficiency 0 (unknown)**, **keeping its notes, translation and tags**. Clear them with `PATCH {notes: null, customTranslation: null, tags: []}`. A sentence item is really deleted (`deleted: true`, then 404) |
| **Deleting tags** | **No endpoint.** Tag names persist on the account after their last use (test leftovers: `book:test-slug`, `lexipoint-test`, `lexipoint-test-2`). Keep device-created tag names few and predictable |

## Rank by language (sampled 2026-09-25, `dictionary/lookup`, for the card's rank words)

| Japanese | rank | list | Chinese | rank | list |
|---|---|---|---|---|---|
| する | 15 | N3 | 的 | 1 | HSK-1 |
| 時間 | 259 | N2 | 时间 | 334 | HSK-1 |
| 勉強 | 649 | N3 | 选择 | 1,113 | HSK-4 |
| 猫 | 726 | N5 | 学习 | 1,265 | HSK-1 |
| 電車 | 1,951 | N5 | 猫 | 1,656 | HSK-2 |
| 景色 | 2,643 | N4 | 约定 | 5,742 | HSK-6 |
| 兄貴 | 3,125 | – | 景色 | 8,123 | HSK-3 |
| 憂鬱 | 17,185 | N1 | 引擎 | 13,961 | HSK-7+ |
| 煩わしい | 29,774 | N1 | 忧郁 | 17,045 | HSK-7+ |
| 微睡む | 123,851 | – | 电车 | 17,930 | HSK-6 |

Chinese ranks run higher for words as common, so the thresholds are per language
(`config::kRankBandLimitsJa` / `Zh`): Japanese 1k / 5k / 20k, Chinese 1k / 10k / 30k (the first stays
1k: the approved reference shows 选择 #1,113 as *common*). A word that isn't in the dictionary (蓋然性,
或然性) has no rank.

## Still not in the reference (P0 checks with curl)
- **`Retry-After`** on 429, and the error body format in general.

## Reported to Lexirise (2026-09-24, by claritise)

The contextual meaning endpoint, empty `grammar[]`, a review endpoint plus a due list, and the bad data (一緒 → `ichiitoguchi`, 𠮟る split as 𠮟 + る, 一日中雨 as one token).

**Replies:**
- **2026-09-24:** the Lexirise developer is happy to support the use case with new endpoints.
- **2026-09-25** (Lexirise's announcement bot): **being built, not live yet.** Lexirise will post in
  the thread when they are.

| Announced | What it does (as announced) | Check in the reference once live |
|---|---|---|
| `GET /v1/vocabulary/due` | The cards due now | Does each card carry its content (word, reading, meaning, the saved sentence) or only IDs? A `limit` / pagination? Our parser caps a body at 64 KB (`../v0.1/lexirise-client.md` §1), so a large backlog with sentences needs pages of ~20 |
| `POST /v1/vocabulary/{id}/review` | Takes a grade **1–4**; the server runs the scheduler and returns the updated card | The grade scale (Again / Hard / Good / Easy expected). Does the response carry the next due date? Not idempotent: never resend after a dropped connection (same rule as a save) |
| `POST /v1/words/context` | Sentence + word → the meaning in that context | **Does it return the reading too?** The announcement only says "meaning", and the reported cases were mostly readings (四月一日 → tsuitachi, 一枚上手 → uwate, 长得 → zhǎng). If it's meaning-only, ask for the reading: that's the one follow-up worth sending |

- **Grammar explained:** `analyze/text` answers `morphoPending: true` for new text, and grammar comes
  from a slower second pass that the API wasn't starting, so `grammar[]` stayed empty. The API will
  start that pass, so **calling `analyze/text` again returns the grammar.** Not a bug on our side:
  the v0.1 client skips `grammar` / `grammarStates` (`../v0.1/lexirise-client.md` §2) and doesn't
  call again.
- **Timestamp on reviews:** the second post asked for "grade + timestamp", but never said why, and the
  announcement is grade only. That's fine for online reviews (server time is the review time). **No
  follow-up needed: on-device review is online-only** (claritise, 2026-09-25; v0.2 C11).
- **Bad data:** not mentioned in the announcement. Re-test 一緒, 𠮟る and 一日中雨 once the endpoints are live.
- **Nothing gets better on the device by itself** except fixed server data. Grammar, contextual
  meaning and reviews each need firmware work (v0.2 C10, C11, C19).
