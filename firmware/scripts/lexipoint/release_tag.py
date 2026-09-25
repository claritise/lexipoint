#!/usr/bin/env python3
"""Lexipoint's release tags (firmware-base.md §6): `<base>-lexi.<n>` (<base>: the CrossPoint version it's built on), with `-rc` for a prerelease, built
from platformio.ini ([crosspoint] version, [lexirise] release). release.yml runs `check` before building, so a
release the devices' OTA updater couldn't offer or install is refused:

  python3 scripts/lexipoint/release_tag.py expected [--prerelease]
  python3 scripts/lexipoint/release_tag.py check TAG [--prerelease] [--releases FILE|-]

--releases: Lexipoint's published (non-pre)release tags, one per line (`gh release list --limit
RELEASE_LIST_LIMIT ...`): TAG must be newer than the highest of the others.
"""

from __future__ import annotations

import argparse
import configparser
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))  # firmware/
REPO_ROOT = os.path.dirname(REPO)  # the Lexipoint repo: .github/, docs/
RELEASE_WORKFLOW = os.path.join(REPO_ROOT, ".github", "workflows", "release.yml")
LEXI_MARKER = "-lexi."  # src/lexirise/ota/ReleaseVersion.cpp kLexiMarker
RC_SUFFIX = "-rc"
# The release asset: <ASSET_PREFIX><tag>-x4pro.bin (LexiriseConfig.h kReleaseAssetPrefix, release.yml).
ASSET_PREFIX = "lexipoint-"
# The OTA updater keeps a tag in ReleaseJsonParser's 32-byte buffer and names the asset in a 48-byte one
# (OtaUpdater.cpp): 48 - len("lexipoint--x4pro.bin") - 1.
MAX_TAG_LENGTH = 48 - len(f"{ASSET_PREFIX}-x4pro.bin") - 1
RELEASE_LIST_LIMIT = 100  # release.yml's `gh release list --limit`: plenty for Lexipoint's releases
MAX_NUMBER = 1000000  # src/lexirise/ota/ReleaseVersion.cpp kMaxVersionComponent: past it, not a version
BASE_VERSION = re.compile(r"^\d+\.\d+\.\d+$")
TAG = re.compile(r"^(\d+)\.(\d+)\.(\d+)" + re.escape(LEXI_MARKER) + r"(\d+)(" + re.escape(RC_SUFFIX) + r")?$")


def configured(ini_path: str = os.path.join(REPO, "platformio.ini")) -> tuple[str, str]:
    """[crosspoint] version and [lexirise] release, as written."""
    cp = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";",))
    cp.read(ini_path)
    return cp["crosspoint"]["version"].strip(), cp["lexirise"]["release"].strip()


def expected_tag(version: str, release: str, prerelease: bool) -> str:
    return f"{version}{LEXI_MARKER}{release}{RC_SUFFIX if prerelease else ''}"


def numbers(tag: str) -> tuple[int, int, int, int] | None:
    """(major, minor, patch, n) of a Lexipoint tag, or None."""
    m = TAG.match(tag)
    return tuple(int(g) for g in m.groups()[:4]) if m else None  # type: ignore[return-value]


def previous_release(releases: list[str], tag: str) -> str:
    """The highest-versioned published Lexipoint release other than `tag` ("" when there's none). By version,
    not by date: devices compare versions, whatever order the releases were made in."""
    others = [t.strip() for t in releases if t.strip() and t.strip() != tag and numbers(t.strip())]
    return max(others, key=numbers, default="")


def check(tag: str, version: str, release: str, prerelease: bool, latest: str = "") -> list[str]:
    """Why `tag` can't be released (empty: it can)."""
    errors = []
    if not BASE_VERSION.match(version):
        errors.append(f"[crosspoint] version {version!r} isn't X.Y.Z: devices would read it as a non-release")
    if not release.isdigit() or not 1 <= int(release) <= MAX_NUMBER:
        errors.append(f"[lexirise] release {release!r} must be a number from 1 to {MAX_NUMBER}: lexi.0 is never "
                      "offered, and a larger one isn't read as a version")
    want = expected_tag(version, release, prerelease)
    if tag != want:
        errors.append(f"tag {tag!r} isn't {want!r} (no leading v: devices look for {ASSET_PREFIX}<tag>-x4pro.bin)")
    if len(tag) > MAX_TAG_LENGTH:
        errors.append(f"tag {tag!r} is over {MAX_TAG_LENGTH} characters: the OTA updater can't hold it")
    if latest:
        new, old = numbers(tag), numbers(latest)
        if new and old and new <= old:
            errors.append(f"tag {tag!r} isn't newer than the latest release {latest!r}: devices would never "
                          "be offered it (bump [lexirise] release; reset it to 1 only with a new [crosspoint] version)")
    return errors


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("expected")
    e.add_argument("--prerelease", action="store_true")
    c = sub.add_parser("check")
    c.add_argument("tag")
    c.add_argument("--prerelease", action="store_true")
    c.add_argument("--releases", help="Lexipoint's published release tags, one per line, any order ('-': stdin)")
    a = ap.parse_args(argv)
    version, release = configured()
    if a.cmd == "expected":
        print(expected_tag(version, release, a.prerelease))
        return 0
    latest = ""
    if a.releases:
        stream = sys.stdin if a.releases == "-" else open(a.releases, encoding="utf-8")
        with stream:
            latest = previous_release(stream.read().splitlines(), a.tag)
    errors = check(a.tag, version, release, a.prerelease, latest)
    for error in errors:
        print(f"release_tag: {error}", file=sys.stderr)
    if not errors:
        print(f"release_tag: {a.tag} OK")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
