# Handoff: where things stand

The living status for whoever picks the work up next (a person or an agent). Newest first; an entry stays as written
once it's on `main` (supersede in place, as everywhere). The rules are in `../AGENTS.md`; the phases and the ledger are
in `v0.2/01-build-order.md`.

## 2026-10-01

- **`main`:** v0.2 is built apart from V8's font step (waits for claritise's font choice, S1). V6 (the card additions)
  landed this day after its review rounds, and passed its main device checks in Japanese and Chinese
  (`v0.1/device-checks.md` "v0.2 V6"); the rest of that list is still owed.
- **In flight:** `lexi/fix-marks`: the page marks under an open card now show a level change at once (a level tapped,
  then a side-button step within the 2 s Undo window, left the old underline). Built and reviewed twice with no
  behaviour bug; landing once its test gaps are closed. Its device check is owed (it writes to the account: ask
  first).
- **Next:** v0.3's on-device study (C11, `v0.2/00-overview.md`): draw its screens as mockups for claritise's sign-off
  before building anything.
- **Waiting on claritise:** the font (S1); when to release (C24, `firmware/scripts/lexipoint/publish_release.py`);
  the owed device checks; the "Synced · 0 words changed" wording; the offline fallback for a stale cached meaning;
  whether to correct or hide the romaji shown for は.
- **Parked by claritise:** V9b–V9d, V10 (the reading and sense from the sentence), V11 (grammar), V12 (difficulty
  preview).

## Working notes

- **Two Macs.** Either can build: the toolchain comes from the repo (`firmware/platformio.ini`, the test CMake files,
  clang-format 21). Only one works on a phase branch, or drives the device, at a time; push before switching. The
  gitignored `research/` and `sd-card/` folders are cloned separately (`research/NOTES.md` there says how).
- **Device sessions:** one serial connection per session (opening it reboots the reader); a scratch helper that reads
  steps from a file and runs them through `lxctl.Harness` keeps it open (`v0.1/dev-harness.md` §3). Screenshots are
  taken over serial and can drop log lines sent at the same moment.
- **Review loop lesson (V6, 38 rounds):** once rounds stop finding behaviour bugs and only find one more missing test
  each, add those tests and land (claritise, 2026-10-01; `../AGENTS.md`, "When to stop"). Feed reviewers a growing
  list of mutants already shown equivalent.
- **Lexirise, measured:** `../docs/reference/lexirise-api-notes.md` is the home for how the API behaves.
