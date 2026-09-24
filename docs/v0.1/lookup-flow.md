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
  std::string_view sentence;   // UTF-8, ≤ 120 codepoints (D5)
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
