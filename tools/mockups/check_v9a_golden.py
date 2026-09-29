#!/usr/bin/env python3
"""V9a: the bench golden (firmware/test/lexirise_page/bench) against the signed-off mockup's rule.

  python3 tools/mockups/check_v9a_golden.py   # prints the counts and whether every fill is equal; exit 1 if not
"""
# The bench golden against the signed-off mockup's rule (tools/mockups/v9a_annotations.py token_segments + draw_marks):
# one segment per word per line from its first character's x to its last character's advance end, solid [x0+2, x1-2),
# dotted 2x2 every 4 px from x0+2 while x < x1-3; on the bench's geometry (26 px em, 36 px lines from y 14, x from 12,
# the first line after a leading ideographic space, ascender 28, the mark 5 px under the baseline for a 26 px em).
import json
from pathlib import Path

BENCH = Path(__file__).resolve().parents[2] / "firmware/test/lexirise_page/bench"
d = json.load(open(BENCH / 'v9a-ja-analysis.json'))
lines = ["　灯台守の祖父は、毎朝五時に起", "きて海を眺めていた。天気が荒れ", "そうな日には、窓辺に座ったま", "ま、じっと雲の動きを追ってい", "た。"]
pos = []  # (x, line) for each character of the page text (the leading space isn't in it)
for li, line in enumerate(lines):
    for ci, ch in enumerate(line):
        if li == 0 and ci == 0:
            continue
        pos.append((12 + 26 * ci, li))
state = d['stateByEntryId']
fills = []
for o in d['occurrences']:
    if not o['isWordLike']:
        continue
    key = str(o.get('lemmaEntryId') or o['entryId'])
    st = state.get(key) or state.get(str(o['entryId']))
    lvl = st['proficiency'] if st else None
    mark = 'new' if lvl is None or lvl <= 0 else 'learn' if lvl <= 2 else None
    if not mark:
        continue
    segs = {}
    for i in range(o['charStart'], o['charEnd']):
        x, li = pos[i]
        a, b = segs.get(li, (x, x + 26))
        segs[li] = (min(a, x), max(b, x + 26))
    for li, (x0, x1) in sorted(segs.items()):
        y = 14 + 36 * li + 28 + 5
        a, b = x0 + 2, x1 - 2
        if mark == 'new':
            fills.append((a, y, b - a, 2))
        else:
            fills += [(x, y, 2, 2) for x in range(a, b - 1, 4)]
golden = [tuple(map(int, l.split())) for l in open(BENCH / 'v9a-ja-marks.golden')]
print("fills", len(golden), "mockup rule", len(fills), "equal:", golden == fills)
raise SystemExit(0 if golden == fills else 1)
