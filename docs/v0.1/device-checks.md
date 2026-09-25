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
