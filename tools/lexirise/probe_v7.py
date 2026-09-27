#!/usr/bin/env python3
"""What v0.2 V7 (page analysis, the vocab mirror) is built on, measured read-only (page-annotations.md §1):

1. The vocab mirror: GET /v1/vocabulary pages (limit 200), each item's fields and size, and whether
   `sortId=updated_at&sortDesc=true` really orders by the last change (the incremental sync stops at the first item
   older than the last sync).
2. Page analysis: analyze/text on page-sized text (written for this probe, ~150 / ~300 / ~600 characters): the
   time, the answer's size, and whether it came back refined.

Key from ~/.lexirise_key (never printed). Raw answers go to research/v7/ (gitignored); the summary printed is what
the docs record (lexirise-api-notes.md, "V7's foundations, measured").

  python3 tools/lexirise/probe_v7.py
  python3 tools/lexirise/probe_v7.py --ties   # V7a R1: how updated_at ties are ordered across offset pages

--ties (read-only; run 2026-09-28: ties are ordered stably across offset pages, lexirise-api-notes.md "V7's
foundations"): whether items sharing an updated_at keep one order when the list is paged (limit=200 once, against
limit=10 pages over the same range), and how many ties there are. The incremental sync's stop at a tie with its
cursor relies on it.
"""

import json
import os
import time
import urllib.request

BASE = "https://api.lexirise.app"
PARAGRAPH = ("駅までの道はいつも混んでいて、毎朝の満員電車が煩わしくて、彼はとうとう会社を辞めることにした。"
             "春の風が少しだけ優しく感じられた。新しい町では、朝早く起きて川沿いを歩くのが日課になった。"
             "本屋の主人は無口だったが、彼が選んだ本にはいつも小さな栞が挟まれていた。")
ZH_PARAGRAPH = ("几年不见，他长得很像他的父亲了。院子里的树也长高了不少。我们明天一起去图书馆看书吧。"
                "他一边吃饭一边看电视，好像什么都没听见。")


def call(key, method, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    r = urllib.request.Request(BASE + path, data=data, method=method,
                               headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
    t = time.monotonic()
    with urllib.request.urlopen(r, timeout=60) as x:
        raw = x.read()
    return time.monotonic() - t, len(raw), json.loads(raw)


def main():
    key = open(os.path.expanduser("~/.lexirise_key")).read().strip()
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "research", "v7")
    os.makedirs(out_dir, exist_ok=True)
    raw = {}

    print("1. The vocab mirror")
    for lang in ("zh", "ja"):
        took, size, d = call(key, "GET", f"/v1/vocabulary?language={lang}&limit=200")
        items = d.get("items", [])
        raw[f"list_{lang}"] = d
        per = size / max(1, len(items))
        print(f"  {lang}: {len(items)} items in {took:.2f} s, {size} bytes (~{per:.0f} B/item), "
              f"totalCount {d.get('totalCount')}, nextOffset {d.get('nextOffset')}")
        if items:
            print(f"     item fields: {sorted(items[0].keys())}")
        took, size, s = call(key, "GET", f"/v1/vocabulary?language={lang}&limit=200&sortId=updated_at&sortDesc=true")
        raw[f"sorted_{lang}"] = s
        stamps = [i.get("updated_at") for i in s.get("items", [])]
        desc = all(a >= b for a, b in zip(stamps, stamps[1:]) if a and b)
        print(f"     sortId=updated_at&sortDesc=true: {len(stamps)} items, newest first: {desc}, "
              f"first {stamps[:1]}, last {stamps[-1:]}")
        took, size, p = call(key, "GET", f"/v1/vocabulary?language={lang}&limit=2&offset=1")
        print(f"     paging: limit=2&offset=1 -> {len(p.get('items', []))} items, nextOffset {p.get('nextOffset')}")

    print("2. Page analysis")
    for lang, text in (("ja", PARAGRAPH[:150]), ("ja", PARAGRAPH * 2), ("ja", PARAGRAPH * 4), ("zh", ZH_PARAGRAPH * 3)):
        took, size, d = call(key, "POST", "/v1/analyze/text", {"text": text, "language": lang})
        raw[f"analyze_{lang}_{len(text)}"] = {"took_s": took, "bytes": size, "morphoPending": d.get("morphoPending"),
                                              "occurrences": len(d.get("occurrences", []))}
        print(f"  {lang} {len(text)} chars: {took:.2f} s, {size} bytes (~{size / len(text):.0f} B/char), "
              f"{len(d.get('occurrences', []))} occurrences, morphoPending {d.get('morphoPending')}")

    with open(os.path.join(out_dir, "probe-v7-2026-09-28.json"), "w", encoding="utf-8") as f:
        json.dump(raw, f, ensure_ascii=False, indent=1)


def ties():
    key = open(os.path.expanduser("~/.lexirise_key")).read().strip()
    for lang in ("zh", "ja"):
        q = f"/v1/vocabulary?language={lang}&sortId=updated_at&sortDesc=true"
        _, _, whole = call(key, "GET", q + "&limit=200")
        order = [i.get("id") for i in whole.get("items", [])]
        stamps = [i.get("updated_at") for i in whole.get("items", [])]
        tied = sum(1 for a, b in zip(stamps, stamps[1:]) if a == b)
        paged = []
        for offset in range(0, min(len(order), 60), 10):
            _, _, page = call(key, "GET", q + f"&limit=10&offset={offset}")
            paged += [i.get("id") for i in page.get("items", [])]
        same = paged == order[:len(paged)]
        print(f"  {lang}: {len(order)} items, {tied} adjacent updated_at ties, paged order same as one page: {same}")


if __name__ == "__main__":
    import sys

    ties() if "--ties" in sys.argv else main()
