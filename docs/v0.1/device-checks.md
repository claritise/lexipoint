# Device checks: results

Checks run on claritise's X4 Pro over the USB dev harness (`lxctl`, one serial session; `dev-harness.md`),
against the firmware named in each section. Results only: no screenshots are committed (they show book pages).
Each phase's ledger row in `01-build-order.md` links here for what was checked and what's still owed.

## 2026-09-25, `lexipoint` @ `9c316fbc` (P10), `1.6.5-lexi.1-x4pro` dev build

Book: a Simplified Chinese novel (EPUB), portrait, Show Reader Menu = Tap, Long-press Menu = Reader Menu.

| Check | Result |
|---|---|
| `lxctl reader-longpress` (P9/P10, `lookup-flow.md` §5e/§5f/§5h) | **pass**: bottom margin `ignored`, left margin `ignored`, a word mid-page `taken` → word select → card; a tap on a word above the card → the card closed and the new word's card opened; Back closed to the reader |
| Long-press Menu choices (P10 §5g) | **pass**: KOSync, Disabled, Bookmark, Reader Menu; no Dictionary |
| Long-press button behavior = Chapter skip (P10 §5f) | **pass**: bottom margin `ignored`; left margin `left` (CrossPoint's zone) and its release skipped back a chapter; blank space mid-page `ignored` (no menu, no turn); right margin `left`, skipped forward (back on the same page, screenshot-identical). Setting restored to Off |
| Home-pad hold with Long-press Menu = Reader Menu (P10 §5g) | **pass**: opens the toolbar menu; More panel has no Look Up; **Lookup language** reads Chinese (Simplified) (claritise's setting). Nit: the label is cut to "Lookup langu…" beside that long value |
| Live Chinese card (P5) | **pass**: 那 → pinyin nà, HSK 1, part of speech, meaning, rank bars; the word highlighted on the page; a saved word (的) shows its level (Known) wherever it recurs |
| WiFi join fails (P6) | **pass**: `WiFi failed after 6037 ms` → the card closed, `Lexirise unavailable (no-wifi): no StarDict` → the notice, back to the reader. The retry joined in 4 s, TLS verified in 2.5 s |
| Tap the card's own word (P10 §5h) | **pass**: nothing (screenshot-identical) |
| Tap another word with the card open (P10 §5h) | **pass**: 那 → 经历: the card closed and the tapped word's card opened |
| Side buttons through a sentence and into the next (P9) | **pass**: 经历 → … → 话 → 这首歌深深… (the next sentence, its strip restarting there); the detail view's strip puts each word at the left edge with what fits after it; punctuation is skipped; the line counter follows (6/11 → 9/11) |
| Save and Undo (P5) | **pass**: T on 深深 → toast "Saved as tracked · Undo" → the save sent after its window (`POST /v1/vocabulary` 200); ⋯ → Undo save → `DELETE` + `PATCH` (notes, tags cleared) 200, the card back to "not saved". A second try's Undo tap came before the toast's frame was on screen, so it counted as a page tap and closed the card (by design: taps are matched to the frame shown), which sent the save |
| **Left over:** 深深 saved as tracked (tag `xteink`) in claritise's account from that second try: Lexirise's analysis has since split 深深 into 深 + 深 on every path, so no card for 深深 can be reached to undo it. The dev key (`~/.lexirise_key`) is on another account. claritise to delete it in Lexirise | **owed (claritise)** |
| Button press during the card's first network call (P9 §5d, known) | seen: a side-button press made and released while the card's first lookup blocked (WiFi join + TLS) was never seen, as documented |
| **Bug found:** the card view's strip highlights the whole glued token (话。 with its full stop inverted), while the detail view's strip and the page highlight only 话 | **open** |
