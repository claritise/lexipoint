# Standalone repo: Lexipoint for the Xteink X4 Pro

**Status:** decided 2026-09-25 by claritise, not started. This is phase **M** in `01-build-order.md`, and it
runs after P10 lands. Decisions D20, D21 and D22 in `00-overview.md`. Once M lands, it supersedes D2, D19 and
the fork parts of `firmware-base.md` (§0, §1, §1a, §4, §5).

Written for the agent that builds M. Where this doc gives a default, use it unless claritise says otherwise.
Anything listed under "Needs claritise" is never guessed.

---

## 0. What changes, and why

Lexipoint stops being a fork of CrossPoint Reader and becomes **its own project, in one repo**:
`github.com/claritise/lexipoint`, holding the firmware, the SD-card files and the docs. CrossPoint is
**the code Lexipoint started from**, credited as such, and not an upstream it keeps up with.

claritise's reasons (2026-09-25):

- **Lexipoint only supports touch devices.** CrossPoint's own work is mostly for devices without touch
  (the X4, X3 and X4 Classic, all buttons only). Merging it is cost with nothing gained, and the fork had
  already grown apart: 57 commits and about 36.6k lines added over 490 files, on base `54337e6`
  (tag `1.6.5rc`). Upstream was only 2 commits ahead, both empty merges, so nothing is lost by stopping.
- **One repo, one history.** A phase's code and its ledger row land in the same commit, instead of a docs
  commit that says "merged as `<sha>`" in another repo.
- **One target.** The Xteink X4 Pro is the only device built, tested and released (D20).
- **Accurate branding.** The repo, the docs and what users see should describe Lexipoint for the X4 Pro,
  not "a CrossPoint fork" (D22).

The license allows all of this. CrossPoint is MIT (© 2025 Dave Allie), so its `LICENSE` notice stays with the
code, and credit goes in the README and a `NOTICE` file.

## 1. Decisions

**D20: device scope.** The **Xteink X4 Pro** (`env:x4pro`, ESP32-S3, touch) is the only primary target.
Other devices with touchscreens that the FreeInk SDK supports (its `FREEINK_CAP_TOUCH` list: Sticky,
PaperMono, PaperS3, M5Paper, LilyGo, Murphy, Murphy M4, EEGO A4) **may** be supported later. None of them is
built or tested now. **Devices without touch are out for good.** This includes the X4 Classic, which D2 had
listed as a later target. Supporting another device later means adding its env back, a device check, and
the X4 Pro constants that turn out to be device-specific (§6).

**D21: one standalone repo.** `claritise/lexipoint` holds `firmware/` (the fork's history, kept),
`sd-card/` and `docs/`. There are no scheduled upstream syncs and no rebases. Taking an upstream change later
is a one-off cherry-pick, recorded in the "Taken from CrossPoint" table (§4 step 12). Once M lands, the old
fork `claritise/crosspoint-reader` is archived on GitHub by claritise.

**D22: rebrand.** Every doc, the README and everything users see calls the product **Lexipoint**, firmware
for the **Xteink X4 Pro**. CrossPoint is named only as the base Lexipoint is built on (credit, license, and
where a doc explains code that came from it). The words "fork", "upstream sync", "rebase onto upstream" and
"parity with upstream" go, except where history is recorded.

## 2. Target layout

```
lexipoint/                   github.com/claritise/lexipoint (public)
  README.md                  what Lexipoint is, the X4 Pro, install (points at docs/user-guide.md), credits
  NOTICE                     built on CrossPoint Reader (MIT, © 2025 Dave Allie) @ 54337e6 (1.6.5rc); FreeInk SDK
  .gitmodules                firmware/freeink-sdk → https://github.com/Free-Ink/freeink-sdk.git (same pinned commit)
  .github/workflows/         moved from the firmware, running in firmware/ (§4 step 5)
  firmware/                  was ~/Projects/crosspoint-reader @ lexipoint, with history
    LICENSE                  CrossPoint's MIT license, unchanged
    platformio.ini, src/, lib/, test/, scripts/, freeink-sdk/ (submodule) …
  sd-card/                   unchanged
  docs/                      unchanged location; content rebranded (§5)
```

Lexipoint's own code is already separate from the base code: `src/lexirise/`, `test/lexirise_*/`,
`scripts/lexipoint/`, plus marked hooks in base files (`firmware-base.md` §3). Keep it that way. The later
fascia/Tendon work depends on it (§8).

## 3. Before starting

- **P10 has landed** (ledger row `done…`), and no other `lexi/*` phase branch is open. M moves every path,
  so nothing may run beside it.
- **Checked 2026-09-25: no release exists on `claritise/crosspoint-reader`** (`gh release list` is empty).
  No device in the field reads the fork's release URL, so the OTA URL, versions and asset names can all
  change now at no cost. Check again before starting. If a release has appeared since, stop and ask
  claritise: a device on it would need one last release on the fork that points at the new repo.
- Leave the local clone `~/Projects/crosspoint-reader` untouched. It holds the local-only
  `lexi/P*-wip-archive` branches, and it is the fallback if M goes wrong. Only claritise deletes it.

## 4. Steps

Work on branch `lexi/M` in the `lexipoint` repo. Commit as you go. The gate is in §7.

1. **Bring in the firmware with its history.** Make a *fresh* clone of the fork (not the working clone) at
   branch `lexipoint`, and run `git filter-repo --to-subdirectory-filter firmware`. Rename its tags so they
   can't collide with Lexipoint's own (`--tag-rename '':'crosspoint-'`), or drop them. Then, in `lexipoint`
   on `lexi/M`: `git merge --allow-unrelated-histories` the filtered branch. Check that
   `git log --follow firmware/src/lexirise/LexiriseService.cpp` shows the P1 history.
2. **Submodule.** `filter-repo` moves `.gitmodules` to `firmware/.gitmodules`, which git ignores. Recreate it
   at the root with path `firmware/freeink-sdk`, the same URL, and the same pinned commit (`e30d25a`, which
   is on Free-Ink's `main`). Delete `firmware/.gitmodules`. A `git clone --recursive` of the repo must build.
3. **Build envs (D20).** In `firmware/platformio.ini`, keep `x4pro`, `x4pro-gh_release` and
   `x4pro-gh_release_rc` and the sections they extend. Delete `default`, `gh_release`, `gh_release_rc`,
   `slim`, `sticky*`, `x4c*` and `papermono*`. Set `default_envs = x4pro`. Remove whatever is left unused
   (for example `firmware_tuned_c3`, if nothing extends it).
4. **Touch is required at compile time.** Add one check to Lexipoint's code, not to a base file (for example
   in `src/lexirise/LexiriseConfig.h`):
   `#if !FREEINK_CAP_TOUCH` / `#error "Lexipoint needs a touchscreen device (FREEINK_CAP_TOUCH)"`.
   This way the build enforces touch instead of the code quietly assuming it, and a future touch device
   passes on its own.
5. **CI and releases.** Move `firmware/.github/workflows/*` to the root `.github/workflows/`, and make them
   run in `firmware/` (`defaults.run.working-directory: firmware`, plus the paths in cache keys and
   artifacts). They trigger on pushes to `main` and on releases of this repo.
   - Build matrix: `x4pro` and `x4pro-gh_release`, plus **one `x4pro` build with `LEXIRISE` undefined**.
     That replaces `x4c` as the Lexirise-off build while the `LEXIRISE` gate still exists (§6).
   - `cppcheck` runs on `x4pro`, not `default` (which is deleted, and never built Lexirise anyway).
   - The `lexipoint` job (key scan, script tests) stays. `test_gen_bench_fixtures` now finds the docs in the
     same checkout, so it no longer skips in CI.
   - Delete the workflows and `.github` files that exist only for CrossPoint's project:
     `pr-firmware-links.yml`, `FUNDING.yml`, `ISSUE_TEMPLATE/`, `PULL_REQUEST_TEMPLATE.md`. Look at
     `release-fonts.yml`, and keep it only if Lexipoint's releases use it. Delete `firmware/.github/`
     once it's empty, apart from `skills/` (see step 10).
6. **OTA and release naming (D22).** Point `kReleasesLatestUrl` (`src/lexirise/LexiriseConfig.h`) at
   `https://api.github.com/repos/claritise/lexipoint/releases/latest`, and update the assertion in
   `scripts/lexipoint/test_lxctl.py`. Rename the release asset from `crosspoint-<tag>-x4pro.bin` to
   `lexipoint-<tag>-x4pro.bin` in all three places that must agree: `release.yml`, the OTA asset match, and
   `release_tag.py` and its tests. Keep the tag within the updater's 26-character limit. Keep the version
   scheme `<upstream>-lexi.<n>` unless claritise picks another (see §9).
7. **Paths from the firmware to the docs.** `scripts/lexipoint/{cardshots,gen_bench_fixtures}.py` and
   `test_gen_bench_fixtures.py` default to `~/Projects/lexipoint/docs/v0.1`. Make them find the docs
   relative to the script (`<repo>/docs/v0.1`), and keep `--docs` as an override. Also fix the comment in
   `test/lexirise_card/CardRender.cpp` that names `~/Projects/lexipoint/sd-card/…`.
8. **What users see (D22).**
   - The boot and sleep screens draw `STR_CROSSPOINT` (`"CrossPoint"`). Show "Lexipoint" instead: a new
     `STR_LEXIPOINT`, drawn by a small marked hook, not a changed base string.
   - `STR_LEXI_SET_SAME_AS_CROSSPOINT` ("Same as CrossPoint") becomes wording that doesn't name the base,
     e.g. "Same as reader". claritise must sign off the wording, because it's visible in settings.
   - The web UI's titles and the device name that WiFi/mDNS announces, if they say CrossPoint: same
     treatment.
   - Leave the calibre instruction ("Install CrossPoint Reader plugin") alone. That really is the plugin's
     name.
   - The card is not affected (D12 stays binding).
9. **Root files.** `README.md`: what Lexipoint is (Lexirise lookups on an e-ink reader), the X4 Pro as the
   supported device, and that other touch devices may follow (D20). It links to `docs/user-guide.md`, says
   how to build (`cd firmware && pio run -e x4pro`), and ends with credits. `NOTICE`: CrossPoint Reader
   (MIT, © 2025 Dave Allie, base commit `54337e6`, tag `1.6.5rc`) and the FreeInk SDK (its own license, in
   the submodule). Don't put a root `LICENSE` for Lexipoint's own code until claritise picks one (§9).
10. **CrossPoint's project files in `firmware/`.** Delete the ones that speak for CrossPoint's project or send
    people there: `GOVERNANCE.md`, `ROADMAP.md`, `SCOPE.md`, `USER_GUIDE.md` (Lexipoint has its own user
    guide), and CrossPoint's `README.md` (replace it with a short "Firmware; see ../README.md"). Read
    `CLAUDE.md`, `AGENTS.md` and `.github/skills/` before deciding. Keep the coding conventions they hold
    (formatting, memory limits, how to build), and rewrite whatever describes contributing to CrossPoint.
    Keep `firmware/docs/` (CrossPoint's technical docs about the base code) unless it duplicates or
    contradicts ours. Delete `LICENSE` never.
11. **Docs (D22).** See §5.
12. **Record the base.** Replace `firmware-base.md` §4 ("Upstream sync") and §5 ("Cherry-picks and
    divergences") with one section, "Taken from CrossPoint", that has the base commit and a table of any later
    cherry-picks (commit, why). To take one: add CrossPoint as a remote, then
    `git cherry-pick -X subtree=firmware <sha>`.
13. **Key scan over the whole repo.** The firmware's `scripts/lexipoint/keyscan.py` and the docs repo's scan
    now cover one tree. Run it over everything before the merge, and again before the first push. The history
    being brought in was already public on the fork, but check it anyway.
14. **Land.** Merge `lexi/M` into `main` directly (no PR), and commit the ledger row in the same history.
    Pushing needs claritise's OK, as always. Then ask claritise to enable Actions on `claritise/lexipoint`,
    turn on secret-scanning push protection there, and archive `claritise/crosspoint-reader`.

## 5. Docs rebrand

Every doc under `docs/` describes a Lexipoint repo for the X4 Pro. Rules:

- **Product and repo:** "Lexipoint" and "the Lexipoint repo". Firmware paths are written from the repo root
  (`firmware/src/lexirise/…`), or relative to `firmware/` where a doc already says so at the top.
- **CrossPoint** is named where a doc explains base code ("CrossPoint's word-select flow", "the base's
  `MappedInputManager`"). That is accurate and stays. What goes is the fork framing: "the fork", "upstream",
  "rebase", "keep merging cleanly from upstream", "parity with upstream".
- **Device:** "the X4 Pro" everywhere a device is meant. Remove mentions of other devices as targets, except
  in D20's "may come later" line and in history.
- **Commands:** `cd firmware` before `pio` / `cmake` / `ctest` / `scripts/lexipoint/…`. `~/Projects/crosspoint-reader`
  becomes `~/Projects/lexipoint/firmware`.
- **History is not rewritten.** Ledger rows, "As built" notes and dated decisions keep what was true then
  ("fork `lexipoint` @ `47cff03b`"). Add a one-line note where a reader could be misled, e.g.
  "(fork commits; after M these are in `firmware/` history)". `context-brief.md` is the brief as received:
  leave the text, and add a banner that it predates D21 and D22.

Per file (mentions of "crosspoint"/"fork" counted 2026-09-25):

| Doc | What changes |
|---|---|
| `v0.1/firmware-base.md` (36) | Rename to "Firmware base: what Lexipoint is built on and where our code lives". §0 becomes why Lexipoint is standalone (§0 of this doc). §1 and §1a become the layout (§2 of this doc). §2 build targets: X4 Pro envs only. §4 and §5 become "Taken from CrossPoint" (§4 step 12). §6: releases on `claritise/lexipoint`, the new asset name, CI in the root `.github/`. §3's hook table stays: those really are edits to base files |
| `v0.1/01-build-order.md` (24) | "How to run": branch `lexi/<phase-id>` in this repo, merged into `main`, with the ledger row in the same merge. Global rules: see §6 of this doc. Uniform gate: `cd firmware`; item 2's Lexirise-off build and item 3's `x4c` build become the `x4pro` Lexirise-off build. P0/P8 text stays as history, with a note |
| `v0.1/00-overview.md` (12) | D20–D22 rows (added with this doc). Mark D2 and D19 superseded by them. D1 stays as history of where the base came from. Doc index row for `firmware-base.md` reworded |
| `user-guide.md` (19) | Lexipoint for the X4 Pro. Releases page = `claritise/lexipoint`. Asset `lexipoint-<version>-x4pro.bin`. CrossPoint only in credits, and where a screen still shows base UI |
| `v0.1/settings.md` (16), `lookup-flow.md` (13), `languages.md` (5), `sentence-extraction.md` (3), `popup-ui.md` (3), `lexirise-client.md` (2), `dev-harness.md` (1) | Mostly base-code references, which stay. Fix fork/upstream framing, paths and commands. `settings.md`: the "Same as CrossPoint" wording (§4 step 8) |
| `v0.2/00-overview.md` (3) | Same rules |
| `context-brief.md` (20) | Banner only |

## 6. Rules that change, and rules that don't (yet)

Several global rules in `01-build-order.md` exist only to keep rebases cheap:

- every base-file change wrapped in `#if LEXIRISE` and marked `// LEXIPOINT:`;
- "don't modify `util/Dictionary*`, it must keep merging cleanly from upstream";
- the Lexirise-off build;
- base-file hooks in their own commits.

**M doesn't change them.** Changing them while every path moves makes M harder to check. M only swaps the
`x4c` Lexirise-off build for an `x4pro` one. Relaxing them is the first follow-up, not part of M:

- **Follow-up M2, cleanup (not scheduled):** decide whether the `LEXIRISE` gate and markers stay. They still
  show which base files we edited, which will matter for fascia coverage (§8). Delete code for devices
  without touch that no env builds any more: the X3/X4 key maps in `MappedInputManager`, the button-legend UI,
  and the C3-only paths. Pull X4 Pro constants (480×800 panel, thumb zones, the Home pad, UC8279 refresh
  timings) into one device profile only when a second touch device is actually taken on (D20), not before.
- **The version scheme** (`<upstream>-lexi.<n>`) names a CrossPoint version Lexipoint no longer follows.
  See §9.

## 7. Gate

1. **History:** `git log --follow` on a `src/lexirise/` file shows its history, and `git blame` works on base files.
2. **Fresh clone builds:** `git clone --recursive` of `claritise/lexipoint` (or of the local repo), then
   `cd firmware`. `pio run -e x4pro`, `x4pro-gh_release`, `x4pro-gh_release_rc` and `x4pro` with `LEXIRISE`
   undefined all build, with no new warnings.
3. **The touch check works:** a scratch env with only `-DFREEINK_DEVICE_X4=1` fails with the `#error`
   message. Don't commit that env.
4. **Nothing lost:** the host suite and the Python script tests pass, with **the same counts as P10 landed
   with**. `test_gen_bench_fixtures` runs instead of skipping.
5. **Only X4 Pro envs are left:** `grep -n '^\[env' firmware/platformio.ini` lists only `x4pro*`.
6. **No stale paths:** `git grep -n -e 'Projects/crosspoint-reader' -e 'claritise/crosspoint-reader'` finds
   only history (ledger rows, dated notes) and `NOTICE`/credits.
7. **Rebrand:** `git grep -n -i -w fork -- docs README.md` finds only history. The boot and sleep screens say
   Lexipoint (a screenshot from the device via `lxctl`).
8. **Device:** flashed, it boots and opens a book. *Check for updates* asks `claritise/lexipoint`. Until the
   first release exists, the 404 error is expected.
9. **Key scan** clean over the whole repo, and `git grep -qF "$(cat ~/.lexirise_key)"` finds nothing.
10. **CI:** green on `claritise/lexipoint` once claritise enables Actions. Until then, run the workflows'
    commands locally from the root.

## 8. Later: fascia / Tendon (not part of M)

claritise plans to build Lexipoint with **Tendon**, the coding harness that fascia's runtime is being split
into (`~/Projects/fascia`, `~/Projects/fascia-round`; see that repo's
`docs/ideas/fascia-tendon-split.md`). Lexipoint would be a project run through Tendon's **code profile**. It
would not be a fascia profile. M's layout is chosen with that in mind: `docs/` at the root with version folders,
code under `firmware/` as the one code root, Lexipoint's code apart from the base, and host tests that run
unattended.

What was found on 2026-09-25 (fascia-round `7ba05ae`): the code profile can't read C++ yet.
`CODE_EXTENSIONS` in `packages/core/src/binding/index.ts` is `.ts/.tsx/.js/.jsx` only, so `.cpp`/`.h`
files are never scanned. A tag on a `template <…>` line covers only that line. Otherwise the tag syntax and
function spans already work on C++. The embedded-specific questions still open are: evidence from host
tests versus the device, the build as a check, PlatformIO in a fresh worktree's setup, and leaving the
CrossPoint base out of coverage. All of these were handed to the fascia agent on 2026-09-25. Nothing here
blocks M.

## 9. Needs claritise

| # | Question | Default until answered |
|---|---|---|
| H11 | **License for Lexipoint's own code.** The repo root has no license today. | No root `LICENSE`. CrossPoint's MIT `LICENSE` stays in `firmware/` |
| H12 | **Version scheme.** Keep `<upstream>-lexi.<n>` (e.g. `1.6.5-lexi.1`), or switch to Lexipoint's own (`0.1.0`, as `LEXIPOINT_VERSION` already says) before the first release? Switching changes `ota::isNewerRelease`, `release_tag.py` and their tests. | Keep `<upstream>-lexi.<n>` in M. If switching, do it as its own phase before the first release |
| H13 | **Wording that replaces "Same as CrossPoint"** in settings. | "Same as reader" |
