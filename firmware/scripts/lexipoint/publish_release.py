#!/usr/bin/env python3
"""Publish a Lexipoint release from this Mac (firmware-base.md §6). There is no CI: this does what a release
workflow would, in an order that never leaves a release without its firmware, or its tag on other source.

  cd firmware
  python3 scripts/lexipoint/publish_release.py --dry-run          # the checks, then the commands it would run
  python3 scripts/lexipoint/publish_release.py [--prerelease]     # publish (needs claritise's OK, and gh logged in)

1. Before building: the tag is platformio.ini's (`release_tag.py expected`), fits the OTA updater and is newer
   than every published release (`release_tag.check`); no release or tag of that name exists yet; the tree is
   clean, HEAD is a commit on GitHub's `main` and `origin` is the repo devices read; nothing local would reach the build
   (a PLATFORMIO_* variable, a platformio.local.ini); the key scan is clean.
2. A clean build of `x4pro-gh_release` (`x4pro-gh_release_rc` for a prerelease, built from `main` too), named
   `lexipoint-<tag>-x4pro.bin`, the name the device's updater looks for.
3. The tree and HEAD checked again (nothing moved while it built), then the release created with the file
   attached, and its tag at HEAD, in one step.
4. A full release must then be GitHub's latest, with that asset: devices read /releases/latest.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import release_tag  # noqa: E402

FIRMWARE = release_tag.REPO
DEVICE = "x4pro"
ENVS = {False: "x4pro-gh_release", True: "x4pro-gh_release_rc"}
CONFIG_H = os.path.join(FIRMWARE, "src", "lexirise", "LexiriseConfig.h")
REMOTE, BRANCH = "origin", "main"
LOCAL_INI = "platformio.local.ini"  # platformio.ini's extra_configs: gitignored, but it reaches every build
RC_HASH_LENGTH = 7  # platformio.ini's CROSSPOINT_RC_HASH: a short SHA
SHORT_SHA = 8  # in messages
LATEST_TRIES, LATEST_WAIT_S = 3, 5  # GitHub's /releases/latest can lag a new release by a few seconds


def releases_repo(config_h: str = CONFIG_H) -> str:
    """The owner/repo the device's updater reads (LexiriseConfig.h kReleasesLatestUrl): the one to publish to."""
    with open(config_h, encoding="utf-8") as f:
        m = re.search(r'kReleasesLatestUrl = "https://api\.github\.com/repos/([^/"]+/[^/"]+)/releases/latest"',
                      f.read())
    if not m:
        raise RuntimeError("kReleasesLatestUrl not found in LexiriseConfig.h")
    return m.group(1)


def repo_of_url(url: str) -> str:
    """owner/repo of a GitHub remote URL (https or ssh), "" for anything else."""
    m = re.search(r"github\.com[:/]([^/]+/[^/]+?)(?:\.git)?/?$", url.strip())
    return m.group(1) if m else ""


def asset_name(tag: str) -> str:
    return f"{release_tag.ASSET_PREFIX}{tag}-{DEVICE}.bin"


def tree_problems(porcelain: str, head: str, sha: str, head_on_main: bool) -> list[str]:
    """Why the checkout can't be released as `sha` (empty: it can). Checked before and after the build."""
    problems = []
    if porcelain.strip():
        problems.append("the working tree has changes: commit or stash them, so the release is what's committed")
    if head != sha:
        problems.append(f"HEAD moved from {sha[:SHORT_SHA]} to {head[:SHORT_SHA]} during the build")
    if not head_on_main:
        problems.append(f"HEAD isn't a commit on GitHub's {BRANCH}: push it first (with claritise's OK)")
    return problems


def publish_problems(tag: str, repo: str, origin_url: str, remote_tag_sha: str, release_exists: bool,
                     environ: dict[str, str], local_ini: bool, keys_clean: bool) -> list[str]:
    """Why `tag` can't be published from here (empty: it can)."""
    problems = []
    if repo_of_url(origin_url).lower() != repo.lower():
        problems.append(f"{REMOTE} is {origin_url!r}, not {repo}, the repo devices read")
    if release_exists:
        problems.append(f"a release {tag} exists already: a number is released once (bump [lexirise] release)")
    if remote_tag_sha:
        problems.append(f"the tag {tag} exists on GitHub already (at {remote_tag_sha[:SHORT_SHA]}): a release would "
                        "keep it, not HEAD's source. Delete it on GitHub first, if that release was deleted")
    leaking = sorted(k for k in environ if k.startswith("PLATFORMIO_"))
    if leaking:
        problems.append(f"{', '.join(leaking)} set: it would reach the release build, unset it")
    if local_ini:
        problems.append(f"{LOCAL_INI} exists: its envs and flags would reach the release build, move it aside")
    if not keys_clean:
        problems.append("keyscan.py didn't pass (a key-shaped string, or it failed): run it to see why")
    return problems


def latest_problems(tag: str, latest: str, assets: list[str]) -> list[str]:
    """Why devices won't see a just-published full release (empty: they will)."""
    if latest != tag:
        return [f"GitHub's latest release is {latest!r}, not {tag}"]
    if asset_name(tag) not in assets:
        return [f"the latest release has {assets}, not {asset_name(tag)}"]
    return []


def plan(tag: str, prerelease: bool, sha: str, repo: str) -> list[tuple[list[str], dict[str, str]]]:
    """The commands that build and publish `tag` at commit `sha`, each with the extra environment it needs."""
    env = ENVS[prerelease]
    build_env = {"CROSSPOINT_RC_HASH": sha[:RC_HASH_LENGTH]} if prerelease else {}
    built = os.path.join(".pio", "build", env, "firmware.bin")
    asset = os.path.join(".pio", "build", env, asset_name(tag))
    base = tag.split(release_tag.LEXI_MARKER)[0]
    notes = (f"Lexipoint {tag} for the Xteink X4 Pro, built on CrossPoint Reader {base}. Install and update: "
             f"https://github.com/{repo}/blob/{tag}/docs/user-guide.md")
    create = ["gh", "release", "create", tag, asset, "--repo", repo, "--target", sha, "--title", tag, "--notes", notes,
              "--prerelease" if prerelease else "--latest"]
    # -j1 as the release workflow built: slower, but the one setting releases have always been built with.
    return [(["pio", "run", "-e", env, "-t", "clean"], {}), (["pio", "run", "-e", env, "-j1"], build_env),
            (["cp", built, asset], {}), (create, {})]


class Runner:
    """Runs commands in firmware/. Tests pass a fake."""

    def run(self, cmd: list[str], extra_env: dict[str, str] | None = None) -> str:
        env = dict(os.environ, **(extra_env or {}))
        return subprocess.run(cmd, cwd=FIRMWARE, env=env, check=True, text=True, capture_output=True).stdout

    def succeeds(self, cmd: list[str]) -> bool:
        return subprocess.run(cmd, cwd=FIRMWARE, capture_output=True).returncode == 0

    def step(self, cmd: list[str], extra_env: dict[str, str]) -> None:  # output shown: builds take minutes
        subprocess.run(cmd, cwd=FIRMWARE, env=dict(os.environ, **extra_env), check=True)

    def sleep(self, seconds: float) -> None:
        time.sleep(seconds)

    def exists(self, path: str) -> bool:
        return os.path.exists(os.path.join(FIRMWARE, path))


def checkout_state(r: Runner, sha: str) -> list[str]:
    head = r.run(["git", "rev-parse", "HEAD"]).strip()
    on_main = r.succeeds(["git", "merge-base", "--is-ancestor", "HEAD", f"{REMOTE}/{BRANCH}"])
    return tree_problems(r.run(["git", "status", "--porcelain"]), head, sha, on_main)


def publish_state(r: Runner, tag: str, repo: str, environ: dict[str, str]) -> list[str]:
    """GitHub's side (origin, the tag, the release) and what would leak into the build (env, local ini, keys)."""
    remote_tag = r.run(["git", "ls-remote", "--tags", REMOTE, f"refs/tags/{tag}"]).split()
    return publish_problems(tag, repo, r.run(["git", "remote", "get-url", REMOTE]), remote_tag[0] if remote_tag else "",
                            r.succeeds(["gh", "release", "view", tag, "--repo", repo]), environ,
                            r.exists(LOCAL_INI), r.succeeds([sys.executable, "scripts/lexipoint/keyscan.py"]))


def read_latest(r: Runner, repo: str) -> tuple[str, list[str]]:
    """GitHub's latest release, tag and asset names, from one read ("" when there's none: a 404). Any other
    failure (a login, the network) is raised: it says nothing about the release."""
    try:
        latest = json.loads(r.run(["gh", "api", f"repos/{repo}/releases/latest"]))
    except subprocess.CalledProcessError as e:
        if "404" in (e.stderr or ""):
            return "", []
        raise
    return latest.get("tag_name", ""), [a.get("name", "") for a in latest.get("assets", [])]


def publish(r: Runner, prerelease: bool, dry_run: bool, environ: dict[str, str], out=print) -> int:
    repo = releases_repo()
    version, release = release_tag.configured()
    tag = release_tag.expected_tag(version, release, prerelease)

    r.run(["git", "fetch", "-q", REMOTE, BRANCH])
    sha = r.run(["git", "rev-parse", "HEAD"]).strip()
    problems = checkout_state(r, sha)
    published = r.run(["gh", "release", "list", "--repo", repo, "--exclude-drafts", "--exclude-pre-releases",
                       "--limit", str(release_tag.RELEASE_LIST_LIMIT), "--json", "tagName", "-q", ".[].tagName"])
    problems += release_tag.check(tag, version, release, prerelease,
                                  release_tag.previous_release(published.splitlines(), tag))
    problems += publish_state(r, tag, repo, environ)
    for problem in problems:
        out(f"publish_release: {problem}")
    if problems:
        return 1

    steps = plan(tag, prerelease, sha, repo)
    out(f"publish_release: {tag} at {sha[:SHORT_SHA]} to {repo} as {asset_name(tag)}")
    for cmd, extra in steps:
        out("  " + " ".join([f"{k}={v}" for k, v in extra.items()] + cmd))
    if dry_run:
        return 0
    *build, create = steps
    for cmd, extra in build:
        r.step(cmd, extra)
    moved = checkout_state(r, sha) + publish_state(r, tag, repo, environ)  # nothing moved while it built
    for problem in moved:
        out(f"publish_release: {problem}; nothing published")
    if moved:
        return 1
    try:
        r.step(*create)
    except (subprocess.CalledProcessError, KeyboardInterrupt):
        # gh creates a draft, uploads, then publishes: a failed upload can leave the draft (and its tag) behind.
        out(f"publish_release: creating {tag} failed. If GitHub shows a draft {tag}, delete it and its tag "
            f"(gh release delete {tag} --repo {repo} --cleanup-tag --yes), then run this again")
        return 1

    if not prerelease:
        try:
            late = confirm_latest(r, tag, repo)
        except (subprocess.CalledProcessError, KeyboardInterrupt) as e:
            failed = isinstance(e, subprocess.CalledProcessError)
            why = ((e.stderr or "").strip() or e.returncode) if failed else "stopped"
            out(f"publish_release: {tag} is published, but reading GitHub's latest release failed "
                f"({why}): check it on GitHub, don't publish it again")
            return 1
        if late:
            for problem in late:
                out(f"publish_release: {problem}: devices won't be offered {tag}. The release exists: fix it on "
                    "GitHub (mark it latest), or delete it and its tag and publish again")
            return 1
    out(f"publish_release: {tag} published")
    return 0


def confirm_latest(r: Runner, tag: str, repo: str) -> list[str]:
    """Why devices won't see `tag` (empty: they will), after up to LATEST_TRIES reads."""
    for attempt in range(LATEST_TRIES):
        late = latest_problems(tag, *read_latest(r, repo))
        if not late:
            return []
        if attempt + 1 < LATEST_TRIES:
            r.sleep(LATEST_WAIT_S)
    return late


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prerelease", action="store_true", help="a release candidate: not offered to devices")
    ap.add_argument("--dry-run", action="store_true", help="run the checks and print the commands, publish nothing")
    a = ap.parse_args(argv)
    for tool in ("gh", "pio"):
        if not shutil.which(tool):
            print(f"publish_release: {tool} isn't installed", file=sys.stderr)
            return 2
    try:
        return publish(Runner(), a.prerelease, a.dry_run, dict(os.environ))
    except subprocess.CalledProcessError as e:
        detail = (e.stderr or "").strip() if isinstance(e.stderr, str) else ""
        print(f"publish_release: `{' '.join(e.cmd)}` failed ({e.returncode}){': ' + detail if detail else ''}",
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
