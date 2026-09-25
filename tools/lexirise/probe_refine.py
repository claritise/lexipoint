#!/usr/bin/env python3
"""A wider sample for v0.2 V1: how often does Lexirise's refined second pass change the split or the entries,
and how long does it take? Sends each sentence once, then polls every POLL_S until `morphoPending` is false or
WAIT_S passes. Sentences run in parallel. Key from ~/.lexirise_key (never printed); raw responses to
research/v02-morpho/refine-*.json (gitignored).

  python3 tools/lexirise/probe_refine.py
"""

import concurrent.futures as cf
import json
import os
import time

from probe_morpho import call

POLL_S, WAIT_S = 15, 150
SENTENCES = [
    ("ja", "きのうはあめがふっていたので、いえでほんをよんでいました。"),
    ("ja", "おかあさんはだいどころでごはんをつくっています。"),
    ("ja", "そのときはじめて、かれがうそをついていたことにきがついた。"),
    ("ja", "一日中雨が降っていたので、どこにも行かなかった。"),
    ("ja", "彼女は小さな声で「ありがとう」と言った。"),
    ("ja", "駅のまえでまっていると、とつぜんともだちがあらわれた。"),
    ("ja", "この本はとてもおもしろかったので、一気に読んでしまった。"),
    ("zh", "他长得很高，但是性格很温和。"),
    ("zh", "我们明天一起去图书馆看书吧。"),
    ("zh", "这首歌深深地打动了我。"),
    ("zh", "他一边吃饭一边看电视。"),
]


def words(d):
    return [(o.get("word"), o.get("entryId")) for o in d.get("occurrences", [])]


def probe(n, language, text, key, out):
    start = time.monotonic()
    answers = [(0.0, call(key, language, text)[1])]
    while answers[-1][1].get("morphoPending") and time.monotonic() - start < WAIT_S:
        time.sleep(POLL_S)
        answers.append((time.monotonic() - start, call(key, language, text)[1]))
    with open(os.path.join(out, f"refine-{n}-{language}.json"), "w", encoding="utf-8") as f:
        json.dump([{"at_s": at, "response": d} for at, d in answers], f, ensure_ascii=False, indent=1)
    first, last = answers[0][1], answers[-1][1]
    return dict(n=n, language=language, text=text, pending_first=first.get("morphoPending"),
                done_at=None if last.get("morphoPending") else answers[-1][0],
                split_changed=[w for w, _ in words(first)] != [w for w, _ in words(last)],
                entries_changed=words(first) != words(last),
                first=" ".join(str(w) for w, _ in words(first)), last=" ".join(str(w) for w, _ in words(last)),
                grammar=[g.get("slug") for g in (last.get("grammar") or [])])


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "research", "v02-morpho")
    os.makedirs(out, exist_ok=True)
    with open(os.path.expanduser("~/.lexirise_key")) as f:
        key = f.read().strip()
    with cf.ThreadPoolExecutor(len(SENTENCES)) as ex:
        results = list(ex.map(lambda a: probe(*a, key, out), [(n, l, t) for n, (l, t) in enumerate(SENTENCES)]))
    for r in results:
        done = "still pending" if r["done_at"] is None else f"refined by {r['done_at']:.0f}s"
        print(f"[{r['n']}] {r['language']} first pending={r['pending_first']} {done} "
              f"split_changed={r['split_changed']} entries_changed={r['entries_changed']} grammar={r['grammar']}")
        print(f"    first: {r['first']}")
        if r["split_changed"] or r["entries_changed"]:
            print(f"    last:  {r['last']}")


if __name__ == "__main__":
    main()
