# Lookup flow: long-press to card

**Status:** proposed 2026-09-24. Decisions D3, D4, D6, D12 in `00-overview.md`.

Related: `sentence-extraction.md` (step 3), `lexirise-client.md` (steps 4–5), `popup-ui.md`
(steps 6–7), `offline-and-errors.md` (every failure branch).

---

## 0. What exists today (CrossPoint 1.6.5)

```
EpubReaderActivity
  ├─ Confirm long-press (setting: Long-press Menu = Dictionary) ─┐
  ├─ Reader menu → Look Up ──────────────────────────────────────┤
  └─ Home button action = Dictionary ────────────────────────────┤
                                                                 ▼
DictionaryWordSelectActivity(page, margins)
  extractWords(): one WordBox per selectable TextBlock token on the page
  Left/Right/Up/Down move · touch-down moves · TAP → performLookup()
  performLookup(): Dictionary::lookup(word) ──► DictionaryDefinitionActivity (full screen)
                                          └─► "Not found" / error popup (1.5 s)
```

Two facts shape everything below:

- **A Japanese token is one character.** `ParsedText` splits CJK runs at every break opportunity
  (`cjkCharacterBreakByteOffsets`), so each kanji and kana is its own `WordBox`. Tapping 食 in
  食べる selects 食.
- **Word select holds the whole `Page`** (`std::unique_ptr<Page>`), with every line's `TextBlock`.
  That is enough to build a sentence without going back to the EPUB.

## 1. Entry: long-press on the page

New: **touch long-press on the reading page** (`MappedInputManager::wasScreenLongPress(x, y)`,
already implemented, unused by the reader) opens `DictionaryWordSelectActivity` with an
**initial touch point**. On entry, word select picks `wordAt(x, y)` and **looks it up straight
away**, with no second tap. If the press lands on no word, it opens as normal with the highlight at
the nearest word.

**On the X4 Pro this is the primary entry point** (D15): the device has no Confirm button, so the
upstream "hold Confirm" entry doesn't exist there. The other existing entry points (reader menu →
Look Up, the Home action) still work and go through the same provider chain.

Needs a constructor overload (`initialTouch`) and a branch in `EpubReaderActivity::loop()`, where
screen long-press is checked before the page-turn tap zones. Watch out for conflicts with existing
touch gestures (tap zones for page turns, a long-press that might already mean something in a
future upstream). Check `wasScreenLongPress` for suppression of the release that follows it, the
way `wasLongPressed` does for buttons.

## 2. Selection → tapped character

`performLookup()` knows `words[selected]`. We need two things from it:

- **The tapped token's position in the page's text**, so the sentence builder can find it. That
  means the `(line index, token index)` pair, not the `WordBox` alone. Store both indices in
  `WordBox` during `extractWords()` (it already iterates them).
- **Whether the token is CJK.** If the token is Latin (loanwords, romaji, English books), the token
  *is* the word, and the flow still runs through `analyze/text` so saved state works, just with a
  trivial match.

## 3. Sentence and tap offset

`SentenceBuilder::build(page, lineIdx, tokenIdx) → { text, tapOffset, tapLen }`. The full rules
are in `sentence-extraction.md`. The output is UTF-8 text, plus the tapped token's offset **in the
unit Lexirise uses for `charStart`** (H5). The offset is converted once, here, so nothing
downstream deals with units.

## 4. The provider chain

```cpp
// src/lexirise/LookupProvider.h
struct LookupRequest {
  std::string_view sentence;   // UTF-8, ≤ 120 UTF-16 units (D5)
  uint16_t tapOffset;          // in Lexirise's charStart unit
  uint16_t tapLen;
  std::string_view tapToken;   // the raw tapped token, for StarDict
};

enum class LookupOutcome : uint8_t { Card, NotFound, Unavailable };

class LookupProvider {
 public:
  virtual ~LookupProvider() = default;
  // Unavailable = "try the next provider" (no WiFi, timeout, 5xx, 429, no key).
  // NotFound    = "this provider answered: no such word". The chain stops here.
  virtual LookupOutcome lookup(const LookupRequest&, LookupCard& out, ProgressFn) = 0;
};
```

Chain order: **Lexirise → StarDict**. If Lexirise returns `Unavailable`, StarDict runs. The card
shows a small `offline` mark, and StarDict opens its existing `DictionaryDefinitionActivity`. We
**don't** squeeze StarDict HTML into the card. If Lexirise returns `NotFound`, StarDict does
**not** run. Lexirise's lemmatiser is better than StarDict's English-only stemmer, so a Lexirise
miss is final. (Revisit if people read books where StarDict has specialist dictionaries.)

StarDict for Japanese and Chinese needs a word, not a character. The folder is chosen per language (`stardict_ja` / `stardict_zh`, `languages.md` §4). `StarDictLookupProvider` tries **longest
prefix first**: starting at the tapped character, it looks up the longest run of up to 8 CJK
characters on the line, then 7, … then 1. That's at most 8 index probes, which is cheap with the
`.qidx` sidecar. The StarDict code isn't modified. This is the usual approach for JMdict StarDict
dumps.

## 5. The Lexirise provider

```
WifiSession::ensureUp()                     (D10; Unavailable if it fails within 6 s)
open TLS session to api.lexirise.app        (one per lookup, keep-alive, D6)
① POST /v1/analyze/text {text: sentence, language}   (language per book, languages.md §1)
   stream-parse → occurrences[] (only the fields we need) + the entryMetaById and
   stateByEntryId entries for ONE entry (see ②)
② pick the occurrence: charStart ≤ tapOffset < charEnd, isWordLike = true
   none → retry with the nearest word-like occurrence on the tapped side; still none → NotFound
   → card.word, reading, POS, entryId, lemma, lemmaEntryId, saved state
   → PROGRESS: render the card now (popup-ui.md §2, phase A)
③ POST /v1/dictionary/lookup {text: lemma, language}   (same session)
   → translation (first 1–2 senses), rank, frequency_score, translation_status
   → PROGRESS: fill the card (phase B)
close session
```

**Which entry's state counts as "saved"?** Check the **lemma's** entry (`lemmaEntryId`) first,
then the surface entry (`entryId`). A learner who saved 食べる should see "saved" when tapping
食べた. Both IDs are kept on the card so Save can target the right one (D9/H2).

**Parse only what we need.** `analyze/text` returns every token in the sentence, with metadata
for each. The parser buffers `occurrences[]` as a compact array (word offsets, entry IDs, lemma
and reading as short strings). It keeps `entryMetaById[id]` / `stateByEntryId[id]` **only for
the chosen IDs**, and skips everything else while streaming. That needs `occurrences` to arrive
before the two maps. If the server doesn't guarantee key order, buffer the maps' raw bytes for
the needed IDs (`lexirise-client.md` §4).

### 5a. As built (P3)

What P3 shipped, where it differs from the plan above (code: `src/lexirise/lookup/`, tests:
`test/lexirise_lookup`):

- **No `LookupProvider` interface yet.** With two providers, the chain is written out in
  `DictionaryWordSelectActivity::performLookup()`. First `lexiriseLookup(card)`: `describeTap` →
  `lookupWithLexirise(api, TapContext, card)` → `LookupOutcome {Card, NotFound, Unavailable}` plus
  the `ApiError`. Then the pure `chainStep(outcome, starDictSet)` decides: `ShowCard`,
  `ShowNotFound` (a Lexirise miss is final), `RunStarDict` (the unchanged StarDict code with
  `starDictCandidates()`) or `NoDictionary`. The network sits behind `api::LexiriseApi` (`analyze`,
  `lookup`), which `LexiriseService` implements and a fake implements in tests. Add the provider
  interface when a third provider appears.
- **Lexirise isn't asked at all** when `TapContext` has no sentence or no language to send
  (Lexirise off, the language switched off, a non-CJK book). StarDict answers as before.
- **Match (②)**: `matchOccurrence()` takes the word-like occurrence covering the tap. If there is
  none (a tap on punctuation), it takes the **nearest** word-like occurrence on either side, the
  earlier one on a tie. Only a sentence with no word-like occurrence at all is `NotFound`.
- **Parsing keeps every `entryMetaById` / `stateByEntryId` entry** (up to `kMaxEntries` each, in
  maps; extra entries are dropped, not fatal), not only the chosen IDs. It is simpler, key order doesn't
  matter, and a sentence's worth is a few KB. `stateFor()` counts an entry as saved only when it
  has a `saved_expression_id`. The card takes the lemma's state, else the surface's.
- **`dictionary/lookup`** gives: reading (overrides the surface's), the first
  `kMaxTranslations` non-empty senses (≤ `kMaxTranslationBytes` each, cut on a character),
  `level` (the first `JLPT-N1..5` / `HSK-1..9` / `HSK-7+` in `system_tags`), `rank`, and
  `translationPending` (status isn't `ready`). If it fails, the lookup is **still a card**, with
  `translationUnavailable` set (word, reading and saved state, without a meaning). The lookup asks
  for the headword (the lemma, or the surface when the lemma is empty). Until it answers, the reading is
  the lemma's own `entryMetaById` reading (only when the server named a `lemmaEntryId`), or the
  surface's only when the surface is the headword, so 食べる never shows "tabesaserareta".
- **One blocking call, no phases yet.** The busy popup shows, both requests run in the activity's
  loop (≤ 2 × `kMaxCallMs`), then the placeholder opens (`DictionaryDefinitionActivity` with
  `LookupCard::headword()` / `plainText()`). Phase A/B rendering arrives with the card (P4/P5).
- **Long-press (§1)**: `EpubReaderActivity::loop()`, before link taps, peeks at the long-press
  (`MappedInputManager::peekScreenLongPress`, its touch-down point, as the page-turn zones use), then
  `takeLongPress()`: the lookup owns that zone (`lookupOwnsLongPress()`), and only then is
  `lookupsAvailable()` (which reads the settings) asked. Only then is it consumed (`wasScreenLongPress`). With
  CrossPoint's `longPressButtonBehavior` on in a tap mode (normal or inverted: a hold of ≥ 700 ms on a
  page-turn zone, acted on at release), the outer zones (`ReaderUtils::pageTurnZoneWidth`, shared)
  stay CrossPoint's and the lookup owns the centre. With it off, in swipe mode, or with touch controls
  off, the lookup owns the whole page. **If nothing can answer** (no StarDict dictionary, and Lexirise
  unusable for this book: off, no key, or the book's known language isn't sent, e.g. zh-TW or a
  switched-off language), the long-press isn't consumed, so a slow tap still turns the page or opens
  the menu. A book with no language tag, or a non-CJK one, still counts (its sentences decide:
  Japanese novels stamped "en" exist), so with a key and no StarDict a slow tap in an English book
  opens word select. `wasScreenLongPress` suppresses the rest of the contact, so the
  finger lift doesn't tap word select. Word select takes the point through `setInitialTouch(x, y)`
  (a setter, not a constructor overload), selects `wordAt(x, y)` and looks it up on its first
  `loop()` after the first render. A press on no word opens word select as usual.
- Word select now **opens without a StarDict dictionary** when Lexirise is usable (enabled and a key
  set, `lookup::lexiriseConfigured()`). If Lexirise then has no answer, word select shows "No dictionary
  set" (not a dictionary error).
- **StarDict**: `starDictCandidates()` gives the longest run of Japanese/Chinese word characters (Han,
  kana, ー, 々: `chars::isJaZhWordChar`) from the tapped character along the line
  (≤ `kStarDictMaxPrefixChars`, stopping at anything else or the line end), down to 1. Hangul isn't
  included (Korean is written with spaces, which the page model doesn't record), so a Korean tap is
  looked up as its token.
  `probeStarDict()` tries them in order, and a read error stops the probing. A token that starts with
  punctuation (「食) is looked up as it stands. **Not yet:** the per-language folders (`stardict_ja` / `stardict_zh`,
  `languages.md` §4). P3 uses the one dictionary chosen in CrossPoint's settings, and the folder
  choice comes with the device settings (P6).
- `Unavailable` is only logged (`LXLOOK`) for now. The `offline` mark and error UI are P6.

### 5b. As built (P5)

The card replaced the P3 placeholder (code: `src/lexirise/card/`, `src/lexirise/lookup/`; tests:
`test/lexirise_card`, `test/lexirise_lookup`):

- **Word select opens the card at once** (`openLexiriseCard()`): phase 0 (the tapped character, highlighted
  alone) is on screen while WiFi and TLS come up. The card (`LexiriseCardActivity` with a `LiveSource`)
  runs **one network call per loop pass**: `LiveSource::fetch()` makes it outside the render lock and
  changes nothing `render()` reads; `apply()` takes the answer under the lock; the render task draws it
  before the next call starts (`CardSession::shouldFetch`). Order: `analyze/text` (phase A), then the word on screen's
  `dictionary/lookup` (phase B), then queued saves. Phase A is always drawn before B is asked for, so the ~300 ms A/B merge of popup-ui.md §2 is the
  bench's only: a live B costs its own partial refresh (about 0.5 s).
- **The lookup is split** (`lookup::analyzeTap` → `cardFor` → `completeCard`): the sentence is analyzed
  once, every word-like occurrence becomes a phase-A card without asking again, and Left/Right re-run only
  `dictionary/lookup` for a word not yet looked up (§6). A phase-B failure still shows the word (no meaning,
  not retried by itself).
- **Not found / no answer**: the card ends and hands back through a shared `LiveOutcome`: word select shows
  "Not found", or runs StarDict on its next `loop()` (`runStarDict()`), or says "No dictionary set".
- **The page under the card** is the reader's own (word select's `Page::render`), with the word highlighted
  where it stands: `BuiltSentence::chars` maps every sentence character back to its page token, so a
  Lexirise range `[charStart, charEnd)` finds its boxes (`card::readerScene`, from `card::readerPageFor`'s
  snapshot). A reader in landscape gets the card in portrait over a blank page (the page was laid out for
  the other orientation; question 5 in the P4 ledger note); the word then counts as covered, so the card
  view shows its line in the strip.
- **Saving (§7, popup-ui.md §3.2):** T L F K sets the level on the card at once (with the toast) and queues a
  `LevelChange`. A change with an Undo toast waits out its window (`config::kToastMs`) before anything is
  sent, and a later change to the same entry merges into it: T then Undo sends nothing, K then T is one
  PATCH. The loop makes no call while a frame is on its way (`CardSession::shouldFetch`), so the toast is
  drawn before any network wait. `LiveSource` sends them in order: a new word's `POST /v1/vocabulary` (D9: the lemma, the
  first translation, the sentence as `notes`, the settings' tags, `proficiency` = the level, 1-4; it waits
  for the word's phase B, looking it up first if the card moved on; on a fresh session, since a POST isn't
  resent on a stale one), a saved word's `PATCH {proficiency}`, and a removal (⋯ Undo save, or an Undo after
  its window) as `DELETE`, plus `PATCH {notes: null, customTranslation: null, tags: []}` when this card
  saved the item (an item the user made in the app keeps its notes, translation and tags). A save whose
  word's lookup failed retries it once, so the POST carries the translation. The new item's
  `savedExpressionId` is kept for later changes. A refused or unanswered write puts the level back with
  "Save failed · Retry" (or the key-rejected / rate-limited toast, offline-and-errors.md §3a; even when the card has moved on) and drops the changes queued after
  it for that word. A tap on the level already set sends nothing (the double-press guard). After a removal,
  saving a word this card saved is a full POST again (tags, notes and translation); the user's own item
  stays in Lexirise at proficiency 0 with its notes, so a later level is a PATCH; a word that was already at
  proficiency 0 when the card opened (undone earlier) is changed with a PATCH, so notes and tags the user
  kept in the app aren't replaced. The same entry twice in a sentence is one word: its level and id are
  shared. Writes still queued when the card closes (or the device sleeps with it open) are sent first,
  without looking up anything they don't need; a removal whose clear fails still counts (logged). The log
  never shows a saved-expression id. The glue between input, card and network is `card::CardSession`
  (pure, host-tested through laid-out frames and taps).
- **Not yet:** the Kanji/Chars tab stays empty on a live card (Lexirise's `breakdown` would need a lookup per
  character: v0.2), the Form tab lists only the book's form, "Met before" is never filled, the Context tab
  has no page number, the ⋯ tab's v0.2 actions say "Not in this version yet", and "Saving…" isn't shown
  (the level shows at once instead). Errors beyond the save toast are P6 (`offline-and-errors.md`).

### 5c. As built (P7)

- **A long-press never also turns the page** (P3 already): the reader takes the long-press before its
  page-turn zones and consumes it, which suppresses the rest of the contact, so the lift isn't a tap. P7
  changed nothing there; the device check is owed.
- **Back by entry point** (`popup-ui.md` §3): word select remembers a long-press on a word opened it
  (`touchEntry`). Then any close of the card (Back, Home, ✕, a tap or swipe down outside it) goes straight
  back to the reader (`card::AfterCard::BackToReader`), and so does closing StarDict's definition. Opened
  from the menu, the card closes to word select, as before. A long-press on no word still opens word
  select as usual, and it behaves as menu-opened. For a touch-opened word select, a lookup that ends in a
  notice instead (Not found, No dictionary set, a dictionary error) also goes back to the reader once the
  notice has been read.
- **A long-press on another word while the card is open** (`popup-ui.md` §3.2): in card view, a
  long-press on the page outside the card closes it with that point (`Outcome::lookUpAt` →
  `LiveOutcome::lookUpAt` → `AfterCard::LookUpAt`), and word select looks up the word there on its next
  `loop()`. The card writes its queued saves first, as on any close. On the card itself or over the detail
  view (which covers the page) a long-press does nothing, and so it does when the reader's page isn't
  drawn under the card (a landscape book, whose page is laid out in other coordinates) and on the bench.
  The card always takes (consumes) a long-press, so the finger's lift is never also a tap on it. A long-press on
  no word closes the card as a tap outside would. An unsent save is said first (`UnsentSave`), and then
  the close carries on (`card::afterClosed`: the long-pressed word, or back to the reader).
- **Swipes** (`popup-ui.md` §3.2, deferred from P4): `CardController::swipe`: up on the card opens the
  detail view, down goes back to the card and from the card closes it, left / right step the detail view's
  tabs (stopping at Meaning and ⋯). A swipe only counts when it starts on the card
  (`handleInput` matches its start against the frame on screen, like a tap), at least
  `config::kCardSwipeEdgeMarginPx` (85 px, ~10 mm) inside the left, top and bottom edges, and when the
  SDK wouldn't read it as an edge gesture (`fui::edgeSwipe`, its own bands: a right swipe starting in the
  left quarter is Back, a down swipe from the top 14 % the frontlight panel, an up swipe from the bottom
  14 % the reader menu or Home). So a "previous tab" swipe must start right of x = 120
  (`swipeClearOfEdges`). Both ends come from `MappedInputManager::peekSwipe` (hook), and the direction is
  the SDK's dominant-axis rule. `lxctl card-gestures` drives them on the bench card.
- **Each language's own offline dictionary** (`settings.md` §1b): `lookup::chooseStarDict`. If the
  language's folder can't be opened (removed from the card while its row is hidden, say), CrossPoint's
  own dictionary answers instead. One settings copy serves the whole lookup (gate, sentence, card). The tap is
  only described (sentence and language) when Lexirise is asked or a language has its own dictionary, so
  a plain StarDict lookup stays upstream's. A language's own dictionary makes the long-press a lookup
  (`lookup::anyStarDict`) unless the book's override or metadata names the other language; an untagged
  or non-CJK-tagged book counts, as it does for Lexirise.

## 6. Left/Right on the card

The analyze response already covers the whole sentence. Keep the compact `occurrences[]` (plus
their `entryId`s) alive while the card is open. **Left/Right** moves to the previous/next
word-like occurrence. That needs its meta and state, which we only kept for one entry. There are
two ways to handle this:

- **v0.1:** keep meta and state for **every** occurrence in the sentence. A 120-codepoint sentence
  is roughly 40 tokens × ~100 bytes. That's ~4KB, which is trivial with PSRAM. Only
  `dictionary/lookup` is re-run for the new lemma.
- If memory ever matters (C3 port): keep only the chosen entry, and re-run analyze on move.

v0.1 takes the first. The page highlight follows the card's word.

## 7. Save

`POST /v1/vocabulary` over a **fresh** session (the lookup session was closed after ③, and the user
may take a while to press Save). The payload is in D9. If it succeeds, the card flips to "saved ·
tracked". If it fails, it shows an inline error with Confirm = retry (`offline-and-errors.md` §3).
The card never closes on its own after a save. The user dismisses it with Back.
