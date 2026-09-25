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
| `POST /v1/analyze/context` | C10 (live 2026-09-25) | Sentence + the word's `charStart` / `charEnd` → `meaning`, `conciseMeaning`, `reading` (when the offsets match one token). See "Reported to Lexirise" |
| `GET /v1/study/summary`, `POST /v1/study/sessions`, `GET /v1/study/sessions/{id}`, `POST /v1/study/sessions/{id}/reviews` | C11 (live 2026-09-25) | Due counts; sessions with card content; batched, deduped reviews with `reviewedAt`. See "Study API" |
| `POST /v1/uploads/chapters` | C8 | Multipart. `content_type: document` accepts **EPUB/TXT**. `external_id` / `Idempotency-Key` for idempotency. Processing is async (`/status`) |

## What the reference answers (open questions from the docs)

| Question | Answer |
|---|---|
| H6: does `/v1/vocabulary` take `notes`? | **Yes.** Resolved |
| "Already exists" on save | **Upsert** ("creates or updates"). There's no 409 path. Re-saving **replaces** `tags`, `notes`, the custom translation and `proficiency` (tested 2026-09-24, "Account write tests" below). Never re-POST a word that's already saved (v0.1 doesn't). Use `PATCH` |
| H7: Chinese code | **`zh`**, used in the reference's own examples (`vocabulary` sentence, `uploads/series`). The site also lists Chinese, Japanese and Korean. Pinyin format verified 2026-09-24: tone marks, space-separated ("Verified against the live API" below) |
| Proficiency on save | 1–4 (0 isn't accepted on create). Our default of 1 is valid |
| Deck ID on save | **No.** `POST /v1/vocabulary` has no deck field. **Dynamic tag decks exist**, which is better for C4 (see v0.2) |
| Vocab count | **Yes**: `totalCount` / `languageCount` on the list response. One request with `limit=1` |
| Server-side analysis of uploaded books, fetchable by the device | **No such endpoint.** Upload status only reports ingest progress |
| Sentence translation if `translation` is omitted | Not stated in the reference. **Tested 2026-09-24: Lexirise translates it straight away**, and a sentence saved without `proficiency` defaults to 2 ("Account write tests" below) |

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

- **There's no endpoint to record a review** *(superseded 2026-09-25: the study API records reviews, see "Study API" below)*. `PATCH /v1/vocabulary/{id}` accepts `proficiency`,
  `suspended` and so on, but no grade, and no FSRS fields.
- **There's no "due" filter** *(superseded 2026-09-25: `GET /v1/study/summary` counts due cards and a study session returns them)*. `sortId` has `last_review_at` but not `next_review_at`. Due cards
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
  in use (claritise's impression, not measured).
  - **The sentence:** ときどきどこかの教室のとびらのあけしめされる音がだれもいない廊下にうつろにひびく。
    Language `ja`. Expected: とびら as one noun (扉, door). Seen: と (particle) + びら (handbill), on the
    first `analyze/text` answer, which the v0.1 card uses.
  - **Likely cause (not confirmed):** `morphoPending: true` answers are "fast tokens only"
    (documented 2026-09-25), so the refined second call may already split it correctly.
  - **Plan (claritise, 2026-09-25): don't report it yet.** v0.2 adds the second call (C19). Test the
    sentence then: if the refined answer returns とびら whole, it's fixed on our side and there's
    nothing to report. **If it's still split after v0.2, report it to Lexirise** with this sentence,
    both answers' occurrences, and the expected reading.
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

## The second `analyze/text` pass, measured (2026-09-26, v0.2 V1)

`tools/lexirise/probe_morpho.py` and `probe_refine.py` (dev key; raw responses in `research/v02-morpho/`):
15 sentences (10 Japanese, 5 Chinese), each sent once, then polled until `morphoPending` was false.

- **Timing:** refined 35–80 s after the first call (most by ~48 s at 15 s polling; one Chinese sentence was
  still pending at 69 s in the first run). Far longer than a card is usually open. The first call starts it;
  polling doesn't speed it up.
- **Cached server-side:** text Lexirise has refined before comes back refined on the **first** call
  (`morphoPending: false`, grammar filled). The とびら sentence did: so the device's cards already use the
  refined split for any sentence seen before.
- **What refining does to the split: it cuts into grammatical morphemes.** 6 of 11 splits changed:
  一日中雨 → 一日中 · 雨 (better: the known bad token), but also 深深 → 深 · 深, 一边 → 一 · 边,
  小さな → 小さ · な, 长得 → 长 · 得, 一気に → 一気 · に (worse for looking a word up: the dictionary word is the
  whole one). とびら stays と · びら in the refined answer too. **So the refined split is not a better word
  split,** and the v0.1 card showing 深 for 深深 (`../v0.1/device-checks.md`, the save/Undo leftover) is this cache.
- **Grammar arrives with it,** well filled: ～ている, ので, ～てしまう, ても, と (conditional), 受身形, the
  得 degree complement, 了, verb reduplication, 地… Each item: `slug`, `title`, `level` (e.g. JLPT N4),
  `subtitle`, `indices` (occurrence indices) and `anchors` (`start`/`end` in characters, `token`).
  ～ことにした still isn't detected (reported, Open #6). `grammarStates` was `{}` for this account.
- **`fast: true` on an already-refined sentence still returns the word-level split** (tested 2026-09-26 on
  深深, 一边, 小さな, 长得, 一気に: all whole), with each word's `entryId`, `entryMetaById` (reading, part of
  speech, rank) and `stateByEntryId` (saved words' state), but no `lemmaEntryId`. ~1.0 s, like the default.
  Real words the refined pass cut up all have a `rank`; the bad merge 一日中雨 has none (and an unusually large
  entry id), so the rank tells them apart (v0.2 V1, `lookup::wholeWords`). **Watch:** some inflected forms carry
  a rank of their own (言った, ついた); if the refined pass ever cut one, the whole word would come back without its
  lemma (looked up and saved as 言った). Not seen in the samples: none cut an inflected word.
- **For Lexirise (claritise's call when to report):** the refined pass over-splits words learners look up
  (深深, 一边, 小さな, 长得) and the cached refined answer then replaces the good first split for everyone;
  とびら is split in both passes. Examples above.

## Tested live, 2026-09-24 (second round)

| Question | Answer |
|---|---|
| **Traditional Chinese (H8)** | Sent as `zh`, 到了最後…選擇 **is understood**: it maps to the Simplified entries (最後 → `最后` id 636, 選擇 → `选择` id 1504), and occurrences come back **in Simplified**. `zh-TW` / `zh-Hant` return 200 with near-empty bodies, and `zh_TW` returns 500. **Parked: Simplified first (claritise, 2026-09-24).** |
| **`fast: true`** | ~1.0 s vs ~1.3 s for the default, same segmentation, but **no lemmas** (煩わしくて stays 煩わしくて, not 煩わしい). **The default is the lookup's call:** lookups need the lemma, and page marks need it to match inflected words to saved lemmas. **Since v0.2 V1, `fast` is asked as well when the default answer came back already refined:** it still returns the word-level split (below, "The second `analyze/text` pass, measured"). The default reports `morphoPending: true` on a first pass even though its lemmas are present |
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
- **2026-09-25, earlier** (Lexirise's announcement bot): being built, announced as
  `GET /v1/vocabulary/due`, `POST /v1/vocabulary/{id}/review` (grade 1–4) and `POST /v1/words/context`.
- **2026-09-25, later: live and documented in the reference, in a different shape** (read 2026-09-25):
  the study-session API and `POST /v1/analyze/context`, both below. The announced paths don't exist.
  The reported readings (长得, 四月一日, 𠮟る, 一緒) and ～ことにした not being detected are **filed and
  being worked on** separately. 一日中雨 and とびら (tokenizer notes) weren't in that list; とびら was
  never reported.

### Study API (live 2026-09-25)

| Endpoint | Body / query | Returns | Notes |
|---|---|---|---|
| `GET /v1/study/summary` | `language` (optional) | `dueCount`, `newCount`, `reviewedToday`, `newCards {dailyLimit, remainingToday}`, `today {cardCount, dueCount, newCount, estimatedMinutes}`, `languages[]`, `decks[] {deckId, title, language, unitType, itemCount, dueCount, newCount}` | "dueCount counts cards due now and newCount saved cards never studied" |
| `POST /v1/study/sessions` | `language` (required); `mode` `study` (default) / `cram`; `source {type: all / deck / tags / vocabulary / recent / today, deckId, tags, vocabularyIds}`; `filters {proficiency, partOfSpeech, unitType}`; `directions` `forward` / `reverse` / `listen`; `limit` ≤ 180 | `sessionId`, `mode`, `language`, `cards[] {vocabularyId, direction, isNew, dueAt, unitType, text, reading, translation, sourceSentence {text, translation}, audioUrl, proficiency}` | 422 when there's nothing to study |
| `GET /v1/study/sessions/{sessionId}` | — | Same as above | Resume. "Learning cards due within 20 minutes are included, as in the app" |
| `POST /v1/study/sessions/{sessionId}/reviews` | `reviews[]` (≤ 200): `id` (UUID, ours), `vocabularyId`, `direction`, `rating` `again` / `hard` / `good` / `easy`, **`reviewedAt` (required, ISO)**, `durationMs` (optional) | `results[] {id, status: applied / duplicate / rejected, error?, card {vocabularyId, direction, dueAt, proficiency, state, suspended}}` | **A resent `id` is `duplicate`**: batches are safe to retry. Applied oldest first. Scheduled exactly like reviews in the app |

- **Offline review is possible** with this API (timestamps, client ids, dedupe). v0.2 C11 keeps
  claritise's online-only decision until it's reopened.
- `reviewedAt` is required, so even online reviews need the device's real time (NTP after WiFi-up).
- A 180-card session with sentences and translations may exceed our 64 KB body cap
  (`../v0.1/lexirise-client.md` §1). **Measure** a real session's size; use a smaller `limit`.
- `listen` needs audio; the X4 Pro has none.

**Tested live, 2026-09-25 17:38 UTC** (claritise's account, with their OK; responses and the session ID are
kept in `research/study-test/`, gitignored, never committed):
- `GET /v1/study/summary` (ja, zh): 200, ~200–250 bytes. `newCards.dailyLimit` 5 on this account.
- `POST /v1/study/sessions` (`zh`, `study`, `forward`, `limit: 1`): 200, 306 bytes, one card.
  **Starting a session changed nothing**: the summary afterwards showed the same `newCount` and
  `remainingToday`. So fetching cards is safe to do on every sync.
- `GET /v1/study/sessions/{id}` right away: 200, same 306 bytes (resume works).
- **Size:** 233 bytes for a new card with no source sentence; a card with a sentence and its
  translation will be larger. A 180-card session is probably ~40–90 KB, over or near the 64 KB cap, so
  stream it (v0.2 C11). Measure again with a sentence card.
- **Session validity test, in progress:** the same session gets **one real review on 2026-09-27**
  (a card claritise would genuinely grade that way; it changes that card's schedule). Accepted
  (`applied`) means a session survives at least ~1–2 days. Rejected means offline review needs a
  fallback (create a new session, then send the queued answers against it). Record the result here.

### `POST /v1/analyze/context` (live 2026-09-25)

Body: `language`, `text` (≤ 1600 characters), **`charStart` / `charEnd` from `analyze/text`**, optional
`question` (defaults to the word's meaning). Returns `meaning` (full), `conciseMeaning` (short) and
`reading`, which is optional: returned "when the offsets match one token". "Only the answer uses a model."
Works like the app's Context tab. Not tested from here yet: whether it gets 四月一日 → tsuitachi,
一枚上手 → uwate, 长得 → zhǎng.

### `morphoPending` (documented 2026-09-25)

"When true, the response has fast tokens only. Call again later for grammar and refined segmentation."
So the first answer's word boundaries **can change** on the second call, not only its grammar. The v0.1
client uses the first answer and doesn't call again (`../v0.1/lexirise-client.md` §2), so today's card
may show a rough split. v0.2 C19 has the plan. How long "later" is isn't documented: measure it.

- **Earlier notes, now superseded:** "a grade-only endpoint, no follow-up needed" (the API takes
  `reviewedAt` after all); "not idempotent: never resend" (reviews are deduped by `id`).
- **Nothing gets better on the device by itself** except fixed server data. Grammar, contextual
  meaning and reviews each need firmware work (v0.2 C10, C11, C19).
