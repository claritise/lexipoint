# Sentence extraction: from a rendered page to a sentence plus a tap offset

**Status:** proposed 2026-09-24. Decision D5 in `00-overview.md`. **This is new code. CrossPoint
has no sentence extraction** (the brief assumed it did).

Related: `lookup-flow.md` §3 (the caller), `lexirise-client.md` §3 (what the server does with it).

---

## 0. Input

`DictionaryWordSelectActivity` owns the `Page`: a list of elements, of which `PageLine`s carry
a `TextBlock`. Each block exposes `wordCount()`, `wordText(i)` (a NUL-terminated token), styles,
and `getRubyTexts()`. Tokens are the layout's units:

- **CJK:** usually one character per token (`ParsedText::cjkCharacterBreakByteOffsets`). Some
  pairs stay attached where line breaking forbids a split (`isNoBreakBeforeCjkPunctuation`: 。、」
  stay glued to the previous character).
- **Latin:** one word per token, with a space between tokens.
- `TokenBoundary` bits record whether a gap is a real space, a CJK zero-width gap, or an
  attachment. **Rejoin with a space only where the original had one.** Joining Japanese tokens
  with spaces would break the server's tokenizer.

## 1. Output

```cpp
struct BuiltSentence {
  std::string text;      // UTF-8, base text only, ≤ MAX_SENTENCE_CP codepoints
  uint16_t tapOffset;    // tapped token start, in Lexirise's charStart unit (H5)
  uint16_t tapLen;       // tapped token length, same unit
  bool truncatedLeft;    // cut by the cap or the page top, not by a sentence end
  bool truncatedRight;
};
```

## 2. Rules

1. **Walk outwards from the tapped token** across `PageLine`s on the page, in reading order.
2. *(Punctuation is per language. The Japanese set is below; the Chinese set is in `languages.md` §2.)*
   **Stop left** after a sentence terminator: `。 ！ ？ ! ? ．` plus `…` when it is followed by a
   closing bracket or ends the paragraph. **Stop right** at the terminator, and include it along
   with any closing brackets or quotes straight after it (`」 』 ） 】 " '`).
3. **A paragraph end is a sentence end.** Blocks that don't flow into each other (a new `TextBlock`
   with a paragraph start) stop the walk. This matters for dialogue-heavy novels, where each 「…」
   is its own paragraph with no 。.
4. **Page bounded.** The walk stops at the page's first and last line and sets `truncated*`. v0.1
   doesn't read the neighbouring page. Chapter files are parsed per section, and loading the
   previous page costs an SD read and a layout pass, for a sentence that is only a little more
   complete. Revisit if field testing shows many truncated sentences.
5. **Cap at `MAX_SENTENCE_CP = 120` codepoints**, centred on the tap as far as the boundaries allow.
   Japanese sentences in fiction are mostly under 60. The cap guards against unpunctuated run-ons
   and poetry.
6. **Ruby is excluded.** `getRubyTexts()` is annotation, not text. Sending 漢字(かんじ) inline
   would corrupt tokenization. The server gives us the reading anyway.
7. **Normalise lightly.** Collapse runs of whitespace to one space (Latin only). Drop soft hyphens
   (U+00AD) and zero-width characters. Keep full-width forms as they are: the server normalises.
8. **Skip non-text elements** (images, `PageImage`) without ending the sentence.

## 3. The offset unit (H5, resolved 2026-09-24: UTF-16 code units)

> Verified on the live API: 𠮟 (U+20B9F) occupies `charStart 0 – charEnd 2`. The conversion below is **UTF-8 byte offset → UTF-16 code units**.

`charStart`/`charEnd` come from a JS/TS backend, so the likeliest unit is **UTF-16 code units**
(`String.prototype.length`). For almost every Japanese character, the codepoint count equals the
UTF-16 count. They differ only for non-BMP characters (CJK Extension B+, e.g. 𠮟 U+20B9F, 𩸽
U+29E3D) and emoji. These do show up in real novels.

P0 checks the unit with one curl on `"𠮟る猫"`: if 猫's `charStart` is 3, the unit is UTF-16.
If it is 2, the unit is codepoints. Then `SentenceBuilder` converts the tapped token's UTF-8 byte
offset into that unit (`Utf8` lib helpers) **once**. Everything downstream compares in server
units.

## 4. Tests (host, `test/lexirise_sentence/`)

Build fake `Page`s from strings (with the layout split applied), then assert text + offsets:

- A plain sentence in the middle of a line; a sentence spanning three lines.
- A tap on the terminator itself, and a tap on a closing bracket.
- Dialogue: 「…」 as its own paragraph with no 。.
- Truncation at the top and bottom of the page, and the 120-codepoint cap on an unpunctuated
  run-on.
- A ruby line, where the reading is not in the output.
- Mixed text: 彼はiPhoneを買った。, where Latin tokens join without extra spaces.
- A non-BMP character before the tap, checking the offset in both candidate units.
- Chinese: the cases in `languages.md` §7.
- A pure English page, where words join with spaces and stop at `. ! ?`.
