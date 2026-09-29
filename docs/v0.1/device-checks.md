# Device checks: results

Checks run on claritise's X4 Pro over the USB dev harness (`lxctl`, one serial session; `dev-harness.md`),
against the firmware named in each section. Results only: no screenshots are committed (they show book pages).
Each phase's ledger row in `01-build-order.md` links here for what was checked and what's still owed.
Sections from before phase M name the fork's branch `lexipoint` and its SHAs; `../reference/firmware-commit-map.md`
gives each one's SHA in `firmware/` here.

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
| **Left over:** 深深 saved as tracked (tag `xteink`) in claritise's account from that second try: Lexirise's analysis has since split 深深 into 深 + 深 on every path, so no card for 深深 can be reached to undo it. The dev key (`~/.lexirise_key`) is on another account. claritise to delete it in Lexirise | ~~**owed (claritise)**: still saved on 2026-09-26 (V1 device check); reachable from the card again~~ **Superseded, resolved:** claritise keeps it in their account (2026-09-26, "Keep the word in lexirise its fine") |
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
- **Leftover in claritise's account:** 深深, tracked, tag `xteink` (see the save/Undo row). A second try (2026-09-25, P10 build): long-presses on either 深 still analyse as 深 + 深, so no card for 深深 can be reached from the reader; claritise to delete it in Lexirise (V1 made it reachable from the card again, 2026-09-26: it still shows tracked). **Superseded, resolved:** claritise keeps it (2026-09-26, "Keep the word in lexirise its fine"). That session's first WiFi join after the restart failed at 6 s again (the case P11 fixes), the next took 3.5 s.
- **Nit:** the More panel shows "Lookup langu…" beside "Chinese (Simplified)".

### Still owed on the device (need claritise, or a proxy)

- ~~`websmoke.py`: this Mac's Claude app has no Local Network access (see above).~~ Not needed (claritise, 2026-09-25: "no need to test the network"); it passed at P1 and P9–P11 don't touch the web server.
- A pasted wrong key, no key, a forced 429, a 5xx, a malformed response (P6 table; need a key change by claritise or a
  local proxy as `base_url`); WiFi dropped mid-save, and the next-sentence toast with WiFi off.
- A stored Long-press Menu = Dictionary loading as Reader Menu (P10 §5g; host-tested, needs the settings file edited).
- Physical: daylight photos (P4), thumb reach, ghosting after 20 cards, an hour of real reading; (P4's sign-off on the
  pairs: done, 2026-09-26).
- Japanese books on the device (only the bench's Japanese was driven; the SD card has a Chinese book).

## 2026-09-25 (later), `lexipoint` @ `d58ded3c` (P11), flashed and verify-flash matched

| Check | Result |
|---|---|
| First join of a boot (no hint yet) | scan: `WiFi up in 3607 ms (channel 10)` |
| Five cold lookups (`lxctl lexi soak 5 cold`: WiFi released before each) | **pass**: 5/5, every rejoin direct: 1118–1168 ms (was 3551 ms every time on P10); a whole cold lookup ~4.4 s |
| Restart, File Transfer joins first, leave it, look up (P11 R1) | **pass**: Lexipoint's first join of that boot was direct, `WiFi up in 1118 ms (channel 10)`: File Transfer's connection was remembered |
| `Radio not reported stopped` (P11 R3) | never logged over the session |
| The card view's strip on 话。 (P11) | **pass**: only 话 inverted, 。 plain |
| Before flashing, on P10: the first join after a restart | failed at 6 s once more (the case P11 fixes); not seen on P11 |

Still owed for P11: a router on a new channel; away from the saved network (the direct attempt, then the scan, under ~11 s).

## 2026-09-25 (later), `lexipoint` @ `4f416e26` (P12), flashed and verify-flash matched

| Check | Result |
|---|---|
| The card view's strip row at claritise's size (NotoSerifCJK 18 pt, 54 px line) | **pass**: the row measures 62 px (54 + 2 × 4) where P11 drew 51 with the line over its borders; the card starts 11 px higher, the rest of the card unchanged (its dividers at the same y) |

## 2026-09-26, `lexipoint` @ `62d8d739` (P13), flashed and verify-flash matched

`settings.md` §5's P13 step, on Settings → System → Lexirise over serial; every switch put back as it was
(all on).

| Check | Result |
|---|---|
| Japanese's Lookups off | **pass**: 10 rows; Readings and "Language when a book doesn't say" hide, Japanese's Offline dictionary stays, the cursor stays on the toggle |
| Chinese's Lookups off too (Lexirise on, no language on) | **pass**: 11 rows; "Language when a book doesn't say" shows again |
| Both back on | **pass**: 12 rows, values kept (Kana, the dictionaries, Japanese) |
| Lexirise lookups off | **pass**: 7 rows: the Account group, each language heading with its Offline dictionary, and "Language when a book doesn't say"; back on: 12 |
| Harness note | Turning Chinese back on hid a row near the end of the list, so the list scrolled back up by a row and a tap aimed by the old screenshot opened the API key editor. Nothing was typed; the Back swipe left it with the key unchanged (an empty entry keeps the key). Take a screenshot after every tap that shows or hides rows |

Not run: the web page's rows (needs File Transfer's WiFi; host tests cover `shows`), and a Han-only tap
with Lexirise off (covered by `BookLanguage` and `StarDictChoice` tests).

## 2026-09-26, `main` @ `30148173` (M, the first build from the Lexipoint repo), flashed and verify-flash matched

| Check | Result |
|---|---|
| Boots, version | **pass**: `1.6.5-lexi.1-x4pro`, Home as before |
| Opens a book (M gate 8) | **pass**: 活着 opens on its page |
| Lexirise settings | **pass**: both Offline dictionary rows read "Same as reader" |
| *Check for updates* (M gate 8) | **pass**: joined the saved network, asked `api.github.com/repos/claritise/lexipoint/releases/latest`, got 404 (no release yet), showed "Update failed" |

Not checked over serial (for claritise, when convenient): the boot screen's "Lexipoint" (it passes too fast for a
screenshot; the sleep screen is set to Cover), the router listing the reader as `Lexipoint-<MAC>`, File
Transfer's hotspot `Lexipoint` and `lexipoint.local`, the web pages' title, heading and footer, USB Drive's name,
and calibre connect.

## 2026-09-26, `main` @ `f352b16e` (v0.2 V1), flashed and verify-flash matched

| Check | Result |
|---|---|
| 这首歌深深地打动了我 (活着, p. 1): long-press 深 | **pass**: the card shows **深深** whole (shēn shēn, HSK 6, adverb, "deeply, profoundly", #4,184), highlighted as one piece on the strip. The log: ① `analyze/text` 200 (already refined), then `analyze: the word-level split (fast)`, a second `analyze/text` 200 **0.7 s** later, then `dictionary/lookup` 200. Tap to card: 9.1 s to ① (WiFi joined first), +0.7 s for the word-level call |
| Stepping (right side button) | **pass**: 深深 → 地 in one step (not 深 → 深) |
| Left over | 深深 shows **tracked** (tag `xteink`): the save/Undo test's leftover (below, 2026-09-25) is still in claritise's account, and V1 makes it reachable from the card again |

## 2026-09-27, `main` @ `42bce33c` (v0.2 V4), flashed by claritise (upload hash verified)

Run by the harness (one held `lxctl` session after the incident below), claritise at the device. Book: a Japanese
novel (EPUB), portrait. Page turns were fine (claritise). Writes, with claritise's OK ("its ok do it"): 和子 saved from the card (with this book's deck), and a note set then cleared on the dev account's throwaway word 蓋然性 from the Mac.

| Check | Result |
|---|---|
| Lookup on a Japanese page, then the side buttons along the sentence | **pass**: long-press → card (ふたり); right button steps word by word (の, うしろ, 姿, を, 見くらべた, 和子), left steps back |
| Conjugation name (C16): 並んで | **pass**: headword 並ぶ, "並んで te-form" |
| Conjugation name (C16): 行った | **pass**: headword 行く, "行った past"; the **Form** tab lists 行く dictionary form → 行った past |
| A merged token the namer can't build: 捨ててくる‹捨てる› | **pass** (unnamed, as designed: "-te kuru" isn't generated; a missing name, not a wrong one) |
| A compound Lexirise gives the first verb as lemma: 見くらべた‹見る› | **pass** (unnamed: 見くらべた isn't a form of 見る). Lexirise's lemma is wrong for looking the word up: the card shows 見る, "see", not 見比べる, "compare" (`../reference/lexirise-api-notes.md`, "Data quirks") |
| `[LXCARD] names` timing and stack (C16) | **pass**: 10 words 75 ms, 8 words 10 ms; **5688 B** of the loop task's stack free both times (the host estimate of the search's deepest frames was ~2.3 KB) |
| Save from the card, with Deck per book on (V2, V3) | **pass**: T on 和子 → "tracked"; after the Undo window `POST /v1/vocabulary` 200, then the book deck: `step list` → `GET /v1/decks?language=ja` 200 → `step create` → `POST /v1/decks` 200 → `Deck ja:hf7cd8e8a: recorded`. Left in claritise's account on purpose for the Met before retest: 和子 (tracked, this book's `book:` tag) and this book's deck |
| **Met before (C14): fail** | 和子, saved from ふたりのうしろ姿を見くらべた和子は、…, looked up in 理科教室にもどった和子は、… (another sentence, same book): the card shows it **tracked**, and the Context tab says "First time you've met this word." **Cause:** `analyze/text` never carries a saved word's `notes` or `user_tags` (proved from the Mac the same day: `../reference/lexirise-api-notes.md`, "A saved word's notes and tags"); they're only in `GET /v1/vocabulary/{id}`. **Open:** V4's fix (fetch the item for a saved word), then retest here |
| Conjugation on more forms | **pass**: 感じられる‹感じる› "passive or potential"; やさしくて‹やさしい› "te-form" (adjective), its highlight across a line break; names timing 19 words 49 ms, 29 words 20 ms, stack 5688 B free each time |
| **Incident: USB link and a boot loop** | Every opening of the serial port reset the reader (`rst:0x15`, USB_UART_CHIP_RESET), though lxctl keeps DTR/RTS low; so each separate `lxctl` command rebooted it and multi-step checks lost their state. Two processes opening the port at once left the link silent until a replug. The harness's "wait until it answers" loops then opened the port every 1–2 s, and claritise saw the reader boot-loop whenever it was on the Mac's USB (never on battery or a wall charger). **Most likely cause: those loops** (each open is a reset). With them stopped and one process holding the port, it booted once and stayed up (70 s, then the rest of the session); with nothing holding it, it stayed up too. Not a firmware fault as far as seen: the boot log is normal to Home each time, no panic. Fix: how to connect is now in `dev-harness.md` §3 (one held session, `LX:AWAKE 1` first, never poll or double-open) |

## 2026-09-27 (later), `main` @ `b60ef05e` (v0.2 V4b), flashed by claritise (upload hash verified)

Run by the harness (one held `lxctl` session), claritise at the device. Same book; 和子 still saved from the first
session.

| Check | Result |
|---|---|
| **Met before (C14), the retest** | **pass**: 和子 in 校舎の裏庭にゴミを捨て、理科教室にもどった和子は、… → Context tab: "This book" (this sentence), then **"Met before · [筒井康隆] 時をかける少女"** with the saved sentence ふたりのうしろ姿を見くらべた和子は、また、笑い出しそうになった。, 和子 underlined. The log: `analyze/text` 200 (and the word-level `fast` call), names, `dictionary/lookup` 200, then **one** `GET /v1/vocabulary/{id}` 200 |
| Stack after the item (V4b) | **pass**: the next analysis still reports 5688 B free (unchanged) |

## v0.2 V9a: still owed on the device

V9a (the page marks and A3: `../v0.2/page-annotations.md` §2 "V9a decisions" and "As built (V9a)") is on `lexi/V9`,
not yet run on the device. A Japanese and a Chinese book, a key, a vocabulary with a few saved words at each level.

- **Marks on an analyzed page:** open a card (WiFi up), close it, read on at a normal pace: the next page turns with its
  marks (no second refresh); solid under every unsaved word (particles too), dotted under tracked and learning words,
  none under fresh and known ones; they don't touch the glyphs or the next line. Compare with
  `reference/v9a-annotations.html`. Record `[LXPAGE] marks:` lines (the read's time, the draw's time).
- **Offline:** WiFi off, pages analyzed before: still marked. A page never analyzed: plain, no extra refresh later.
- **A save shows:** save a new word at L on a card, close it: its mark turns dotted; set it to K: gone. Ignore a word
  (⋯): its marks go on the page. A word suspended in Lexirise, after a sync: no mark.
- **A3:** on an analyzed page, a side button with the card open skips the known words to the next marked one, over a
  sentence with none, and stops at the page's last marked word; with Settings → Side buttons on a card → Every word,
  it steps every word; on a page not analyzed, every word.
- **Settings:** the On the page rows on the device and the web page, each toggled and kept after a reboot
  (`config.ini`'s `[page]`); `lxctl.py settings-smoke` (16 rows at most).
- **Page marks per book:** the reader menu's row and the toolbar's More panel's: Off hides the marks on the page drawn
  after the menu and A3 steps every word; On again brings them back on the page drawn right after the menu (R3);
  `marks-off.ini` after a reboot.
- **Heap and time:** the free internal heap with a marked page on screen and after 50 page turns (no drop); the page
  turn's time with marks against without (`[ERS] Page render` lines).
- **Image pages** on the UC8279 panel (absolute grayscale): whether the marks survive the gray pass.
- **(R8) Text anti-aliasing On** (the overlay gray pass, and the combined base on Paper Mono): the marks stay after the
  gray pass.
- **(R1) Back turns and jumps:** turn forward three pages and back: each page marked again (`lxctl.py marks-smoke`,
  read-only, reports the draw and read times and the lowest heap); open the book again at an analyzed page, and jump
  (TOC, go to %) to one: marked on its first drawing; record the `read as drawn` time.
- **(R1) After a restart:** a word saved before it shows its mark as saved on the first page (R3: the mirror loaded as
  the book opens, `ReaderMarks::open`: record `the ja mirror loaded in <ms> ms`, a pause before the first page).
- **(R3) A save or an ignore under a card:** save a new word on a card (or ⋯ Ignore it): the marks on the page under the
  card change on the card's next frame; tap another word on the page (a new card): the page under it shows the change;
  `lxctl.py ignore-smoke` reports the marks under the card before and after its Ignore. Record the A3 step's time with
  marks on against off.
- **(R1) The underline's place** at 18 pt with Tight line spacing: clear of the next line's glyphs and ruby.
- **(R7) An idle sync page under a card:** leave a card open until the vocab mirror syncs a page (`lxctl.py
  vocab-smoke`), with marks on: the card's step and frame time while it's applied; a page with nothing new doesn't
  redraw the marks under the card (no `[LXPAGE] marks:` line after it).
- **(R2) Fast turns:** `lxctl.py marks-smoke fast` (10 turns back to back, no settle; R6: each press released before
  the next, `BUTTON_HOLD_S`, as the harness refuses a press while one is held): record the pages read as drawn,
  their slowest read, and the page renders' total and slowest time (R6: the tiled renders counted too); (R8) and a turn into a new chapter (its
  first page always read as drawn: the loop reads ahead only within a section): its `read as drawn` time; then the same with Mark words on the page off,
  for the difference. And a card's step with marks on against off (A3's step latency: the page under the card is marked
  once per card page).
- **(R4) A Lookup language change on the same page:** a book whose metadata doesn't say, with its fallback language
  switched off (no marks); set its Lookup language to Japanese in the reader menu: the page drawn after the menu is
  marked, and stays marked through the next loop passes (no page turn); a card opened right then has the marks under
  it and A3 steps marked words.
- **(R5) Memory:** the free internal heap (and PSRAM) with a marked book open after the three pages are kept, against
  the same with Mark words on the page off; move the kept analyses to PSRAM only if it's measured to matter.
- **(R5) Rotate screen from the reader menu** on an analyzed page (a reflow; the page index may stay): the page drawn
  after is marked when its new text was analyzed, and the next page is read ahead (`[LXPAGE] marks:` lines).
- **(R4) Word select's own screen:** whether word select's screen (a StarDict answer, a "not found" or notice popup) is
  ever shown over a marked page, and whether the marks show under it (it draws the page through its own path, not the
  marks'): note what's seen.
- **(R2) Lookup language:** change a book's Lookup language in the reader menu: the page's marks go until its analysis
  in the new language is kept (the prefetcher analyzes it again over WiFi).

## v0.2 V8: still owed on the device

V8 (the slimming: `../v0.2/slimming.md` §8 "As built (V8)") is on `lexi/V8`, not yet run on the device. Flash the
release build (`x4pro-gh_release`) over an SD card last used with the build before V8, so the upgrade is checked too.
Record the free internal heap and PSRAM on the Home screen and with a book open (the P0 boot-log method) beside the
numbers before V8 (slimming.md §5 gate 3: nothing may get worse).

- **Boot and sleep (step 2: the board code went):** a cold boot on battery, a power-button wake from sleep, a boot with
  USB in (stays awake), the frontlight's double click, sleep with "Last screen" (the moon, a half refresh), and the
  battery latch (unplug USB while asleep: the next press wakes, not a cold boot). The recovery boot (Down + Power).
- **No button legend anywhere (step 2):** walk every settings screen, the file browser, the library, WiFi (the
  keyboard's text field as wide as before), the reader menu and its toolbar, word select, the dictionary screen and
  the percent picker (the side buttons still step it: left down, right up).
- **The ecosystems are gone (step 3):** the home menu has no OPDS row, File Transfer offers Join Network, Create
  Hotspot and USB Drive only, the reader menu has no Sync Progress row, Settings → System has no KOReader Sync or OPDS
  rows, Long-press Menu offers Disabled / Bookmark / Reader Menu (a card whose setting was KOReader Sync shows
  Disabled). The web settings page has no OPDS card; upload a book from the Files page (the WebSocket upload); run
  `websmoke.py` against the device.
- **English only (step 4a):** Settings → System has no Language or Keyboard Layouts row; over an SD card whose UI
  was set to another language, every screen is in English. The keyboard (a WiFi password, the Lexirise tags, library
  search) is English QWERTY with no language key; its symbols layer and Shift work.
- **Hyphenation (step 4b):** with Hyphenation on, an English EPUB still hyphenates; a Japanese and a Chinese EPUB
  lay out as before.
- **No bidi (step 4c):** a Japanese and a Chinese EPUB with ruby, an English EPUB and a TXT lay out and look as
  before; the WiFi password field's cursor follows taps.
- **Lyra only (step 5c):** Settings → Display has no UI Theme row, nor the web settings page; over an SD card whose
  theme was Classic, Lyra Extended or RoundedRaff, the home screen, the lists and the Sync Vocabulary row and popup
  are Lyra's; the card looks as before.
- **Settings screens (step 5):** Settings → Reader has no Manage Fonts row and Controls no Remap Front Buttons; the
  Home key and the left-edge swipe still go back. Upload a `.cpfont` family from the web Fonts page: it shows in Font
  Family.
- **Always Lexirise (step 7):** the boot and sleep screens say Lexipoint; Settings → Check for updates asks
  Lexipoint's releases; the web pages carry the Lexirise link; a lookup, a save and the home screen's Sync Vocabulary
  work (the gate's removal kept their code; the cppcheck rewrites touched the card, the settings, the web API and the
  mirror).
- **The caches after the upgrade (R1):** the first open of a book already cached before V8 lays it out again (the
  section format is 47): time it on a long Japanese or Chinese chapter. A pre-V8 cached Arabic or Hebrew EPUB then
  draws in logical order with no overlapping or gapped words. The first lookup of a word cached before V8 logs its
  format-1 lemma bucket removed (`[LXLOOK] ... unreadable: removed`) and misses; the next is a hit.
- **More after the upgrade (R5):**
  - A TXT opened before V8 is indexed again on its first open (the TXT page index is version 4).
  - Change the Lexirise API key (web page) and look up a word cached under the old key: it misses and is fetched
    again (not the old account's answer). A format-1 bucket from before V8 is removed on its first read.
  - Side-button page turns in all four orientations, with the side-button layout on Prev/Next and on Next/Prev (the
    buttons follow the screen in the inverted and counter-clockwise landscape orientations): step 5a rewrote the
    button mapping.
  - An EPUB paragraph with `dir="rtl"` or CSS `direction: rtl` and English or Japanese text: its words read left to
    right, pushed to the right margin (centred if the book centres it), not reversed. A card last used with a V8 dev
    build from before R5 (R1–R4) may still hold that book's reversed layout under the same format (47): clear the
    book's cache (Clear Cache) first. Released cards never had it.
- **More after the upgrade (R8):**
  - File Transfer → USB Drive mounts the SD card on a computer and ejects cleanly (`HalStorage` lost its `#if`; a
    `static_assert` now guards the build).
  - Settings → Check for updates, once a release is published: it finds `lexipoint-<tag>-x4pro.bin` on the latest
    release (a newer tag) and installs it.
  - With a card open, change the API key over the web page, then look up the same word again on that card: it's
    asked again (the log's lookup call), not answered from the card's pending answer.
- **Recovery without the SPI guide (R2):** can an X4 Pro that won't boot reach the ESP32-S3's ROM download mode
  (e.g. holding the side button on GPIO0, a boot strap, while resetting or plugging in USB), so `esptool.py` can
  flash it over USB? Record what works; the user guide's recovery note depends on it.
- **Leftover files (R1):** on a card that had KOReader sync or OPDS set up, `/.crosspoint/koreader.json` and
  `/.crosspoint/opds.json` are still there after the upgrade and nothing reads them (the user guide says so).
- **The settings survive (steps 2–5):** every setting set before the upgrade is kept (compare a few against the old
  build); the web settings page lists no removed setting.

## v0.2 V7c: still owed on the device

V7c (the lemma cache and TLS session resumption: `../v0.2/00-overview.md` C21 "As built (V7c)") is on `lexi/V7`, not
yet run on the device. Read-only from Lexirise. A dev build; the log's `[LXT] Verified <host> (<version>, <cipher>,
resumed|full) in <ms> ms, free heap <n>` and `[LXLOOK] cache <hit|miss|stale> in <ms> ms (<h> of <n> hits this boot)`
lines say what each check needs. **`lxctl.py cache-smoke [x1 y1 x2 y2]`** (V7c R3; a dev build, one held session,
read-only, two words not looked up before) drives the first ones: a card on the first word (a miss, one
`dictionary/lookup`, the answer written as it closes), past the TLS idle close the same word again (a hit, no lookup),
then the second word (a miss; some TLS session after the first card resumed, which call isn't said), and prints the
read, write and handshake times. Never run here.

- **A full and a resumed handshake:** after a boot, open a card (a `full` line: its time and free heap), close it, wait
  over 30 s (`kTlsIdleCloseMs`: the session closes), open another: a `resumed` line. Record both times and the free
  heap after each (and the lowest and largest block if the harness gives them); the design's estimate is 1-1.5 s saved
  of the ~2.5 s full handshake. Then a page prefetch with WiFi up after 30 s idle: `resumed` too.
- **The kept session's heap:** the free internal heap with WiFi up and no connection open, before the first call of a
  boot and after a card closed and its session closed (the difference is the kept `WOLFSSL_SESSION`, ~0.4 KB by `nm`);
  and after leaving the reader (WiFi given back), the same heap as before the first call (R2: dropped then).
- **The heap during a resumed connection (V7c's carried nit, 2026-09-29):** the free internal heap and the largest free
  block while a resumed call is open (its `Verified ... resumed` line), against the same call made in full: a resumed
  connection holds the kept session and the copy wolfSSL makes of it for the handshake, so it may need a few hundred
  bytes more than a full one; record both.
- **The largest free block with a session kept (R3):** the internal heap's largest free block after a card closed and its
  session closed (the kept `WOLFSSL_SESSION` in place), against `HttpDownloader::MIN_TLS_MAX_ALLOC` (the TLS
  pre-flight): the next handshake must still pass it.
- **A resumed call given up right after its handshake (R5):** with WiFi up after 30 s idle, turn the page just as a
  page prefetch's `Verified ... resumed` line shows (its call given up for input): the next call's `Verified` line
  says `resumed` or `full`, never a `Handshake ... failed (resuming)` line.
- **A refused session:** hard to force; if a `Handshake ... failed (resuming)` line ever shows, the next line must be a
  `full` `Verified` for the same call (the fallback), and the card must not show an error.
- ~~**A rustls-fronted TLS 1.3 server (R7):** a KOSync or OPDS server behind rustls (32-byte ticket nonces; and one with
  a long ticket lifetime if one can be found): it connects and syncs or lists after its handshake (before
  `WOLFSSL_TICKET_NONCE_MALLOC`, the read after the handshake failed).~~ (Superseded 2026-09-29: removed in v0.2 V8, `../v0.2/slimming.md` §8.)
- **The flag's other users** (`HAVE_SESSION_TICKET` reaches every wolfSSL user): an OTA check (Settings, check for
  updates) ~~, a KOSync sync (when one is set up) and a font download each~~ still connects and finishes as before.
  Superseded 2026-09-29: KOSync and the font download were removed in v0.2 V8 (`../v0.2/slimming.md` §8).
- **The lemma cache:** tap a word (a `miss` line, then its phase B from the network), close the card after its meaning
  shows, tap the same word again (on another page or card): a `hit` line, the meaning at once, and no
  `POST /v1/dictionary/lookup` in the log. Record the miss's read time (what the cache adds to every phase B) and a
  hit's; and the Flush step's time on an idle card (~~`[LXVOCAB] mirror file read or written in <ms> ms` includes it~~
  superseded 2026-09-28, V7c R3: timed apart since R2, `[LXLOOK] cache: <n> answers written in <ms> ms`).
- **A full hit on an analyzed page makes no call:** WiFi down (after `wifi_idle_min`), a page analyzed earlier, a tap
  on a word cached before: the card completes with no WiFi join and no call.
- **A close after quick steps (R2):** open a card and step through 8 or more words it hasn't looked up before without
  pausing 3 s (no idle Flush step), then close it: record the close's `[LXLOOK] cache: <n> answers written in <ms> ms
  (closing)` and `[LXVOCAB] mirror file read or written in <ms> ms (closing)` (how long the card stays up after the
  close). No guard until it's measured.
- **Sleep with a card open after quick steps (R9):** open a card, step through a few words it hasn't looked up
  without pausing 3 s, then let the reader sleep (or press power) with the card open: the log's `[LXLOOK] cache: <n>
  answers written in <ms> ms (closing)` (and the mirror's `(closing)` line when it had changes) come before the sleep,
  and the sleep screen still draws promptly (the flush runs under the exit's render lock).
- **Stack during a miss's full handshake (R2):** after a boot, a card whose word isn't cached (a full handshake for
  phase B): the loop task's free stack at its lowest (a dev build's stack low-water, as P1's soak recorded it).
- **The hit count over a reading session:** the last `[LXLOOK]` line's `<h> of <n>` after an hour's reading with
  lookups, for C21's measured estimate (16-42% of rare-word taps).

## v0.2 V7b: still owed on the device

V7b (page analysis: `../v0.2/page-annotations.md` §1.1 "As built (V7b)") is on `lexi/V7`, not yet run on the device.
Read-only from Lexirise. `lxctl.py page-smoke [x y]` (a dev build, one held session) drives the first checks below
(WiFi up from a card, the dwell, a turn, fast turns, a card on an analyzed page) and records each step's time and
heap. Each step logs `[LXPAGE] <this|next> page (...): <kind> in <ms> ms (<error>), ... calls; heap
<free> free, <min> min, <largest> largest`; a card on an analyzed page logs `[LXPAGE] card: page <s>-<start> analyzed:
no analyze/text for its sentences`.

- **First: with no input, a page's call completes and V7a's page isn't given up** (V7b R1): the log's `[LXIN] touch
  line idles <high|low>` once, then `[LXPAGE] ... analyzed` (not `given up for input`) and an idle card's `[LXVOCAB]
  page ...` without `given up for input`. If calls are given up with no finger down, the touch line's learned level is
  wrong: say which, and the fix is to drop the line from `input::inputCame` (buttons only).
- **Only over WiFi already up:** after a reboot (radio off), read a few pages: no `[LXPAGE]` line and no join. Open a
  card (WiFi comes up), close it, read on at a normal pace: each page logs `this page ... kept already` or `analyzed`
  and `next page ... analyzed`; once `wifi_idle_min` has passed and WiFi is down, nothing more.
- **Pages turned fast:** flip pages faster than one per 1.5 s: no `[LXPAGE]` line until you stop.
- **A tap or a side button during a page's call** (the heart of it): turn the page (a tap on the page-turn zone, and
  separately a side button) while a call is under way (within ~3 s of a page settling, WiFi up): the page turns at
  once and the log says `given up for input`. Record whether a quick tap is ever lost or late (the touch line is read
  every few ms while the call waits; a tap between samples may be missed). If taps are lost, the fallback is a worker
  task for the call (measure its internal RAM first).
- **The call's time and heap** with a fresh TLS session (the card's closed after 30 s): the `[LXPAGE]` line's time
  (the Mac measured ~1.9 s for a 300-380-unit page) and the heap's `min` and `largest` against a card's lookup.
- **The file's write time:** the `[LXPAGE]` line's `written in <ms> ms`, and `/.lexirise/pages/` on the card
  afterwards (13-15 KB per page).
- **A card on an analyzed page:** tap a word on a page logged `analyzed`: the card's phase A without `analyze/text` in
  the log (`[LXS] POST /v1/analyze/text` absent; `dictionary/lookup` only), and its time against a card on a page not
  analyzed. Save a word, turn to the next page (analyzed ahead, the word on it): the card shows it saved (the mirror).
- **The probe as a card opens:** change a word's level in the Lexirise app, then open a card on a page holding it
  (more than 5 min after the last probe, the mirror loaded): about a second after phase B, `[LXVOCAB] card probe: 5
  items ... 1 entries changed, the card redrawn` and the word shows the new level. Tap a word during the probe: handled
  at once (`given up for input`). A card opened again within 5 min: no probe line.
- **Touch-line noise:** after a minute of normal tapping and swiping, and quick double taps (two taps in a row, a few
  times: the idle level may be sampled on a debounce edge), no `[LXIN] touch line changes with no finger down` line
  (the line wasn't given up on). `TouchLine::idle` adopts any differing level at once (V7b R7: measured first, no code
  change); if a quick double tap gives the line up, the candidate fix is to adopt a new idle level only after N
  agreeing samples.
- **A finger held at boot:** keep a finger on the screen through the boot and the first page: the line's idle level
  is learned only once it's lifted (the `[LXIN] touch line idles ...` line after that), and a call afterwards isn't
  given up with no finger down.
- **The first card after a boot on a cached page:** save a word, close the card within 3 s (before the mirror loads),
  then tap the word again on the same (analyzed) page: it shows saved (the reader's pending save wins).
- **Automatic page turns and the toolbar with WiFi up:** with auto page turn on, no `[LXPAGE]` step and the turns keep
  their time; with the toolbar (or Contents, Text, More) open for longer than 1.5 s, no `[LXPAGE]` step until it
  closes and the page has been up 1.5 s again.
- **A page turn while a prefetch opens a new session** (the TLS session closed, a weak signal): turn the page just as
  a `[LXPAGE]` step starts: the TCP connect (DNS and SYN, up to `kHttpTimeoutMs`) can't be interrupted; record how
  long the turn waits.
- **A press during the home sync's join:** press a side button (and tap) while Sync Vocabulary joins WiFi (WiFi off
  before, the saved network slow or out of reach): the join can't be interrupted; time how long until "Sync stopped".
- **Page analysis on a chapter's first read** (V7b R8): delete the book's section cache (`.crosspoint/epub_<hash>/
  sections/`), open the chapter and run `lxctl.py page-smoke`: pages are analyzed while the chapter is still being
  laid out ahead.
- **The first Sync Vocabulary after a full power-off** (the clock unset): the result comes, and the mirror file's
  last-sync time is set (copy it to the Mac, or the next card's probe/page lines behave as synced).
- **Confirm, Back and a tap on each result popup** (V7b R9): on "Vocabulary up to date", "Synced · N words
  changed", "Sync failed · No Wi-Fi", "Lexirise: rate limited" and "Sync stopped", press Confirm, then Back, then tap
  the popup: each dismisses it on its release, and nothing else happens (no second sync, no book opened, no row
  acted on).
- **A page turn during a prefetch's handshake and first-byte wait:** by side button and by tap, just as a
  `[LXPAGE]` step starts (TLS closed) and ~1 s into it: the turn is prompt, the step `given up for input`.
- **The prefetch's heap with a fresh TLS session during a first read** (the section's build paused ahead): the
  `[LXPAGE]` line's `min` and `largest`.
- **Sync Vocabulary while 429-blocked:** after a rate limit on a card, press it: "Lexirise: rate limited" at once, no
  join (`home sync: WiFi not up`).
- **WiFi after a home sync:** after the result popup goes, WiFi is off (no `wifi_idle_min` wait; the log's release).
- **A stale mirror and a page analyzed mid-sync** (V7b R3): with a mirror not synced for a while, save a word in the
  app, then read on so a page holding it is analyzed while a full pass is under way (a fresh mirror: its pages come
  on idle cards): a card on that page shows the word saved (the page's snapshot), not unsaved.
- **The home screen's freeze as WiFi is given back:** after a home sync's result, the `home sync: WiFi given back in
  <ms> ms` line: how long the home screen doesn't answer (the TLS close and the radio's teardown).
- **A tap exactly as a home sync's step starts:** tap repeatedly while it runs: each tap stops it at once ("Sync
  stopped"), none lost, none acting on the menu underneath.
- **`lxctl.py home-sync-smoke`** (a dev build, read-only): runs the sync from the home screen and checks the result,
  the calls and WiFi given back.
- **The reader's removal on a prefetched page** (V7b R5 M1): open a card on the first page after a boot, close it
  within ~3 s (the mirror not loaded yet), let the page be analyzed, remove a saved word on it (⋯ Undo save, or T
  then Undo), then reopen the card on the same page: the word shows unsaved.
- **A press with the hour's pages spent:** press Sync Vocabulary until a press says "Lexirise: rate limited" (after
  `kVocabManualSyncPagesPerHour` pages): no WiFi join for it (no `[LXS]` line, no radio).
- **A held button on a page** (V7b R10): with WiFi up, on a page up longer than 1.5 s, hold a side button (release-mode
  page turns on) and, separately, hold Confirm: no stream of `given up for input` lines while it's held (none, or one).
- **A press during a home sync's page** (V7b R10): press a side button while a page downloads (not during the join):
  "Sync stopped" stays up ~2 s, not dismissed as the button comes up.
- **A long sync with sleep at 1 min:** set auto-sleep to 1 min and run a first sync (a fresh mirror) longer than that:
  the reader doesn't sleep until the result.
- **Removing the page cache after an unreadable index:** put a junk `/.lexirise/pages/index.bin` on a card with many
  cached pages; time the loop's pause on the next page analysis.
- **Sync Vocabulary on the home screen:** with Lexirise on and a key set, the row sits just above Settings (and is
  absent with Lexirise off or no key). Press it with WiFi off: it joins, "Syncing vocabulary..." with the bar, then
  "Vocabulary up to date" (or "Synced · N words changed" after a change in the app) for 2 s, and the menu again; the
  log's `[LXVOCAB] home sync: ...`. With the saved network out of reach: "Sync failed · No Wi-Fi". A side button or a
  tap while it runs: "Sync stopped" at once. With a fresh mirror (vocab-<lang>.bin moved aside): the full pass's bar
  moves, and the time per page.
- **A refined page:** a page Lexirise refined already (read it twice more than ~3 min apart, after a font change so
  it's asked again): the line says `(refined, merged)` and `2 calls`; a word the refined pass cuts (深深, 一边, 小さな)
  is whole on the card.

## v0.2 V7a: still owed on the device

V7a (the vocab mirror: `../v0.2/page-annotations.md` §1.2 "As built (V7a)") is on `lexi/V7`, not yet run on the
device. Read-only from Lexirise: the log must show only `GET /v1/vocabulary?…` for the sync (and a card's usual calls).
A dev build logs each page: `[LXVOCAB] <full|incremental> <ja|zh> offset <n>: <items> items, mirror <words> words`,
then `[LXVOCAB] page <items> items in <ms> ms (<error>[, given up for input]), applied in <ms> ms (file <written|not
written|write failed>); heap
<free> free, <min> min, <largest> largest` (internal RAM; `min` is the lowest since boot); the file's read or write,
`[LXVOCAB] mirror file read or written in <ms> ms`. **`lxctl.py vocab-smoke [x y] [press]`** (a dev build, one held
session: opening the port resets the reader, `../v0.1/dev-harness.md` §3, which lists it with the other one-session smokes; it sends `AWAKE 1` first) opens the card on
the word at x y, leaves it idle until a page is logged and checks from the log: a page with its time and heap (V7b:
on a synced mirror, the card's probe instead, `[LXVOCAB] card probe: 5 items in <ms> ms ...` about a second after the
card settles, which usually ends the pass so no idle page follows for `kVocabSyncIntervalMs`), and only
`GET /v1/vocabulary?` during the sync; `press`: you press and release a real side button while a page
streams (an injected press isn't seen), and it must be given up; the log is read past pages that came whole until one
is. It needs a whole page to press into: a synced mirror's first call is the card's quick probe, so move
`/.lexirise/vocab-<lang>.bin` aside first (a full pass runs), or change more words in the app than the probe holds. It
records the numbers the checks below ask for.

- **A full sync of claritise's real vocabulary** (a fresh card: no `/.lexirise/vocab-<lang>.bin`): open cards in a
  book of that language and leave each idle; record how many pages and cards it takes, each page's time, the heap
  (free, the lowest, the largest block) during it, and the file's size once `synced`. Whether `kVocabPageItems` pages
  fit the request deadline on the real network; whether a page's few seconds are acceptable on an idle card.
- **An incremental sync after a save in the Lexirise app:** change a word's level (or save one) in the app, then on
  the reader (more than `config::kVocabSyncIntervalMs` after the last sync, or after a reboot) open a card and leave
  it idle: one incremental page, and the mirror has the change (copy the file to the Mac and read it, or look the
  word up offline once V9 shows marks).
- **Offline saved state from the mirror:** after a sync, a word's saved state is in the file for that entry (the
  card itself still shows `analyze/text`'s; V9's marks are the first to show the mirror).
- **A save on the card** is in the file right after the save's POST (same check); a card closed by sleep writes it at
  the next card.
- **A side button mid-page** (`vocab-smoke … press`): press and release a side button while a whole page streams (a
  synced mirror's first page is a quick probe, too short: move `/.lexirise/vocab-<lang>.bin` aside so a full pass
  runs, or change more words in the app than the probe holds): the card steps at once, the log says `given up: input
  came` (no `failed`), and the next page comes after another idle window. Time the next word's phase B too: the cancel
  closed the TLS connection, so it pays a new handshake.
- **The first idle card on a large mirror:** with a `kVocabMirrorMax`-word (20,000) `vocab-<lang>.bin` on the card
  (a synthetic one: records written by a host tool, or a real sync of a large account), open a card: it opens as fast
  as without one (nothing read then), and the first idle step logs `mirror file read or written in <ms> ms`; record
  it, and the heap after.
- **A slow page against the request deadline:** a 50-item Chinese page (~340 KB) on a weak signal: does it finish
  within `config::kRequestDeadlineMs`, or fail and wait `kVocabFailureWaitMs` each time?
- **The internal heap during a page that opens a fresh TLS session** (the card's session closed, `kTlsIdleCloseMs`):
  the page line's `min` and `largest` against a page on a warm session.
- **A side button held as a page starts:** the page is given up before its request (`given up: input came`, no
  `[LXS] GET /v1/vocabulary?` line), the press is handled, and no page follows until the card has been idle
  `kVocabIdleMs` again.
- **Deep sleep right after a save:** save a word, let the reader sleep before the card goes idle; after waking, the
  word's state comes back with the next incremental pass (it wasn't in the file).
- **Each page's write on a large mirror:** each page that changes the mirror or leaves a pass under way rewrites the
  whole file; with the 20,000-word file, note the `applied in <ms> ms (file written)` of a few pages (a first full sync
  of 20,000 words writes it once per page, about 64 MB in all, and fills the mirror by sorted inserts, `Mirror::put`
  moving the entries after each new one: that's part of the apply time to watch). If it's too slow, the fallback is to
  write the file every N pages during a full pass (its progress is resumable either way: a lost page is read again).
- **The close write on a large mirror:** with the 20,000-word file, save a word and close the card at once: time
  from the close to word select's page (`[LXVOCAB] mirror file read or written` isn't logged at close; time it by
  eye or from the next log line).
- **A tied bulk import:** after importing many words at once in the Lexirise app (one `updated_at`), an idle card's
  incremental pass after the first takes one page, not the whole group (the log's page lines).
- **A full sync while the account changes:** during a first full sync (several cards), add, delete and change words
  in the Lexirise app between pages; once `synced` and the incremental pass after it has run, every word in the
  account is in the file.
- **A save is in the file before any page:** save a word on the card and leave it idle: the file has it after
  `config::kDeckIdleMs` (`mirror file read or written`), before any page.
- **A card whose words the mirror already has closes without a write:** on a large mirror, open a card on a word the
  mirror has as it is and close it: no `mirror file read or written` at idle, and the close returns to the page as
  fast as without a mirror (time it).
- **A quick tap or Home press during a page: seen or lost?** Tap the card (or press Home) and let go while a page
  streams: is it handled after the page, or lost? Likely lost (the touch controller is read only by the loop's input
  update, which the page blocks); if so, try the candidate fix in `../v0.2/page-annotations.md` §1.2 Known limits
  (the touch interrupt line OR'd into the cancel). One still held when the page ends should be seen after it.
- **An incremental pass longer than one wake:** change more words in the Lexirise app than two pages hold (e.g.
  150 level changes), then let the reader sleep after each card: each wake's pages carry on at the next offset
  (the log's `incremental <lang> offset <n>` lines go on from where the last wake stopped, never back to 0 until the
  pass ends), and the cursor moves once it does.
- **A quiet incremental pass on a wake, against a large file:** with the 20,000-word file and nothing changed in the
  app, the first idle card after a wake runs one page: its line says `applied in <ms> ms (file not written)`, near 0
  (no file write; corrected 2026-09-28, V7b: the line said "written" either way), not the whole file's write time.
- **A tap during the first idle card's file read** (carried from V7a's review): with the 20,000-word file, open a
  card and tap a word (or step) just as the first idle window reads the mirror (`mirror file read or written`): the
  tap is handled after the read, not lost (the read blocks the loop like a page, with no cancel).
- **The wake's probe page:** the first card after a wake, nothing changed in the app: ~~the page line says `5 items`~~
  (V7b R6: the card's probe comes first) `[LXVOCAB] card probe: 5 items in <ms> ms` about a second after the card
  settles (`config::kVocabProbeItems`); note its time against a whole page's.
- **A card's close during a weekly resync on a large mirror:** with the 20,000-word file and a full pass under way,
  open a card on words the mirror has as they are and close it: no `mirror file read or written` for them, and the
  close as fast as without a mirror.
- **No radio time added:** with "Keep WiFi on after a lookup" off, no page goes after the card closes, and none on a
  card whose lookup failed offline.

## v0.2 V5: still owed on the device

V5 (Ignore a word, the reader's own list: `../v0.2/00-overview.md` C17 "As built (V5, local)") is on `lexi/V5`, not
yet run on the device. **No writes to Lexirise:** the log must show no POST, PATCH or DELETE on `/v1/vocabulary` for
any step below (a saved word's Met before `GET /v1/vocabulary/{id}` is expected).

- **`lxctl.py ignore-smoke [x y]`** (a dev build, a book open upright, one held session: `../v0.1/dev-harness.md` §3):
  long-presses the word at x y (one not ignored yet), taps ⋯ → Ignore this word, then the toast's Undo, where the card
  logged them, and checks from the log `ignore <key> on written`, then `off written`, and no POST, PATCH or DELETE on
  `/v1/vocabulary`. It covers "Ignore, then Undo" below except the file on the SD card. The toast's Undo lasts
  `config::kIgnoreToastMs` (5 s), so it taps Undo on the first frame drawn after the Ignore (no SYNC between); the
  card logs a frame's targets only once it's on screen, so that tap lands on the toast. **If it fails halfway** (it
  says "… remove `ja:<id>` from /.lexirise/ignored.ini": the toast expired, or the Undo tap took nothing off; or it
  stops after the Ignore), the word is left ignored: take the SD card to the Mac and delete that line from
  `/.lexirise/ignored.ini` (the key is in the message or the log's `[LXCARD] ignore … on written` line), or pick
  another word next time; the card itself has no un-ignore once the toast is gone.
- **The log (dev build):** each change shows `[LXCARD] ignore ja:<id> on|off written|unchanged|failed` (unchanged:
  already so, nothing written), and no POST, PATCH or DELETE on `/v1/vocabulary`.
- **A double tap** on "Ignore this word" keeps "· Undo" on the toast, and the Undo still works.
- **The Ignore's toast lasts about 5 s** (`config::kIgnoreToastMs`); a save's still about 2 s.
- **Ignore, then Undo:** ⋯ "Ignore this word" on an unsaved word (a name): "Ignored: won't be marked again · Undo",
  the card otherwise unchanged ("not saved"); Undo clears the toast. `/.lexirise/ignored.ini` holds the word's line
  after the ignore (`ja:<id>`) and not after the Undo (read the SD card from the Mac, or `lxctl` if it lists files).
- **It holds:** ignore a word, close the card, reboot; look the word up again: ⋯ Ignore says "Ignored: won't be
  marked again" without Undo. A saved word ignored keeps its level on the card and in Lexirise.
- **The SD card full or write-protected** (if it can be arranged): "Save failed", the word not ignored.
- **Tap to toast:** how long from the tap on "Ignore this word" to the toast on screen (the SD write goes first).
- **Heap on a full list:** put a 1000-id `ignored.ini` on the card (e.g. `ja:1` … `ja:1000`), then log free heap and
  the largest free block (dev build) as a card opens and after an ignore and its Undo, WiFi and TLS up.

## v0.2 V4: still owed on the device

V4 (Met before, and the conjugation) is on `main` (`42bce33c`); its ledger row in
`../v0.2/01-build-order.md` links here. What's owed on the device:

- ~~**Met before, V4b's retest** (`lexi/V4b`: the saved item, `../v0.2/00-overview.md` C14 "As built (V4b)"): 和子
  is saved in claritise's account for it (this book's `book:` tag, the 2026-09-27 section above): look it up in
  理科教室にもどった和子は、… (another sentence): the Context tab shows "Met before · <title>" over the sentence it was
  saved from, the card's first frame and meaning not held back (the item's `GET /v1/vocabulary/{id}` in the log after
  the lookup, once per card); offline, "First time you've met this word." and no error; the loop task's free
  stack after an item fetch (dev build: the `[LXCARD] names` line's figure on the next analysis; `Fetched` grew
  ~60 B and is held about three times on that stack).~~ **Done 2026-09-27** (the "(later)" section above: Met before shown, one item call after the lookup, stack unchanged); **still owed:** the offline case ("First time you've met this word.", no error).
- **Met before:** a saved word met in another book (~~and whether the live state carries `notes` and `user_tags` at
  all: not seen yet, `../reference/lexirise-api-notes.md`, "A saved word's notes and tags";~~ (answered 2026-09-27:
  it never does, the section above) and that a note the user
  wrote in the app, not a sentence, shows as "Met before": it would); a word saved on another device ("Met before"
  without a book title); a copy of a saved word in another sentence shows "Met before" after a save; a short line of
  dialogue as its own sentence whose saved copy is inside a longer sentence ("Met before" shown); changing the font
  size so a sentence's page break moves, then tapping a saved word again (no "Met before" of its own sentence).
- **Long sentences (over the 120-character cap):** a sentence repeating a saved word, stepping into its second cut
  (no "Met before"); the tapped word in the first cut, saved from the second (its "Met before" gone once the second
  cut loads); step on, then back before the next cut arrives (the word's "Met before" updates on screen); a verb
  ending the first cut, stepped past (named once the next cut loads); a verb split across a page turn (書け | ない),
  stepped onto with the side buttons (unnamed).
- **The conjugation:** ~~a conjugated verb~~ (done 2026-09-27: 並んで, 行った, the Form tab; above) (食べさせられた, 行って still worth a look), an i-adjective's te-form, a five-step form
  (食べさせられていません named, unnamed when でした follows), and a する verb (勉強した named "past", its Form tab
  from 勉強する); a page with 話しは or 見出し (a noun with okurigana し: unnamed); しようがない (unnamed: "no way to", not
  a volitional). How `analyze/text` splits conjugated verbs is measured from the Mac
  (`../reference/lexirise-api-notes.md`, "How analyze/text splits conjugated verbs"); on the device, only that the
  card's offsets and next character line up with those tokens on a real page.
- ~~**Timing and stack:** `[LXCARD] names <n> words <ms> ms, stack <bytes> B free` (dev build) on a long Japanese
  sentence: how long the names hold phase A back, and how much of the loop task's stack is left (host estimate of the
  search's frames: about 2.3 KB at its deepest).~~ **Done 2026-09-27** (the section above: 75 ms for 10 words,
  5688 B free); a longer sentence is still worth a look.
