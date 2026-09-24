#!/usr/bin/env python3
"""The approved card layout, pinned (popup-ui.md §1.1, P4): each reference state's display list and touch
targets from the host layout with the device fonts' metrics (LexiriseCardRender), compared with the
committed golden files in test/lexirise_card/golden/. Runs in ctest (LexiriseCardGolden).

  python3 scripts/lexipoint/cardgolden.py [--tool build/test/lexirise_card/LexiriseCardRender]      check
  python3 scripts/lexipoint/cardgolden.py --update                                                  rewrite

A difference is a layout change: check it against the reference first (cardshots.py), and only update the
goldens for a change claritise has signed off (the card is binding, pixel-perfect).
"""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

from cardstates import render_args, states

REPO = pathlib.Path(__file__).resolve().parents[2]
GOLDEN = REPO / "test/lexirise_card/golden"
DEFAULT_TOOL = REPO / "build/test/lexirise_card/LexiriseCardRender"


def render(tool: pathlib.Path, state: dict, tmp: pathlib.Path) -> dict:
    out = tmp / "state.json"
    subprocess.run([str(tool), str(out), *render_args(state)], check=True)
    return json.loads(out.read_text())


def first_difference(a, b, path: str = "") -> str:
    """Where two JSON values first differ, as a readable path."""
    if type(a) is not type(b):
        return f"{path or '/'}: {a!r} != {b!r}"
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b:
                return f"{path}/{k}: only in {'golden' if k in b else 'layout'}"
            d = first_difference(a[k], b[k], f"{path}/{k}")
            if d:
                return d
        return ""
    if isinstance(a, list):
        for i, (x, y) in enumerate(zip(a, b)):
            d = first_difference(x, y, f"{path}[{i}]")
            if d:
                return d
        return f"{path}: {len(a)} items != {len(b)}" if len(a) != len(b) else ""
    return "" if a == b else f"{path or '/'}: {a!r} != {b!r}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tool", type=pathlib.Path, default=DEFAULT_TOOL)
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()
    if not args.tool.exists():
        print(f"build the host tool first: cmake --build build/test --target LexiriseCardRender", file=sys.stderr)
        return 1
    failures = 0
    names = set()
    with tempfile.TemporaryDirectory() as t:
        for st in states():
            names.add(st["name"])
            got = render(args.tool, st, pathlib.Path(t))
            path = GOLDEN / f"{st['name']}.json"
            if args.update:
                GOLDEN.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(got, ensure_ascii=False, indent=None, separators=(",", ":")) + "\n")
                continue
            if not path.exists():
                print(f"{st['name']}: no golden file (run with --update)", file=sys.stderr)
                failures += 1
                continue
            diff = first_difference(got, json.loads(path.read_text()))
            if diff:
                print(f"{st['name']}: {diff}", file=sys.stderr)
                failures += 1
    stale = sorted(p.stem for p in GOLDEN.glob("*.json") if p.stem not in names)
    if stale and not args.update:
        print(f"golden files for no state: {', '.join(stale)}", file=sys.stderr)
        failures += len(stale)
    if args.update:
        for name in stale:
            (GOLDEN / f"{name}.json").unlink()
        print(f"wrote {len(names)} golden files to {GOLDEN.relative_to(REPO)}")
        return 0
    if failures:
        print(f"{failures} difference(s): see above", file=sys.stderr)
        return 1
    print(f"{len(names)} states match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
