# Working on Lexipoint (for coding agents)

Lexipoint is e-reader firmware for the Xteink X4 Pro (ESP32-S3), built on CrossPoint Reader, with Lexirise
lookups for Japanese and Chinese. This file is how work is done here: read it before changing anything. It links to
where each rule lives rather than repeating it; when a linked doc and this file disagree, the linked doc wins.

`firmware/AGENTS.md` is the firmware guide (hardware limits, coding standards, memory rules, build commands). Read
it before touching C++.

## The rules that never bend

- **The repo is public (MIT).** Never commit or print API keys, Lexirise user or saved-expression IDs, emails, WiFi
  names or raw API responses; test fixtures are synthetic. The owner is "claritise" (they/them) in everything
  committed, never a real name. No product, device or partnership plans in the repo.
- **The dev key** is `~/.lexirise_key` on the build Mac: read-only calls only (`analyze/text`, `dictionary/lookup`,
  GETs), never printed. Raw answers go only to `research/`.
- **`research/` and `sd-card/` are gitignored and precious.** Never `git clean -x` or `-X`, never delete them.
- **Ask claritise first** (in chat, each time) before: any write to a Lexirise account, flashing or connecting to the
  device, a release or tag, and anything visible and new on the card or a screen (that needs a mockup and their
  sign-off: `docs/v0.1/popup-ui.md` §1.1 and `docs/v0.1/reference/card-reference.html` are binding,
  pixel-for-pixel).
- **Questions to claritise:** few, plain, with your recommendation first. They prefer simple.
- **Never guess an open needs-human item** (`docs/v0.1/00-overview.md`, H-items): stop and ask.

The full list is "Global rules" in `docs/v0.1/01-build-order.md`.

## How a piece of work runs

One phase = one branch `lexi/<phase-id>` from `main` = one gate = one ledger row. The phase list, the ledger and the
process rules are in `docs/v0.2/01-build-order.md` ("How to run this document", "Process rules"); read those first.

1. **Design first when it shows.** Anything new on screen gets a mockup (the reference HTML's style) and waits for
   claritise's sign-off, recorded in the ledger.
2. **Measure before guarding.** If a design depends on how Lexirise behaves, probe it read-only from the Mac
   (`tools/lexirise/`), write the finding in `docs/reference/lexirise-api-notes.md` and pin it in tests. Don't
   defend against behaviour nobody has seen.
3. **Build** on the branch, committing `wip(<phase>): …`. New logic goes in `firmware/src/lexirise/`; a change to a
   CrossPoint file is marked `// LEXIPOINT:` and listed in `docs/v0.1/firmware-base.md` §3. Pure logic gets host
   tests; anything only the device can show gets a written check in `docs/v0.1/device-checks.md`.
4. **Docs in the same commit as the code.** The spec says what, the ledger row says when, the spec's "As built"
   says how. One home per fact; text that was on `main` is struck (`~~old~~ **Superseded <date>:** new`), never
   deleted; no counts in prose outside dated ledger rows.
5. **Run the gate** ("The uniform gate", `docs/v0.1/01-build-order.md`) and keep its output visible. In practice,
   from `firmware/`:
   - `cmake -S test -B build/test && cmake --build build/test -j6 && ctest --test-dir build/test -j6`
   - `python3 -m unittest discover -s scripts/lexipoint -p 'test_*.py'`
   - `python3 scripts/lexipoint/cardgolden.py` (the card's goldens) and `python3 ../tools/mockups/check_v9a_golden.py`
   - `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high`
   - `pio run -e x4pro` and `pio run -e x4pro-gh_release`
   - `python3 scripts/lexipoint/keyscan.py` and `! git grep -qF "$(cat ~/.lexirise_key)" -- :/`
   - clang-format 21 from a venv: `PATH=<venv>/bin:$PATH ./bin/clang-format-fix`, then
     `clang-format --dry-run --Werror` on every C++ file changed since `main`. Never hide the formatter's output:
     a broken venv once let unformatted code through silently.
6. **Review** (below) until it's clean.
7. **Land:** `git branch lexi/<phase>-wip-archive`, then on `main` `git merge --squash lexi/<phase>` and one commit
   whose message says what was built and summarises the review rounds. Mark the ledger row landed, rerun the key
   checks, and push `main` (pushing `main` after a landing needs no extra OK; releases and tags do).
8. **Device checks** follow with claritise's OK: record who ran it, the firmware commit, the date and the result in
   `docs/v0.1/device-checks.md`.

## The review loop

Each round is a fresh, sealed reviewer: a separate agent in its own git worktree, which never sees the build brief.
Give it the diff range (`git diff main...<sha>`), the gate results you claim, the docs that apply, the known and
accepted behaviours, and the mutants already shown equivalent, and ask it to rerun the gate itself. Its question is
claritise's fixed prompt, word for word:

> How sure are you there are no breaking changes or unexpected feature regressions from your changes, and your
> changes only? Can you see any potential issues leading to race conditions or sync issues? Run tests if you can. If
> you think there are, what should I test? Should test coverage of additions or changes be added, both unit tests and
> smoke tests? Are magic numbers being dealt with in a centralised and clean manner? Is everything implemented in the
> cleanest, least spaghetti code possible, clean, robust and extensible for the future? If you need to ask whether to
> make a refactor for testing purposes, the answer is yes. You don't even need to ask.

How the reviewers work:

- **Setup in the worktree:** `git checkout --detach <sha>`, then `rmdir firmware/freeink-sdk && ln -s
  <main checkout>/firmware/freeink-sdk firmware/freeink-sdk`. Never push, never touch the main checkout, never run
  `git clean`, never call Lexirise.
- **Mutation-test the new logic.** Save a copy of the file first; sleep over a second before each write and each
  restore (make's timestamps have one-second resolution); after restoring, touch the file, force a rebuild, confirm
  `git diff HEAD -- <file>` is empty, and run a no-op control. A mutant that only hangs or crashes counts as killed.
  Reviewers running in parallel use their own scratch prefix and kill only processes they started.
- **The report:** a verdict line first (`CLEAN` or `FINDINGS`), then each finding with an id (S = should-fix,
  N = nit), file:line, the failing input or the surviving mutant, and the fix. Any proposed test must already pass
  on the unchanged code. Equivalent mutants are reported as such, not as findings.
- **Between rounds,** fix the findings in one `wip(<phase>): R<a>-R<b>: …` commit, check that each new test kills
  its mutant, rerun the gate, and add the newly found equivalent mutants to the next brief. Remove each reviewer's
  worktree and branch when its report is in.
- **When to stop:** two clean rounds in a row (two parallel reviewers on the same commit count as two rounds). Once
  rounds stop finding behaviour bugs and only find one more missing test each, fix any real bug, add those tests and
  land, rather than loop (claritise, 2026-10-01).

## On the device

The harness is `firmware/scripts/lexipoint/lxctl.py`; `docs/v0.1/dev-harness.md` says how. Opening the serial port
reboots the reader, so hold one connection for a whole session (send `LX:AWAKE 1` first) and never open it from two
processes. Flash with `pio run -e x4pro -t upload --upload-port <port>`, only with claritise's OK.

## Ending a session

Before stopping or handing over: stop every agent you started, remove reviewer worktrees and their branches
(`git worktree list`, `git worktree remove`, `git branch -D`, `git worktree prune`), close the serial connection,
commit or write down any work in progress, and tell claritise where things stand.
