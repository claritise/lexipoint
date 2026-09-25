#!/usr/bin/env python3
"""Measure Lexirise's second analyze/text pass (v0.2 V1, 00-overview.md C19, Open #4 and #5): call analyze/text,
then again after growing delays until `morphoPending` is false, and record what changed (the split, the entries,
grammar). Reads the key from ~/.lexirise_key (never printed). Raw responses go to research/v02-morpho/
(gitignored: they can hold account state); the summary is printed.

  python3 tools/lexirise/probe_morpho.py [--out research/v02-morpho]
"""

import argparse
import json
import os
import time
import urllib.request

BASE = "https://api.lexirise.app"
DELAYS_S = (0.5, 1, 2, 4, 8, 15, 30)  # after the first answer, cumulative waits between re-calls
SENTENCES = [
    ("ja", "ときどきどこかの教室のとびらのあけしめされる音がだれもいない廊下にうつろにひびく。"),  # とびら split
    ("ja", "僕は来年から日本で働くことにした。"),  # ～ことにした grammar
    ("ja", "猫がコーヒーを飲んだ。"),  # a plain one
    ("zh", "我在1993年中文版的自序里写下这样一段话。"),  # Chinese, from the book on the device
]


def call(key: str, language: str, text: str) -> tuple[float, dict]:
    body = json.dumps({"text": text, "language": language}).encode()
    req = urllib.request.Request(f"{BASE}/v1/analyze/text", data=body, method="POST",
                                 headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
    t = time.monotonic()
    with urllib.request.urlopen(req, timeout=30) as r:
        data = json.load(r)
    return time.monotonic() - t, data


def split(data: dict) -> list[str]:
    return [f"{o.get('word')}|{o.get('entryId')}" for o in data.get("occurrences", [])]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "research", "v02-morpho"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.expanduser("~/.lexirise_key")) as f:
        key = f.read().strip()
    for n, (language, text) in enumerate(SENTENCES):
        start = time.monotonic()
        took, first = call(key, language, text)
        answers = [(0.0, took, first)]
        for delay in DELAYS_S:
            if not answers[-1][2].get("morphoPending"):
                break
            time.sleep(delay)
            took, data = call(key, language, text)
            answers.append((time.monotonic() - start, took, data))
        with open(os.path.join(a.out, f"{n}-{language}.json"), "w", encoding="utf-8") as f:
            json.dump([{"at_s": at, "took_s": tk, "response": d} for at, tk, d in answers], f, ensure_ascii=False,
                      indent=1)
        print(f"\n[{n}] {language} {text}")
        for at, tk, d in answers:
            print(f"  at {at:5.1f}s took {tk:4.2f}s morphoPending={d.get('morphoPending')} "
                  f"occurrences={len(d.get('occurrences', []))} grammar={len(d.get('grammar') or [])}")
        base = split(answers[0][2])
        last = split(answers[-1][2])
        if base != last:
            print("  split changed:\n    first: " + " ".join(t.split('|')[0] for t in base)
                  + "\n    last:  " + " ".join(t.split('|')[0] for t in last))
        else:
            print("  split unchanged: " + " ".join(t.split('|')[0] for t in base))


if __name__ == "__main__":
    main()
