#!/usr/bin/env python3
"""Key-leak scan (01-build-order.md, uniform gate 5): no tracked file may hold a string
shaped like a Lexirise API key, except the obviously synthetic keys the tests use (they carry a marker:
lx_TESTKEY..., lx_OTHERkey..., lx_NEWER..., lx_OLDER..., lx_FULLSECRETKEY..., lx_YOUR_KEY_HERE, an alphabet
run). A real key never goes in the repo (settings.md §2): it's pasted into the device's /lexirise page.

  python3 scripts/lexipoint/keyscan.py        # exit 1 and list the lines when a key-shaped string is found
"""

from __future__ import annotations

import re
import subprocess
import sys

# Whatever the firmware accepts after the prefix (Settings.cpp isPlausibleApiKey: letters, digits, _ and -),
# from 8 characters on so a partial key is caught too. LexiriseConfig.h kApiKeyPrefix.
KEY_PREFIX = "lx_"
KEY = re.compile(KEY_PREFIX + r"[A-Za-z0-9_-]{8,}")
SYNTHETIC_MARKERS = ("TEST", "OTHER", "NEWER", "OLDER", "SECRET", "YOUR", "AbCdEfGhIjKlMn")  # the last: an alphabet run


def is_synthetic(key: str) -> bool:
    return any(marker in key for marker in SYNTHETIC_MARKERS)


def leaks(grep_lines: list[str]) -> list[str]:
    """The `git grep -n` lines holding a key-shaped string that isn't a synthetic test key."""
    return [line for line in grep_lines if any(not is_synthetic(k) for k in KEY.findall(line))]


def key_shaped_lines(cwd: str = ".") -> list[str]:
    """Every tracked line in the whole repo holding a key-shaped string (`path:line:text`, paths from the
    repo root), wherever in the repo it's run from."""
    root = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=cwd, capture_output=True, text=True,
                          check=True).stdout.strip()
    out = subprocess.run(["git", "grep", "-n", "-I", "-E", KEY.pattern], cwd=root, capture_output=True, text=True)
    if out.returncode not in (0, 1):  # 1: no match at all
        raise RuntimeError(out.stderr)
    return out.stdout.splitlines()


def main() -> int:
    try:
        lines = key_shaped_lines()
    except (RuntimeError, subprocess.CalledProcessError) as e:
        print(f"keyscan: {e}", file=sys.stderr)
        return 2
    found = leaks(lines)
    for line in found:
        print(line)
    if found:
        print(f"keyscan: {len(found)} line(s) hold a string shaped like a Lexirise key", file=sys.stderr)
        return 1
    print("keyscan: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
