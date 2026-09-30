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
| `GET /v1/vocabulary/{id}` | C14 ("Met before", v0.2 V4b) | Read-only. The item with `notes`, `user_tags` and `sentence_text`, beside the SRS fields (measured 2026-09-27: "A saved word's notes and tags in `analyze/text`" below). The `{id}` is the `saved_expression_id` from `stateByEntryId` |
| `PATCH /v1/vocabulary/{id}` | C9 (set the level from the card); C17 Ignore (`suspended`, measured below) | `proficiency` 0–4, `notes`, `customTranslation`, `tags`, `suspended`, … The `{id}` is the `saved_expression_id` from `stateByEntryId` |
| `PUT /v1/vocabulary/{id}/tags` | C2 re-tagging only | **Replaces every tag.** Appending needs a read-modify-write |
| `POST /v1/decks` | C4 | `deck_type` `snapshot` or **`dynamic`**. A dynamic deck with `rule_type: "user_tag_filter"` + `user_tags` fills itself from tagged vocabulary. Also `unit_type`, `parent_deck_id` (subdecks) . Used by V3, with `GET /v1/decks` and `GET /v1/decks/{id}` ("Decks" below) |
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
| **Japanese reading** | ~~**Romaji, not kana.**~~ **Superseded 2026-09-29, measured on the device:** on claritise's account `analyze/text`'s `transliteration` comes back **in kana** (彼 `かれ`, 東京 `とうきょう`, 行った `いった`, は `は`; one read-only analyze through the dev harness, `lxctl.py lexi analyze ja`); on the dev key's account it was romaji the same day (V9b's probe, 2026-09-29: "Levels for A5 (V9b)", raw in `research/v9b/`). Whether that's per account, per account setting or a server change isn't known; the card takes either form (`../v0.1/languages.md` §3a). With the dev key: **romaji, not kana.** Native words use **spelled-out long vowels** (`toukyou`, `ookii`, `kyou`) and `'` after ん (`kin'youbi`), which converts losslessly to hiragana. Katakana words use **macrons** (`kōhī`, `bīru`). Bad readings seen: 一緒 → `ichiitoguchi`. Other examples: `aniki`, `neko`, `nonda`. The device converts these to kana (`../v0.1/languages.md` §3a). `multipleReadings` gives `{primary, alternatives[]}` in romaji too (猫: `neko`, alternatives `nekoma`, `byou`) |
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

- **A compound verb's lemma can be its first verb** (seen on the device 2026-09-27, V4): 見くらべた came back with
  lemma 見る, so the card looks up 見る ("see") instead of 見比べる ("compare"). The namer leaves the form unnamed
  (it isn't a form of 見る), but the meaning shown is the wrong word's. ~~Worth reporting to Lexirise (claritise's call).~~
  ~~**Measured 2026-09-27** (12 compounds …)~~ **Superseded the same day by a wider measurement** (30 forms, read-only;
  raw in `research/v4/compound-*.json`). The first verb comes back as the lemma in two cases:
  - **Aspect compounds, any spelling:** 〜出す "start to", 〜続ける, 〜始める, 〜すぎる/〜過ぎる: 笑い出した‹笑う›,
    泣き出した‹泣く›, 歩き出した‹歩く›, 走り続けた‹走る›, 読み続けた‹読む›, 降り始めた‹降る›, 食べすぎた and 食べ過ぎた
    ‹食べる›, and the dictionary form 笑い出す‹笑う› too. `dictionary/lookup` has complete entries for every one of the
    compounds (笑い出す 144406320, 泣き出す 106251878, 歩き出す 92564515, 走り続ける 145484911, 読み続ける 193666552,
    降り始める 169236341, 食べすぎる 92114219, 食べ過ぎる 146636085).
  - **A lexical compound in a mixed kanji/kana spelling:** 見くらべた and 見くらべる‹見る›, 書きこんだ‹書く›, とり出した
    ‹とる›, while the standard spelling is right (見比べた‹見比べる›, 書き込んだ‹書き込む›, 取り出した‹取り出す›) and so
    is all-kana みくらべた‹みくらべる›.
  - **Right:** 飛び出した, 飛びだした, 思い出した, 読み終わった, 振り返った, 見上げた, 話し合った.
  - The token is one occurrence each time: only `lemma`/`lemmaEntryId` is off. `breakdown` doesn't explain it: it
    splits every word into kanji and kana runs for display (見比べた is 見 + 比 + べた), not into the analyzer's pieces.
  ~~**Bug report drafted for claritise to send** (2026-09-27); not yet reported.~~ **Reported 2026-09-27** by claritise
  on Lexirise's Discord (the short version: 見くらべた, 笑い出した, 走り続けた, and the ones that come back right).
  **Reply the same day** (Lexirise's bot): logged with the earlier analysis reports, "見くらべた should resolve to
  見くらべる, not 見る". The reply names only 見くらべた; the aspect compounds (〜出す, 〜続ける, 〜始める, 〜すぎる) aren't
  confirmed as bugs yet. The post also said the dictionary has 見くらべる (1005346382); that entry was likely created
  on demand by a lookup (above), so it isn't evidence the spelling was known.
- **`dictionary/lookup` creates an entry for a word it doesn't have** (seen 2026-09-27): 書きこむ and とり出す (mixed
  spellings) came back with new ids (1005679463, 1005679465) and `status: "pending"`, no translation yet; 見くらべる's
  id 1005346382 is in the same range, so it was likely generated on demand too. So a lookup can add to Lexirise's shared
  dictionary; a card should treat `pending` as "no meaning yet".
- **Sense order can lead with a rare sense:** 行く's meanings came back as "die; go" (die first: 逝く's sense).

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

## Reported to Lexirise (2026-09-27, by claritise)

Compound verbs whose lemma is the first verb ("A compound verb's lemma can be its first verb", above). Logged by
Lexirise the same day; confirmed for 見くらべた.

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
Works like the app's Context tab. ~~Not tested from here yet: whether it gets 四月一日 → tsuitachi,
一枚上手 → uwate, 长得 → zhǎng.~~ **Superseded 2026-09-30:** measured, below.

#### analyze/context, measured (2026-09-30, dev key, read-only)

`tools/lexirise/probe_v10.py`: nine sentences written for it (raw in `research/v10/`). Every answer had `meaning`,
`conciseMeaning` and `reading`, in 2-6 s.

- **The reading is wrong where it matters:** 一日 in 四月一日 → `ichinichi` (the lookup's default, `tsuitachi`, was
  right); 上手 in 一枚上手 → `jouzudatta`; 长 in 长得 → `cháng`. Right on the easy ones (教室 `kyoushitsu`, 长 in 这条路很长
  `cháng`, 银行 `yínháng`).
- **The reading is the tapped token's, inflected:** 上手だった → `jouzudatta`, 気づいた → `kidzuita`, 一日中 →
  `ichinichijuu`. Romaji or pinyin with the dev key.
- **The short meaning fits the sentence:** 一日 in 四月一日 "the first day of the month", 上手 in 一枚上手 "had the upper
  hand", 长 in 长得 "to grow; to be (of appearance)", 长 in 这条路很长 "long". The lookup's first sense for 长 is "grow,
  develop" either way.

### `morphoPending` (documented 2026-09-25)

"When true, the response has fast tokens only. Call again later for grammar and refined segmentation."
So the first answer's word boundaries **can change** on the second call, not only its grammar. The v0.1
client uses the first answer and doesn't call again (`../v0.1/lexirise-client.md` §2), so today's card
may show a rough split. v0.2 C19 has the plan. How long "later" is isn't documented: measure it.

- **Earlier notes, now superseded:** "a grade-only endpoint, no follow-up needed" (the API takes
  `reviewedAt` after all); "not idempotent: never resend" (reviews are deduped by `id`).
- **Nothing gets better on the device by itself** except fixed server data. Grammar, contextual
  meaning and reviews each need firmware work (v0.2 C10, C11, C19).

## Decks (v0.2 V3, read and measured 2026-09-26)

**From the reference** (`/api-reference`, 2026-09-26):
- `POST /v1/decks`: required `title`, `language`, `unit_type` (`word` or `sentence`); optional `description`,
  `parent_deck_id`, `position`, `deck_type` (`snapshot`, the default, or `dynamic`), `rule_type`
  (`saved_vocab_query` or `user_tag_filter`), the dynamic filters `proficiency`, `part_of_speech`, `system_tags`,
  `user_tags`, and for snapshots `saved_expression_selection` / `import_items`. Returns `{ success, deck }`.
- `GET /v1/decks?language=`: `{ decks }`, the decks the user owns **or has starred**. No paging documented.
- `GET /v1/decks/{id}` (`limit`, `offset`, `maxRank`): `{ success, deck, items, totalCount, hasMore }`, **404 for an
  unknown id**. Deck fields named: `id`, `title`, `deck_type`, `unit_type`, `rule_type`, `user_tags`.
- **A deck is words or sentences**, one `unit_type`; no mixed mode is documented. So C3's sentence saves would need
  a second deck per book (`unit_type: "sentence"` on the same tag).
- No limits on a title's or a tag's length, or on the number of tags, are documented.

**Measured live (read-only, the dev key; no deck was created):** `GET /v1/decks`, with `?language=ja`, `=zh` and
none, answered 200 with a 12-byte body, the size of `{"decks":[]}`: the account has no decks, so a listed deck's
real shape is still unseen. `GET /v1/decks/999999999` answered **404** `{"success":false,"error":"Deck not found"}`.
Responses kept in `research/decks/` (gitignored).

**Open (check on the device, V3's gate, with claritise's OK):**
- The field names of a listed deck (the reference's snake_case; `GET /v1/study/summary` lists decks in camelCase,
  `deckId`, `unitType`), and whether `user_tags` comes back on a dynamic deck. V3 finds the book's deck by its tag,
  else by its title, so a list without `user_tags` still finds it by title.
- Whether a dynamic `user_tag_filter` deck fills with words saved **before** it was made (it should: it's a rule).
- Whether `GET /v1/decks` honours `?language=`, and whether a listed deck says its language (read as `language`,
  `lang`, `source_lang`/`sourceLang`, `source_language`/`sourceLanguage`): a deck that says another language is
  never taken, since a book's ja and zh decks share title and tag.
- Whether a listed deck says whose it is (read as `owned`, `isOwner`/`is_owner`, `isOwned`/`is_owned`) and whether
  it's starred (`starred`, `isStarred`/`is_starred`): ownership decides when said, else a starred deck counts as
  someone else's (the list holds starred decks too).
- Whether `GET /v1/decks` pages (`hasMore` / `nextOffset`, as `GET /v1/vocabulary` does): a list that says so
  never counts as "the book has no deck", and neither does a `totalCount` / `total_count` above the entries
  that came. Only top-level fields are read: paging in a nested object (a
  `pagination: {…}`) would go unseen, so the device check must look at a real list's top level.
- Whether a deck's title has a length limit, and whether a starred deck of someone else's can carry our title.
- **The request's names are the reference's snake_case** (`unit_type`, `deck_type`, `rule_type`, `user_tags`). If
  the server wants other names it may make a plain snapshot deck instead; the device logs a warning when the
  creation's answer says a type or rule other than `dynamic` / `user_tag_filter`. The parsers read snake_case and
  camelCase, and log an unreadable answer's first bytes (`LXDECK`).
- Deleting decks: `DELETE /v1/decks/{id}` isn't used; a deck the user deletes is found missing (404) and made again
  on the book's next save. A user who doesn't want it turns **Deck per book** off.

## How `analyze/text` splits conjugated verbs (v0.2 V4, measured 2026-09-27)

Why: C16's namer (`../v0.2/00-overview.md` C16) names a form from the occurrence it's handed (`word`, `lemma`) and
the page's next character. Rounds of V4's review kept guarding against splits nobody had seen (書け · ない named
"imperative"), so this measures them. `tools/lexirise/probe_splits.py` (read-only, dev key, run from the Mac;
raw responses in `research/v4-splits/`, gitignored) sent 28 Japanese sentences written for the probe, each holding
one risky form, and recorded the occurrences covering it in the first answer, the refined answer (polled until
`morphoPending` was false) and the `fast: true` answer on the refined sentence. Five more sentences with する verbs
were sent once, by hand; they're the tool's last five sentences now, so a rerun measures them the same way. The real namer (`conjugationOf`) was then run on each measured token with its next character.

- **Conjugated verbs come back whole,** with the plain verb as the lemma, in the first, refined and `fast`
  answers alike: 書けない‹書く›, 書けば‹書く›, 書こう‹書く›, 書けず‹書く›, 食べたがる‹食べる›, したがる‹する›,
  見ていたかった‹見る›, 食べちゃう‹食べる›, 着いたら‹着く›, 読んだり‹読む›, 書け‹書く› before ！, 食べろ‹食べる›,
  and a six-step 食べさせられていませんでした‹食べる›. **None of the feared stem splits (書け · ない, 書け · ば,
  書こ · う, 食べ · たがる) happened.** The potential's lemma is the plain verb (書ける‹書く›), which is what C16
  assumes.
- **Some tokens are longer than the verb:** 食べないで‹食べる› (first and refined), 行けそうだ‹行く› (refined) and
  降るかもしれない‹降る› (first answer, already refined: seen before). They go unnamed or get a longer form's name;
  no wrong label.
- **Splits did happen, just rarely:** 行けそう was 行け‹行く› · そう in the first answer and in `fast` (the next
  character そ leaves 行け unnamed, C16's follower rule: the defence is needed); 勝てっこない came back as
  勝 · てっこない‹てる› (a bad split; neither piece gets a name). `fast` on the refined sentence cut
  降るかもしれない to …しれない. **So the namer's guards against a cut stem stay:** they fire rarely, and when they do
  they only drop a name.
- **The bare potential stem is real:** 日本語が話せ、 came back as 話せ‹話す›, the case V4 round 16 fixed (no name
  before 、).
- **The lemma can change between passes:** なれない was ‹なれる› first and ‹なる› refined. The namer names it
  "negative" and "potential negative" respectively: both right.
- **A する verb's lemma is the noun:** 勉強した‹勉強›, 勉強しています‹勉強›, 電話して‹電話›, 心配させる‹心配›,
  結婚している‹結婚›, 掃除させられた‹掃除›. Generating forward from 勉強 finds nothing, so every する verb went
  unnamed (V4 R19 names them from noun + する).
- **Nouns and する, measured 2026-09-27** (first and `fast` answers agree; these sentences are in the tool too):
  - a noun that isn't a する verb isn't merged with what follows: 二人 · して‹する› · 笑った, 皆 · して‹する› ·
    騒い‹騒ぐ› · だ, やっと · 彼氏 · できた‹できる›, 友達 · できた‹できる›;
  - a real する noun is: 勉強できる‹勉強› (so the namer's noun + する gate keeps でき);
  - one-kanji verbs come back with their own lemma: 愛した‹愛す› (not 愛), 話した‹話す›, 貸して‹貸す› (and
    お金‹金›), 出した‹出す›; so the gate never takes a one-kanji lemma for a する noun;
  - 食べないです is 食べない‹食べる› · です‹だ›: 食べない before で is left unnamed (it could be 食べないで cut short;
    one character can't tell), no wrong label; もう書いたらしい comes back whole, 書いたらしい‹書く› (unnamed).
  - **The refined pass on the same sentences** (re-run the same day with `probe_splits.py`'s `probe()` on its
    last sixteen sentences; every one came back already refined, within 2 s): no new merges, and the same tokens
    as the first answer except two. 愛した's lemma became ‹愛する› (named "past" either way); 騒いだ was whole
    (騒いだ‹騒ぐ›, "past") where the first answer had 騒い · だ, while `fast` still splits it 騒い · だ, which is
    what the card rejoins with (v0.2 V1), so the card shows 騒い unnamed. No wrong label on any of them.
- **Timing:** the refined pass arrived 34–195 s after the first call (two sentences were already refined; one,
  勝てっこない, was still pending at 180 s: the run used `--wait 180`; the tool's default is 150).
- **Result on the card, measured tokens through the real namer:** no wrong label. Named: 書けない "potential
  negative", 書けば "-ba conditional", 書こう "volitional", 食べたがる "-tagaru", 食べないで "negative te-form",
  着いたら "-tara conditional", 書け before ！ "imperative", 呼ばれた "passive past", 寒くなかった "negative past".
  Unnamed (names lost, allowed): 書けず, 見ていたかった, 読んだり, 食べちゃう, the six-step form, 行けそうだ,
  降るかもしれない, the する verbs (before R19).
- **Still for the device:** these are the server's answers; whether the card's offsets and next character line
  up with them on a real page is V4's device check (`../v0.1/device-checks.md`).

## V7's foundations, measured (2026-09-28, read-only)

`tools/lexirise/probe_v7.py` (dev key, from the Mac; raw in `research/v7/`, gitignored), for
`../v0.2/page-annotations.md` §1:

- **`GET /v1/vocabulary` items are large:** each embeds its `dictionary_entry` (with morphology, rank, system tags)
  beside the SRS fields, `notes`, `user_tags`, `sentence_text`, media URLs: **~6.8 KB per item** (the dev account's
  Chinese list, 102 items, 690 KB in 2.7 s; the Japanese items were ~1.8 KB). A 1000-word vocabulary is ~7 MB for
  the first full sync, ~1.35 MB per 200-item page. So the vocab mirror must stream-parse and keep only its few
  fields (entry id, proficiency, saved id, suspended, next review, ~~seen~~), never hold a page in memory. (`seen`
  struck 2026-09-28: V7a keeps no seen count, nothing reads it: `../v0.2/page-annotations.md` §1.2.)
- **Sorting for the incremental sync works:** `sortId=updated_at&sortDesc=true` returns newest first. **Paging:**
  `offset`/`limit` with `nextOffset` (null on the last page).
- **Which ids (read from the same answers, 2026-09-28, for V7a):** an item's `id` is the number `stateByEntryId` calls
  `saved_expression_id`, and its `dictionary_id` is the entry `stateByEntryId` is keyed by (`entry_id`; equal to the
  embedded `dictionary_entry.id`), both JSON numbers; `context_lemma_entry_id` was null on most items, so it isn't
  used. Every item has `unit_type` (`word` on all of the dev account's), `updated_at` and `next_review_at` as ISO 8601
  UTC with milliseconds (`2026-09-24T06:11:34.058Z`) or null. Items at level 0 are listed (the V5 probe's removed
  words: a dictionary word's `DELETE` keeps the item, "Suspended (Ignore)" below). ~~**Open:** how items sharing an
  `updated_at` are ordered across `offset` pages (`probe_v7.py --ties`, read-only, not yet run).~~ **Measured
  2026-09-28** (`probe_v7.py --ties`, read-only): the dev account's Chinese list has 99 adjacent `updated_at` ties in
  102 items, and paging it gives the same order as one page, so ties are ordered stably across `offset` pages. Not
  measured: whether `updated_at` follows commit order (V7a's cursor assumes it; a change stamped earlier than one
  already read is caught only by the weekly full pass; since V7b R5 it also makes a card on a page analyzed before the
  missed change show the word unsaved until a later answer or that pass: `../v0.2/page-annotations.md` §1.1 Known
  limits). **Open measurement** (V7b R6): save or change a word in the app during a long-running incremental pass,
  or two in quick succession from two clients, and compare their `updated_at` with the order the list returns.
- **Page-sized `analyze/text`:** ~160–260 bytes of answer per character (128 chars: 26 KB in 1.4 s; 256: 44 KB in
  2.0 s; 512: 81 KB in 2.7 s; 186 Chinese chars: 48 KB in 3.0 s), all first-pass (`morphoPending: true`) for new
  text. ~~A ~300-character page is ~50 KB and 2–3 s, which a prefetch during reading covers.~~ (Superseded
  2026-09-28: 76–93 KB and ~1.9 s for 300–380 units with new text, "Page analysis (V7b), measured" below; the
  repeated paragraph here was partly seen text.) (The 2026-09-24 figure of
  ~70 B/char was the 20,000-character test, where the per-answer overhead is spread thin.)

## Page analysis (V7b), measured (2026-09-28, read-only)

`tools/lexirise/probe_v7b.py` (dev key, from the Mac, a fresh connection per call; raw in `research/v7b/`, gitignored),
on two pages written for it (Japanese 382 UTF-16 units, Chinese 301; each starting and ending mid-sentence), for
`../v0.2/page-annotations.md` §1.1 "V7b design":

- **Size and time (default mode, first pass):** Japanese 76 KB (~200 B/unit), Chinese 93 KB (~310 B/unit: the dev
  account has saved Chinese words, and `stateByEntryId` held 28 of them); **~1.3 s to the first byte, ~1.9 s to the
  end**. Occurrences are most of it (62 of 76 KB; 71 of 93 KB), then `entryMetaById`. The pages held 206 and 220
  occurrences (177 and 193 word-like), 107 and 129 entries. Keys arrive in the order `occurrences`, `grammar`,
  `grammarStates`, `morphoPending`, `entryMetaById`, `stateByEntryId` (every answer). `fast: true`: 43 and 56 KB,
  ~1.7–1.8 s, the same split. The same page again seconds later: still a first pass, the same split.
- **A sentence alone splits as the page does:** each of the pages' 26 sentences, sent alone (as the card's ① sends
  it), gave exactly the page's occurrences over its span (words, spans, entries, lemma entries), the cut first and
  last sentences included.
- **The refined pass at page scale:** the Japanese page refined by ~93 s, the Chinese by ~140 s (polled every 20 s),
  with grammar (16 and 42 items). 13 and 18 first-pass tokens changed. `fast` on the refined page gave the first
  answer's split again; V1's rule (a ranked `fast` word over several refined tokens) put back 5 and 14 whole words
  (小さな, 一気に, 九時, 一度; 深深, 长得, 陆陆续续, 空荡荡, 一口气, …).
- **Rate limit:** `/v1/me` says `rateLimitMax: 1200`, `rateLimitTimeWindow: 3600000`; no answer carries rate-limit
  headers, so the device can count only its own requests.

## Caches (V7c), measured (2026-09-28, read-only)

`tools/lexirise/probe_v7c.py` (dev key, from the Mac; raw in `research/v7c/`, gitignored), for `../v0.2/00-overview.md`
C21 "V7c design". The Mac's round trip to the server is ~0.35 s (a `HEAD /` on a kept-alive connection), so the Mac's
times are mostly network, not the server's work.

- **`dictionary/lookup` (phase B):** 18 words written for the probe (10 Japanese, 8 Chinese, common and rare): answers
  of 0.5–1.3 KB (median 0.7 KB), all `translation_status: ready`. What the card keeps of one (`api::LookupResult`:
  the word, reading, two senses with their part of speech, level, rank, frequency) is 39–140 bytes of text (median
  ~90). On a kept-alive connection a lookup takes 0.35–0.40 s, one round trip: the server's own time is a few tens
  of ms at most, and the same word asked again takes the same. A fresh connection adds ~0.66 s from the Mac (TCP and
  TLS: two round trips). On the device a warm call was ~370 ms (P1's soak) and a new session 2.5 s
  (`../v0.1/device-checks.md`).
- **Answers change a little over days:** 16 lookups kept from 2026-09-27 (`research/v4`) asked again: the same ids;
  ranks moved by under 1% on 13 of them (the dictionary grows), the frequency score on one; one word's senses were
  rewritten (笑う: "laugh" · "to laugh, to smile" became "to laugh, to smile" · "to ridicule, to make fun of"); the two
  entries created `pending` by a lookup the day before (書きこむ, とり出す: "dictionary/lookup creates an entry" above)
  were `ready` with meanings.
- **TLS:** TLS 1.3 (`TLS_AES_256_GCM_SHA384`), nginx on one address. The leaf's key is P-256 (its CertificateVerify is
  `ecdsa_secp256r1_sha256`); the chain's signatures are P-384; the Certificate message is 3.4 KB. **The server hands
  out session tickets and accepts them:** two NewSessionTickets after every handshake (a resumed one too), lifetime
  hint 86400 s, each ticket 32 bytes (an id into the server's session cache, not a self-contained ticket). Resumed
  8 of 8 right after, the same ticket twice, and one ticket each after 6, 16, 31 and 61 minutes (`openssl s_client
  -sess_out` / `-sess_in`): every one resumed. A resumed handshake
  sends no Certificate and no CertificateVerify, and still exchanges an X25519 key share (`psk_dhe_ke`); early data
  is off (max early data 0). From the Mac a resumed handshake takes as long as a full one (~0.33 s, one round trip),
  so what resumption saves on the device is the CPU time of parsing and verifying the chain (three P-384 signatures
  and the P-256 CertificateVerify), which only the device can time.
- **Keep-alive:** an idle HTTP connection stays open 70 s and is closed by 80 s (nginx's default is 75 s).
- **Lemma repetition over a real text:** the manga spike's volume (`manga.md` §4: 178 pages of `analyze/text`
  answers, private, in `research/spike/`), Japanese dialogue: 10,272 word-like occurrences of 2,233 lemmas. Counting
  by lemma entry, and taking the words a reader taps to be those ranked above 3000 (or unranked: 2,563 occurrences,
  1,477 lemmas), the share of taps that meet a lemma already looked up in the volume is 42% if the reader taps every
  occurrence of such a word, 27% if they tap half the later ones again, 16% if a quarter; within 35-page windows
  (a chapter's size) 23–35%, 13–21% and 7–12%. Ranked above 10000: 40%, 25%, 14%. **All words** (stepping on a card
  goes word by word): 78% over the volume, 48–69% per window. So most taps on rare words are a first meeting, and a
  cache kept across chapters (and books) pays about half again what one chapter's does. Not measured: prose, Chinese,
  and what readers really tap (the device log will say).

## Readings and counts for V6's card additions (measured 2026-09-28, read-only)

Dev key, from the Mac; raw in `research/v6/` (gitignored).

- **`multipleReadings` on `dictionary/lookup`** is `{primary, alternatives[], hasMultiple}`, plus for Chinese
  `frequencies[]` (one per alternative) and `primaryFrequency`. **Chinese polyphones have it, in pinyin with tone
  marks:** 长 cháng, alternatives zhǎng; 行 xíng, háng; 得 dé, de and děi. **Japanese alternatives are romaji only**
  (一日: primary tsuitachi; alternatives ichinichi, ichijitsu, tsukitachi, hitohi, ippi), so a card showing kana has
  to convert them (V6's "also" reading). (2026-09-29: `analyze/text` came back in kana on claritise's account, "Japanese reading" above, so alternatives may too: take either with
  `text::japaneseReading`, `../v0.1/languages.md` §3a.)
- **`GET /v1/vocabulary?language=…&limit=1`** answers `languageCount`, `totalCount`, `nextOffset`, `availableTags`
  (and the items). A `unit_type` query parameter is ignored (the same counts, the same item). ~~Whether
  `languageCount` counts sentence cards is still open: the dev account has none to compare (V6's C7 check).~~
  **Answered 2026-09-28 (a write on the dev account, claritise's OK: "yes keep going"; undone):** saving one sentence
  card took `languageCount` 2 → 3 while `totalCount` stayed 2, so **`languageCount` counts sentence cards and
  `totalCount` doesn't** (C7's "words in Japanese" is `totalCount`, or says "cards").
- **The same sentence saved twice** (`POST /v1/vocabulary`, `mode: "sentence"`): the second answers `result.status:
  "updated"`, `reason: "already_exists"`, with the **same item**, and replaces it like a word's re-save (tags, notes,
  proficiency overwritten). So a sentence save must check first, as a word's does. **A sentence card's `DELETE`**
  answers `deleted: true` and the item is gone (404), unlike a dictionary word's.

## Levels for A5 (V9b), measured (2026-09-29, read-only)

`tools/lexirise/probe_v9b.py` (dev key, from the Mac; raw in `research/v9b/`, gitignored; 237 calls), for
`../v0.2/page-annotations.md` "V9b design" (A5, marking only words above a JLPT / HSK target).

- **`analyze/text` still carries no level:** `entryMetaById` has `entryId`, `rank`, `frequencyScore`,
  `partOfSpeech`, `transliteration`, `status`, `subTokenCount`, `lang`; occurrences nothing level-like either. The level
  is only in `dictionary/lookup`'s `system_tags`, one word per call (the reference has no batch lookup).
- **Vocabulary items carry the level at their top level:** `dictionary_entry_system_tags` (equal to the embedded
  `dictionary_entry.system_tags` on every item read), so the vocab mirror could keep a saved word's level from a field
  its visitor already sees. (The reference's text says items don't show system tags; they do.) Only saved words, though.
- **Coverage** (every word-like entry of V7b's two probe pages, looked up by its lemma): Japanese 68 of 105 entries have a
  JLPT tag; the 37 without are particles and endings (の, を, に, ながら, く) and compounds (港町, 坂道). Chinese 101 of 129 have
  an HSK tag; the 28 without are mostly compounds (老街, 店铺, 屋檐, 陆陆续续). Every lookup's entry was the page's lemma
  entry (105 of 105; 127 of 129), so a level could be kept by entry id.
- **Rank as a stand-in for the level:** JLPT tags follow rank loosely (N5 words' ranks 32-18,050, median 1,627; N3's
  median 141; N2's 3,875): the best rank cut agrees with "above N3" for 74% of the tagged entries (3,000: 17 above marked,
  9 below marked, 9 above missed). HSK follows it better: 91% for "above HSK 3" (cut 3,000), 92% for HSK 4 (5,000) and 5
  (12,000).
- **What a lookup per word would cost:** a page's distinct words are 105 (the Japanese probe page) and 129 (the Chinese);
  the manga volume's pages 43 on average (`research/spike`, 177 pages), and its 2,233 lemmas over 177 pages are ~12.6 new
  lemmas a page. At 60 pages an hour that's ~750 lookups an hour for the manga alone, and several times more for prose,
  beside the page analysis, against the key's 1200: not affordable.
- **What a rank cut would leave marked** (distinct entries rarer than the cut, per page): at 3,000, 39 of 105 (Japanese
  page), 42 of 129 (Chinese page), 10 of 43 (manga pages).

## Suspended (Ignore), measured 2026-09-27 (v0.2 V5)

`tools/lexirise/probe_suspend.py` (writes to the dev key's account, with claritise's OK: "ok"; raw in `research/v5/`,
gitignored), on the throwaway word 蓋然性 (saved) and 寸暇 (unsaved):

- **A saved word:** `PATCH /v1/vocabulary/{id}` `{"suspended": true}` → 200, the item echoes `suspended: true`;
  `{"suspended": false}` undoes it.
- **`analyze/text` doesn't show it:** a suspended word's `stateByEntryId` state is the same four fields as any saved
  word's (`entry_id`, `proficiency`, `saved_expression_id`, `seen_count`); `GET /v1/vocabulary/{id}` has
  `suspended`. So the card learns a word is ignored only from the saved item (V4b's call).
- **A save can't set it:** `POST /v1/vocabulary` drops an unknown `suspended` (`item.suspended: false`). A save needs
  `proficiency` 1–4: `0` is a 422 (`{"type":"validation","on":"body",…}`).
- **An unsaved word** (a name): save it (`proficiency: 1`, `result.status: "added"`), then `PATCH suspended: true`.
  It then shows in `analyze/text` as saved at level 1, not as ignored.
- **`DELETE` keeps `suspended`:** a dictionary word's `DELETE` answers `{success: true, deleted: false}`, resets it to
  proficiency 0 and **leaves `suspended: true`**. Undoing an ignore is `PATCH suspended: false` first, then the
  `DELETE` if the word was saved only to be ignored (if Ignore wrote to Lexirise; it doesn't: `../v0.2/00-overview.md`
  C17 "As built (V5, local)").
- Left in the dev account: 寸暇 at proficiency 0, not suspended; 蓋然性 back as it was.

## A saved word's notes and tags in `analyze/text` (v0.2 V4, measured 2026-09-26)

The brief's example (`../v0.1/context-brief.md`) shows `stateByEntryId[id]` with `notes` (a string or null) and
`user_tags` (objects `{id, name}`), beside `saved_expression_id`, `entry_id`, `proficiency`, `expression_text`,
`seen_count`, `updated_at`, `images`. **Live, read-only (the dev key, `analyze/text` on a sentence holding the dev
account's one saved word, whose notes and tags had been cleared):** its state held only `saved_expression_id`,
`entry_id`, `proficiency`, `seen_count`: no `notes` or `user_tags` keys at all, not even as null. So either they're
left out when empty, or the analysis doesn't carry them any more. `dictionary/lookup` carries no saved state.
Responses kept in `research/v4/` (gitignored). ~~**Open (V4's device check):** save a word from a book, then look it
up in another sentence: does its state carry `notes` and `user_tags`, and in which shape? V4 reads the documented
shape (and plain-string tags); without them the card says "First time you've met this word." as before.
~~ **Answered 2026-09-27: `analyze/text` never carries them.**
The dev account's throwaway word 蓋然性 was given a note (`PATCH /v1/vocabulary/{id}` `{"notes": …}` → 200, the
item echoing the note), then `analyze/text` on a sentence holding it: its `stateByEntryId` state still had only
`entry_id`, `proficiency`, `saved_expression_id`, `seen_count` (no `notes`, no `user_tags`); the note was cleared
again after. On the device the same day, 和子 saved from one sentence (the note sent) and looked up in another showed
"First time you've met this word." (`../v0.1/device-checks.md`). **Where they are:** `GET /v1/vocabulary/{id}`
(read-only; the id is the state's `saved_expression_id`) returns the item with `notes`, `user_tags` and also
`sentence_text`, beside the SRS fields. So Met before needs that one extra call for a saved word (v0.2 V4's fix,
`../v0.2/00-overview.md` C14).