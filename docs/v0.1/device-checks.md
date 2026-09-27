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

## v0.2 V4: still owed on the device

V4 (Met before, and the conjugation) is on `main` (`42bce33c`); its ledger row in
`../v0.2/01-build-order.md` links here. What's owed on the device:

- **Met before, V4b's retest** (`lexi/V4b`: the saved item, `../v0.2/00-overview.md` C14 "As built (V4b)"): 和子
  is saved in claritise's account for it (this book's `book:` tag, the 2026-09-27 section above): look it up in
  理科教室にもどった和子は、… (another sentence): the Context tab shows "Met before · <title>" over the sentence it was
  saved from, the card's first frame and meaning not held back (the item's `GET /v1/vocabulary/{id}` in the log after
  the lookup, once per card); offline, "First time you've met this word." and no error; the loop task's free
  stack after an item fetch (dev build: the `[LXCARD] names` line's figure on the next analysis; `Fetched` grew
  ~60 B and is held about three times on that stack).
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
