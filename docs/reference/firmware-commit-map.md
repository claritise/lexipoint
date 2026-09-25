# Firmware commits: fork SHAs to this repo's SHAs

Phase M (2026-09-26) brought the fork `claritise/crosspoint-reader` (branch `lexipoint` @ `62d8d739`) into
`firmware/` with `git filter-repo`, which gives every commit a new SHA. Ledger rows and notes from before M name
the fork's SHAs, as they were then; this table finds the same commit here. The base is CrossPoint's `a1ceb633` (tag `1.6.5rc`), `24516d4c` here; the table starts at the last commit shared with CrossPoint's `master`. Older commits are CrossPoint's own
(its history is under `firmware/` too; the full map is in `research/` locally).

| Fork | Here | Commit |
|---|---|---|
| `54337e6d` | `ace3a4f8` | chore: refresh README.md (#3374) |
| `7db14a01` | `605b5bbe` | chore: update pioarduino to 55.03.311 (#3397) |
| `b7bceb56` | `b943ea88` | fix: Fixes KOSync memory checks and reduces memory pressure (#3412) |
| `a7df86fe` | `0bef6abf` | fix: pack font manifest catalog into one arena (#3398) |
| `77b53171` | `68274643` | docs(issue forms): Correct links to scope/roadmap (#3433) |
| `da7feed5` | `15ff78b5` | fix(reader): synchronize end-of-book menu selection (#3418) |
| `f437bb50` | `b77dc81f` | fix: render NFD Hangul filenames from macOS transfers (#3036) |
| `2bad1d09` | `c04fa286` | fix: reader's menu book chapter current position (#3437) |
| `1f3d7458` | `1835fec0` | fix: stabilize X3 EPUB anti-aliasing (#3439) |
| `9ba16086` | `9331b83e` | fix(input): wake the idle poll on raw button contact so short presses register (#3463) |
| `b5fb406f` | `d16a521b` | feat: HTTP serve static with Cache-Control and ETag headers (#2560) |
| `fe653598` | `45c86145` | chore: add direct download links for PR artifacts (#3389) |
| `03c48477` | `e6164f09` | fix(webserver): normalize every user-supplied path and escape file names in the files page (#3353) |
| `e5dcc64f` | `6352fa67` | chore: Consolidates grayscale capability checks and enables absolute grayscale for supported screens (#3478) |
| `6eda8f0b` | `f57f4e45` | fix: don't display elements with hidden HTML attribute (#3390) |
| `8a1a8769` | `34eefb6c` | fix(KOSync): compare mapped KOReader sync positions (#3111) |
| `46d91253` | `2d4253d5` | fix: update OTA to recognize the new format (#3493) |
| `3e60e130` | `316980c2` | fix(debugging_monitor): if PSRAM is logged, add subplot (#3490) |
| `472b5e48` | `cc99252b` | fix(KOSync): preserve precise KOSync upload progress positions (#3174) |
| `c33a8b0e` | `bd732a12` | refactor: reduce EPUB heap fragmentation with unique ownership (#3518) |
| `c80c537f` | `626641d3` | fix: reduce font-cache heap fragmentation (#3521) |
| `c4d8c395` | `1bfcccee` | fix: release font caches before EPUB chapter layout (#3527) |
| `71342eac` | `336348e2` | chore: add x4 Classic to CI pipelines (#3532) |
| `0475e693` | `d0005f1d` | docs: make roadmap easier to scan (#3517) |
| `2b2ba72f` | `a220449b` | fix: number ordered lists and fix list container indents (#3500) |
| `483c5cf6` | `1c7b87e8` | fix: dropped presses while a list repaints (#3534) |
| `e33e3cf3` | `56181779` | perf: batch SdFat's SPI transfers on ESP32 (#3501) |
| `123f3760` | `074c4f24` | fix: USB OTG not disconnected when you unplug the cable (#3538) |
| `652ae0d8` | `2b829af0` | feat: Library view (#3366) |
| `92ef5707` | `784f14be` | fix: Skip bw rendering on sleep images & fix white as transparent for sleep covers (#3541) |
| `bac8f73c` | `31629089` | fix: collapsed list back navigation |
| `a1ceb633` | `24516d4c` | chore: extend asset creation to RCs |
| `70af658d` | `d0cf0c87` | feat(lexipoint): USB dev harness modules, host tool and tests |
| `0184a3f5` | `8d4fc6d2` | feat(lexipoint): dev harness hooks in HalGPIO, main loop and x4pro env |
| `fe69e9c8` | `cd018897` | lexipoint P1: settings store, verified Lexirise client, /lexirise web page (new files) |
| `561b24a6` | `1ebe8198` | lexipoint P1: hooks (boot load, loop tick, web routes, file-manager hidden-path guard, nav link, wolfSSL P-384) |
| `38702767` | `903257e3` | lexipoint P1: review round 1 fixes |
| `4cc04899` | `ccee0dfa` | lexipoint P1: review round 2 fixes |
| `74229e51` | `43f36b59` | lexipoint P1: review round 3 fixes |
| `b29af912` | `837a7122` | lexipoint P1: review round 4 fixes |
| `009451f0` | `d31e66d4` | lexipoint P1: review round 5 fix |
| `b8f39c61` | `152d61fb` | lexipoint P1: round 6 polish (root-listing smoke check, WebApi comment) |
| `a4700955` | `c8bffab6` | lexipoint P2: sentence extraction and book language (new files) |
| `061aa92c` | `1649ebca` | lexipoint P2: hooks (WordBox line/token, setBook, tap debug log) |
| `69250833` | `b3ff82ca` | lexipoint P2: review round 1 fixes |
| `61482a4f` | `7416ce05` | lexipoint P2: review round 2 fixes |
| `dd211334` | `077971fe` | lexipoint P2: review round 3 fixes |
| `37927df0` | `c7e6919f` | lexipoint P2: review round 4 fixes |
| `7b792440` | `128a8165` | lexipoint P2: review round 5 fixes |
| `1ee2037b` | `a00ad003` | lexipoint P2: round 6 polish (smallest-gap advance, contraction table test) |
| `4b6a278c` | `dd85ddc2` | P3: Lexirise lookup provider |
| `2a64d98a` | `c9940311` | P3: reader hooks for the Lexirise lookup |
| `d7b2103f` | `21a18d37` | P4: the Lexirise card bench |
| `e1d16a4d` | `246c6c05` | P5: the live Lexirise card, and saving |
| `8f43c818` | `4b1f93d3` | P6: errors and fallback |
| `2056cac3` | `de4ff69e` | P7: long-press polish and the settings screen |
| `b6d06669` | `bfa90833` | P8: closeout (releases, OTA from the fork, CI) |
| `47cff03b` | `99a6bab4` | P9: step on into the next sentence, the detail strip from the word, long-press only on words, each book's lookup language |
| `9c316fbc` | `8ecdc00e` | P10: a long-press off the text does nothing, no lookup mode, tap another word to change the card |
| `d58ded3c` | `f9b18561` | P11: the strip inverts only the word, the bench page wraps, a direct WiFi join |
| `4f416e26` | `27d12518` | P12: the card's strips grow with the reader's font size |
| `62d8d739` | `ffa121f9` | lexi P13: offline dictionaries always show; one default-language rule; the page's rows come from the device |
