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
- **Built in P2:** those bits exist only during layout; a loaded `Page` doesn't keep them (and every
  line is its own `TextBlock`, with no paragraph marker). So `text/PageModelAdapter` turns the Page into
  a plain `PageModel` (text lines 1:1 with the lines `extractWords()` walks, every token, never ruby),
  and the builder decides spaces by script: **a space only between two non-CJK tokens** (Latin words),
  never next to a CJK character, and none after a Latin hyphen at a line break. Paragraph starts come
  from line geometry (`text/ParagraphBreaks.h`: the previous line ended ≥ 2 em short of the column, the
  gap grew ≥ 1.3× the usual advance, a new indent, a leading `　`, or a block-style change).

## 1. Output

```cpp
struct BuiltSentence {       // src/lexirise/text/SentenceBuilder.h
  std::string text;          // UTF-8, base text only, ≤ kMaxSentenceCodepoints (120)
  uint32_t tapOffset;        // tapped token start, in UTF-16 code units (Lexirise's charStart, H5)
  uint32_t tapLength;        // tapped token length, same unit (the token as laid out, e.g. 猫。)
  bool truncatedLeft;        // cut by the cap or the page top, not by a sentence end
  bool truncatedRight;
};
```
`text/TapContext` pairs it with the language decision (`languages.md` §1): with no metadata the
sentence is first cut with Japanese rules, the language read off it, then recut with the right rules.

## 2. Rules

1. **Walk outwards from the tapped token** across `PageLine`s on the page, in reading order.
2. *(Punctuation is per language. The Japanese set is below; the Chinese set is in `languages.md` §2.)*
   **Stop left** after a sentence terminator: `。 ！ ？ ! ? ．` plus `…` when it is followed by a
   closing bracket or ends the paragraph. **Stop right** at the terminator, and include it along
   with any closing brackets or quotes straight after it (`」 』 ） 】 " '`).
3. **A paragraph end is a sentence end.** Blocks that don't flow into each other (a new `TextBlock`
   with a paragraph start) stop the walk. This matters for dialogue-heavy novels, where each 「…」
   is its own paragraph with no 。. **Also** (P2): a closer followed by an opener (」「) is a break
   even on one line, and in Japanese a closed quote followed by the quotative と / って continues the
   sentence (「行こう。」と彼は言った。 is one sentence, not two).
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
