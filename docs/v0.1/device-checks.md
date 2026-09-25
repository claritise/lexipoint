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
| `lxctl card-smoke` (P4 gate) | **pass**: all 21 reference states driven and shot on the device (upright portrait, default side buttons) |
| Design conformance, geometry (P4 gate) | **pass**: every frame and divider line (runs ≥ 300 px across, ≥ 150 px down) in all 21 device screenshots sits exactly where the host layout (`cardshots.py`, already matched box for box to `card-reference.html` in P4) puts it: 0 px off everywhere. The pairs for claritise's sign-off: `cardshots.py` panels + these screenshots (not committed) |
| **Bug found (P9's bench fix, R20):** a bench page line is cut token by token at the right padding, so a long token goes whole: some lines lose most of their text, and in `ja-card-saved` the looked-up 煩わしくて isn't on the page at all (so no highlight). Bench only; real pages are laid out by the reader. Fix: re-wrap the bench's tokens at the panel width instead of dropping them | **open** |
| `lxctl card-gestures` (P7) | **pass** |
| `lxctl card-sentence` (P9) | **pass**: words 3 → 11, the wait at the sentence's last word and the jump into the bench's next sentence |
| `lxctl settings-smoke` (P7) | **pass**: 12 rows |
| `lxctl lexi me` (P1) | **pass**: `connected ok` (account details not recorded) |
| `lxctl lexi analyze ja` / `zh` (P1) | **pass**: 彼は東京へ行った。 → 6 occurrences, 行った's lemma 行く, 437 ms; 我们明天去北京看朋友。 → 7, pinyin with tone marks, 374 ms |
| `lxctl lexi soak 20` and `soak 20 cold` (P1) | **pass**: 20/20 each, free heap flat (+0 and −3 B/call), stack low-water 5688 B; the warm run's first call 6.8 s (WiFi join + TLS), then ~370 ms |
| File Transfer, join a saved network (P1) | **pass**: joined (RSSI −62 dBm), web server up with mDNS `crosspoint.local` |
| `websmoke.py` (P1) | **not run**: this Mac's Claude app is refused the local network (`No route to host` from the shell with and without its sandbox, and from the in-app browser), so nothing here can reach the reader's web pages. claritise: allow the app under System Settings → Privacy & Security → Local Network, or open the page from a phone |
| A word broken over two lines (P10 R4) | **pass**: 老黑奴 (老黑 ending line 3, 奴 starting line 4) highlighted as its pieces; a tap on either piece does nothing (screenshot-identical); a tap on 美 on the same line switched the card to 美国 |
| Landscape book (P4/P10 §5h) | **pass**: Reading Orientation → Landscape CW; a long-press on a word opens the card in portrait (the strip shows its line, the page isn't drawn under it); a tap above the card just closes it (no lookup) and the reader comes back in landscape. Restored to Portrait, same page |
| WiFi join (P6/P7 note) | **seen twice**: `WiFi failed after 6037 ms` on the first join after another WiFi user (File Transfer) let go; successful joins took 3.5–4.1 s. The next press joined. Worth watching: the join deadline may be tight right after a WiFi hand-back |
| Rank row without a frequency rank (P4) | as designed: 老黑奴 has no rank, so the row shows the save state ("not saved"), `CardLayout` "no rank: the row shows only the state" |
| Stability over the session (~45 min, ~60 lookups, 2 soaks) | **pass**: no reset, panic or watchdog in the log (the one `rst` is the harness's own connect); internal heap low-water 53,740 B free, largest block 31,732 B (240 samples) |

### Found this session

- **Bug:** the card view's strip highlights a whole glued token (话。, full stop included); the detail view's strip
  and the page highlight only the word. Fixed in P11 (`StripLine::activeStartCp/activeEndCp`).
- **Bug (from P9 R20):** the bench page drops a whole token that runs past its right padding, so lines lose text and
  one reference state (`ja-card-saved`) loses the looked-up word. Bench only. Fix: re-wrap the bench's tokens at the
  panel width. Fixed in P11 (`bench::wrapLines`).
- **Improvement:** a WiFi join is ~3.5 s of all-channel scanning (`WIFI_ALL_CHANNEL_SCAN`) against a 6 s limit
  (`config::kWifiConnectMs`), and twice the join ran out (right after File Transfer let WiFi go). Joining with the
  last BSSID/channel (a fast scan) would save ~3 s on every lookup that needs WiFi and make the limit comfortable.
  Done in P11 (`offline-and-errors.md` §5, as built P11).
- **Leftover in claritise's account:** 深深, tracked, tag `xteink` (see the save/Undo row).
- **Nit:** the More panel shows "Lookup langu…" beside "Chinese (Simplified)".

### Still owed on the device (need claritise, or a proxy)

- `websmoke.py`: this Mac's Claude app has no Local Network access (see above).
- A pasted wrong key, no key, a forced 429, a 5xx, a malformed response (P6 table; need a key change by claritise or a
  local proxy as `base_url`); WiFi dropped mid-save, and the next-sentence toast with WiFi off.
- A stored Long-press Menu = Dictionary loading as Reader Menu (P10 §5g; host-tested, needs the settings file edited).
- Physical: daylight photos (P4), thumb reach, ghosting after 20 cards, an hour of real reading; P4's sign-off on the
  pairs (claritise).
- Japanese books on the device (only the bench's Japanese was driven; the SD card has a Chinese book).
